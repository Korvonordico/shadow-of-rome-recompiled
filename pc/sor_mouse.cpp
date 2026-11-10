// Shadow of Rome PC port - looking around with the mouse (launcher: CONTROLS > MOUSE LOOK, MOUSE SPEED, MOUSE Y),
// and the picture of the video card brought back when the game window gets the focus again.
//
// Environment (the launcher's Play sets them, like SOR_PAD_SWAP):
//   SOR_MOUSE_LOOK=1       on (the launcher's default). Missing or 0: off, nothing here touches the pointer or the pad,
//                          the game is exactly as before (test runs that start sor.exe by hand stay as they were).
//   SOR_MOUSE_SPEED=1..10  how fast the mouse turns the camera (5).
//   SOR_MOUSE_INVERT_Y=1   moving the mouse forward works as pulling the stick down instead of pushing it up.
//
// Main thread: the window loop (PS2Runtime::run in ps2_runtime.cpp) calls sorTurnoFinestra() once per turn, after
// EndDrawing (raylib has just read the mouse and the keys). There:
// - The pointer is taken (hidden and kept inside the window: raylib's DisableCursor, GLFW's "disabled" cursor with raw
//   mouse motion) when the player clicks in the game window, or once at start when the game starts in full screen.
//   It is given back (EnableCursor) with Alt (either), with Esc when no action of the keyboard mapping uses Esc
//   (pc/sor_tasti.cpp; Esc is free with the standard keys), when the window loses the focus (Alt+Tab), and while the
//   engine panel (F12) is open; a click takes it again. Alt can never be an action's key (the launcher and the game
//   refuse it), so Alt only frees the pointer (and Alt+Enter still switches full screen).
// - While the pointer is taken, the mouse movement is summed up: for the right stick, or for the free camera while it
//   is detached (pc/sor_camera.cpp).
// - When the window gets the focus back (Alt+Tab, restored from the taskbar), sorGsRiprendi() (pc/sor_profilo.cpp)
//   shows the video card's window again over the whole game window: "[sor] focus back: picture restored".
// Game thread:
// - Direct mouse camera (the normal camera while playing): a hook on classCAMERA_CTRL::move (0x269450), registered only
//   with MOUSE LOOK on, turns the mouse movement straight into the game camera's own angles, like a PC game (details
//   below, "How the direct camera works").
// - sorMouseStick() (from PSPadBackend::readState, ps2_pad.cpp, port 0), only in the camera types the game drives
//   itself (see below): the movement since the last pad read becomes a right stick value, with a small smoothing, added
//   to the controller's stick and to the camera keys, clamped to the stick's range. When the mouse stops the value goes
//   back to the centre within a few reads (no drift).
//   The game ignores small stick values (a dead zone, func_12CEB0/func_12CD40: v = byte - 0x80, 0 while |v| <= dz,
//   then 0x80 * (v - dz) / (0x7F - dz)); dz is a byte of the game's pad data (0x4BC400 + 0x1FC * [0x4BC400 + 0xF24]
//   + 0x505), read here so that the camera turns in proportion to the mouse, even for small movements.
// - sorMouseFreeCamera(): the movement as angles for the free camera (pc/sor_camera.cpp), same degrees per pixel.
// Log: "[sor] mouse look: on, speed 5, invert off" at start; "[sor] mouse: captured" / "[sor] mouse: released (...)";
// "[sor] mouse camera: direct (yaw 0x4C8B62, pitch 0x4C8B60), auto-level off ..." the first time the mouse turns the
// camera; "[sor] mouse camera: camera type 0x.. is the game's own ..." once per such type.
//
// How the direct camera works (PAL ELF, DWARF names; LAVORO-SHADOW-OF-ROME.md, 09/11, "telecamera diretta col mouse"):
// - Every frame classGAME_TASK::task_move_main calls classCAMERA_CTRL::move (Camera_ctrl 0x523230) -> free_move
//   (0x2677E0), which picks a camera type (mCtrl_type, +0x180) and runs its function (jump table 0x49FEE0). The normal
//   camera while playing is type 6 (classCAMERA_CTRL::get_normal_cam_type 0x25BE90) = move_type07 (0x264BE0).
// - move_type07 keeps its angles in the camera object itself, classCAMERA Camera (0x4C8B40), 0x1000 = one turn:
//   yaw = Camera.mDir_y (+0x22, read at its start, written back at its end), pitch = Camera.mDir_x (+0x20). The right
//   stick X adds to the yaw (mCam_analog_rx / 5 or 8 per frame, so it has a top speed). The stick Y does NOT turn the
//   pitch: it moves a target pitch = a fixed base (-0x32 or -0x8C by mAdmin +0x183) + the stick's offset (up to +0x154
//   with the stick up, -0x280 down; virtual pad bits 0x1000/0x2000), and the pitch is pulled towards that target by at
//   most 0x10 per frame. Stick released = target back to the base: that is the automatic re-levelling.
// - With the mouse: in the hook, before move runs, the angles the player chose are written into Camera.mDir_y/mDir_x
//   every frame (the "hold"), and the mouse movement is added to them (MOUSE SPEED: degrees per pixel). The game then
//   runs as always (its collision, distance and smoothing), may pull the pitch by at most 0x10 (1.4 degrees) in that
//   frame, and the next frame starts again from the player's angles: nothing drifts back. The pitch stays inside the
//   stick's own range [base - 0x280, base + 0x154] (the game's limits, so the camera is never put lower than the game
//   puts it). Which way is right and up on the screen is measured once on the game's own picture (the world-to-screen
//   matrix 0x4D3930), defaults: mouse right = yaw down, mouse up = pitch up.
// - The hold ends (the game's camera as always, re-levelling included) when the controller's right stick or the camera
//   keys move (mPad_analog_rx/ry +0xEC/+0xEE not 0), when the centre button starts (mCenter_cam_timer +0xE8), when the
//   camera type is not 6, when the area or scene changes, and after a pause of the camera control longer than 250 ms
//   (films, events, loading: then classCAMERA_CTRL::move is not called at all). The next mouse movement starts it again.
// - Camera types the game drives itself (all the others: type 7 = move_type0b in areas 0x30/0x33/0x34/0x36/0x37, which
//   sets the pitch from tables; 5 with two players; 0x16/0x19 aux cameras; events, lock and scripted shots): the mouse
//   works there as before, as the right stick. Pause menus (camera control stopped, System.mStop_flg[0] bit 0x08000000):
//   also as before; the angles chosen are kept.
//
// Mouse buttons and the wheel as keys (launcher: CONTROLS > KEYBOARD, a click or a wheel notch while a row waits;
// pc/sor_tasti.cpp names them MOUSE_LEFT ... WHEEL_DOWN). Nothing here runs unless an action uses one of them.
// - Main thread, turnoTastiMouse() right after turnoMouse(): raylib has just read the mouse (EndDrawing polls the events;
//   the wheel's movement is kept only until the next poll, so it must be read here, once per turn). The buttons count
//   only while the mouse is "usable":
//     MOUSE LOOK on:  while the pointer is taken (above). The click that takes it does nothing else.
//     MOUSE LOOK off: while the game window has the focus (raylib IsWindowFocused) and the pointer is over it (raylib
//                     IsCursorOnScreen: GLFW's "cursor entered / left" messages; in full screen it is always over it).
//     Never while the engine panel (F12) is open or the test remote control holds the keys.
//   The buttons already down when the mouse becomes usable, or pressed in the next 150 ms, are ignored until they are let
//   go: the click that takes the pointer, or the click that brings the window to the front.
// - A short click counts for at least 50 ms (the game may read the pad only 30 times a second). A wheel notch holds the
//   action down for 80 ms, then up for 60 ms before the next notch (at most 2 waiting): the game sees one press per notch.
// - Game thread: sorMouseTastoGiu(i), from sor_tasti.cpp, reads what the main thread left (atomics), with the clock.
// Log: in "[sor] keys:" (sor_tasti.cpp) the mouse inputs used; "[sor] mouse: first MOUSE_LEFT press passed to the game".
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "raylib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

