// Shadow of Rome PC port - the keyboard and controller mapping (the launcher's CONTROLS > KEYBOARD and CONTROLLER pages).
//
// ps2recomp's PSPadBackend::readState (ps2xRuntime/src/lib/ps2_pad.cpp) is the only place where the game reads the
// keyboard and the controller: it calls sorTastiLeggi() here, which turns the keys and buttons into the PS2 pad's bits
// and sticks.
//
// The launcher passes the mapping in two environment variables, as it does with SOR_PAD_SWAP:
//   SOR_KEYS        = "cross=T;circle=ESCAPE;square=NONE;..." (keyboard: raylib key names without "KEY_", see kTasti)
//   SOR_PAD_BUTTONS = "cross=A;circle=NONE;..."               (controller: Xbox positions, see kPulsanti)
// NONE (cleared with Delete in the launcher): no key / no button does that action, it is never pressed.
// The launcher writes one key per action; a list "X,SPACE" (written by hand) is still understood.
// Mouse buttons and the wheel are keys too (the author, 9/11; launcher: a click or a wheel notch while a KEYBOARD row
// waits): MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE, MOUSE_X1 (the side button "back"), MOUSE_X2 ("forward"), WHEEL_UP,
// WHEEL_DOWN. They are read in pc/sor_mouse.cpp (sorMouseTastoGiu): they count only while the pointer is taken (MOUSE
// LOOK on; the click that takes it does nothing else) or, with MOUSE LOOK off, while the game window has the focus and
// the pointer is over it; never while the engine panel (F12) is open. A wheel notch holds the action down for a moment.
// No standard key is a mouse button.
// Missing, unknown or refused entries keep the standard mapping (kAzioni), so with no variables the game is as the
// launcher's RESET.
// Refused keys (the launcher refuses them too): F3 drawing dump, F6 VU1 exact check, F7 wireframe, F8 free camera, F9 black box, F11 full screen, F12 engine panel,
// 1-9 free camera points, Alt (Alt+Enter is full screen), the Windows keys.
//
// How it adds up with the other CONTROLS options:
// - SWAP (SOR_PAD_SWAP=1, controller only) comes after the mapping: whatever controller button does Cross does Circle
//   and the other way round. The keyboard is never swapped (as before).
// - VERTICAL / HORIZONTAL (SOR_PAD_INVERT_Y / _X) invert the controller's right stick, as before; the sticks themselves
//   are not remapped. The keyboard's camera keys are not inverted (as before).
// - The keys of the D-pad (arrows by default) also push the left stick the same way, as before: the game's menus read
//   only the D-pad, walking reads the stick.
// - The free camera (pc/sor_camera.cpp) keeps its own fixed keys; while it is detached the game sees a pad at rest.
// - TOGGLE (SOR_TOGGLE=l1; the author, 9/11: to stay crouched without holding the key) makes one of the ten buttons
//   cross ... r3 a toggle: one press holds it down, the next lets it go (pc/sor_interruttore.h). It works on the final
//   buttons, so the key, the mouse button and the controller button of that action all toggle it. Unset, "off" or
//   unknown: no toggle.
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "raylib.h"
#include "sor_interruttore.h"

// pc/sor_prove.cpp: with the test remote control the keys come from there and the real keyboard does not count.
bool sorTelecomandoAcceso();
bool sorTastoIniettato(int codice);
// pc/sor_mouse.cpp: mouse input i (0-6, kTopo's order) counts as held down now.
bool sorMouseTastoGiu(int i);

namespace
{
    constexpr uint16_t PAD_LEFT = 0x0080u;
    constexpr uint16_t PAD_DOWN = 0x0040u;
    constexpr uint16_t PAD_RIGHT = 0x0020u;
    constexpr uint16_t PAD_UP = 0x0010u;
    constexpr uint16_t PAD_START = 0x0008u;
    constexpr uint16_t PAD_R3 = 0x0004u;
    constexpr uint16_t PAD_L3 = 0x0002u;
    constexpr uint16_t PAD_SELECT = 0x0001u;
    constexpr uint16_t PAD_SQUARE = 0x8000u;
    constexpr uint16_t PAD_CROSS = 0x4000u;
    constexpr uint16_t PAD_CIRCLE = 0x2000u;
    constexpr uint16_t PAD_TRIANGLE = 0x1000u;
    constexpr uint16_t PAD_R1 = 0x0800u;
    constexpr uint16_t PAD_L1 = 0x0400u;
    constexpr uint16_t PAD_R2 = 0x0200u;
    constexpr uint16_t PAD_L2 = 0x0100u;

