// Free camera for testing (European game SLES-52950, ELF CRC B1995E29). A tool for us, not for players.
//
// On only with the environment variable SOR_FREECAM=1 (the launcher's hidden DEBUG page, row FREE CAMERA). Without it
// nothing here runs and nothing is registered: the game is exactly as before.
//
// In the game:
//   F8          detach the camera from the game (the game stands still) / attach it again (the game goes on).
//               F9 is taken by the black box report (pc/sor_blackbox.cpp), F11 is fullscreen, F12 the engine panel.
//   W/S, A/D    forward/back, left/right.  Q/E down/up.  Arrows turn (left/right, look up/down).  Shift = faster.
//   Controller  left stick moves, right stick turns, L2/R2 down/up, R1 = faster.
//   Mouse       turns the view (launcher: CONTROLS > MOUSE LOOK on, the pointer taken by a click; pc/sor_mouse.cpp).
//   Ctrl+1..9   save the camera as point 1..9 in camera_points.ini (in the working folder, _build when the launcher
//               starts the game; the environment variable SOR_CAMERA_POINTS can name another file).
//   1..9        jump to point 1..9 (from the game too: the camera detaches first). The file is read again each time.
// camera_points.ini is plain text, one line per point:  point3=x,y,z,dx,dy,dz,area
//   x,y,z = where the camera is; dx,dy,dz = where it looks (any length); area = the game's area number in hex, optional,
//   only used to warn when a point is from another area. Lines starting with ; or # are comments.
// Log lines: "[sor] free camera on/off", "[sor] camera point 3 saved: ..." / "jumped: ...", and every 2 s while
// detached "[sor] camera at x y z looking dx dy dz (area 012 scene 3)".
//
// How it works (addresses and names from the DWARF data of the PAL ELF; LAVORO-SHADOW-OF-ROME.md, 09/11, free camera):
// - The game's camera is classCAMERA Camera (0x4C8B40, 112 bytes): mPos +0x00 and mDes +0x10 (x,y,z,w floats), the
//   angles mDir_x/y/z +0x20/+0x22/+0x24 (short), mType +0x26. classCAMERA::set(this, type) (0x146F90) with type 0 builds
//   the view from mPos and mDes (front = mDes - mPos); classCAMERA::make_view_mat (0x146AA0) adds the roll mDir_z.
//   Every frame classGAME_TASK::task_move_main calls classCAMERA_CTRL::move (0x269450), which ends with set(Camera, 0).
// - The pause is the game's own: when an overlay menu opens, classSUB_TASK::task_init (0x2EDFB8) ORs 0xFFE98000 into
//   the stop flags (player, enemies and people, objects, camera control, weapons, area timer, actors...) and the scene
//   is still drawn. Here the same bits are ORed into System.mStop_flg[0] (classSYSTEM System 0x4EA140, +0x30) in every
//   frame while detached, inside classSYSTEM::wbuf_swap (0x1E92D0), which classSYSTEM::main_loop calls before
//   TASK::manager. main_loop copies mStop_set into mStop_flg at the end of each frame, so the bits go away by themselves
//   once they are no longer written. mStop_set is never touched.
// - While detached, the hook on classCAMERA_CTRL::move skips the game's camera control: it writes the free camera into
//   Camera.mPos/mDes (roll 0) and goes on into classCAMERA::set(Camera, 0) as a tail call with the caller's return
//   address, so the game builds its own view matrices from it. The hook on classCAMERA::set does the same for any other
//   caller while detached. Guest code is never called from here except as that tail call (a call in the middle of a
//   hook could stop at a scheduler checkpoint).
// - The pad the game reads is held neutral (ps2_stubs::setPadOverrideState) while detached: the keys and sticks used by
//   the camera do nothing in the game.
// - Attach: the 112 bytes of Camera saved at detach are written back and the pad override is cleared; in that same frame
//   the game's camera control runs again from where it stopped.
// - Detach is accepted only while the game's camera control runs freely (in the last 3 frames, with its stop bit off):
//   not in films, events, menus or loading.
// - Left/right and up are measured at detach: two points are projected through the game's world-to-screen matrix
//   (0x4D3930), so the keys follow the picture whatever the axis conventions of the game are.
// Known limit: the game decides what to draw from its own camera and stage logic, which stand still: far from the play
// area parts of the scene may be missing. Not fixed here.
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "raylib.h"
#include "../ps2recomp/ps2xRuntime/src/lib/Kernel/Stubs/Pad.h" // setPadOverrideState / clearPadOverrideState

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