bool sorTelecomandoAcceso();          // pc/sor_prove.cpp: the test remote control holds the keys
bool sorTastoDiUnAzione(int codice);  // pc/sor_tasti.cpp: true if an action of the keyboard mapping uses that key
bool sorPannelloAperto();             // pc/sor_profilo.cpp: the engine panel (F12) is open
void sorGsRiprendi();                 // pc/sor_profilo.cpp: the video card's window shown again, fitted to the game window
unsigned sorTastiMouseUsati();        // pc/sor_tasti.cpp: bit i = mouse input i (MOUSE_LEFT ... WHEEL_DOWN) does an action

namespace
{
    bool g_on = false;
    int g_speed = 5;
    bool g_invert = false;
    float g_gain = 1.0f;         // (stick fraction) per (pixel per second)
    float g_degPerPx = 0.15f;    // direct camera and free camera: degrees per pixel
    float g_radPerPx = 0.002f;   // free camera: radians per pixel
    float g_unitsPerPx = 1.7f;   // direct camera: game angle units (0x1000 = one turn) per pixel
    uint8_t *g_rdram = nullptr;  // the game's memory (for the dead zone and the direct camera)

    // Direct mouse camera (see the top of the file)
    const uint32_t kCtrlMove = 0x269450u;      // classCAMERA_CTRL::move(this)
    const uint32_t kCamCtrl = 0x523230u;       // classCAMERA_CTRL Camera_ctrl
    const uint32_t kCamera = 0x4C8B40u;        // classCAMERA Camera
    const uint32_t kPitch = kCamera + 0x20u;   // Camera.mDir_x (sshort)
    const uint32_t kYaw = kCamera + 0x22u;     // Camera.mDir_y (sshort)
    const uint32_t kStopFlg0 = 0x4EA170u;      // System.mStop_flg[0]
    const uint32_t kCamCtrlStop = 0x08000000u; // the bit classCAMERA_CTRL::move tests to stand still
    const uint32_t kWorldScreen = 0x4D3930u;   // FMATRIX world -> screen
    const uint32_t kArea = 0x4BE2E8u, kScene = 0x4BE2E0u; // Global.Area_no, Global.Scene_no (ushort)
    const uint8_t kNormalType = 6;             // mCtrl_type of the normal camera (move_type07)
    // mCtrl_type 0x0A = move_type16 (0x262020), the camera while crouching (L2): free_move's table 0x49FEE0 entry 0x0A
    // -> 0x2681A0 -> jal 0x262020. Same angles as move_type07: yaw read from Camera.mDir_y into +0xFE, turned by
    // +-0x10 a frame with the virtual pad bits 0x4000/0x8000 (a fixed speed, so the mouse as a stick felt like a pad),
    // written back to mDir_y; pitch pulled towards a target kept at +0xFC (+-0x10 a frame by bits 0x1000/0x2000,
    // clamped to -0x280..0x154, func_131DA0). Its target is not reset each frame, so the hold writes it too.
    // (10/11/2026, the author: crouched, the mouse did not turn the view as when standing.)
    const uint8_t kCrouchType = 0x0A;
    PS2Runtime::RecompiledFunction g_nextMove = nullptr; // the game's classCAMERA_CTRL::move (or the free camera's hook)
    std::atomic<long long> g_directFino{0};    // ms: until then the mouse turns the camera directly, not via the stick
    // game thread
    bool g_hold = false;                        // the player's angles are written every frame
    float g_holdYaw = 0, g_holdPitch = 0;       // game units
    long long g_lastMoveMs = 0;
    uint32_t g_lastWhere = 0xFFFFFFFFu;
    float g_yawSign = -1.0f, g_pitchSign = 1.0f; // mouse right -> yaw change sign; mouse up -> pitch change sign
    bool g_measured = false, g_toldDirect = false;
    uint32_t g_toldTypes[8] = {};               // camera types already named in the log