    // The actions, in the launcher's order. The first 16 are the PS2's buttons (keyboard and controller); the last 8
    // are the two sticks, keyboard only (the controller uses its own sticks).
    enum
    {
        A_CROSS, A_CIRCLE, A_SQUARE, A_TRIANGLE, A_L1, A_R1, A_L2, A_R2, A_L3, A_R3, A_START, A_SELECT,
        A_UP, A_DOWN, A_LEFT, A_RIGHT,
        A_MOVE_UP, A_MOVE_DOWN, A_MOVE_LEFT, A_MOVE_RIGHT,
        A_CAMERA_UP, A_CAMERA_DOWN, A_CAMERA_LEFT, A_CAMERA_RIGHT,
        A_COUNT
    };
    constexpr int kBottoni = 16;

    struct Azione
    {
        const char *nome;  // the name in SOR_KEYS / SOR_PAD_BUTTONS and in launcher.ini (key_<name>, pad_<name>)
        uint16_t bit;      // the PS2 pad bit (0: a stick direction)
        const char *tasti; // the standard key (one per action since 9/11: SPACE, ESCAPE, KP_0 and KP_1, the second keys
                           // of cross, circle, square and triangle, were dropped; they can be assigned in the launcher)
        const char *pad;   // today's controller button (nullptr: keyboard only)
    };
    // Standard keys = the author's own layout (9/11/2026), the same as KeyDefaults in launcher/Launcher.cs: keep them equal
    const Azione kAzioni[A_COUNT] = {
        {"cross", PAD_CROSS, "MOUSE_LEFT", "A"},
        {"circle", PAD_CIRCLE, "E", "B"},
        {"square", PAD_SQUARE, "Q", "X"},
        {"triangle", PAD_TRIANGLE, "F", "Y"},
        {"l1", PAD_L1, "LEFT_CONTROL", "LB"},
        {"r1", PAD_R1, "MOUSE_RIGHT", "RB"},
        {"l2", PAD_L2, "C", "LT"},
        {"r2", PAD_R2, "G", "RT"},
        {"l3", PAD_L3, "KP_8", "LS"},
        {"r3", PAD_R3, "KP_9", "RS"},
        {"start", PAD_START, "ESCAPE", "START"},
        {"select", PAD_SELECT, "TAB", "BACK"},
        {"up", PAD_UP, "UP", "DPAD_UP"},
        {"down", PAD_DOWN, "DOWN", "DPAD_DOWN"},
        {"left", PAD_LEFT, "LEFT", "DPAD_LEFT"},
        {"right", PAD_RIGHT, "RIGHT", "DPAD_RIGHT"},
        {"move_up", 0, "W", nullptr},
        {"move_down", 0, "S", nullptr},
        {"move_left", 0, "A", nullptr},
        {"move_right", 0, "D", nullptr},
        {"camera_up", 0, "I", nullptr},
        {"camera_down", 0, "K", nullptr},
        {"camera_left", 0, "J", nullptr},
        {"camera_right", 0, "L", nullptr},
    };