bool sorTelecomandoAcceso();         // pc/sor_prove.cpp: the test remote control holds the keys
bool sorTastoIniettato(int codice);
void sorMouseFreeCameraOn(bool on);  // pc/sor_mouse.cpp: the mouse turns the free camera while it is detached
bool sorMouseFreeCamera(float &yaw, float &pitch);

namespace
{
    const uint32_t kSwap = 0x1E92D0u;       // classSYSTEM::wbuf_swap, once per frame before TASK::manager
    const uint32_t kCtrlMove = 0x269450u;   // classCAMERA_CTRL::move(this)
    const uint32_t kCameraSet = 0x146F90u;  // classCAMERA::set(this, ushort type)
    const uint32_t kCamera = 0x4C8B40u;     // classCAMERA Camera
    const uint32_t kCameraSize = 112u;
    const uint32_t kStopFlg0 = 0x4EA170u;   // System.mStop_flg[0]
    const uint32_t kFreezeBits = 0xFFE98000u; // what classSUB_TASK::task_init ORs for the overlay menus (kinds 4, 7, 11)
    const uint32_t kCamCtrlStop = 0x08000000u; // the bit classCAMERA_CTRL::move tests to stand still
    const uint32_t kWorldScreen = 0x4D3930u; // FMATRIX world -> screen (view-screen x world-view)
    const uint32_t kArea = 0x4BE2E8u;       // Global.Area_no (ushort)
    const uint32_t kScene = 0x4BE2E0u;      // Global.Scene_no (ushort)