    // main thread
    bool g_ready = false, g_captured = false, g_focusWas = true, g_minimizedWas = false, g_startCapture = false;
    int g_skip = 0;

    // shared, under g_lock
    std::mutex g_lock;
    float g_stickX = 0, g_stickY = 0; // pixels for the right stick
    float g_camX = 0, g_camY = 0;     // pixels for the free camera
    std::atomic<bool> g_freeCam{false};

    // game thread
    float g_sx = 0, g_sy = 0; // smoothed stick fraction, -1..1
    std::chrono::steady_clock::time_point g_lastRead{};
    int g_deadZoneTold = -1;

    // Mouse buttons and wheel as keys (see the top of the file). Inputs 0-4: the buttons, 5: wheel up, 6: wheel down.
    constexpr int kInput = 7;
    const char *const kInputNome[kInput] = {"MOUSE_LEFT", "MOUSE_RIGHT", "MOUSE_MIDDLE", "MOUSE_X1", "MOUSE_X2", "WHEEL_UP", "WHEEL_DOWN"};
    // raylib 5.5 on Windows (GLFW): SIDE = the first side button (XBUTTON1, "back"), EXTRA = the second (XBUTTON2, "forward")
    const int kBottone[5] = {MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_SIDE, MOUSE_BUTTON_EXTRA};
    constexpr long long kClicMs = 50, kRotellaGiuMs = 80, kRotellaSuMs = 60, kGraziaMs = 150;
    // main thread -> game thread
    std::atomic<unsigned> g_bottoniGiu{0};      // bit i: button i is down and counts
    std::atomic<long long> g_fino[kInput];      // ms (steady clock): input i counts as down until then (static: starts at 0)
    // main thread
    bool g_usabile = false;
    unsigned g_bloccati = 0, g_valevano = 0, g_detti = 0;
    long long g_graziaFino = 0;
    int g_tacche[2] = {0, 0};          // wheel notches waiting (up, down)
    long long g_rotellaLibera[2] = {0, 0}; // the next notch may start from then