    struct Nome
    {
        const char *nome;
        int codice;
    };
    // Keys by their place on the keyboard (raylib/GLFW name them after a US keyboard, whatever the layout); the launcher
    // has the same names (Launcher.cs, KeyNames). Letters and the digits are added in nomeTasto().
    const Nome kTasti[] = {
        {"SPACE", KEY_SPACE}, {"ESCAPE", KEY_ESCAPE}, {"ENTER", KEY_ENTER}, {"TAB", KEY_TAB}, {"BACKSPACE", KEY_BACKSPACE},
        {"INSERT", KEY_INSERT}, {"DELETE", KEY_DELETE}, {"RIGHT", KEY_RIGHT}, {"LEFT", KEY_LEFT}, {"DOWN", KEY_DOWN},
        {"UP", KEY_UP}, {"PAGE_UP", KEY_PAGE_UP}, {"PAGE_DOWN", KEY_PAGE_DOWN}, {"HOME", KEY_HOME}, {"END", KEY_END},
        // not here: Caps Lock and Num Lock (raylib keeps them "down" while their light is on), Pause and Print Screen
        // (Windows sends their press and release together), so they cannot be held like a button
        {"SCROLL_LOCK", KEY_SCROLL_LOCK},
        {"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3}, {"F4", KEY_F4}, {"F5", KEY_F5}, {"F6", KEY_F6}, {"F7", KEY_F7},
        {"F8", KEY_F8}, {"F9", KEY_F9}, {"F10", KEY_F10}, {"F11", KEY_F11}, {"F12", KEY_F12},
        {"LEFT_SHIFT", KEY_LEFT_SHIFT}, {"LEFT_CONTROL", KEY_LEFT_CONTROL}, {"LEFT_ALT", KEY_LEFT_ALT},
        {"LEFT_SUPER", KEY_LEFT_SUPER}, {"RIGHT_SHIFT", KEY_RIGHT_SHIFT}, {"RIGHT_CONTROL", KEY_RIGHT_CONTROL},
        {"RIGHT_ALT", KEY_RIGHT_ALT}, {"RIGHT_SUPER", KEY_RIGHT_SUPER}, {"MENU", KEY_KB_MENU},
        {"APOSTROPHE", KEY_APOSTROPHE}, {"COMMA", KEY_COMMA}, {"MINUS", KEY_MINUS}, {"PERIOD", KEY_PERIOD},
        {"SLASH", KEY_SLASH}, {"SEMICOLON", KEY_SEMICOLON}, {"EQUAL", KEY_EQUAL}, {"LEFT_BRACKET", KEY_LEFT_BRACKET},
        {"BACKSLASH", KEY_BACKSLASH}, {"RIGHT_BRACKET", KEY_RIGHT_BRACKET}, {"GRAVE", KEY_GRAVE},
        {"WORLD_2", 162}, // GLFW_KEY_WORLD_2: the extra key next to left Shift on European keyboards (< >); raylib has no name
        {"KP_0", KEY_KP_0}, {"KP_1", KEY_KP_1}, {"KP_2", KEY_KP_2}, {"KP_3", KEY_KP_3}, {"KP_4", KEY_KP_4},
        {"KP_5", KEY_KP_5}, {"KP_6", KEY_KP_6}, {"KP_7", KEY_KP_7}, {"KP_8", KEY_KP_8}, {"KP_9", KEY_KP_9},
        {"KP_DECIMAL", KEY_KP_DECIMAL}, {"KP_DIVIDE", KEY_KP_DIVIDE}, {"KP_MULTIPLY", KEY_KP_MULTIPLY},
        {"KP_SUBTRACT", KEY_KP_SUBTRACT}, {"KP_ADD", KEY_KP_ADD}, {"KP_ENTER", KEY_KP_ENTER}, {"KP_EQUAL", KEY_KP_EQUAL},
    };
    // The mouse as keys: codes from kTopoBase (raylib's key codes end at 348), kTopoBase + i = sor_mouse.cpp's input i
    // (0-4 the buttons, raylib MOUSE_BUTTON_LEFT/RIGHT/MIDDLE/SIDE/EXTRA; 5-6 the wheel).
    constexpr int kTopoBase = 0x1000;
    constexpr int kTopoQuanti = 7;
    const Nome kTopo[kTopoQuanti] = {
        {"MOUSE_LEFT", kTopoBase + 0}, {"MOUSE_RIGHT", kTopoBase + 1}, {"MOUSE_MIDDLE", kTopoBase + 2},
        {"MOUSE_X1", kTopoBase + 3}, {"MOUSE_X2", kTopoBase + 4}, {"WHEEL_UP", kTopoBase + 5}, {"WHEEL_DOWN", kTopoBase + 6},
    };
    // Controller buttons by their place, with Xbox names (raylib gives every known controller the same places:
    // A = the bottom button, a PlayStation controller's cross).
    const Nome kPulsanti[] = {
        {"A", GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {"B", GAMEPAD_BUTTON_RIGHT_FACE_RIGHT},
        {"X", GAMEPAD_BUTTON_RIGHT_FACE_LEFT}, {"Y", GAMEPAD_BUTTON_RIGHT_FACE_UP},
        {"LB", GAMEPAD_BUTTON_LEFT_TRIGGER_1}, {"RB", GAMEPAD_BUTTON_RIGHT_TRIGGER_1},
        {"LT", GAMEPAD_BUTTON_LEFT_TRIGGER_2}, {"RT", GAMEPAD_BUTTON_RIGHT_TRIGGER_2},
        {"LS", GAMEPAD_BUTTON_LEFT_THUMB}, {"RS", GAMEPAD_BUTTON_RIGHT_THUMB},
        {"START", GAMEPAD_BUTTON_MIDDLE_RIGHT}, {"BACK", GAMEPAD_BUTTON_MIDDLE_LEFT},
        {"DPAD_UP", GAMEPAD_BUTTON_LEFT_FACE_UP}, {"DPAD_DOWN", GAMEPAD_BUTTON_LEFT_FACE_DOWN},
        {"DPAD_LEFT", GAMEPAD_BUTTON_LEFT_FACE_LEFT}, {"DPAD_RIGHT", GAMEPAD_BUTTON_LEFT_FACE_RIGHT},
    };

    // A key's code from its name, -1 if unknown or refused (the reason in *perche).
    int codiceTasto(const std::string &n, const char **perche)
    {
        *perche = "unknown";
        if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z')
            return KEY_A + (n[0] - 'A');
        if (n.size() == 1 && n[0] >= '0' && n[0] <= '9')
        {
            if (n[0] != '0')
            {
                *perche = "kept for the free camera's points";
                return -1;
            }
            return KEY_ZERO;
        }
        for (const Nome &t : kTopo)
            if (n == t.nome)
                return t.codice;
        for (const Nome &t : kTasti)
            if (n == t.nome)
            {
                if (t.codice == KEY_F3 || t.codice == KEY_F6 || t.codice == KEY_F7 || t.codice == KEY_F8 || t.codice == KEY_F9 || t.codice == KEY_F11 || t.codice == KEY_F12)
                {
                    *perche = "kept for F3 drawing dump, F6 VU1 exact check, F7 wireframe, F8 free camera, F9 black box, F11 full screen, F12 engine panel";
                    return -1;
                }
                if (t.codice == KEY_LEFT_ALT || t.codice == KEY_RIGHT_ALT)
                {
                    *perche = "kept for Alt+Enter (full screen)";
                    return -1;
                }
                if (t.codice == KEY_LEFT_SUPER || t.codice == KEY_RIGHT_SUPER)
                {
                    *perche = "the Windows key opens the Start menu";
                    return -1;
                }
                return t.codice;
            }
        return -1;
    }

    int codicePulsante(const std::string &n)
    {
        for (const Nome &p : kPulsanti)
            if (n == p.nome)
                return p.codice;
        return -1;
    }

    std::string maiuscolo(std::string s)
    {
        for (char &c : s)
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        return s;
    }

    std::string pulito(const std::string &s)
    {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
            ++a;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
            --b;
        return s.substr(a, b - a);
    }

    std::vector<std::string> dividi(const std::string &s, char sep)
    {
        std::vector<std::string> out;
        size_t a = 0;
        while (a <= s.size())
        {
            size_t b = s.find(sep, a);
            if (b == std::string::npos)
                b = s.size();
            out.push_back(pulito(s.substr(a, b - a)));
            a = b + 1;
        }
        return out;
    }

    struct Mappa
    {
        std::vector<int> tasti[A_COUNT];     // keyboard: any of these keys does the action
        std::vector<std::string> nomiT[A_COUNT];
        int pad[kBottoni];                   // controller: the button that does the action
        std::string nomiP[kBottoni];
        bool swap = false;
        float invX = 1.0f, invY = 1.0f;
        unsigned topo = 0;                   // bit i: mouse input i (kTopo) does an action
        uint16_t interruttore = 0;           // TOGGLE: the PS2 bit one press holds down (0: none)
    };

    // "a,b,c" -> codes; false if empty or any name is unknown or refused (the entry is then ignored)
    bool leggiTasti(const std::string &lista, std::vector<int> &codici, std::vector<std::string> &nomi, std::string &errore)
    {
        codici.clear();
        nomi.clear();
        if (maiuscolo(pulito(lista)) == "NONE")
        {
            nomi.push_back("NONE"); // cleared in the launcher: no key does it
            return true;
        }
        for (const std::string &pezzo : dividi(lista, ','))
        {
            if (pezzo.empty())
                continue;
            const std::string n = maiuscolo(pezzo);
            const char *perche = "";
            const int c = codiceTasto(n, &perche);
            if (c < 0)
            {
                errore = n + " (" + perche + ")";
                return false;
            }
            codici.push_back(c);
            nomi.push_back(n);
        }
        if (codici.empty())
        {
            errore = "no key";
            return false;
        }
        return true;
    }

    int trovaAzione(const std::string &nome, int quante)
    {
        for (int i = 0; i < quante; ++i)
            if (nome == kAzioni[i].nome)
                return i;
        return -1;
    }

    bool acceso(const char *var)
    {
        const char *v = std::getenv(var);
        return v && v[0] == '1';
    }

    Mappa costruisci()
    {
        Mappa m;
        std::string err;
        for (int i = 0; i < A_COUNT; ++i)
            leggiTasti(kAzioni[i].tasti, m.tasti[i], m.nomiT[i], err);
        for (int i = 0; i < kBottoni; ++i)
        {
            m.pad[i] = codicePulsante(kAzioni[i].pad);
            m.nomiP[i] = kAzioni[i].pad;
        }
        m.swap = acceso("SOR_PAD_SWAP");
        m.invX = acceso("SOR_PAD_INVERT_X") ? -1.0f : 1.0f;
        m.invY = acceso("SOR_PAD_INVERT_Y") ? -1.0f : 1.0f;

        int cambiatiT = 0, cambiatiP = 0;
        if (const char *v = std::getenv("SOR_KEYS"))
            for (const std::string &voce : dividi(v, ';'))
            {
                if (voce.empty())
                    continue;
                const size_t eq = voce.find('=');
                const std::string nome = pulito(voce.substr(0, eq == std::string::npos ? voce.size() : eq));
                const int a = trovaAzione(nome, A_COUNT);
                std::vector<int> codici;
                std::vector<std::string> nomi;
                std::string errore;
                if (a < 0 || eq == std::string::npos)
                    std::printf("[sor] keys: '%s' not understood, ignored\n", voce.c_str());
                else if (!leggiTasti(voce.substr(eq + 1), codici, nomi, errore))
                    std::printf("[sor] keys: %s refused: %s; kept %s\n", nome.c_str(), errore.c_str(), kAzioni[a].tasti);
                else
                {
                    m.tasti[a] = codici;
                    m.nomiT[a] = nomi;
                    ++cambiatiT;
                }
            }
        if (const char *v = std::getenv("SOR_PAD_BUTTONS"))
            for (const std::string &voce : dividi(v, ';'))
            {
                if (voce.empty())
                    continue;
                const size_t eq = voce.find('=');
                const std::string nome = pulito(voce.substr(0, eq == std::string::npos ? voce.size() : eq));
                const int a = trovaAzione(nome, kBottoni);
                const std::string p = eq == std::string::npos ? "" : maiuscolo(pulito(voce.substr(eq + 1)));
                const int c = codicePulsante(p);
                if (a < 0 || eq == std::string::npos)
                    std::printf("[sor] pad: '%s' not understood, ignored\n", voce.c_str());
                else if (p == "NONE")
                {
                    m.pad[a] = -1; // cleared in the launcher: no button does it
                    m.nomiP[a] = p;
                    ++cambiatiP;
                }
                else if (c < 0)
                    std::printf("[sor] pad: %s refused: '%s' unknown; kept %s\n", nome.c_str(), p.c_str(), kAzioni[a].pad);
                else
                {
                    m.pad[a] = c;
                    m.nomiP[a] = p;
                    ++cambiatiP;
                }
            }

        // TOGGLE (the launcher's CONTROLS > TOGGLE): one of the ten buttons cross ... r3
        if (const char *v = std::getenv("SOR_TOGGLE"))
        {
            std::string nome;
            for (const char *q = v; *q; ++q)
                if (!std::isspace(static_cast<unsigned char>(*q)))
                    nome += static_cast<char>(std::tolower(static_cast<unsigned char>(*q)));
            if (!nome.empty() && nome != "off" && nome != "0")
            {
                const int a = trovaAzione(nome, A_R3 + 1);
                if (a >= 0)
                {
                    m.interruttore = kAzioni[a].bit;
                    std::string tasti;
                    for (size_t k = 0; k < m.nomiT[a].size(); ++k)
                        tasti += (k ? "+" : "") + m.nomiT[a][k];
                    std::printf("[sor] toggle: %s (key %s, controller %s): one press holds it down, the next lets it go\n",
                                kAzioni[a].nome, tasti.c_str(), m.nomiP[a].c_str());
                }
                else
                    std::printf("[sor] toggle: '%s' unknown (only cross ... r3); off\n", nome.c_str());
            }
        }

        // the mouse inputs some action uses (sor_mouse.cpp reads only these)
        std::string topo;
        for (int i = 0; i < A_COUNT; ++i)
            for (size_t k = 0; k < m.tasti[i].size(); ++k)
                if (m.tasti[i][k] >= kTopoBase && m.tasti[i][k] < kTopoBase + kTopoQuanti)
                {
                    m.topo |= 1u << (m.tasti[i][k] - kTopoBase);
                    topo += std::string(topo.empty() ? "" : ", ") + m.nomiT[i][k] + "=" + kAzioni[i].nome;
                }
        if (!topo.empty())
            topo = "; mouse: " + topo +
                   (acceso("SOR_MOUSE_LOOK") ? " (they count only while the pointer is taken: click in the game window; that click does nothing else)"
                                             : " (MOUSE LOOK off: they count only while the game window has the focus and the pointer is over it)");

        // one line each (short), then a warning if one key or button does two things
        std::string riga = "[sor] keys:";
        for (int i = 0; i < A_COUNT; ++i)
        {
            riga += std::string(" ") + kAzioni[i].nome + "=";
            for (size_t k = 0; k < m.nomiT[i].size(); ++k)
                riga += (k ? "+" : "") + m.nomiT[i][k];
        }
        std::printf("%s (%d set in the launcher%s)\n", riga.c_str(), cambiatiT, topo.c_str());
        riga = "[sor] pad:";
        for (int i = 0; i < kBottoni; ++i)
            riga += std::string(" ") + kAzioni[i].nome + "=" + m.nomiP[i];
        std::printf("%s (%d set in the launcher; cross/circle swap %s, right stick inverted x %s y %s)\n", riga.c_str(),
                    cambiatiP, m.swap ? "on" : "off", m.invX < 0 ? "on" : "off", m.invY < 0 ? "on" : "off");
        for (int i = 0; i < A_COUNT; ++i)
            for (int j = i + 1; j < A_COUNT; ++j)
                for (size_t a = 0; a < m.tasti[i].size(); ++a)
                    for (size_t b = 0; b < m.tasti[j].size(); ++b)
                        if (m.tasti[i][a] == m.tasti[j][b])
                            std::printf("[sor] keys: %s does both %s and %s\n", m.nomiT[i][a].c_str(), kAzioni[i].nome, kAzioni[j].nome);
        for (int i = 0; i < kBottoni; ++i)
            for (int j = i + 1; j < kBottoni; ++j)
                if (m.pad[i] >= 0 && m.pad[i] == m.pad[j])
                    std::printf("[sor] pad: %s does both %s and %s\n", m.nomiP[i].c_str(), kAzioni[i].nome, kAzioni[j].nome);
        std::fflush(stdout);
        return m;
    }

    bool tastoGiu(int codice)
    {
        if (sorTelecomandoAcceso())
            return sorTastoIniettato(codice);
        return codice >= kTopoBase ? sorMouseTastoGiu(codice - kTopoBase) : IsKeyDown(codice);
    }

    bool unoGiu(const std::vector<int> &codici)
    {
        for (int c : codici)
            if (tastoGiu(c))
                return true;
        return false;
    }

    // Built once, on the first use (game thread or window thread: a function-local static is safe for both).
    const Mappa &mappa()
    {
        static const Mappa m = costruisci();
        return m;
    }
}

// pc/sor_mouse.cpp: true if an action of the keyboard mapping uses this key (Esc frees the mouse pointer only when
// no action uses it).
bool sorTastoDiUnAzione(int codice)
{
    const Mappa &m = mappa();
    for (int i = 0; i < A_COUNT; ++i)
        for (int c : m.tasti[i])
            if (c == codice)
                return true;
    return false;
}

// pc/sor_mouse.cpp (main thread, once per turn of the window loop): bit i set = mouse input i (MOUSE_LEFT ... WHEEL_DOWN,
// 0-6) does an action; 0 = the mouse's buttons and wheel are not read at all.
unsigned sorTastiMouseUsati()
{
    return mappa().topo;
}

// btns: the PS2 buttons, 1 = up (a pressed button clears its bit); data[4..7]: right x, right y, left x, left y (0x80 = rest).
void sorTastiLeggi(bool useGamepad, int gamepad, uint16_t &btns, uint8_t *data)
{
    const Mappa &m = mappa();
    if (useGamepad)
    {
        for (int i = 0; i < kBottoni; ++i)
            if (m.pad[i] >= 0 && IsGamepadButtonDown(gamepad, m.pad[i]))
            {
                uint16_t bit = kAzioni[i].bit;
                if (m.swap && (bit == PAD_CROSS || bit == PAD_CIRCLE))
                    bit = bit == PAD_CROSS ? PAD_CIRCLE : PAD_CROSS;
                btns &= static_cast<uint16_t>(~bit);
            }

        float lx = GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_X);
        float ly = GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_Y);
        float rx = m.invX * GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_X);
        float ry = m.invY * GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_Y);
        data[6] = static_cast<uint8_t>(128 + lx * 127);
        data[7] = static_cast<uint8_t>(128 + ly * 127);
        data[4] = static_cast<uint8_t>(128 + rx * 127);
        data[5] = static_cast<uint8_t>(128 + ry * 127);
    }

    // the keyboard always counts, even with a controller connected
    bool giu[A_COUNT];
    for (int i = 0; i < A_COUNT; ++i)
    {
        giu[i] = unoGiu(m.tasti[i]);
        if (giu[i] && i < kBottoni)
            btns &= static_cast<uint16_t>(~kAzioni[i].bit);
    }
    // the left stick: the move keys, and the D-pad keys too (the game walks with the stick; its menus read only the D-pad)
    const auto levetta = [](bool meno, bool piu, uint8_t &asse)
    {
        if (meno != piu)
            asse = meno ? 0x00u : 0xFFu;
    };
    levetta(giu[A_LEFT] || giu[A_MOVE_LEFT], giu[A_RIGHT] || giu[A_MOVE_RIGHT], data[6]);
    levetta(giu[A_UP] || giu[A_MOVE_UP], giu[A_DOWN] || giu[A_MOVE_DOWN], data[7]);
    levetta(giu[A_CAMERA_LEFT], giu[A_CAMERA_RIGHT], data[4]);
    levetta(giu[A_CAMERA_UP], giu[A_CAMERA_DOWN], data[5]);

    // TOGGLE: on the final buttons (keyboard, mouse and controller together); one press holds, the next lets go
    static SorInterruttore interruttore;
    interruttore.bit = m.interruttore;
    btns = interruttore.applica(btns);
}