    struct V3
    {
        float x, y, z;
    };
    V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    V3 mul(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
    float len(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

    PS2Runtime::RecompiledFunction g_swap = nullptr, g_move = nullptr, g_set = nullptr;

    bool g_detached = false;
    uint8_t g_saved[kCameraSize];  // the game's Camera at detach, written back at attach
    V3 g_pos{0, 0, 0};
    float g_yaw = 0, g_pitch = 0;  // direction: f = (sin yaw cos pitch, sky * sin pitch, cos yaw cos pitch)
    float g_sky = -1.0f;           // +1 or -1: the world Y direction that points up on the screen
    float g_turnSign = 1.0f;       // +1 or -1: yaw change that turns the picture to the right
    float g_rightSign = 1.0f;      // right = g_rightSign * (cos yaw, 0, -sin yaw)
    float g_dist = 1.0f;           // mDes = mPos + f * g_dist (the game's distance at detach)
    float g_speed = 1.0f;          // world units per second
    bool g_invertY = false;        // the launcher's "invert Y" for the right stick
    uint64_t g_frame = 0, g_lastFreeMove = 0;
    bool g_movedOnce = false;
    std::chrono::steady_clock::time_point g_lastTick, g_lastLog;
    std::string g_pointsFile = "camera_points.ini";
    bool g_prevKey[400] = {};

    uint8_t *at(uint8_t *rdram, uint32_t a) { return rdram + (a & PS2_RAM_MASK); }
    float rdF(uint8_t *rdram, uint32_t a)
    {
        float v;
        std::memcpy(&v, at(rdram, a), 4);
        return v;
    }
    void wrF(uint8_t *rdram, uint32_t a, float v) { std::memcpy(at(rdram, a), &v, 4); }
    uint16_t rd16(uint8_t *rdram, uint32_t a)
    {
        uint16_t v;
        std::memcpy(&v, at(rdram, a), 2);
        return v;
    }
    uint32_t rd32(uint8_t *rdram, uint32_t a)
    {
        uint32_t v;
        std::memcpy(&v, at(rdram, a), 4);
        return v;
    }
    V3 rdV(uint8_t *rdram, uint32_t a) { return {rdF(rdram, a), rdF(rdram, a + 4), rdF(rdram, a + 8)}; }

    bool keyDown(int k)
    {
        return sorTelecomandoAcceso() ? sorTastoIniettato(k) : IsKeyDown(k);
    }
    // True once, in the frame the key goes down (this runs once per game frame).
    bool keyPressed(int k)
    {
        const bool d = keyDown(k);
        bool &prev = g_prevKey[k >= 0 && k < 400 ? k : 0];
        const bool hit = d && !prev;
        prev = d;
        return hit;
    }

    V3 forward()
    {
        const float cp = std::cos(g_pitch);
        return {std::sin(g_yaw) * cp, g_sky * std::sin(g_pitch), std::cos(g_yaw) * cp};
    }
    V3 right() { return mul(V3{std::cos(g_yaw), 0.0f, -std::sin(g_yaw)}, g_rightSign); }
    void setDirection(V3 d)
    {
        const float l = len(d);
        if (!(l > 1e-6f))
            return;
        d = mul(d, 1.0f / l);
        g_pitch = std::asin(std::max(-1.0f, std::min(1.0f, d.y * g_sky)));
        g_pitch = std::max(-1.48f, std::min(1.48f, g_pitch));
        if (std::fabs(d.x) > 1e-6f || std::fabs(d.z) > 1e-6f)
            g_yaw = std::atan2(d.x, d.z);
    }

    // World point -> screen through the game's matrix (rows: v' = m0*x + m1*y + m2*z + m3, then x/w, y/w).
    bool project(uint8_t *rdram, V3 p, float &sx, float &sy)
    {
        float m[16];
        std::memcpy(m, at(rdram, kWorldScreen), sizeof(m));
        const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
        const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        if (!std::isfinite(w) || std::fabs(w) < 1e-6f)
            return false;
        sx = x / w;
        sy = y / w;
        return std::isfinite(sx) && std::isfinite(sy);
    }

    void where(uint8_t *rdram, char *out, size_t n)
    {
        std::snprintf(out, n, "area %03x scene %u", rd16(rdram, kArea), rd16(rdram, kScene));
    }

    void logCamera(uint8_t *rdram, const char *head)
    {
        const V3 f = forward();
        char w[48];
        where(rdram, w, sizeof(w));
        std::printf("[sor] %s %.2f %.2f %.2f looking %.4f %.4f %.4f (%s)\n", head, g_pos.x, g_pos.y, g_pos.z, f.x, f.y,
                    f.z, w);
        std::fflush(stdout);
    }

    // The free camera into the game's Camera, for classCAMERA::set type 0 (front = mDes - mPos, roll 0).
    void writeCamera(uint8_t *rdram)
    {
        const V3 des = add(g_pos, mul(forward(), g_dist));
        wrF(rdram, kCamera + 0x00, g_pos.x);
        wrF(rdram, kCamera + 0x04, g_pos.y);
        wrF(rdram, kCamera + 0x08, g_pos.z);
        wrF(rdram, kCamera + 0x10, des.x);
        wrF(rdram, kCamera + 0x14, des.y);
        wrF(rdram, kCamera + 0x18, des.z);
        const uint16_t noRoll = 0;
        std::memcpy(at(rdram, kCamera + 0x24), &noRoll, 2); // mDir_z
    }

    bool canDetach() { return g_movedOnce && g_frame - g_lastFreeMove <= 3; }

    void detach(uint8_t *rdram)
    {
        std::memcpy(g_saved, at(rdram, kCamera), kCameraSize);
        const V3 pos = rdV(rdram, kCamera + 0x00);
        const V3 des = rdV(rdram, kCamera + 0x10);
        V3 front = sub(des, pos);
        float dist = len(front);
        if (!(dist > 1e-3f) || !std::isfinite(dist))
        {
            front = {0.0f, 0.0f, 1.0f};
            dist = 1.0f;
        }
        const V3 f = mul(front, 1.0f / dist);
        g_pos = pos;
        g_dist = dist;
        g_speed = dist; // about one camera distance per second
        // Which world direction is up on the screen, and which side is right: measured on the game's own picture.
        // Default for up: the opposite of the camera's up vector mUp_vec (+0x40), which the game passes as the "y"
        // axis of sceVu0CameraMatrix (screen y grows downwards on the PS2).
        g_sky = rdF(rdram, kCamera + 0x44) > 0.0f ? -1.0f : 1.0f;
        float ax, ay, bx, by;
        const V3 a = add(pos, mul(f, dist));
        bool measuredUp = false, measuredRight = false;
        if (project(rdram, a, ax, ay) && project(rdram, add(a, V3{0.0f, 0.1f * dist, 0.0f}), bx, by) &&
            std::fabs(by - ay) > 1e-3f)
        {
            g_sky = (by < ay) ? 1.0f : -1.0f; // the +Y point is higher on the screen: +Y is up
            measuredUp = true;
        }
        g_yaw = 0;
        g_pitch = 0;
        setDirection(f);
        g_rightSign = 1.0f;
        if (project(rdram, a, ax, ay) && project(rdram, add(a, mul(right(), 0.1f * dist)), bx, by) &&
            std::fabs(bx - ax) > 1e-3f)
        {
            if (bx < ax)
                g_rightSign = -1.0f;
            measuredRight = true;
        }
        // d(horizontal front)/d(yaw) = (cos yaw, 0, -sin yaw): turning right means moving the front towards right().
        g_turnSign = g_rightSign;
        ps2_stubs::setPadOverrideState(0xFFFFu, 0x80u, 0x80u, 0x80u, 0x80u); // the game sees a pad at rest
        g_detached = true;
        sorMouseFreeCameraOn(true);
        g_lastTick = g_lastLog = std::chrono::steady_clock::now();
        char w[48];
        where(rdram, w, sizeof(w));
        std::printf("[sor] free camera on (%s): F8 back to the game; WASD move, Q/E down/up, arrows or the mouse turn, Shift faster; "
                    "1-9 jump, Ctrl+1-9 save (%s)\n",
                    w, g_pointsFile.c_str());
        std::printf("[sor] free camera: up is %sY%s, right sign %+d%s, distance %.2f\n", g_sky > 0 ? "+" : "-",
                    measuredUp ? "" : " (not measured)", static_cast<int>(g_rightSign),
                    measuredRight ? "" : " (not measured)", dist);
        logCamera(rdram, "camera at");
    }

    void attach(uint8_t *rdram)
    {
        std::memcpy(at(rdram, kCamera), g_saved, kCameraSize);
        ps2_stubs::clearPadOverrideState();
        g_detached = false;
        sorMouseFreeCameraOn(false);
        std::printf("[sor] free camera off: the game's camera is back, the game goes on\n");
        std::fflush(stdout);
    }

    std::string trim(const std::string &s)
    {
        const size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos)
            return std::string();
        const size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
    // "point3" for a line "point3=...", lower case; "" for comments and other lines.
    std::string keyOf(const std::string &line, std::string *value)
    {
        const std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#')
            return std::string();
        const size_t eq = t.find('=');
        if (eq == std::string::npos)
            return std::string();
        std::string k = trim(t.substr(0, eq));
        for (char &c : k)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (value)
            *value = trim(t.substr(eq + 1));
        return k;
    }
    std::vector<std::string> readLines(bool &exists)
    {
        std::vector<std::string> lines;
        std::ifstream in(g_pointsFile);
        exists = static_cast<bool>(in);
        std::string l;
        while (exists && std::getline(in, l))
            lines.push_back(l);
        return lines;
    }

    void savePoint(uint8_t *rdram, int n)
    {
        const V3 f = forward();
        const unsigned area = rd16(rdram, kArea);
        char value[160];
        std::snprintf(value, sizeof(value), "%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%03x", g_pos.x, g_pos.y, g_pos.z, f.x, f.y, f.z,
                      area);
        const std::string key = "point" + std::to_string(n);
        bool exists = false;
        std::vector<std::string> lines = readLines(exists);
        if (!exists)
        {
            lines.push_back("; Shadow of Rome free camera points (DEBUG page, FREE CAMERA). In the game: F8 detaches the");
            lines.push_back("; camera, 1-9 jumps to a point, Ctrl+1-9 saves the camera there.");
            lines.push_back("; pointN=x,y,z,dx,dy,dz,area   (dx,dy,dz = where it looks; area in hex, optional)");
        }
        bool replaced = false;
        for (std::string &l : lines)
            if (keyOf(l, nullptr) == key)
            {
                l = key + "=" + value;
                replaced = true;
            }
        if (!replaced)
            lines.push_back(key + "=" + value);
        std::ofstream out(g_pointsFile, std::ios::trunc);
        for (const std::string &l : lines)
            out << l << "\n";
        out.close();
        char w[48];
        where(rdram, w, sizeof(w));
        if (out)
            std::printf("[sor] camera point %d saved: %.2f %.2f %.2f looking %.4f %.4f %.4f (%s) in %s\n", n, g_pos.x,
                        g_pos.y, g_pos.z, f.x, f.y, f.z, w, g_pointsFile.c_str());
        else
            std::printf("[sor] camera point %d NOT saved: cannot write %s\n", n, g_pointsFile.c_str());
        std::fflush(stdout);
    }

    void jumpTo(uint8_t *rdram, int n)
    {
        const std::string key = "point" + std::to_string(n);
        bool exists = false;
        const std::vector<std::string> lines = readLines(exists);
        std::string value;
        bool found = false;
        for (const std::string &l : lines)
        {
            std::string v;
            if (keyOf(l, &v) == key)
            {
                value = v; // the last line with that key wins
                found = true;
            }
        }
        if (!found)
        {
            std::printf("[sor] camera point %d: not in %s%s\n", n, g_pointsFile.c_str(),
                        exists ? "" : " (the file does not exist yet)");
            std::fflush(stdout);
            return;
        }
        float v[6] = {};
        int got = 0;
        const char *p = value.c_str();
        while (got < 6 && *p)
        {
            while (*p == ',' || *p == ' ' || *p == '\t' || *p == ';')
                ++p;
            char *end = nullptr;
            const float x = std::strtof(p, &end);
            if (end == p)
                break;
            v[got++] = x;
            p = end;
        }
        bool ok = got >= 3;
        for (int i = 0; i < got; ++i)
            ok = ok && std::isfinite(v[i]);
        if (!ok)
        {
            std::printf("[sor] camera point %d: cannot read \"%s\" (pointN=x,y,z,dx,dy,dz,area)\n", n, value.c_str());
            std::fflush(stdout);
            return;
        }
        int area = -1;
        while (*p == ',' || *p == ' ' || *p == '\t')
            ++p;
        if (*p)
        {
            char *end = nullptr;
            const unsigned long a = std::strtoul(p, &end, 16);
            if (end != p)
                area = static_cast<int>(a);
        }
        g_pos = {v[0], v[1], v[2]};
        if (got >= 6)
            setDirection(V3{v[3], v[4], v[5]});
        char head[48];
        std::snprintf(head, sizeof(head), "camera point %d jumped:", n);
        logCamera(rdram, head);
        const int now = rd16(rdram, kArea);
        if (area >= 0 && area != now)
        {
            std::printf("[sor] camera point %d was saved in area %03x, this is area %03x: the place may be empty here\n",
                        n, area, now);
            std::fflush(stdout);
        }
    }

    float stick(float v)
    {
        const float dead = 0.2f;
        if (std::fabs(v) < dead)
            return 0.0f;
        return (v > 0 ? v - dead : v + dead) / (1.0f - dead);
    }

    // Keys, sticks and movement, once per frame while detached.
    void steer()
    {
        const auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - g_lastTick).count();
        g_lastTick = now;
        dt = std::max(0.0f, std::min(dt, 0.1f));

        float fwd = 0, side = 0, vert = 0, turn = 0, look = 0;
        bool fast = keyDown(KEY_LEFT_SHIFT) || keyDown(KEY_RIGHT_SHIFT);
        fwd += (keyDown(KEY_W) ? 1.0f : 0.0f) - (keyDown(KEY_S) ? 1.0f : 0.0f);
        side += (keyDown(KEY_D) ? 1.0f : 0.0f) - (keyDown(KEY_A) ? 1.0f : 0.0f);
        vert += (keyDown(KEY_E) ? 1.0f : 0.0f) - (keyDown(KEY_Q) ? 1.0f : 0.0f);
        turn += (keyDown(KEY_RIGHT) ? 1.0f : 0.0f) - (keyDown(KEY_LEFT) ? 1.0f : 0.0f);
        look += (keyDown(KEY_UP) ? 1.0f : 0.0f) - (keyDown(KEY_DOWN) ? 1.0f : 0.0f);
        if (!sorTelecomandoAcceso() && IsGamepadAvailable(0))
        {
            side += stick(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X));
            fwd -= stick(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y));
            turn += stick(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X));
            const float ry = stick(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y));
            look += g_invertY ? ry : -ry;
            vert += (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) ? 1.0f : 0.0f) -
                    (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_2) ? 1.0f : 0.0f);
            fast = fast || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
        }
        const float k = fast ? 4.0f : 1.0f;
        const float turnRate = 1.6f * (fast ? 2.0f : 1.0f); // radians per second
        g_yaw += g_turnSign * std::max(-1.0f, std::min(1.0f, turn)) * turnRate * dt;
        g_pitch += std::max(-1.0f, std::min(1.0f, look)) * turnRate * dt;
        float mouseYaw, mousePitch; // the mouse: already angles (right, down)
        if (sorMouseFreeCamera(mouseYaw, mousePitch))
        {
            g_yaw += g_turnSign * mouseYaw;
            g_pitch -= mousePitch;
        }
        g_pitch = std::max(-1.48f, std::min(1.48f, g_pitch));
        const float step = g_speed * k * dt;
        const V3 up{0.0f, g_sky, 0.0f};
        g_pos = add(g_pos, mul(forward(), std::max(-1.0f, std::min(1.0f, fwd)) * step));
        g_pos = add(g_pos, mul(right(), std::max(-1.0f, std::min(1.0f, side)) * step));
        g_pos = add(g_pos, mul(up, std::max(-1.0f, std::min(1.0f, vert)) * step));
    }

    void freeCamSwap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        ++g_frame;
        if (ctx->pc == kSwap && IsWindowReady())
        {
            const bool ctrl = keyDown(KEY_LEFT_CONTROL) || keyDown(KEY_RIGHT_CONTROL);
            if (keyPressed(KEY_F8))
            {
                if (g_detached)
                    attach(rdram);
                else if (canDetach())
                    detach(rdram);
                else
                {
                    std::printf("[sor] free camera: not now (only while playing: not in films, events, menus or "
                                "loading)\n");
                    std::fflush(stdout);
                }
            }
            for (int n = 1; n <= 9; ++n)
            {
                if (!keyPressed(KEY_ONE + n - 1))
                    continue;
                if (ctrl)
                {
                    if (g_detached)
                        savePoint(rdram, n);
                    else
                    {
                        std::printf("[sor] camera point %d not saved: press F8 first (free camera)\n", n);
                        std::fflush(stdout);
                    }
                    continue;
                }
                if (!g_detached)
                {
                    if (!canDetach())
                    {
                        std::printf("[sor] camera point %d: not now (only while playing: not in films, events, menus "
                                    "or loading)\n",
                                    n);
                        std::fflush(stdout);
                        continue;
                    }
                    detach(rdram);
                }
                jumpTo(rdram, n);
            }
            if (g_detached)
            {
                steer();
                const auto now = std::chrono::steady_clock::now();
                if (now - g_lastLog >= std::chrono::seconds(2))
                {
                    g_lastLog = now;
                    logCamera(rdram, "camera at");
                }
            }
        }
        if (g_detached)
        {
            // The game's own freeze for overlay menus, for this frame (main_loop clears it at the end of the frame).
            const uint32_t v = rd32(rdram, kStopFlg0) | kFreezeBits;
            std::memcpy(at(rdram, kStopFlg0), &v, 4);
        }
        g_swap(rdram, ctx, rt);
    }

    void freeCamMove(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc != kCtrlMove || !g_detached)
        {
            if (ctx->pc == kCtrlMove && !(rd32(rdram, kStopFlg0) & kCamCtrlStop))
            {
                g_lastFreeMove = g_frame; // the game's camera control is running freely: detach is safe
                g_movedOnce = true;
            }
            g_move(rdram, ctx, rt);
            return;
        }
        // Detached: no camera control; the free camera goes into classCAMERA::set(Camera, 0), returning to our caller.
        writeCamera(rdram);
        SET_GPR_U32(ctx, 4, kCamera);
        SET_GPR_U64(ctx, 5, 0);
        ctx->pc = kCameraSet;
        g_set(rdram, ctx, rt);
    }

    void freeCamSet(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (g_detached && ctx->pc == kCameraSet && GPR_U32(ctx, 4) == kCamera)
        {
            writeCamera(rdram);
            SET_GPR_U64(ctx, 5, 0);
        }
        g_set(rdram, ctx, rt);
    }
}