    long long adessoMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    float leggiNumero(const char *var, float base, float lo, float hi)
    {
        const char *v = std::getenv(var);
        if (!v || !*v)
            return base;
        char *end = nullptr;
        const float f = std::strtof(v, &end);
        if (end == v || !std::isfinite(f))
            return base;
        return std::max(lo, std::min(hi, f));
    }

    void leggiImpostazioni()
    {
        const char *on = std::getenv("SOR_MOUSE_LOOK");
        g_on = on && on[0] == '1';
        g_speed = static_cast<int>(leggiNumero("SOR_MOUSE_SPEED", 5.0f, 1.0f, 10.0f) + 0.5f);
        const char *inv = std::getenv("SOR_MOUSE_INVERT_Y");
        g_invert = inv && inv[0] == '1';
        // stick (camera types the game drives): speed 5 = about 1000 pixels per second is the stick pushed all the way;
        // each step 35% more or less
        const float mult = std::pow(1.35f, static_cast<float>(g_speed - 5));
        g_gain = mult / 1000.0f;
        // direct and free camera: speed 5 = 0.15 degrees per pixel (a full turn in 2400 pixels, a common PC default),
        // each step 30% more or less: 1 = 0.053, 10 = 0.56 degrees per pixel
        g_degPerPx = 0.15f * std::pow(1.3f, static_cast<float>(g_speed - 5));
        g_radPerPx = g_degPerPx * 3.14159265f / 180.0f;
        g_unitsPerPx = g_degPerPx * 4096.0f / 360.0f;
    }

    void cattura()
    {
        DisableCursor();
        g_captured = true;
        g_skip = 2; // the first movement after taking the pointer is not counted
        std::printf("[sor] mouse: captured (Alt%s frees it)%s\n", sorTastoDiUnAzione(KEY_ESCAPE) ? "" : " or Esc",
                    sorTastiMouseUsati() ? "; the click that took it is not passed to the game" : "");
        std::fflush(stdout);
    }

    void rilascia(const char *perche)
    {
        EnableCursor();
        g_captured = false;
        {
            std::lock_guard<std::mutex> l(g_lock);
            g_stickX = g_stickY = g_camX = g_camY = 0;
        }
        std::printf("[sor] mouse: released (%s)%s\n", perche, std::strcmp(perche, "focus lost") == 0 ? "" : ", click in the game to take it again");
        std::fflush(stdout);
    }

    void turnoMouse(bool focused)
    {
        if (!g_on || sorTelecomandoAcceso())
            return;
        const bool panel = sorPannelloAperto();
        if (g_captured)
        {
            const bool alt = IsKeyPressed(KEY_LEFT_ALT) || IsKeyPressed(KEY_RIGHT_ALT);
            const bool esc = IsKeyPressed(KEY_ESCAPE) && !sorTastoDiUnAzione(KEY_ESCAPE);
            if (!focused)
                rilascia("focus lost");
            else if (panel)
                rilascia("engine panel open");
            else if (alt)
                rilascia("Alt");
            else if (esc)
                rilascia("Esc");
        }
        else if (focused && !panel && IsWindowReady() && !IsWindowMinimized())
        {
            if (g_startCapture || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
            {
                g_startCapture = false;
                cattura();
            }
        }
        if (!g_captured)
            return;
        const Vector2 d = GetMouseDelta();
        if (g_skip > 0)
        {
            --g_skip;
            return;
        }
        if (!(std::isfinite(d.x) && std::isfinite(d.y)) || (d.x == 0.0f && d.y == 0.0f))
            return;
        std::lock_guard<std::mutex> l(g_lock);
        float &ax = g_freeCam.load() ? g_camX : g_stickX;
        float &ay = g_freeCam.load() ? g_camY : g_stickY;
        ax = std::max(-4000.0f, std::min(4000.0f, ax + d.x));
        ay = std::max(-4000.0f, std::min(4000.0f, ay + d.y));
    }

    // Mouse buttons and wheel as keys, once per turn after turnoMouse (see the top of the file).
    void turnoTastiMouse(bool focused)
    {
        const unsigned usati = sorTastiMouseUsati();
        if (!usati)
            return;
        bool usabile = false;
        if (focused && !sorTelecomandoAcceso() && !sorPannelloAperto())
            usabile = g_on ? g_captured : IsCursorOnScreen();
        if (!usabile)
        {
            if (g_usabile)
            {   // nothing stays held
                g_usabile = false;
                g_bottoniGiu.store(0);
                for (auto &f : g_fino)
                    f.store(0);
                g_tacche[0] = g_tacche[1] = 0;
                g_valevano = 0;
            }
            return;
        }
        const long long ora = adessoMs();
        unsigned giu = 0;
        for (int b = 0; b < 5; ++b)
            if (IsMouseButtonDown(kBottone[b]))
                giu |= 1u << b;
        if (!g_usabile)
        {   // just became usable: the buttons down now (the click that took the pointer) are not passed
            g_usabile = true;
            g_bloccati = giu;
            g_graziaFino = ora + kGraziaMs;
        }
        if (ora < g_graziaFino)
            g_bloccati |= giu;
        g_bloccati &= giu; // a blocked button let go counts again at its next press
        const unsigned vale = giu & ~g_bloccati & usati & 0x1Fu;
        const unsigned nuovi = vale & ~g_valevano;
        g_valevano = vale;
        for (int b = 0; b < 5; ++b)
            if (nuovi & (1u << b))
                g_fino[b].store(ora + kClicMs); // a short click is still seen
        g_bottoniGiu.store(vale);

        // the wheel: only the vertical one (a tilting wheel's sideways steps are ignored)
        const float w = GetMouseWheelMoveV().y;
        if (std::isfinite(w) && w != 0.0f)
        {
            const int k = w > 0.0f ? 0 : 1;
            if (usati & (1u << (5 + k)))
                g_tacche[k] = std::min(2, g_tacche[k] + 1);
        }
        unsigned partite = nuovi;
        for (int k = 0; k < 2; ++k)
            if (g_tacche[k] > 0 && ora >= g_rotellaLibera[k])
            {
                --g_tacche[k];
                g_fino[5 + k].store(ora + kRotellaGiuMs);
                g_rotellaLibera[k] = ora + kRotellaGiuMs + kRotellaSuMs;
                partite |= 1u << (5 + k);
            }
        // the first press of each input in the log (proof that it reached the game's keys)
        for (int i = 0; i < kInput; ++i)
            if ((partite & (1u << i)) && !(g_detti & (1u << i)))
            {
                g_detti |= 1u << i;
                std::printf("[sor] mouse: first %s press passed to the game\n", kInputNome[i]);
                std::fflush(stdout);
            }
    }

    uint8_t rd8(uint32_t a) { return g_rdram[a & PS2_RAM_MASK]; }
    uint32_t rd32(uint32_t a)
    {
        uint32_t v;
        std::memcpy(&v, g_rdram + (a & PS2_RAM_MASK), 4);
        return v;
    }

    // The game's dead zone for the sticks (see the top of the file), 0 if it cannot be read.
    int zonaMorta()
    {
        if (!g_rdram)
            return 0;
        const uint32_t base = 0x4BC400u;
        const uint32_t idx = rd32(base + 0xF24u);
        if (idx > 7u)
            return 0;
        int dz = rd8(base + idx * 0x1FCu + 0x505u);
        dz = std::min(dz, 0x70);
        if (dz != g_deadZoneTold)
        {
            g_deadZoneTold = dz;
            std::printf("[sor] mouse: the game's stick dead zone is %d of 127\n", dz);
            std::fflush(stdout);
        }
        return dz;
    }

    // One stick axis: the byte the controller and the keys made, plus the mouse's fraction past the dead zone.
    uint8_t somma(uint8_t byte, float s, int dz)
    {
        if (s == 0.0f)
            return byte;
        const float mag = std::fabs(s);
        const float push = (static_cast<float>(dz) + 1.0f + mag * (127.0f - static_cast<float>(dz) - 1.0f)) / 127.0f;
        const float v = (static_cast<float>(byte) - 128.0f) / 127.0f + (s > 0 ? push : -push);
        const float c = std::max(-1.0f, std::min(1.0f, v));
        const long r = std::lround(128.0f + c * 127.0f);
        return static_cast<uint8_t>(std::max(0L, std::min(255L, r)));
    }

    // ---- Direct mouse camera (see the top of the file) ----
    uint16_t rd16(uint32_t a)
    {
        uint16_t v;
        std::memcpy(&v, g_rdram + (a & PS2_RAM_MASK), 2);
        return v;
    }
    void wr16(uint32_t a, uint16_t v) { std::memcpy(g_rdram + (a & PS2_RAM_MASK), &v, 2); }
    float rdF(uint32_t a)
    {
        float v;
        std::memcpy(&v, g_rdram + (a & PS2_RAM_MASK), 4);
        return v;
    }
    // An angle as -0x800..0x7FF (the game stores the pitch either way).
    int angolo(uint16_t v)
    {
        int a = v & 0xFFF;
        return a >= 0x800 ? a - 0x1000 : a;
    }

    struct V3
    {
        float x, y, z;
    };
    // The direction the camera looks for pitch/yaw in game units, as move_type07 places it (Camera.mPos = target -
    // mDis * (sin(yaw+0x800) cos p, sin p, cos(yaw+0x800) cos p)).
    V3 direzione(float pitch, float yaw)
    {
        const float k = 6.28318531f / 4096.0f;
        const float p = pitch * k, y = yaw * k;
        return {-std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
    }
    // World point -> screen through the game's matrix (as in pc/sor_camera.cpp).
    bool proietta(V3 q, float &sx, float &sy)
    {
        float m[16];
        std::memcpy(m, g_rdram + (kWorldScreen & PS2_RAM_MASK), sizeof(m));
        const float x = m[0] * q.x + m[4] * q.y + m[8] * q.z + m[12];
        const float y = m[1] * q.x + m[5] * q.y + m[9] * q.z + m[13];
        const float w = m[3] * q.x + m[7] * q.y + m[11] * q.z + m[15];
        if (!std::isfinite(w) || std::fabs(w) < 1e-6f)
            return false;
        sx = x / w;
        sy = y / w;
        return std::isfinite(sx) && std::isfinite(sy);
    }

    // Once: which way a bigger yaw / pitch moves the picture (the look point projected with the game's own matrix).
    // Returns a short text for the log.
    const char *misuraVersi(float pitch, float yaw)
    {
        if (g_measured)
            return "measured";
        const V3 pos{rdF(kCamera + 0x00), rdF(kCamera + 0x04), rdF(kCamera + 0x08)};
        const V3 des{rdF(kCamera + 0x10), rdF(kCamera + 0x14), rdF(kCamera + 0x18)};
        V3 f{des.x - pos.x, des.y - pos.y, des.z - pos.z};
        const float dist = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
        if (!std::isfinite(dist) || dist < 1e-3f)
            return "not measured (no camera distance), defaults";
        f = {f.x / dist, f.y / dist, f.z / dist};
        const V3 g = direzione(pitch, yaw);
        if (f.x * g.x + f.y * g.y + f.z * g.z < 0.98f)
            return "not measured (the camera's direction does not match its angles), defaults";
        const float d = 20.0f; // game units (1.8 degrees)
        auto punto = [&](V3 v) { return V3{pos.x + v.x * dist, pos.y + v.y * dist, pos.z + v.z * dist}; };
        float ax, ay, bx, by, cx, cy;
        if (!proietta(punto(g), ax, ay) || !proietta(punto(direzione(pitch, yaw + d)), bx, by) ||
            !proietta(punto(direzione(pitch + d, yaw)), cx, cy))
            return "not measured (projection failed), defaults";
        if (std::fabs(bx - ax) < 1e-4f || std::fabs(cy - ay) < 1e-4f)
            return "not measured (no movement on the screen), defaults";
        g_yawSign = bx > ax ? 1.0f : -1.0f;   // a bigger yaw moves the look point to the right: the view turns right
        g_pitchSign = cy < ay ? 1.0f : -1.0f; // a bigger pitch moves the look point up (screen y grows downwards)
        g_measured = true;
        return "measured";
    }

    void diciTipo(uint8_t tipo)
    {
        uint32_t &w = g_toldTypes[tipo >> 5];
        const uint32_t bit = 1u << (tipo & 31u);
        if (w & bit)
            return;
        w |= bit;
        std::printf("[sor] mouse camera: camera type 0x%02X is the game's own, the mouse works there as the right stick\n",
                    tipo);
        std::fflush(stdout);
    }

    // Before classCAMERA_CTRL::move, once per frame (game thread).
    void cameraDiretta()
    {
        const long long ora = adessoMs();
        const bool pausa = g_lastMoveMs == 0 || ora - g_lastMoveMs > 250;
        g_lastMoveMs = ora;
        const uint32_t dove = (static_cast<uint32_t>(rd16(kArea)) << 16) | rd16(kScene);
        if (pausa || dove != g_lastWhere)
            g_hold = false; // a film, an event, loading, another area: the game's camera starts again
        g_lastWhere = dove;
        if (g_freeCam.load() || (rd32(kStopFlg0) & kCamCtrlStop))
        {   // the free camera has the mouse; or a menu: the game's camera stands still, the mouse as before
            g_directFino.store(0);
            return;
        }
        const uint8_t tipo = rd8(kCamCtrl + 0x180);
        const bool centra = static_cast<int16_t>(rd16(kCamCtrl + 0xE8)) > 0;
        const bool crouch = tipo == kCrouchType;
        if ((tipo != kNormalType && !crouch) || centra || rd32(kCamCtrl + 0x8) != kCamera)
        {
            g_hold = false;
            g_directFino.store(0);
            if (tipo != kNormalType && !crouch)
                diciTipo(tipo);
            return;
        }
        g_directFino.store(ora + 250);
        // the controller's right stick or the camera keys moved in the last frame: the game's camera as always
        if (rd16(kCamCtrl + 0xEC) != 0 || rd16(kCamCtrl + 0xEE) != 0)
            g_hold = false;
        float dx, dy;
        {
            std::lock_guard<std::mutex> l(g_lock);
            dx = g_stickX;
            dy = g_stickY;
            g_stickX = g_stickY = 0;
        }
        g_sx = g_sy = 0.0f;
        // the stick's own range for the pitch (see the top of the file)
        const int base = rd8(kCamCtrl + 0x183) == 1 ? -0x32 : -0x8C;
        const float lo = static_cast<float>(crouch ? -0x280 : base - 0x280), hi = static_cast<float>(crouch ? 0x154 : base + 0x154);
        if (dx != 0.0f || dy != 0.0f)
        {
            if (!g_hold)
            {
                g_holdYaw = static_cast<float>(rd16(kYaw) & 0xFFF);
                g_holdPitch = static_cast<float>(angolo(rd16(kPitch)));
                const char *come = misuraVersi(g_holdPitch, g_holdYaw);
                g_hold = true;
                if (!g_toldDirect)
                {
                    g_toldDirect = true;
                    std::printf("[sor] mouse camera: direct (yaw 0x%06X, pitch 0x%06X), auto-level off; %.3f degrees per "
                                "pixel, mouse right = yaw %+d, mouse up = pitch %+d (%s), pitch kept in %d..%d\n",
                                kYaw, kPitch, g_degPerPx, static_cast<int>(g_yawSign), static_cast<int>(g_pitchSign), come,
                                base - 0x280, base + 0x154);
                    std::fflush(stdout);
                }
            }
            g_holdYaw += g_yawSign * dx * g_unitsPerPx;
            g_holdYaw = std::fmod(g_holdYaw, 4096.0f);
            if (g_holdYaw < 0.0f)
                g_holdYaw += 4096.0f;
            const float prima = g_holdPitch;
            float p = prima + g_pitchSign * (-dy) * g_unitsPerPx * (g_invert ? -1.0f : 1.0f);
            // inside the range; a pitch the game had already put outside it may only come back
            p = std::min(p, std::max(hi, prima));
            p = std::max(p, std::min(lo, prima));
            g_holdPitch = p;
        }
        if (!g_hold)
            return;
        wr16(kYaw, static_cast<uint16_t>(std::lround(g_holdYaw) & 0xFFF));
        wr16(kPitch, static_cast<uint16_t>(static_cast<int16_t>(std::lround(g_holdPitch))));
        if (crouch) // move_type16 pulls the pitch towards its own target: the target is the player's pitch too
        {   // and its yaw lives in +0xFE (read from mDir_y only on its first frame, written back to mDir_y at its end)
            wr16(kCamCtrl + 0xFC, static_cast<uint16_t>(static_cast<int16_t>(std::lround(g_holdPitch))));
            wr16(kCamCtrl + 0xFE, static_cast<uint16_t>(std::lround(g_holdYaw) & 0xFFF));
        }
    }

    void cameraMove(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc == kCtrlMove && GPR_U32(ctx, 4) == kCamCtrl && !sorTelecomandoAcceso())
            cameraDiretta();
        g_nextMove(rdram, ctx, rt); // tail call, the caller's return address untouched
    }
}

// pc/main_sor.cpp, after loadELF: the settings and the game's memory.
void sorMouseInstalla(PS2Runtime &rt)
{
    g_rdram = rt.memory().getRDRAM();
    leggiImpostazioni();
    std::printf("[sor] mouse look: %s, speed %d, invert %s%s\n", g_on ? "on" : "off", g_speed, g_invert ? "on" : "off",
                g_on ? " (click in the game window to turn the camera with the mouse; Alt gives the pointer back)" : "");
    std::fflush(stdout);
    if (!g_on)
        return; // MOUSE LOOK off: no hook, the game's camera exactly as before
    // After sorCameraInstalla (pc/main_sor.cpp): its hook on the same function, if any, is chained here.
    g_nextMove = rt.lookupFunction(kCtrlMove);
    if (!g_nextMove)
    {
        std::printf("[sor] mouse camera: classCAMERA_CTRL::move (0x%06X) not found, the mouse works as the right stick\n",
                    kCtrlMove);
        std::fflush(stdout);
        return;
    }
    rt.registerFunction(kCtrlMove, cameraMove);
}

// ps2_runtime.cpp, the window loop: once per turn, after EndDrawing.
void sorTurnoFinestra()
{
    if (!IsWindowReady())
        return;
    const bool focused = IsWindowFocused();
    const bool minimized = IsWindowMinimized();
    if (!g_ready)
    {
        g_ready = true;
        g_focusWas = focused;
        g_minimizedWas = minimized;
        const char *full = std::getenv("SOR_FULLSCREEN");
        g_startCapture = g_on && full && full[0] == '1'; // full screen: the pointer is taken at start
    }
    // The focus is back (Alt+Tab, or restored from the taskbar): the video card's picture shown again.
    if (focused && !minimized && (!g_focusWas || g_minimizedWas))
        sorGsRiprendi();
    g_focusWas = focused;
    g_minimizedWas = minimized;
    turnoMouse(focused && !minimized);
    turnoTastiMouse(focused && !minimized);
}

// pc/sor_tasti.cpp (game thread, every pad read): mouse input i (0-4 buttons, 5 wheel up, 6 wheel down) counts as down.
bool sorMouseTastoGiu(int i)
{
    if (i < 0 || i >= kInput)
        return false;
    if (i < 5 && (g_bottoniGiu.load() & (1u << i)))
        return true;
    return adessoMs() < g_fino[i].load();
}

// ps2_pad.cpp, PSPadBackend::readState: the mouse on the right stick (data[4] x, data[5] y; 0x80 = rest).
void sorMouseStick(int port, uint8_t *data)
{
    if (!g_on || port != 0 || !data)
        return;
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g_lastRead).count();
    g_lastRead = now;
    if (adessoMs() < g_directFino.load())
    {   // the normal camera: the mouse turns it directly (cameraDiretta), the stick is left to the controller and the keys
        g_sx = g_sy = 0.0f;
        return;
    }
    float dx, dy;
    {
        std::lock_guard<std::mutex> l(g_lock);
        dx = g_stickX;
        dy = g_stickY;
        g_stickX = g_stickY = 0;
    }
    float tx = 0, ty = 0;
    if (dt > 0.0f && dt <= 0.25f) // after a long pause (loading) the old movement is dropped
    {
        dt = std::max(dt, 0.008f);
        tx = std::max(-1.0f, std::min(1.0f, dx / dt * g_gain));
        ty = std::max(-1.0f, std::min(1.0f, dy / dt * g_gain)) * (g_invert ? -1.0f : 1.0f);
    }
    // small smoothing: half of the way each read (a single flick keeps its whole size, spread over a few reads)
    g_sx += (tx - g_sx) * 0.5f;
    g_sy += (ty - g_sy) * 0.5f;
    if (tx == 0.0f && std::fabs(g_sx) < 0.02f)
        g_sx = 0.0f;
    if (ty == 0.0f && std::fabs(g_sy) < 0.02f)
        g_sy = 0.0f;
    if (g_sx == 0.0f && g_sy == 0.0f)
        return;
    const int dz = zonaMorta();
    data[4] = somma(data[4], g_sx, dz);
    data[5] = somma(data[5], g_sy, dz);
}

// pc/sor_camera.cpp: the free camera is detached / attached (the mouse goes to it instead of the stick).
void sorMouseFreeCameraOn(bool on)
{
    g_freeCam.store(on);
    std::lock_guard<std::mutex> l(g_lock);
    g_stickX = g_stickY = g_camX = g_camY = 0;
    g_sx = g_sy = 0;
}

// pc/sor_camera.cpp, once per frame while detached: the mouse movement as angles (radians; x right, y down, already
// inverted when MOUSE Y is inverted). False when there is none.
bool sorMouseFreeCamera(float &yaw, float &pitch)
{
    yaw = pitch = 0;
    if (!g_on)
        return false;
    float dx, dy;
    {
        std::lock_guard<std::mutex> l(g_lock);
        dx = g_camX;
        dy = g_camY;
        g_camX = g_camY = 0;
    }
    if (dx == 0.0f && dy == 0.0f)
        return false;
    yaw = dx * g_radPerPx;
    pitch = dy * g_radPerPx * (g_invert ? -1.0f : 1.0f);
    return true;
}