// Called once after loadELF (pc/main_sor.cpp), after sorTrucchiInstalla (its hook on wbuf_swap is chained here).
void sorCameraInstalla(PS2Runtime &rt)
{
    const char *env = std::getenv("SOR_FREECAM");
    if (!env || std::strcmp(env, "1") != 0)
        return;
    g_swap = rt.lookupFunction(kSwap);
    g_move = rt.lookupFunction(kCtrlMove);
    g_set = rt.lookupFunction(kCameraSet);
    if (!g_swap || !g_move || !g_set)
    {
        std::printf("[sor] free camera: game functions not found (0x%06X %d, 0x%06X %d, 0x%06X %d), it stays off\n", kSwap,
                    g_swap ? 1 : 0, kCtrlMove, g_move ? 1 : 0, kCameraSet, g_set ? 1 : 0);
        std::fflush(stdout);
        return;
    }
    if (const char *f = std::getenv("SOR_CAMERA_POINTS"))
        if (*f)
            g_pointsFile = f;
    std::error_code ec;
    const std::filesystem::path full = std::filesystem::absolute(g_pointsFile, ec);
    if (!ec)
        g_pointsFile = full.string();
    const char *inv = std::getenv("SOR_PAD_INVERT_Y");
    g_invertY = inv && std::strcmp(inv, "1") == 0;
    rt.registerFunction(kSwap, freeCamSwap);
    rt.registerFunction(kCtrlMove, freeCamMove);
    rt.registerFunction(kCameraSet, freeCamSet);
    std::printf("[sor] free camera ready: F8 in the game (points: %s)\n", g_pointsFile.c_str());
    std::fflush(stdout);
}
