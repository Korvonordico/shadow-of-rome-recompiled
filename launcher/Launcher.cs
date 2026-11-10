// Shadow of Rome PC port - launcher.
// Build: build.bat, or csc -target:winexe -win32icon:ShadowOfRome.ico -out:ShadowOfRome.exe Launcher.cs
// (the .NET Framework compiler that ships with Windows).
// Every entry works: Play, Options, Graphics, Controls, Mods and Quit.
// Graphics: the video card (PCSX2's GS, from sor-gs.dll next to sor.exe) at 1x to 4x the PS2's size, or the
// processor (the port's own drawing, as in 0.0.7). Saved in launcher.ini only by Apply; Play passes it to the game
// (SOR_GRAPHICS: "gpu <scale> <api>" or "cpu"; SOR_WIDESCREEN, SOR_60HZ; SORGS_VSYNC: every picture in step with
// the screen, vsync= in launcher.ini; SORGS_OSD: the video card's FPS and frame-time statistics on screen, osd=; SORGS_SHARPNESS: 0-2, sharpness=;
// SORGS_FXAA: PCSX2's FXAA antialiasing, antialiasing=fxaa|off).
// OPTIONS > SMOOTH AUDIO: SOR_AUDIO_STRETCH (smoothaudio= in launcher.ini, on unless 0): when the game slows down the
// sound stretches a little (SoundTouch, pc/sor_audio.cpp) instead of having holes; 0 = the old sound.
// OPTIONS > SPEED TESTS (page 7, for tests): VU1 ENGINE passes SOR_VU1=nostro|pcsx2|confronto (vu1engine= in launcher.ini:
// ours, PCSX2's microVU from sor-vu.dll, or both compared in the log); TIMINGS passes SOR_TEMPI=1 (timings=1): [sor-time] lines.
// logo.png (next to the exe) is optional and NOT part of the repository: it is the project's logo (docs/logo.png
// in the repository). Without it the title is drawn in text.
// The first Play reads from the player's own disc image the files the game needs (the executable, SYSTEM.CNF and
// the IOP modules) into disco\, next to the exe (or ..\disco when the game was built with scripts/setup.sh).
using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Windows.Forms;

class Launcher : Form
{
    const string Version = "2.6";     // the version of the whole port (CHANGELOG.md)
    static readonly string[] Items = { "PLAY", "OPTIONS", "GRAPHICS", "CONTROLS", "MODS", "QUIT" };
    static readonly string[] Latin = { "INCIPE", "OPTIONES", "SPECIES", "IMPERIVM", "MVTATIONES", "EXI" };
    static readonly bool[] Ready = { true, true, true, true, true, true };
    int sel = 0, hover = -1;
    string message = "";
    int page = 0;                      // 0: the main menu, 1: graphics, 2: options, 3: mods, 4: controls, 5: debug (hidden, Ctrl+D)
    static readonly string[] OItems = { "DISPLAY", "BGM VOLUME", "SE VOLUME", "MENU SOUNDS", "SMOOTH AUDIO", "BACKGROUND", "UPDATES", "DEBUG", "SPEED TESTS", "APPLY", "BACK" };
    static readonly string[] OLatin = { "SPECTACVLVM", "CANTVS", "SONITVS", "STREPITVS", "LENITAS", "CINIS", "NOVA", "INSPECTIO", "PROBATIO", "CONFIRMA", "REDI" };
    static readonly string[] MItems = { "TEXTURE MODS", "MODS FOLDER", "APPLY", "BACK" };
    static readonly string[] MLatin = { "MVTATIONES", "APERI", "CONFIRMA", "REDI" };
    // page 7, SPEED TESTS (OPTIONS > SPEED TESTS, 10/11/2026): the VU1 engine (SOR_VU1) and the time measure (SOR_TEMPI)
    static readonly string[] TItems = { "VU1 ENGINE", "TIMINGS", "UPSCALE FIX", "APPLY", "BACK" };
    static readonly string[] TLatin = { "VECTORES", "TEMPORA", "ACIES", "CONFIRMA", "REDI" };
    // KEYBOARD and CONTROLLER open the mapping page (page 6); BUTTONS, VERTICAL and HORIZONTAL change a value.
    static readonly string[] CItems = { "KEYBOARD", "CONTROLLER", "BUTTONS", "VERTICAL", "HORIZONTAL", "MOUSE LOOK", "MOUSE SPEED", "MOUSE Y", "TOGGLE", "APPLY", "BACK" };
    static readonly string[] CLatin = { "CLAVES", "MANVS", "PERMVTATIO", "SVRSVM DEORSVM", "DEXTRA SINISTRA", "ASPECTVS MVRE", "VELOCITAS", "MVS INVERSVS", "SEMEL PRESSVM", "CONFIRMA", "REDI" };
    bool mouseLook = true, mouseInvY; int mouseSpeed = 5;   // mouse look (pc/sor_mouse.cpp): on, speed 1..10, up = stick up
    int toggleBtn;   // CONTROLS > TOGGLE: 0 off, 1..10 = cross ... r3 (MapNames[toggleBtn - 1])
    bool swap, invY, invX;             // controls (controller only): cross and circle swapped; the camera's right stick inverted
    int keysChanged, padChanged;       // the CONTROLS page: how many keys / buttons differ from the standard ones
    const string ControlsNote = "BUTTONS, VERTICAL AND HORIZONTAL ARE FOR THE CONTROLLER ONLY";

    // ---- KEYBOARD and CONTROLLER mapping (the author, 9/11): CONTROLS > KEYBOARD / CONTROLLER open page 6.
    // launcher.ini: key_<action>=X,SPACE (keyboard: the names GLFW/raylib give the keys, by their place on the keyboard,
    // as the game reads them) and pad_<action>=A (controller: Xbox places, A = the bottom button). Only the keys that
    // differ from the standard ones are saved; Play passes them in SOR_KEYS / SOR_PAD_BUTTONS ("cross=X,SPACE;..."),
    // read by sor.exe (pc/sor_tasti.cpp). A key can also be a mouse button or the wheel (MouseKeys: key_cross=MOUSE_LEFT;
    // on the page a click or a wheel notch while a row waits, Backspace cancels). In the game SWAP comes after the controller mapping: whatever button does
    // Cross does Circle and the other way round; VERTICAL / HORIZONTAL still invert the controller's right stick.
    static readonly string[] MapNames = { "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3", "start", "select", "up", "down", "left", "right", "move_up", "move_down", "move_left", "move_right", "camera_up", "camera_down", "camera_left", "camera_right" };
    static readonly string[] MapItems = { "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "L1", "R1", "L2", "R2", "L3", "R3", "START", "SELECT", "UP", "DOWN", "LEFT", "RIGHT", "MOVE UP", "MOVE DOWN", "MOVE LEFT", "MOVE RIGHT", "LOOK UP", "LOOK DOWN", "LOOK LEFT", "LOOK RIGHT" };
    static readonly string[] MapLatin = { "CRVX", "CIRCVLVS", "QVADRATVM", "TRIANGVLVM", "LAEVA I", "DEXTRA I", "LAEVA II", "DEXTRA II", "LAEVA III", "DEXTRA III", "INITIVM", "ELECTIO", "SVRSVM", "DEORSVM", "SINISTRORSVM", "DEXTRORSVM", "GRADVS", "GRADVS", "GRADVS", "GRADVS", "ASPECTVS", "ASPECTVS", "ASPECTVS", "ASPECTVS" };
    // the keys the game always had (pc/sor_tasti.cpp, kAzioni): with nothing saved the game is exactly as before
    static readonly string[] KeyDefaults = { "MOUSE_LEFT", "E", "Q", "F", "LEFT_CONTROL", "MOUSE_RIGHT", "C", "G", "KP_8", "KP_9", "ESCAPE", "TAB", "UP", "DOWN", "LEFT", "RIGHT", "W", "S", "A", "D", "I", "K", "J", "L" };
    // the controller's buttons, in MapNames' order: this order is also the standard mapping
    static readonly string[] PadNames = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "START", "BACK", "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT" };
    static readonly string[] PadLabels = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "L STICK", "R STICK", "START", "BACK", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT" };
    static readonly string[] PadPlaces = { "BOTTOM BUTTON", "RIGHT BUTTON", "LEFT BUTTON", "TOP BUTTON", "LEFT BUMPER", "RIGHT BUMPER", "LEFT TRIGGER", "RIGHT TRIGGER", "PRESS THE LEFT STICK", "PRESS THE RIGHT STICK", "START / MENU BUTTON", "BACK / VIEW BUTTON", "D-PAD", "D-PAD", "D-PAD", "D-PAD" };
    // XInput's bit for each of PadNames (LT and RT: the triggers pressed past a quarter, 0x10000 and 0x20000 here)
    static readonly int[] PadXInput = { 0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x10000, 0x20000, 0x0040, 0x0080, 0x0010, 0x0020, 0x0001, 0x0002, 0x0004, 0x0008 };
    const int PadActions = 16;          // the controller maps the 16 buttons; the keyboard also the two sticks (24 rows)
    const int MapVisible = 7;           // action rows shown at once (a scrolling list); then RESET, APPLY, BACK
    static readonly string[] MapRowSlots = new string[10];   // page 6 draws its own rows: only the count is used (Hit)
    bool mapPad;                        // page 6: false the keyboard, true the controller
    int mapScroll;                      // the first action shown
    int capture = -1;                   // the action waiting for its new key or button, -1 none
    string dupName; int dupAction = -1; // the last key/button refused as already used: pressed again, the two swap
    readonly string[] mapKeys = new string[MapNames.Length], savedKeys = new string[MapNames.Length];
    readonly string[] mapPads = new string[PadActions], savedPads = new string[PadActions];
    int MapCount { get { return mapPad ? PadActions : MapNames.Length; } }
    int MapAction(int row) { return row < MapVisible ? mapScroll + row : -1; }

    // The keys by their place (scancode, 0x100 = extended) with the names GLFW/raylib give them (GLFW's win32_init.c,
    // the copy inside raylib 5.5): the game reads keys by place, so a key chosen here is the same key in the game
    // whatever the keyboard's language. Keys missing here cannot be read by the game.
    static readonly Dictionary<int, string> ScanNames = new Dictionary<int, string> {
        {0x00B, "0"}, {0x002, "1"}, {0x003, "2"}, {0x004, "3"}, {0x005, "4"}, {0x006, "5"}, {0x007, "6"}, {0x008, "7"}, {0x009, "8"}, {0x00A, "9"},
        {0x01E, "A"}, {0x030, "B"}, {0x02E, "C"}, {0x020, "D"}, {0x012, "E"}, {0x021, "F"}, {0x022, "G"}, {0x023, "H"}, {0x017, "I"},
        {0x024, "J"}, {0x025, "K"}, {0x026, "L"}, {0x032, "M"}, {0x031, "N"}, {0x018, "O"}, {0x019, "P"}, {0x010, "Q"}, {0x013, "R"},
        {0x01F, "S"}, {0x014, "T"}, {0x016, "U"}, {0x02F, "V"}, {0x011, "W"}, {0x02D, "X"}, {0x015, "Y"}, {0x02C, "Z"},
        {0x028, "APOSTROPHE"}, {0x02B, "BACKSLASH"}, {0x033, "COMMA"}, {0x00D, "EQUAL"}, {0x029, "GRAVE"}, {0x01A, "LEFT_BRACKET"},
        {0x00C, "MINUS"}, {0x034, "PERIOD"}, {0x01B, "RIGHT_BRACKET"}, {0x027, "SEMICOLON"}, {0x035, "SLASH"}, {0x056, "WORLD_2"},
        {0x00E, "BACKSPACE"}, {0x153, "DELETE"}, {0x14F, "END"}, {0x01C, "ENTER"}, {0x001, "ESCAPE"}, {0x147, "HOME"}, {0x152, "INSERT"},
        {0x15D, "MENU"}, {0x151, "PAGE_DOWN"}, {0x149, "PAGE_UP"}, {0x045, "PAUSE"}, {0x039, "SPACE"}, {0x00F, "TAB"},
        {0x03A, "CAPS_LOCK"}, {0x145, "NUM_LOCK"}, {0x046, "SCROLL_LOCK"},
        {0x03B, "F1"}, {0x03C, "F2"}, {0x03D, "F3"}, {0x03E, "F4"}, {0x03F, "F5"}, {0x040, "F6"}, {0x041, "F7"}, {0x042, "F8"},
        {0x043, "F9"}, {0x044, "F10"}, {0x057, "F11"}, {0x058, "F12"},
        {0x038, "LEFT_ALT"}, {0x01D, "LEFT_CONTROL"}, {0x02A, "LEFT_SHIFT"}, {0x15B, "LEFT_SUPER"}, {0x137, "PRINT_SCREEN"},
        {0x138, "RIGHT_ALT"}, {0x11D, "RIGHT_CONTROL"}, {0x036, "RIGHT_SHIFT"}, {0x15C, "RIGHT_SUPER"},
        {0x150, "DOWN"}, {0x14B, "LEFT"}, {0x14D, "RIGHT"}, {0x148, "UP"},
        {0x052, "KP_0"}, {0x04F, "KP_1"}, {0x050, "KP_2"}, {0x051, "KP_3"}, {0x04B, "KP_4"}, {0x04C, "KP_5"}, {0x04D, "KP_6"},
        {0x047, "KP_7"}, {0x048, "KP_8"}, {0x049, "KP_9"}, {0x04E, "KP_ADD"}, {0x053, "KP_DECIMAL"}, {0x135, "KP_DIVIDE"},
        {0x11C, "KP_ENTER"}, {0x059, "KP_EQUAL"}, {0x037, "KP_MULTIPLY"}, {0x04A, "KP_SUBTRACT"} };
    // How a key is written on the page (letters, digits and signs: the character on this keyboard's key, see KeyLabel)
    static readonly Dictionary<string, string> KeyWords = new Dictionary<string, string> {
        {"SPACE", "SPACE"}, {"ESCAPE", "ESC"}, {"ENTER", "ENTER"}, {"TAB", "TAB"}, {"BACKSPACE", "BACKSPACE"}, {"INSERT", "INSERT"},
        {"DELETE", "DELETE"}, {"RIGHT", "ARROW RIGHT"}, {"LEFT", "ARROW LEFT"}, {"DOWN", "ARROW DOWN"}, {"UP", "ARROW UP"},
        {"PAGE_UP", "PAGE UP"}, {"PAGE_DOWN", "PAGE DOWN"}, {"HOME", "HOME"}, {"END", "END"}, {"SCROLL_LOCK", "SCROLL LOCK"},
        {"LEFT_SHIFT", "LEFT SHIFT"}, {"RIGHT_SHIFT", "RIGHT SHIFT"}, {"LEFT_CONTROL", "LEFT CTRL"}, {"RIGHT_CONTROL", "RIGHT CTRL"},
        {"LEFT_ALT", "ALT"}, {"RIGHT_ALT", "ALT GR"}, {"LEFT_SUPER", "WINDOWS"}, {"RIGHT_SUPER", "WINDOWS"}, {"MENU", "MENU KEY"},
        {"CAPS_LOCK", "CAPS LOCK"}, {"NUM_LOCK", "NUM LOCK"}, {"PAUSE", "PAUSE"}, {"PRINT_SCREEN", "PRINT SCREEN"},
        {"KP_DECIMAL", "NUM ."}, {"KP_DIVIDE", "NUM /"}, {"KP_MULTIPLY", "NUM *"}, {"KP_SUBTRACT", "NUM -"}, {"KP_ADD", "NUM +"},
        {"KP_ENTER", "NUM ENTER"}, {"KP_EQUAL", "NUM ="},
        {"MOUSE_LEFT", "LEFT CLICK"}, {"MOUSE_RIGHT", "RIGHT CLICK"}, {"MOUSE_MIDDLE", "MIDDLE CLICK"}, {"MOUSE_X1", "MOUSE BACK"},
        {"MOUSE_X2", "MOUSE FORWARD"}, {"WHEEL_UP", "WHEEL UP"}, {"WHEEL_DOWN", "WHEEL DOWN"} };
    // Mouse buttons and the wheel can be keys too (the author, 9/11): a click or a wheel notch while a KEYBOARD row waits.
    // The same names as the game (pc/sor_tasti.cpp, kTopo); X1 is the side button "back", X2 "forward". In the game they
    // count only while the pointer is taken (MOUSE LOOK on) or over the focused game window (MOUSE LOOK off).
    static readonly string[] MouseKeys = { "MOUSE_LEFT", "MOUSE_RIGHT", "MOUSE_MIDDLE", "MOUSE_X1", "MOUSE_X2", "WHEEL_UP", "WHEEL_DOWN" };
    [System.Runtime.InteropServices.DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint mapType);
    static string KeyLabel(string n)
    {
        string w;
        if (n == "NONE") return "NONE";
        if (KeyWords.TryGetValue(n, out w)) return w;
        if (n.StartsWith("KP_")) return "NUM " + n.Substring(3);
        if (n.Length >= 2 && n.Length <= 3 && n[0] == 'F' && char.IsDigit(n[1])) return n;   // F1-F12
        foreach (var kv in ScanNames)
            if (kv.Value == n && kv.Key < 0x100)
            {
                try
                {   // MAPVK_VSC_TO_VK, then MAPVK_VK_TO_CHAR (the top bit marks an accent key)
                    uint ch = MapVirtualKey(MapVirtualKey((uint)kv.Key, 1), 2) & 0x7FFFFFFF;
                    if (ch > 32 && ch < 0xFFFF) return ((char)ch).ToString().ToUpperInvariant();
                }
                catch (Exception) { }
                break;
            }
        return n.Replace('_', ' ');
    }
    static string KeysLabel(string list)
    {
        var l = new List<string>();
        foreach (var k in list.Split(',')) l.Add(KeyLabel(k));
        return string.Join(" / ", l.ToArray());
    }
    static string PadLabel(string p) { int i = Array.IndexOf(PadNames, p); return i >= 0 ? PadLabels[i] : p; }
    // Why the game cannot take a key, "" if it can. Every message says what to do (the author, 9/11).
    static string Refused(string n)
    {
        if (n == "F3" || n == "F6") return n + " IS A TEST TOOL  ·  PRESS ANOTHER KEY";
        if (n == "F7") return "F7 IS THE WIREFRAME VIEW  ·  PRESS ANOTHER KEY";
        if (n == "F8") return "F8 IS THE FREE CAMERA  ·  PRESS ANOTHER KEY";
        if (n == "F9") return "F9 IS THE BLACK BOX  ·  PRESS ANOTHER KEY";
        if (n == "F11") return "F11 IS FULL SCREEN  ·  PRESS ANOTHER KEY";
        if (n == "F12") return "F12 IS THE ENGINE PANEL  ·  PRESS ANOTHER KEY";
        if (n.Length == 1 && n[0] >= '1' && n[0] <= '9') return "1 TO 9 ARE THE FREE CAMERA'S POINTS  ·  PRESS ANOTHER KEY";
        if (n == "LEFT_ALT" || n == "RIGHT_ALT") return "ALT STAYS FREE FOR ALT+ENTER (FULL SCREEN)  ·  PRESS ANOTHER KEY";
        if (n == "LEFT_SUPER" || n == "RIGHT_SUPER") return "THE WINDOWS KEY OPENS THE START MENU  ·  PRESS ANOTHER KEY";
        if (n == "CAPS_LOCK" || n == "NUM_LOCK" || n == "PAUSE" || n == "PRINT_SCREEN") return "THE GAME CANNOT HOLD THIS KEY DOWN  ·  PRESS ANOTHER KEY";
        return "";
    }
    static bool ValidKey(string n) { return (ScanNames.ContainsValue(n) || Array.IndexOf(MouseKeys, n) >= 0) && Refused(n) == ""; }
    // A saved list of keys, or the standard one when it is empty or has a key the game would refuse
    static string CleanKeys(string s, string standard)
    {
        foreach (var p in s.Split(','))
        {   // one key per action (the author, 9/11): the first one; NONE = the action has no key
            string k = p.Trim().ToUpperInvariant();
            if (k == "") continue;
            if (k == "NONE") return "NONE";
            return ValidKey(k) ? k : standard;
        }
        return standard;
    }
    string SavedKey(int i) { return CleanKeys(ReadIni("key_" + MapNames[i]), KeyDefaults[i]); }
    string SavedPadButton(int i) { string p = ReadIni("pad_" + MapNames[i]).ToUpperInvariant(); return p == "NONE" || Array.IndexOf(PadNames, p) >= 0 ? p : PadNames[i]; }
    void CountChanged()
    {
        keysChanged = 0; padChanged = 0;
        for (int i = 0; i < MapNames.Length; i++) if (SavedKey(i) != KeyDefaults[i]) keysChanged++;
        for (int i = 0; i < PadActions; i++) if (SavedPadButton(i) != PadNames[i]) padChanged++;
    }
    // SOR_KEYS / SOR_PAD_BUTTONS for the game: only what differs from the standard ("" = all standard)
    string MapEnv(bool pad)
    {
        var l = new List<string>();
        for (int i = 0; i < (pad ? PadActions : MapNames.Length); i++)
        {
            string v = pad ? SavedPadButton(i) : SavedKey(i);
            if (v != (pad ? PadNames[i] : KeyDefaults[i])) l.Add(MapNames[i] + "=" + v);
        }
        return string.Join(";", l.ToArray());
    }
    bool MapChanged
    {
        get
        {
            if (mapPad) { for (int i = 0; i < PadActions; i++) if (mapPads[i] != savedPads[i]) return true; }
            else for (int i = 0; i < MapNames.Length; i++) if (mapKeys[i] != savedKeys[i]) return true;
            return false;
        }
    }
    bool MapStandard
    {
        get
        {
            if (mapPad) { for (int i = 0; i < PadActions; i++) if (mapPads[i] != PadNames[i]) return false; }
            else for (int i = 0; i < MapNames.Length; i++) if (mapKeys[i] != KeyDefaults[i]) return false;
            return true;
        }
    }
    string MapValueLabel(int a) { return mapPad ? PadLabel(mapPads[a]) : KeysLabel(mapKeys[a]); }
    string MapHint { get { return mapPad ? "CHOOSE A ROW, THEN PRESS THE NEW BUTTON  ·  OR CHANGE IT WITH ‹ ›" : "CHOOSE A ROW, THEN PRESS THE NEW KEY OR MOUSE BUTTON"; } }

    void OpenMap(bool pad)
    {
        PlayClick();
        page = 6; mapPad = pad; mapScroll = 0; sel = 0; hover = -1; capture = -1; dupAction = -1; dupName = null;
        for (int i = 0; i < MapNames.Length; i++) mapKeys[i] = savedKeys[i] = SavedKey(i);
        for (int i = 0; i < PadActions; i++) mapPads[i] = savedPads[i] = SavedPadButton(i);
        message = MapHint;
        Invalidate();
    }
    void CloseMap(string why) { capture = -1; dupAction = -1; page = 4; sel = mapPad ? 1 : 0; hover = -1; CountChanged(); message = why; Invalidate(); }

    void StartCapture(int a)
    {
        PlayClick();
        capture = a; dupAction = -1; dupName = null;
        if (mapPad) { int m = PadMask(); padPrev = m < 0 ? 0 : m; message = m < 0 ? NoPadMessage : PadAsk(a); }
        else message = KeyAsk(a);
        Invalidate();
    }
    // Keyboard page: a click assigns now, so Backspace cancels (and cannot be chosen here; Esc can, the author's rule)
    static string KeyAsk(int a) { return "PRESS A KEY OR MOUSE BUTTON FOR " + MapItems[a] + "  ·  DELETE CLEARS IT  ·  BACKSPACE CANCELS"; }
    string PadAsk(int a) { return "PRESS THE NEW BUTTON FOR " + MapItems[a] + "  ·  DELETE CLEARS IT  ·  ESC CANCELS"; }
    const string NoPadMessage = "NO CONTROLLER FOUND  ·  CONNECT IT, OR PRESS ESC AND CHOOSE THE BUTTON WITH ‹ ›";
    Keys swallow = Keys.None;
    void ClearAction()
    {   // Delete while waiting: the action gets no key / no button (NONE)
        int a = capture;
        capture = -1; dupAction = -1; dupName = null;
        if (mapPad) mapPads[a] = "NONE"; else mapKeys[a] = "NONE";
        message = MapItems[a] + (mapPad ? " HAS NO BUTTON NOW" : " HAS NO KEY NOW") + "  ·  APPLY TO SAVE";
        Invalidate();
    }
    void CancelCapture()
    {
        int a = capture;
        capture = -1; dupAction = -1; dupName = null;
        message = "NOT CHANGED  ·  " + MapItems[a] + " STAYS " + MapValueLabel(a);
        Invalidate();
    }

    // While waiting for a key every key comes here, arrows, Enter and Tab too (lParam: the scancode, GLFW's way).
    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        if (swallow != Keys.None && (msg.Msg == 0x100 || msg.Msg == 0x104))
        {
            if ((keyData & Keys.KeyCode) == swallow && ((((long)msg.LParam) >> 30) & 1) != 0) return true;
            swallow = Keys.None;
        }
        if (page == 6 && capture >= 0 && (msg.Msg == 0x100 || msg.Msg == 0x104)) { CaptureKey(keyData, (long)msg.LParam); return true; }   // WM_KEYDOWN, WM_SYSKEYDOWN
        return base.ProcessCmdKey(ref msg, keyData);
    }
    protected override void WndProc(ref Message m)
    {
        if (capture >= 0 && m.Msg == 0x105) return;   // WM_SYSKEYUP: Alt or F10 must not open the window's menu while waiting
        base.WndProc(ref m);
    }
    void CaptureKey(Keys keyData, long lParam)
    {
        Keys k = keyData & Keys.KeyCode;
        if (((lParam >> 30) & 1) != 0) return;   // the key was already down (it repeats): only a new press counts
        if (k == Keys.Delete) { ClearAction(); return; }
        if (!mapPad && k == Keys.Back) { swallow = k; CancelCapture(); return; }   // keyboard page: Backspace cancels (a click assigns)
        if (mapPad && (k == Keys.Escape || k == Keys.Up || k == Keys.Down)) { CancelCapture(); return; }
        if (mapPad) { message = "PRESS A BUTTON ON THE CONTROLLER  ·  ESC CANCELS"; Invalidate(); return; }
        if ((keyData & Keys.Alt) != 0 && k != Keys.Menu)
        {
            message = k == Keys.Return ? "ALT+ENTER IS FULL SCREEN  ·  PRESS ANOTHER KEY" : "ALT STAYS FREE FOR ALT+ENTER (FULL SCREEN)  ·  PRESS THE KEY ALONE";
            Invalidate(); return;
        }
        int sc = (int)((lParam >> 16) & 0x1FF);
        if (sc == 0) { uint v = MapVirtualKey((uint)k, 4); sc = (int)(v & 0xFF) | ((v & 0xFF00) == 0xE000 ? 0x100 : 0); }   // MAPVK_VK_TO_VSC_EX
        if (sc == 0x54) sc = 0x137;   // as GLFW: Alt+Print Screen, Ctrl+Pause, a right Shift some keyboards send as extended
        if (sc == 0x146) sc = 0x45;
        if (sc == 0x136) sc = 0x36;
        string name;
        if (!ScanNames.TryGetValue(sc, out name)) { message = "THE GAME CANNOT READ THIS KEY  ·  PRESS ANOTHER KEY"; Invalidate(); return; }
        string why = Refused(name);
        if (why != "") { message = why; Invalidate(); return; }
        swallow = k;   // its auto-repeat must not reach the page (Esc would leave it)
        SetKey(name);
    }
    // A mouse button or a wheel notch while a KEYBOARD row waits: it becomes the key (MouseDown and MouseWheel call these
    // before any other handling; the title bar still drags and closes the window). The click that follows is swallowed.
    bool eatClick;                       // MouseClick: this click set a key, it must not also choose a row
    int wheelLast; bool wheelUsed;       // a scroll that set a key: its next notches (closer than 350 ms) do nothing
    bool MouseAssigns(MouseEventArgs e)
    {
        if (page != 6 || mapPad || capture < 0 || e.Y < TitleH) return false;
        string name = e.Button == MouseButtons.Left ? "MOUSE_LEFT" : e.Button == MouseButtons.Right ? "MOUSE_RIGHT"
            : e.Button == MouseButtons.Middle ? "MOUSE_MIDDLE" : e.Button == MouseButtons.XButton1 ? "MOUSE_X1"
            : e.Button == MouseButtons.XButton2 ? "MOUSE_X2" : null;
        eatClick = true;
        if (name != null) SetKey(name);
        return true;
    }
    bool WheelAssigns(MouseEventArgs e)
    {
        int now = Environment.TickCount;
        bool sameScroll = wheelUsed && now - wheelLast < 350;
        wheelLast = now;
        if (sameScroll) return true;
        wheelUsed = false;
        if (page != 6 || mapPad || capture < 0 || e.Delta == 0) return false;
        wheelUsed = true;
        SetKey(e.Delta > 0 ? "WHEEL_UP" : "WHEEL_DOWN");
        return true;
    }
    static bool InList(string list, string key) { return Array.IndexOf(list.Split(','), key) >= 0; }
    void SetKey(string name)
    {
        int a = capture;
        if (InList(mapKeys[a], name))
        {
            capture = -1; dupAction = -1; dupName = null;
            if (mapKeys[a] != name) { mapKeys[a] = name; message = MapItems[a] + " IS NOW ONLY " + KeyLabel(name) + "  ·  APPLY TO SAVE"; }
            else message = MapItems[a] + " IS ALREADY " + KeyLabel(name);
            Invalidate(); return;
        }
        int other = -1;
        for (int j = 0; j < MapNames.Length; j++) if (j != a && InList(mapKeys[j], name)) { other = j; break; }
        if (other >= 0 && !(dupAction == other && dupName == name))
        {   // already used: say by what; the same key again swaps the two
            dupAction = other; dupName = name;
            message = KeyLabel(name) + " IS ALREADY USED BY " + MapItems[other] + "  ·  PRESS ANOTHER KEY, OR " + KeyLabel(name) + " AGAIN TO SWAP";
            Invalidate(); return;
        }
        capture = -1; dupAction = -1; dupName = null;
        if (other >= 0)
        {
            string t = mapKeys[other]; mapKeys[other] = mapKeys[a]; mapKeys[a] = t;
            message = "SWAPPED  ·  " + MapItems[a] + " IS NOW " + KeysLabel(mapKeys[a]) + ", " + MapItems[other] + " IS " + KeysLabel(mapKeys[other]);
        }
        else { mapKeys[a] = name; message = MapItems[a] + " IS NOW " + KeyLabel(name) + "  ·  APPLY TO SAVE"; }
        Invalidate();
    }
    void SetPad(string p)
    {
        int a = capture;
        if (mapPads[a] == p) { capture = -1; dupAction = -1; dupName = null; message = MapItems[a] + " IS ALREADY " + PadLabel(p); Invalidate(); return; }
        int other = Array.IndexOf(mapPads, p);
        if (other >= 0 && !(dupAction == other && dupName == p))
        {
            dupAction = other; dupName = p;
            message = PadLabel(p) + " IS ALREADY USED BY " + MapItems[other] + "  ·  PRESS ANOTHER BUTTON, OR " + PadLabel(p) + " AGAIN TO SWAP";
            Invalidate(); return;
        }
        capture = -1; dupAction = -1; dupName = null;
        if (other >= 0)
        {
            mapPads[other] = mapPads[a]; mapPads[a] = p;
            message = "SWAPPED  ·  " + MapItems[a] + " IS NOW " + PadLabel(p) + ", " + MapItems[other] + " IS " + PadLabel(mapPads[other]);
        }
        else { mapPads[a] = p; message = MapItems[a] + " IS NOW " + PadLabel(p) + "  ·  APPLY TO SAVE"; }
        Invalidate();
    }
    // ‹ › on a controller row: the next button; the row that had it takes this row's old one (every button stays in use)
    void CyclePad(int a, int d)
    {
        PlayClick();
        string p = PadNames[(Array.IndexOf(PadNames, mapPads[a]) + d + PadNames.Length) % PadNames.Length];
        int other = Array.IndexOf(mapPads, p);
        if (other >= 0 && other != a) { mapPads[other] = mapPads[a]; message = MapItems[a] + " IS NOW " + PadLabel(p) + "  ·  " + MapItems[other] + " TOOK " + PadLabel(mapPads[other]); }
        else message = MapItems[a] + " IS NOW " + PadLabel(p);
        mapPads[a] = p; dupAction = -1; dupName = null;
        Invalidate();
    }

    // ---- the controller, read with XInput (Xbox and compatible controllers) only while waiting for a button
#pragma warning disable 0649   // XInput fills these fields, the compiler cannot see it
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    struct XInputState { public uint packet; public ushort buttons; public byte lt, rt; public short lx, ly, rx, ry; }
#pragma warning restore 0649
    [System.Runtime.InteropServices.DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")] static extern int XInputGetState14(int user, out XInputState s);
    [System.Runtime.InteropServices.DllImport("xinput9_1_0.dll", EntryPoint = "XInputGetState")] static extern int XInputGetState910(int user, out XInputState s);
    int xinput;     // 0 xinput1_4.dll (Windows 8 and later), 1 xinput9_1_0.dll (Windows 7), 2 none
    int padPrev;    // the buttons held at the last look
    // The buttons held on any connected controller (XInput's bits; LT 0x10000, RT 0x20000), -1 if none is connected.
    int PadMask()
    {
        int mask = -1;
        for (int u = 0; u < 4; u++)
        {
            var s = new XInputState(); int r;
            try { r = xinput == 0 ? XInputGetState14(u, out s) : xinput == 1 ? XInputGetState910(u, out s) : -1; }
            catch (Exception) { xinput++; u--; continue; }   // that file is missing: try the next one
            if (r != 0) continue;
            if (mask < 0) mask = 0;
            mask |= s.buttons | (s.lt > 64 ? 0x10000 : 0) | (s.rt > 64 ? 0x20000 : 0);
        }
        return mask;
    }
    void PollPad()
    {
        if (page != 6 || !mapPad || capture < 0) return;
        int m = PadMask();
        if (m < 0) { padPrev = 0; if (message != NoPadMessage && dupAction < 0) { message = NoPadMessage; Invalidate(); } return; }
        if (message == NoPadMessage) { message = PadAsk(capture); Invalidate(); }
        int fresh = m & ~padPrev; padPrev = m;
        if (fresh == 0) return;
        for (int i = 0; i < PadXInput.Length; i++) if ((fresh & PadXInput[i]) != 0) { SetPad(PadNames[i]); return; }
    }

    void MapMove(int d)
    {
        int n = MapCount;
        if (d > 0)
        {
            if (sel == MapVisible - 1 && mapScroll + MapVisible < n) mapScroll++;
            else if (sel == 9) { sel = 0; mapScroll = 0; }
            else sel++;
        }
        else
        {
            if (sel == 0 && mapScroll > 0) mapScroll--;
            else if (sel == 0) { sel = 9; }
            else sel--;
        }
        dupAction = -1; dupName = null;
        Invalidate();
    }
    void MapScroll(int d) { mapScroll = Math.Max(0, Math.Min(MapCount - MapVisible, mapScroll + d)); Invalidate(); }

    void OnMapKey(KeyEventArgs e)
    {
        if (capture >= 0) { CaptureKey(e.KeyData, 0); e.Handled = true; return; }   // only if ProcessCmdKey did not take it
        int a = MapAction(sel);
        if (e.KeyCode == Keys.Down) MapMove(1);
        else if (e.KeyCode == Keys.Up) MapMove(-1);
        else if (e.KeyCode == Keys.PageDown) MapScroll(MapVisible);
        else if (e.KeyCode == Keys.PageUp) MapScroll(-MapVisible);
        else if ((e.KeyCode == Keys.Left || e.KeyCode == Keys.Right) && mapPad && a >= 0) CyclePad(a, e.KeyCode == Keys.Left ? -1 : 1);
        else if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space || e.KeyCode == Keys.X) ChooseMap(sel);
        else if (e.KeyCode == Keys.Escape) ChooseMap(9);
    }

    void ClickMap(int row, int x)
    {
        if (capture >= 0) { CancelCapture(); return; }   // controller page: a click while waiting for a button changes nothing (on the keyboard page the click is the key, MouseAssigns)
        int a = MapAction(row);
        var vr = valueRects[row];
        if (mapPad && a >= 0 && vr.Width > 0 && x >= vr.X - 10 && x <= vr.Right + 10) { sel = row; CyclePad(a, x < vr.X + vr.Width / 2 ? -1 : 1); return; }
        ChooseMap(row);
    }

    void ChooseMap(int row)
    {
        sel = row;
        int a = MapAction(row);
        if (a >= 0) { StartCapture(a); return; }
        PlayClick();
        if (row == MapVisible)
        {   // RESET: the standard keys, saved only by APPLY
            bool was = MapStandard;
            for (int i = 0; i < MapNames.Length; i++) if (!mapPad) mapKeys[i] = KeyDefaults[i];
            for (int i = 0; i < PadActions; i++) if (mapPad) mapPads[i] = PadNames[i];
            dupAction = -1; dupName = null;
            message = was ? "THESE ARE ALREADY THE STANDARD " + (mapPad ? "BUTTONS" : "KEYS") : "STANDARD " + (mapPad ? "BUTTONS" : "KEYS") + " BACK  ·  APPLY TO SAVE THEM";
            Invalidate(); return;
        }
        if (row == MapVisible + 1 && MapChanged)
        {
            try
            {
                if (mapPad) for (int i = 0; i < PadActions; i++) WriteIni("pad_" + MapNames[i], mapPads[i] == PadNames[i] ? null : mapPads[i]);
                else for (int i = 0; i < MapNames.Length; i++) WriteIni("key_" + MapNames[i], mapKeys[i] == KeyDefaults[i] ? null : mapKeys[i]);
            }
            catch (Exception) { message = "COULD NOT SAVE THE SETTINGS  ·  THE FOLDER MAY BE READ-ONLY: MOVE THE GAME TO DOCUMENTS"; Invalidate(); return; }
            CloseMap((mapPad ? "CONTROLLER" : "KEYBOARD") + " SAVED  ·  USED FROM THE NEXT PLAY");
            return;
        }
        CloseMap(row == MapVisible + 2 && MapChanged ? "NOT SAVED  ·  THE " + (mapPad ? "BUTTONS" : "KEYS") + " STAY AS THEY WERE" : "");
    }

    string MapDescribe(int a)
    {
        if (capture == a) return mapPad ? "PRESS A BUTTON  ·  ESC CANCELS" : "KEY OR MOUSE BUTTON";
        if (mapPad)
        {
            if (swap && a <= 1) return "SWAPPED IN THE GAME (BUTTONS ROW)";
            int p = Array.IndexOf(PadNames, mapPads[a]);
            return p >= 0 ? PadPlaces[p] : "";
        }
        if (a >= 12 && a <= 15) return "D-PAD: MENUS, AND IT WALKS TOO";
        if (a >= 16 && a <= 19) return "LEFT STICK: WALK";
        if (a >= 20) return "RIGHT STICK: CAMERA";
        return mapKeys[a] != KeyDefaults[a] ? "STANDARD: " + KeysLabel(KeyDefaults[a]) : "";
    }

    void PaintMap(Graphics g, Brush gold, Brush bone, Brush ash, Brush ashText, Brush dimGold)
    {
        bool changed = MapChanged;
        for (int r = 0; r < MapRowSlots.Length; r++)
        {
            var b = debugBoxes[r];
            int a = MapAction(r);
            bool on = r == sel, live = r != MapVisible + 1 || changed;
            if (on) Highlight(g, b, live, gold);
            string name = a >= 0 ? MapItems[a] : r == MapVisible ? "RESET" : r == MapVisible + 1 ? "APPLY" : "BACK";
            string latin = a >= 0 ? MapLatin[a] : r == MapVisible ? "RESTITVE" : r == MapVisible + 1 ? "CONFIRMA" : "REDI";
            string d = a >= 0 ? MapDescribe(a) : r == MapVisible ? (mapPad ? "THE STANDARD BUTTONS" : "THE STANDARD KEYS")
                : r == MapVisible + 1 ? (changed ? "SAVE FOR THE NEXT PLAY" : "NOTHING CHANGED") : (changed ? "WITHOUT SAVING" : "");
            Spaced(g, name, itemFont, live ? (on ? gold : bone) : ash, b.X + 6, b.Y + 4, 3);
            valueRects[r] = Rectangle.Empty;
            if (a >= 0)
            {
                string v = capture == a ? "· · ·" : mapPad ? "‹ " + MapValueLabel(a) + " ›" : MapValueLabel(a);
                int vw = (int)SpacedWidth(g, v, valueFont, 2);
                // right-aligned in the row; a long name pushes it right, still left of the logo (x 470)
                int vx = Math.Max(b.Right - 6 - vw, b.X + 6 + (int)SpacedWidth(g, name, itemFont, 3) + 14);
                valueRects[r] = new Rectangle(vx, b.Y, vw, b.Height);
                Spaced(g, v, valueFont, on || capture == a ? gold : bone, vx, b.Y + 9, 2);
            }
            Spaced(g, latin + (d != "" ? "  ·  " + d : ""), latinFont, live ? dimGold : ashText, b.X + 8, b.Y + 29, 2);
        }
        // small marks right of the list when more actions are above or below
        var top = debugBoxes[0]; var last = debugBoxes[MapVisible - 1];
        if (mapScroll > 0) g.FillPolygon(dimGold, new[] { new PointF(426, top.Y + 13), new PointF(432, top.Y + 5), new PointF(438, top.Y + 13) });
        if (mapScroll + MapVisible < MapCount) g.FillPolygon(dimGold, new[] { new PointF(426, last.Y + 21), new PointF(432, last.Y + 29), new PointF(438, last.Y + 21) });
    }
    // DEBUG (the author, 9/11): tools for testing, not for players; not in the menu, Ctrl+D on the main menu opens it.
    // The cheats are in pc/sor_trucchi.cpp (SOR_CHEATS): the game's own hidden ones, plus some converted from the US
    // game's public codes (MONEY, UNLOCK ALL, NO FILMS); the engine panel is PS2Recomp's "Runtime Debugger" (F12 in the
    // game window, SOR_DEBUG_PANEL). The free camera is pc/sor_camera.cpp (SOR_FREECAM=1: F8 in the game detaches the
    // camera, 1-9 jump to saved points, Ctrl+1-9 save them in camera_points.ini). Saved in launcher.ini as debug_*=1.
    // 10 rows: a little closer together than the OPTIONS page (debugBoxes).
    static readonly string[] DItems = { "INVINCIBLE", "WEAPONS", "ONE HIT", "MONEY", "UNLOCK ALL", "NO FILMS", "FREE CAMERA", "WIREFRAME", "ENGINE PANEL", "APPLY", "BACK" };
    static readonly string[] DLatin = { "INVICTVS", "ARMA", "ICTVS VNVS", "PECVNIA", "OMNIA", "SINE FABVLIS", "OCVLVS", "RETE", "MACHINA", "CONFIRMA", "REDI" };
    // rows 0-7: the launcher.ini key and the SOR_CHEATS names each row passes (the camera and panel rows pass none)
    static readonly string[] DKeys = { "debug_invincible", "debug_weapons", "debug_onehit", "debug_money", "debug_unlock", "debug_nofilms", "debug_freecam", "debug_wireframe", "debug_panel" };
    static readonly string[] DCheats = { "invincible", "weapons", "onehit", "salvo,gold", "unlock,allsalvos", "nofilms", "", "", "" };
    const int DFreecam = 6;                // the FREE CAMERA row
    const int DWire = 7;                   // the WIREFRAME row (sor-gs.dll, SORGS_WIREFRAME: F7 in the game, DirectX 11 only)
    const int DPanel = 8;                  // the ENGINE PANEL row
    readonly bool[] dbg = new bool[9];     // debug page, edited values
    bool SavedDbg(int i) { return ReadIni(DKeys[i]) == "1"; }
    bool SavedDbgPanel { get { return SavedDbg(DPanel); } }
    bool SavedDbgFreecam { get { return SavedDbg(DFreecam); } }
    bool DebugOn { get { for (int i = 0; i < DKeys.Length; i++) if (SavedDbg(i)) return true; return false; } }
    bool DebugChanged { get { for (int i = 0; i < DKeys.Length; i++) if (dbg[i] != SavedDbg(i)) return true; return false; } }
    string DebugCheats()
    {   // SOR_CHEATS for the game: the names pc/sor_trucchi.cpp knows, "" = no cheats
        var l = new List<string>();
        for (int i = 0; i < DKeys.Length; i++) if (DCheats[i] != "" && SavedDbg(i)) l.Add(DCheats[i]);
        return string.Join(",", l.ToArray());
    }
    void OpenDebug() { PlayClick(); page = 5; for (int i = 0; i < dbg.Length; i++) dbg[i] = SavedDbg(i); sel = 0; hover = -1; message = DebugWarning; armed = -1; Invalidate(); }
    // the author, 9/11: DEBUG is a row in OPTIONS. The warning stays at the bottom; the cheats that write into the save
    // (MONEY, UNLOCK ALL) need a second press to turn on.
    const string DebugWarning = "USE A SEPARATE SAVE  ·  SOME OPTIONS CHANGE IT FOR GOOD";
    int armed = -1;
    bool debugFromOptions;
    // BGM and SE volume are the game's own (0-10, its OPTIONS menu): they live in settings.ini next to sor.exe, which
    // the game reads at start and rewrites when the player changes them in the game (the author, 9/11: one value, two places)
    int bgmVol = 5, seVol = 5; bool menuSnd = true, ashOn = true, smooth = true, timings; int vu1Eng;   // options page, edited values
    string GamePath { get { return Path.Combine(dir, "settings.ini"); } }
    int ReadGameVol(string key)
    {
        try { foreach (var l in File.ReadAllLines(GamePath)) if (l.StartsWith(key + "=")) { int v; if (int.TryParse(l.Substring(key.Length + 1).Trim(), out v) && v >= 0 && v <= 10) return v; } }
        catch (Exception) { }
        return 5;   // package default (the author, 9/11): music and effects at 5 of 10
    }
    void EnsureGameVols()
    {   // first start (no bgm= / se= in settings.ini yet): the game gets the package volumes, as the launcher shows them
        try
        {
            bool b = false, e = false;
            if (File.Exists(GamePath)) foreach (var l in File.ReadAllLines(GamePath)) { if (l.StartsWith("bgm=")) b = true; else if (l.StartsWith("se=")) e = true; }
            if (!b || !e) WriteGameVols(ReadGameVol("bgm"), ReadGameVol("se"));
        }
        catch (Exception) { }
    }
    void WriteGameVols(int bgm, int se)
    {
        var keep = new List<string>();
        if (File.Exists(GamePath)) foreach (var l in File.ReadAllLines(GamePath)) if (!l.StartsWith("bgm=") && !l.StartsWith("se=")) keep.Add(l);
        keep.Add("bgm=" + bgm); keep.Add("se=" + se);
        File.WriteAllLines(GamePath, keep.ToArray());
    }
    bool SavedMenuSnd { get { return ReadIni("menu_sounds") != "0"; } }
    bool SavedAsh { get { return ReadIni("background") != "0"; } }
    bool SavedSmooth { get { return ReadIni("smoothaudio") != "0"; } }   // SMOOTH AUDIO: on unless smoothaudio=0
    int SavedVu1 { get { string v = ReadIni("vu1engine"); return v == "nostro" ? 0 : v == "confronto" ? 2 : 1; } }   // SPEED TESTS > VU1 ENGINE: PCSX2 unless vu1engine=nostro|confronto (the author, 10/11 15:01: same pictures, 2.7x faster)
    // SPEED TESTS > UPSCALE FIX (10/11/2026, the soft double outline seen at 8x): PCSX2's half pixel offset and native
    // scaling, passed as SORGS_HALFPIXEL / SORGS_NATIVESCALING. 0 = as now (PCSX2's fixes for this game: 5 and 2).
    static readonly string[] UpNames = { "SPECIAL", "OFF", "NORMAL", "PCSX2'S OLD FIX", "SPECIAL AGGRESSIVE", "NATIVE, NO SCALING", "NATIVE + TEXTURE" };
    static readonly string[] UpHp = { "", "0", "1", "5", "3", "5", "4" };
    static readonly string[] UpNs = { "", "0", "0", "2", "0", "0", "2" };
    int upFix;
    int SavedUpFix { get { int v; return int.TryParse(ReadIni("upscalefix"), out v) && v >= 0 && v < UpNames.Length ? v : 0; } }
    bool SavedTimings { get { return ReadIni("timings") == "1"; } }   // SPEED TESTS > TIMINGS: off unless timings=1
    bool liveSnd = true, liveAsh = true;   // what the launcher itself uses now
    bool UseSnd { get { return page == 2 ? menuSnd : liveSnd; } }
    bool UseAsh { get { return page == 2 ? ashOn : liveAsh; } }
    int toggleDir = 1;
    readonly Rectangle[] valueRects = new Rectangle[12];
    bool full; bool mods = true;       // options: full screen; mods: replacement textures used
    string[] PageItems { get { return page == 1 ? GItems : page == 2 ? OItems : page == 3 ? MItems : page == 4 ? CItems : page == 5 ? DItems : page == 6 ? MapRowSlots : page == 7 ? TItems : Items; } }
    // Rows 0-9 change a value (ANTIALIASING: PCSX2's FXAA; VSYNC: every picture in step with the screen; OSD: the
    // video card's on-screen statistics; FIELD OF VIEW: the gameplay camera wider, SOR_FOV), 10 saves, 11 goes back
    // without saving. 12 rows: closer together than the other pages (graphicsBoxes, 29 px).
    static readonly string[] GItems = { "RENDERER", "RESOLUTION", "SHARPNESS", "ANTIALIASING", "SCREEN", "FRAME RATE", "VSYNC", "OSD", "VIGNETTE", "FIELD OF VIEW", "APPLY", "BACK" };
    static readonly string[] GLatin = { "PICTOR", "MAGNITVDO", "ACIES", "LEVITAS", "FORMA", "CELERITAS", "CONCENTVS", "NVNTIVS", "VELVM", "PROSPECTVS", "CONFIRMA", "REDI" };
    const int MaxScale = 8;
    // RESOLUTION: 6X and 8X (the author, 9/11) draw the game larger and shrink it to the screen, the strongest
    // antialiasing there is here; PCSX2's GS takes any multiplier (sor-gs.dll, upscale_multiplier), no limit at 4.
    static readonly int[] Scales = { 1, 2, 3, 4, 6, 8 };
    bool hz60;                         // 60 Hz: the game's own 60 Hz mode, chosen at start
    static readonly string[] ScreenNames = { "4:3", "16:9", "21:9", "32:9" };
    int wide;                          // 0: 4:3, 1: widescreen 16:9 (any renderer), 2: 21:9 (video card only)
    bool useGpu; int scale = 3; int api = 1; bool showOsd; bool vsync = true; bool vignette = true; int sharp; // sharp: 0 off (as the PS2), 1 low, 2 high; api: 1 DirectX 11, 2 DirectX 12, 3 Vulkan
    bool fxaa;                         // ANTIALIASING: PCSX2's FXAA on the finished picture (video card only)
    // FIELD OF VIEW (the author, 10/11/2026): the gameplay camera's view this much wider, in percent (100 = as on the
    // PS2); passed as SOR_FOV, used by the translated camera code (pc/widescreen_gen.py, pc/main_sor.cpp).
    static readonly int[] Fovs = { 100, 110, 120, 130 };
    int fov = 100;
           // what the graphics page shows; launcher.ini changes only with APPLY
    readonly string dir = AppDomain.CurrentDomain.BaseDirectory;
    string IniPath { get { return Path.Combine(dir, "launcher.ini"); } }
    readonly Rectangle[] menuBoxes = new Rectangle[Items.Length];
    readonly Rectangle[] graphicsBoxes = new Rectangle[GItems.Length]; // the GRAPHICS page: 12 rows, 29 px apart
    readonly Rectangle[] debugBoxes = new Rectangle[10];    // 10 rows: closer still, above the message line
    readonly Rectangle[] debug11Boxes = new Rectangle[Math.Max(Math.Max(DItems.Length, GItems.Length), CItems.Length)];   // the DEBUG and GRAPHICS pages: 11 rows, closer again
    Rectangle[] boxes { get { return page == 1 ? graphicsBoxes : page == 5 || page == 4 || page == 2 ? debug11Boxes : page == 6 ? debugBoxes :   // the key mapping has 10 rows, OPTIONS 11 (SPEED TESTS)
 page == 4 ? graphicsBoxes : menuBoxes; } }                                // CONTROLS: 7 rows
    readonly Font titleFont, subFont, itemFont, latinFont, smallFont, valueFont;
    readonly Image logo;

    static readonly Color Gold = Color.FromArgb(196, 158, 92);
    static readonly Color Bone = Color.FromArgb(214, 204, 186);
    static readonly Color Ash = Color.FromArgb(64, 60, 56);      // greyed entries: dark, they recede
    static readonly Color AshText = Color.FromArgb(82, 76, 70);

    Launcher()
    {
        Text = "Shadow of Rome Recompiled " + Version;
        try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }
        // our own frame (the author, 9/11): no white Windows bar; a dark title bar drawn here, TitleH tall
        FormBorderStyle = FormBorderStyle.None;
        ClientSize = new Size(960, 540 + TitleH);
        for (int i = 0; i < Cinders.Length; i++) { Cinders[i] = NewAsh(); Cinders[i].Y = (float)(rnd.NextDouble() * 540); }
        var ashTimer = new Timer { Interval = 40 };
        ashTimer.Tick += (s, e) => { UpdateFire(); if (WindowState == FormWindowState.Minimized || !UseAsh) return; StepAsh(); Invalidate(); };
        ashTimer.Start();
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        DoubleBuffered = true;
        KeyPreview = true;
        string serif = FirstFont("Trajan Pro", "Cinzel", "Perpetua Titling MT", "Copperplate Gothic Light", "Georgia");
        titleFont = new Font(serif, 40, FontStyle.Regular);
        subFont = new Font(serif, 10, FontStyle.Regular);
        itemFont = new Font(serif, 18, FontStyle.Regular);
        latinFont = new Font(serif, 8.5f, FontStyle.Regular);
        valueFont = new Font(serif, 11, FontStyle.Regular);
        smallFont = new Font("Segoe UI", 8.5f);
        string lp = Path.Combine(dir, "logo.png");
        if (File.Exists(lp)) using (var f = Image.FromFile(lp)) logo = new Bitmap(f);
        for (int i = 0; i < Items.Length; i++)
            menuBoxes[i] = new Rectangle(90, 150 + i * 46, 330, 42);
        for (int i = 0; i < GItems.Length; i++)
            graphicsBoxes[i] = new Rectangle(90, 124 + i * 29, 330, 29);   // the last description ends above the message (y 482)
        for (int i = 0; i < debugBoxes.Length; i++)   // the last row's description ends above the message (y 482)
            debugBoxes[i] = new Rectangle(90, 130 + i * 34, 330, 34);
        for (int i = 0; i < debug11Boxes.Length; i++)
            debug11Boxes[i] = new Rectangle(90, 126 + i * 31, 330, 31);   // last description ends above the message
        MouseDown += (s, e) =>
        {
            if (MouseAssigns(e)) return;   // a KEYBOARD row waits: the mouse button is its new key, before anything else
            // drag the window by its title bar
            if (e.Button == MouseButtons.Left && e.Y < TitleH && TitleButton(e.Location) < 0) { ReleaseCapture(); SendMessage(Handle, 0xA1, (IntPtr)2, IntPtr.Zero); }
        };
        MouseUp += (s, e) => { eatClick = false; };   // after MouseClick (WinForms raises MouseClick first)
        MouseLeave += (s, e) => { if (titleHover >= 0) { titleHover = -1; Invalidate(); } };
        MouseMove += (s, e) => { int tb = TitleButton(e.Location); if (tb != titleHover) { titleHover = tb; Invalidate(); } };
        MouseMove += (s, e) => { int h = Hit(e.Location); if (h != hover) { hover = h; if (h >= 0 && capture < 0) sel = h; Invalidate(); } };
        MouseWheel += (s, e) => { if (WheelAssigns(e)) return; if (page == 6 && capture < 0) MapScroll(e.Delta > 0 ? -1 : 1); };   // a waiting KEYBOARD row takes the notch; else the key mapping's list scrolls
        var padTimer = new Timer { Interval = 30 };   // the controller is read only while a mapping row waits for a button
        padTimer.Tick += (s, e) => PollPad();
        padTimer.Start();
        MouseClick += (s, e) =>
        {
            if (eatClick) { eatClick = false; return; }   // that press set a key (MouseAssigns)
            int tb = TitleButton(e.Location);
            if (tb == 0) { WindowState = FormWindowState.Minimized; return; }
            if (tb == 1) { Close(); return; }
            int h = Hit(e.Location);
            if (h < 0) return;
            if (page == 1) ChooseGraphics(h, e.Button == MouseButtons.Right ? -1 : 1);   // right click: the value before
            else if (page == 6) ClickMap(h, e.X);
            else if (page >= 2)
            {   // click on the left arrow lowers, anywhere else raises (the author, 9/11)
                var vr = valueRects[h]; int x = e.X;
                toggleDir = vr.Width > 0 && x >= vr.X - 10 && x < vr.X + vr.Width / 2 ? -1 : 1;
                ChooseSmall(h); toggleDir = 1;
            }
            else Choose(h);
        };
        KeyDown += OnKey;
    }

    static string FirstFont(params string[] names)
    {
        using (var f = new InstalledFontCollection())
            foreach (var n in names)
                foreach (var fam in f.Families) if (fam.Name == n) return n;
        return "Georgia";
    }

    int Hit(Point p) { p.Offset(0, -TitleH); int n = PageItems.Length; for (int i = 0; i < n; i++) if (boxes[i].Contains(p)) return i; return -1; }

    void OnKey(object s, KeyEventArgs e)
    {
        if (page == 1) { OnGraphicsKey(e); return; }
        if (page == 6) { OnMapKey(e); return; }
        if (page >= 2) { OnSmallKey(e); return; }
        if (e.Control && e.KeyCode == Keys.D) { OpenDebug(); debugFromOptions = false; return; }   // the hidden DEBUG page
        if (e.KeyCode == Keys.Down) { sel = (sel + 1) % Items.Length; message = ""; Invalidate(); }
        else if (e.KeyCode == Keys.Up) { sel = (sel + Items.Length - 1) % Items.Length; message = ""; Invalidate(); }
        else if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space || e.KeyCode == Keys.X) Choose(sel);
        else if (e.KeyCode == Keys.Escape) Close();
    }

    void Choose(int i)
    {
        PlayClick();
        if (Items[i] == "QUIT") { Close(); return; }
        if (!Ready[i]) { message = Items[i] + " - COMING SOON"; Invalidate(); return; }
        if (Items[i] == "GRAPHICS") { OpenGraphics(); return; }
        if (Items[i] == "CONTROLS") { page = 4; swap = SavedSwap; invY = SavedInvY; invX = SavedInvX; mouseLook = SavedMouseLook; mouseSpeed = SavedMouseSpeed; mouseInvY = SavedMouseInvY; toggleBtn = SavedToggle; CountChanged(); sel = 0; hover = -1; message = ControlsNote; Invalidate(); return; }
        if (Items[i] == "OPTIONS" || Items[i] == "MODS") { page = Items[i] == "OPTIONS" ? 2 : 3; full = SavedFull; bgmVol = ReadGameVol("bgm"); seVol = ReadGameVol("se"); menuSnd = liveSnd; ashOn = liveAsh; smooth = SavedSmooth; mods = SavedMods; sel = 0; hover = -1; message = ""; Invalidate(); return; }
        Play();
    }

    // ---- graphics ----
    bool GpuInstalled { get { return File.Exists(Path.Combine(dir, "sor-gs.dll")); } }
    // The saved choice; with nothing saved, the video card at 3x when sor-gs.dll is there.
    bool SavedGpu { get { return GpuInstalled && ReadIni("graphics") != "cpu"; } }
    int SavedApi { get { int a; return int.TryParse(ReadIni("api"), out a) && a >= 1 && a <= 3 ? a : 2; } }   // package default DirectX 12 (the author, 9/11 21:07)
    int SavedScale { get { int s; return int.TryParse(ReadIni("scale"), out s) && Array.IndexOf(Scales, s) >= 0 ? s : 4; } }   // package default 4X (the author, 10/11: "the best")
    int SavedWide { get { string s = ReadIni("screen"); int w = s == "16:9" ? 1 : s == "21:9" ? 2 : s == "32:9" ? 2 : s == "4:3" ? 0 : 1; return w >= 2 && !SavedGpu ? 1 : w; } }   // package default 16:9 (the author, 9/11)
    bool SavedFull { get { return ReadIni("display") == "full"; } }   // package default: a window (the author, 10/11: his settings as the defaults)
    bool SavedMods { get { return ReadIni("mods") != "0"; } }   // on, unless turned off
    bool SavedSwap { get { return ReadIni("swap") == "1"; } }
    bool SavedMouseLook { get { return ReadIni("mouse_look") != "0"; } }
    int SavedMouseSpeed { get { int v; return int.TryParse(ReadIni("mouse_speed"), out v) && v >= 1 && v <= 10 ? v : 5; } }
    bool SavedMouseInvY { get { return ReadIni("mouse_invert_y") == "1"; } }
    // TOGGLE (the author, 9/11: to stay crouched without holding the key): one of the ten buttons cross ... r3; one press
    // holds it down, the next lets it go (pc/sor_tasti.cpp, SOR_TOGGLE). Off by default (launcher.ini: toggle=l1).
    static readonly string[] ToggleNames = { "OFF", "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "L1", "R1", "L2", "R2", "L3", "R3" };
    int SavedToggle { get { string tg = ReadIni("toggle"); int k = Array.IndexOf(MapNames, string.IsNullOrEmpty(tg) ? "l2" : tg); return k >= 0 && k < 10 ? k + 1 : 0; } }
    string ToggleLabel(int t) { if (t <= 0) return "OFF"; string k = SavedKey(t - 1); return ToggleNames[t] + (k == "" || k == "NONE" ? "" : " \u00B7 " + KeysLabel(k)); }
    bool SavedInvY { get { return ReadIni("invert_y") == "1"; } }
    bool SavedInvX { get { return ReadIni("invert_x") == "1"; } }
    string ModsDir { get { return Path.Combine(dir, @"textures\SLES-52950\replacements"); } }
    // What the MODS FOLDER holds: texture files (anywhere but movies\) and HD films (movies\film-<sector>.mp4, which
    // sor.exe plays in place of the game's own films: pc/sor_film_hd.cpp). Counted at most once a second.
    int modTextures, modFilms;
    DateTime modsCountedAt = DateTime.MinValue;
    static bool IsFilmName(string name)
    {
        if (!name.StartsWith("film-", StringComparison.OrdinalIgnoreCase) || !name.EndsWith(".mp4", StringComparison.OrdinalIgnoreCase)) return false;
        string n = name.Substring(5, name.Length - 9);
        if (n.Length == 0) return false;
        foreach (char c in n) if (c < '0' || c > '9') return false;
        return true;
    }
    string ModsFolderLine()
    {
        if ((DateTime.UtcNow - modsCountedAt).TotalSeconds >= 1)
        {
            modsCountedAt = DateTime.UtcNow;
            modTextures = 0; modFilms = 0;
            try
            {
                string movies = Path.Combine(ModsDir, "movies") + "\\";
                if (Directory.Exists(ModsDir))
                    foreach (string f in Directory.GetFiles(ModsDir, "*", SearchOption.AllDirectories))
                    {
                        if (!f.StartsWith(movies, StringComparison.OrdinalIgnoreCase)) modTextures++;
                        else if (Path.GetDirectoryName(f) + "\\" == movies && IsFilmName(Path.GetFileName(f))) modFilms++;
                    }
            }
            catch (Exception) { }
        }
        string films = modFilms == 1 ? "1 FILM" : modFilms + " FILMS";
        if (modTextures > 0 && modFilms > 0) return "OPEN IT  ·  TEXTURES + " + films + " FOUND";
        if (modFilms > 0) return "OPEN IT  ·  " + films + " FOUND";
        return modTextures > 0 ? "OPEN IT  ·  TEXTURES FOUND" : "OPEN IT  ·  EMPTY FOR NOW";
    }
        bool Saved60 { get { return ReadIni("framerate") != "50"; } }
        bool SavedOsd { get { return ReadIni("osd") != "0"; } }   // package default: on
        int SavedFov { get { int f; return int.TryParse(ReadIni("fov"), out f) && Array.IndexOf(Fovs, f) >= 0 ? f : 130; } } // FIELD OF VIEW: +30% at the first start (the author, 10/11); 100 as on the PS2
        bool SavedVignette { get { return ReadIni("vignette") == "1"; } } // the game's own grain/haze filters: off unless turned on (the author, 9/11)
        bool SavedVsync { get { return ReadIni("vsync") != "0"; } } // on, unless turned off
        int SavedSharp { get { int s; return int.TryParse(ReadIni("sharpness"), out s) && s >= 0 && s <= 2 ? s : 2; } } // HIGH at the first start (the author's package defaults, 9/11)
        bool SavedFxaa { get { return ReadIni("antialiasing") != "off"; } } // package default FXAA (the author, 9/11 21:07); antialiasing=off turns it off
        bool Changed { get { return useGpu != SavedGpu || (useGpu && (scale != SavedScale || api != SavedApi)) || wide != SavedWide || hz60 != Saved60 || vsync != SavedVsync || sharp != SavedSharp || fxaa != SavedFxaa || showOsd != SavedOsd || vignette != SavedVignette || fov != SavedFov; } }

    void OpenGraphics() { page = 1; useGpu = SavedGpu; scale = SavedScale; api = SavedApi; wide = SavedWide; hz60 = Saved60; vsync = SavedVsync; sharp = SavedSharp; fxaa = SavedFxaa; showOsd = SavedOsd; vignette = SavedVignette; fov = SavedFov; sel = 0; hover = -1; message = Hint(); Invalidate(); }

    void OpenSpeed() { page = 7; vu1Eng = SavedVu1; timings = SavedTimings; upFix = SavedUpFix; sel = 0; hover = -1; message = ""; Invalidate(); }

    void Back(string why) { if (page == 7) { page = 0; Choose(Array.IndexOf(Items, "OPTIONS")); sel = 8; message = why; Invalidate(); return; } if (page == 5 && debugFromOptions) { debugFromOptions = false; page = 0; Choose(Array.IndexOf(Items, "OPTIONS")); sel = 7; message = why; Invalidate(); return; } sel = page == 2 ? 1 : page == 3 ? 4 : page == 4 ? 3 : page == 5 ? 0 : 2; page = 0; hover = -1; message = why; Invalidate(); }

    // ---- updates (OPTIONS): asks GitHub for the latest release; if newer, a second Enter opens its page.
    // Nothing is downloaded or replaced by the launcher (the author, 9/11: the light way first, antivirus-safe).
    const string ReleasesApi = "https://api.github.com/repos/Korvonordico/shadow-of-rome-recompiled/releases/latest";
    int updateState;            // 0 idle, 1 asking
    string latestUrl, latestVersion;
    void CheckUpdates()
    {
        if (latestUrl != null)
        {
            try { Process.Start(latestUrl); message = "THE DOWNLOAD PAGE IS OPEN IN YOUR BROWSER"; }
            catch (Exception) { message = "COULD NOT OPEN THE BROWSER  ·  THE PAGE IS ON GITHUB: KORVONORDICO/SHADOW-OF-ROME-RECOMPILED"; }
            Invalidate(); return;
        }
        if (updateState == 1) return;
        updateState = 1; message = ""; Invalidate();
        var t = new System.Threading.Thread(() =>
        {
            string msg, url = null, ver = null;
            try
            {
                string json = Environment.GetEnvironmentVariable("SOR_FAKE_RELEASE");   // tests only
                if (string.IsNullOrEmpty(json))
                {
                    System.Net.ServicePointManager.SecurityProtocol |= (System.Net.SecurityProtocolType)3072;   // TLS 1.2
                    using (var w = new System.Net.WebClient())
                    {
                        w.Headers.Add("User-Agent", "ShadowOfRomeRecompiled-Launcher");
                        json = w.DownloadString(ReleasesApi);
                    }
                }
                var tag = System.Text.RegularExpressions.Regex.Match(json, @"""tag_name""[ \t]*:[ \t]*""v?([0-9.]+)""");
                var page = System.Text.RegularExpressions.Regex.Match(json, @"""html_url""[ \t]*:[ \t]*""(https://github.com/[^""]+/releases/tag/[^""]+)""");
                if (!tag.Success) throw new InvalidDataException();
                ver = tag.Groups[1].Value;
                if (new Version(ver) > new Version(Version))
                {
                    url = page.Success ? page.Groups[1].Value : "https://github.com/Korvonordico/shadow-of-rome-recompiled/releases/latest";
                    msg = "VERSION " + ver + " IS OUT  ·  PRESS ENTER ON UPDATES TO OPEN THE DOWNLOAD PAGE";
                }
                else msg = "YOU HAVE THE LATEST VERSION (" + Version + ")";
            }
            catch (Exception) { msg = "COULD NOT CHECK FOR UPDATES  ·  CHECK YOUR INTERNET CONNECTION AND TRY AGAIN"; }
            try { BeginInvoke((Action)(() => { updateState = 0; latestUrl = url; latestVersion = ver; message = msg; Invalidate(); })); } catch (Exception) { }
        });
        t.IsBackground = true; t.Start();
    }

    // ---- options and mods: one value, then APPLY and BACK (mods: also the folder) ----
    bool SmallChanged { get { return page == 7 ? vu1Eng != SavedVu1 || timings != SavedTimings || upFix != SavedUpFix : page == 2 ? full != SavedFull || bgmVol != ReadGameVol("bgm") || seVol != ReadGameVol("se") || menuSnd != SavedMenuSnd || ashOn != SavedAsh || smooth != SavedSmooth : page == 4 ? swap != SavedSwap || invY != SavedInvY || invX != SavedInvX || mouseLook != SavedMouseLook || mouseSpeed != SavedMouseSpeed || mouseInvY != SavedMouseInvY || toggleBtn != SavedToggle : page == 5 ? DebugChanged : mods != SavedMods; } }
    int ApplyRow { get { return page == 2 ? 9 : page == 7 ? 3 : page == 3 ? 2 : page == 4 ? 9 : page == 5 ? 9 : 1; } }
    bool IsToggle(int i) { return page == 4 ? i >= 2 && i <= 8 : page == 2 ? i <= 5 : page == 5 ? i <= 8 : page == 7 ? i <= 2 : i == 0; }
    void Toggle(int i)
    {
        if (page == 2) { if (i == 0) full = !full; else if (i == 1) bgmVol = (bgmVol + toggleDir + 11) % 11; else if (i == 2) seVol = (seVol + toggleDir + 11) % 11; else if (i == 3) menuSnd = !menuSnd; else if (i == 4) smooth = !smooth; else ashOn = !ashOn; }
        else if (page == 3) mods = !mods;
        else if (page == 7) { if (i == 0) vu1Eng = (vu1Eng + toggleDir + 3) % 3; else if (i == 1) timings = !timings; else upFix = (upFix + toggleDir + UpNames.Length) % UpNames.Length; }
        else if (page == 5) dbg[i] = !dbg[i];
        else if (i == 2) swap = !swap;      // CONTROLS: 2 BUTTONS, 3 VERTICAL, 4 HORIZONTAL
        else if (i == 3) invY = !invY;
        else if (i == 4) invX = !invX;
        else if (i == 5) mouseLook = !mouseLook;
        else if (i == 6) mouseSpeed = (mouseSpeed - 1 + toggleDir + 10) % 10 + 1;
        else if (i == 7) mouseInvY = !mouseInvY;
        else toggleBtn = (toggleBtn + toggleDir + ToggleNames.Length) % ToggleNames.Length;   // 8 TOGGLE
    }

    void ChooseSmall(int i)
    {
        PlayClick();
        sel = i;
        int apply = ApplyRow;
        if (IsToggle(i))
        {
            if (page == 5 && (i == 3 || i == 4) && !dbg[i] && armed != i) { armed = i; message = "THIS CHANGES YOUR SAVE FOR GOOD  ·  PRESS AGAIN TO TURN IT ON"; Invalidate(); return; }
            armed = -1; Toggle(i); message = page == 5 ? DebugWarning : ""; Invalidate(); return;
        }
        if (page == 4 && i <= 1) { OpenMap(i == 1); return; }   // KEYBOARD, CONTROLLER: the mapping page
        if (page == 2 && i == 6) { CheckUpdates(); return; }
        if (page == 2 && i == 7)
        {
            if (SmallChanged) { message = "OPTIONS NOT SAVED YET  ·  APPLY OR GO BACK FIRST"; Invalidate(); return; }
            OpenDebug(); debugFromOptions = true; return;
        }
        if (page == 2 && i == 8)
        {
            if (SmallChanged) { message = "OPTIONS NOT SAVED YET  \u00B7  APPLY OR GO BACK FIRST"; Invalidate(); return; }
            OpenSpeed(); return;
        }
        if (page == 3 && i == 1)
        {
            try { Directory.CreateDirectory(ModsDir); Process.Start("explorer.exe", "\"" + ModsDir + "\""); message = "PUT PCSX2 TEXTURE PACKS FOR SLES-52950 HERE"; }
            catch (Exception) { message = "COULD NOT OPEN THE MODS FOLDER  ·  THE FOLDER MAY BE READ-ONLY: MOVE THE GAME TO DOCUMENTS"; }
            Invalidate(); return;
        }
        if (i == apply && SmallChanged)
        {
            try
            {
                if (page == 2) { WriteIni("display", full ? "fullscreen" : "window"); WriteGameVols(bgmVol, seVol); WriteIni("menu_sounds", menuSnd ? "1" : "0"); WriteIni("background", ashOn ? "1" : "0"); WriteIni("smoothaudio", smooth ? "1" : "0"); liveSnd = menuSnd; liveAsh = ashOn; }
                else if (page == 7) { WriteIni("vu1engine", vu1Eng == 1 ? "pcsx2" : vu1Eng == 2 ? "confronto" : "nostro"); WriteIni("timings", timings ? "1" : "0"); WriteIni("upscalefix", upFix.ToString()); }
                else if (page == 3) WriteIni("mods", mods ? "1" : "0");
                else if (page == 5) { for (int k = 0; k < DKeys.Length; k++) WriteIni(DKeys[k], dbg[k] ? "1" : "0"); }
                else { WriteIni("swap", swap ? "1" : "0"); WriteIni("invert_y", invY ? "1" : "0"); WriteIni("invert_x", invX ? "1" : "0"); WriteIni("mouse_look", mouseLook ? "1" : "0"); WriteIni("mouse_speed", mouseSpeed.ToString()); WriteIni("mouse_invert_y", mouseInvY ? "1" : "0"); WriteIni("toggle", toggleBtn == 0 ? "none" : MapNames[toggleBtn - 1]); }
            }
            catch (Exception) { message = "COULD NOT SAVE THE SETTINGS  ·  THE FOLDER MAY BE READ-ONLY: MOVE THE GAME TO DOCUMENTS"; Invalidate(); return; }
            Back((page == 2 ? "OPTIONS" : page == 4 ? "CONTROLS" : page == 5 ? "DEBUG" : page == 7 ? "SPEED TESTS" : "MODS") + " SAVED  \u00B7  USED FROM THE NEXT PLAY");
            return;
        }
        Back(i == apply + 1 && SmallChanged ? "NOT SAVED  \u00B7  NOTHING CHANGED" : "");
    }

    void OnSmallKey(KeyEventArgs e)
    {
        int n = PageItems.Length;
        if ((e.KeyCode == Keys.Down || e.KeyCode == Keys.Up) && armed >= 0) { armed = -1; message = DebugWarning; }
        if (e.KeyCode == Keys.Down) { sel = (sel + 1) % n; Invalidate(); }
        else if (e.KeyCode == Keys.Up) { sel = (sel + n - 1) % n; Invalidate(); }
        else if ((e.KeyCode == Keys.Left || e.KeyCode == Keys.Right) && IsToggle(sel)) { toggleDir = e.KeyCode == Keys.Left ? -1 : 1; ChooseSmall(sel); toggleDir = 1; }
        else if (e.KeyCode == Keys.Right && page == 4 && sel <= 1) ChooseSmall(sel);   // KEYBOARD, CONTROLLER: "CHANGE ›" opens the page
        else if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space || e.KeyCode == Keys.X) ChooseSmall(sel);
        else if (e.KeyCode == Keys.Escape) ChooseSmall(n - 1);
    }

    string SmallDescribe(int i)
    {
        int apply = ApplyRow;
        if (page == 5 && i == 0) return "THE PLAYER TAKES NO DAMAGE";
        if (page == 5 && i == 1) return "WEAPONS DO NOT WEAR OUT";
        if (page == 5 && i == 2) return "ANY HIT KILLS, EVEN YOU";
        if (page == 5 && i == 3) return "SALVO AND GOLD FULL, SAVED";
        if (page == 5 && i == 4) return "OPTIONS AND SALVOS, SAVED";
        if (page == 5 && i == 5) return "FILMS ARE SKIPPED";
        if (page == 5 && i == 6) return "F8 IN GAME · 1-9 JUMP · CTRL+1-9 SAVE";
        if (page == 5 && i == DWire) return SavedGpu && SavedApi == 1 ? "POLYGONS AS WIRES  ·  F7 IN GAME" : "WORKS ONLY WITH DIRECTX 11";
        if (page == 5 && i == DPanel) return "F12 IN THE GAME WINDOW";
        if (page == 7 && i == 0) return vu1Eng == 1 ? "PCSX2'S VU1  \u00B7  NEEDS AVX2" : vu1Eng == 2 ? "BOTH, COMPARED IN THE LOG  \u00B7  SLOW" : "OURS, THE OLD ONE";
        if (page == 7 && i == 1) return timings ? "VU1 AND VIF TIMES IN THE LOG" : "NO TIME MEASURE";
        if (page == 7 && i == 2) return upFix == 0 ? "THE DEFAULT: SHARP, NO DOUBLE OUTLINES" : "A TEST: LOOK AT EDGES AND TREES";
        if (page == 4 && i == 0) return keysChanged == 0 ? "THE STANDARD KEYS" : keysChanged == 1 ? "1 KEY CHANGED" : keysChanged + " KEYS CHANGED";
        if (page == 4 && i == 1) return padChanged == 0 ? "THE STANDARD BUTTONS" : padChanged == 1 ? "1 BUTTON CHANGED" : padChanged + " BUTTONS CHANGED";
        if (page == 4 && i == 2) return swap ? "CROSS AND CIRCLE CHANGE PLACES" : "CROSS AND CIRCLE AS ON THE PS2";
        if (page == 4 && i <= 4) return "CAMERA, RIGHT STICK";
        if (page == 4 && i == 5) return mouseLook ? "CLICK IN GAME  ·  ALT FREES IT" : "THE MOUSE DOES NOTHING IN THE GAME";
        if (page == 4 && i == 6) return "HOW FAST THE MOUSE TURNS";
        if (page == 4 && i == 7) return mouseInvY ? "MOUSE UP WORKS AS STICK DOWN" : "MOUSE UP WORKS AS STICK UP";
        if (page == 4 && i == 8) return toggleBtn == 0 ? "HOLD A BUTTON WITH ONE PRESS" : "HELD UNTIL YOU PRESS IT AGAIN";
        if (i == 0 && page == 2) return "F11 SWITCHES WHILE PLAYING";
        if (i == 1 && page == 2) return bgmVol == 0 ? "MUSIC AND FILMS SILENT" : "MUSIC AND FILMS  ·  SAME AS IN THE GAME";
        if (i == 2 && page == 2) return seVol == 0 ? "EFFECTS SILENT" : "SOUND EFFECTS  ·  SAME AS IN THE GAME";
        if (i == 3 && page == 2) return "THE LAUNCHER'S CLICKS";
        if (i == 4 && page == 2) return smooth ? "NO CRACKLES WHEN THE GAME SLOWS DOWN" : "THE OLD SOUND  ·  IT CAN CRACKLE";
        if (i == 5 && page == 2) return "ASH AND EMBERS FALLING";
        if (i == 7 && page == 2) return "CHEATS AND TOOLS FOR TESTS";
        if (i == 8 && page == 2) return "VU1 ENGINE AND TIME MEASURE";
        if (i == 6 && page == 2) return updateState == 1 ? "ASKING GITHUB..." : latestUrl != null ? "VERSION " + latestVersion + " IS OUT" : "ASK GITHUB FOR A NEWER VERSION";
        if (i == 0) return mods ? "TEXTURE PACKS IN THE FOLDER" : "ORIGINAL TEXTURES ONLY";
        if (page == 3 && i == 1) return ModsFolderLine();
        if (i == apply) return SmallChanged ? "SAVE FOR THE NEXT PLAY" : "NOTHING CHANGED";
        return SmallChanged ? "WITHOUT SAVING" : "";
    }

    void PaintSmall(Graphics g, Brush gold, Brush bone, Brush ash, Brush ashText, Brush dimGold)
    {
        var items = PageItems; var latin = page == 2 ? OLatin : page == 4 ? CLatin : page == 5 ? DLatin : page == 7 ? TLatin : MLatin;
        int apply = ApplyRow;
        for (int i = 0; i < items.Length; i++)
        {
            var b = boxes[i];
            bool on = i == sel, live = i != apply || SmallChanged;
            if (on) Highlight(g, b, live, gold);
            Spaced(g, items[i], itemFont, live ? (on ? gold : bone) : ash, b.X + 6, b.Y + 4, 3);
            if (IsToggle(i))
            {
                string v = "\u2039 " + (page == 7 ? (i == 0 ? (vu1Eng == 1 ? "PCSX2" : vu1Eng == 2 ? "COMPARE" : "OURS") : i == 1 ? (timings ? "ON" : "OFF") : UpNames[upFix]) : page == 2 ? (i == 0 ? (full ? "FULL SCREEN" : "WINDOW") : i == 1 ? bgmVol.ToString() : i == 2 ? seVol.ToString() : (i == 3 ? menuSnd : i == 4 ? smooth : ashOn) ? "ON" : "OFF") : page == 4 ? (i == 2 ? (swap ? "SWAPPED" : "AS PS2") : i == 5 ? (mouseLook ? "ON" : "OFF") : i == 6 ? mouseSpeed.ToString() : i == 8 ? ToggleLabel(toggleBtn) : (i == 3 ? invY : i == 4 ? invX : mouseInvY) ? "INVERTED" : "NORMAL") : page == 5 ? (dbg[i] ? "ON" : "OFF") : (mods ? "ON" : "OFF")) + " \u203A";
                int vw = (int)SpacedWidth(g, v, valueFont, 2); valueRects[i] = new Rectangle(b.Right - 6 - vw, b.Y, vw, b.Height);
                Spaced(g, v, valueFont, on ? gold : bone, b.Right - 6 - vw, b.Y + 9, 2);
            }
            else if (page == 4 && i <= 1)
            {   // KEYBOARD, CONTROLLER: they open a page
                string v = "CHANGE ›";
                int vw = (int)SpacedWidth(g, v, valueFont, 2); valueRects[i] = new Rectangle(b.Right - 6 - vw, b.Y, vw, b.Height);
                Spaced(g, v, valueFont, on ? gold : bone, b.Right - 6 - vw, b.Y + 9, 2);
            }
            string d = SmallDescribe(i);
            Spaced(g, latin[i] + (d != "" ? "  \u00B7  " + d : ""), latinFont, live ? dimGold : ashText, b.X + 8, b.Y + 29, 2);
        }
    }

    string ClosedMessage(int code)
    {
        // every warning says what to do (the author, 9/11); the number helps when someone reports it
        return "THE GAME CLOSED RIGHT AWAY (ERROR " + code + ")  \u00B7  "
            + (SavedGpu ? "SET GRAPHICS TO PROCESSOR OR CHOOSE YOUR DISC AGAIN" : "PRESS PLAY AND CHOOSE YOUR DISC AGAIN");
    }

    string Hint() { if (sel == 0 && !GpuInstalled) return "THE VIDEO CARD FILE (SOR-GS.DLL) IS MISSING  ·  DOWNLOAD THE PACKAGE AGAIN"; return sel == 1 && useGpu ? "HIGHER IS SHARPER  \u00B7  IF THE GAME SLOWS DOWN, CHOOSE A LOWER ONE" : ""; }

    // Changes the value on a row: d = 1 the next one, -1 the one before (round).
    void Step(int row, int d)
    {
        if (row == 0 && GpuInstalled)
        {   // DirectX 11 > DirectX 12 > Vulkan > processor > DirectX 11 ...
            int s = ((useGpu ? api - 1 : 3) + d + 4) % 4;
            useGpu = s < 3; if (useGpu) api = s + 1;
        }
        else if (row == 1 && useGpu) { int k = Array.IndexOf(Scales, scale); if (k < 0) k = 3; scale = Scales[(k + d + Scales.Length) % Scales.Length]; }
        else if (row == 2 && useGpu) sharp = (sharp + d + 3) % 3;   // PCSX2's sharpening and texture filtering
        else if (row == 3 && useGpu) fxaa = !fxaa;         // PCSX2's FXAA, drawn by the video card's GS (SORGS_FXAA)
        else if (row == 4) { wide = useGpu ? (wide + d + 3) % 3 : 1 - Math.Min(wide, 1);   // 21:9 only with the video card; 32:9 (wide 3) is in sor.exe but broke the view in a test
 }
        else if (row == 5) hz60 = !hz60;
        else if (row == 6 && useGpu) vsync = !vsync;       // in step with the screen: done by the video card's GS
        else if (row == 7 && useGpu) showOsd = !showOsd;   // the statistics are drawn by the video card's GS
        else if (row == 8) vignette = !vignette;           // sor.exe skips the game's grain/haze filters (pc/sor_vignetta.cpp)
        else if (row == 9) { int k = Array.IndexOf(Fovs, fov); if (k < 0) k = 0; fov = Fovs[(k + d + Fovs.Length) % Fovs.Length]; }   // SOR_FOV
        message = row == 4 && wide >= 2 ? "NOT MADE FOR THIS GAME: SOME VIEWS MAY SHOW FLAWS  ·  FILMS STAY 4:3, MENUS STRETCHED" : row == 9 && fov > 100 ? "ONLY THE PLAYING CAMERA  ·  FILMS AND THE GAME'S OWN SCENES STAY AS THEY ARE" : Hint();
        Invalidate();
    }

    void ChooseGraphics(int i, int d)
    {
        PlayClick();
        sel = i;
        if (i <= 9) { Step(i, d); return; }
        if (i == 10 && Changed)
        {
            try { WriteIni("graphics", useGpu ? "gpu" : "cpu"); WriteIni("api", api.ToString()); WriteIni("scale", scale.ToString()); WriteIni("screen", ScreenNames[wide]); WriteIni("framerate", hz60 ? "60" : "50"); WriteIni("vsync", vsync ? "1" : "0"); WriteIni("sharpness", sharp.ToString()); WriteIni("antialiasing", fxaa ? "fxaa" : "off"); WriteIni("osd", showOsd ? "1" : "0"); WriteIni("vignette", vignette ? "1" : "0"); WriteIni("fov", fov.ToString()); }
            catch (Exception) { message = "COULD NOT SAVE THE SETTINGS  ·  THE FOLDER MAY BE READ-ONLY: MOVE THE GAME TO DOCUMENTS"; Invalidate(); return; }
            Back("GRAPHICS SAVED  \u00B7  USED FROM THE NEXT PLAY");
            return;
        }
        Back(i == 11 && Changed ? "NOT SAVED  \u00B7  THE GRAPHICS STAY AS THEY WERE" : "");
    }

    void OnGraphicsKey(KeyEventArgs e)
    {
        int n = GItems.Length;
        if (e.KeyCode == Keys.Down) { sel = (sel + 1) % n; message = Hint(); Invalidate(); }
        else if (e.KeyCode == Keys.Up) { sel = (sel + n - 1) % n; message = Hint(); Invalidate(); }
        else if (e.KeyCode == Keys.Left && sel <= 9) Step(sel, -1);
        else if (e.KeyCode == Keys.Right && sel <= 9) Step(sel, 1);
        else if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space || e.KeyCode == Keys.X) ChooseGraphics(sel, 1);
        else if (e.KeyCode == Keys.Escape) ChooseGraphics(11, 1);
    }

    // What a row's second line says about its value.
    string Describe(int i)
    {
        if (i == 0)
            return !GpuInstalled ? "VIDEO CARD FILE MISSING" : useGpu ? "PCSX2'S GS  \u00B7  SHARP, UPSCALED" : "THE PORT'S OWN  \u00B7  PS2 SIZE";
        if (i == 1)
            return !useGpu ? "ONLY WITH THE VIDEO CARD" : (680 * scale) + "\u00D7" + (510 * scale) + (scale == 1 ? "  \u00B7  AS ON THE PS2" : scale == 6 ? "  \u00B7  HEAVY" : scale == 8 ? "  \u00B7  VERY HEAVY" : "");
        if (i == 2) return !useGpu ? "ONLY WITH THE VIDEO CARD" : sharp == 0 ? "AS ON THE PS2" : sharp == 1 ? "CRISPER TEXTURES AND EDGES" : "CRISPEST, A MODERN LOOK";
        if (i == 3) return !useGpu ? "ONLY WITH THE VIDEO CARD" : fxaa ? "SOFTER EDGES AND TEXT" : "SHARP, EDGES MAY BE JAGGED";   // short: longer lines run into the logo (x 470)
        if (i == 4) return wide >= 2 ? "NOT MADE FOR IT, MAY SHOW FLAWS" : wide == 1 ? "WIDER VIEW, FOR WIDE MONITORS" : "AS ON THE PS2";
        if (i == 5) return hz60 ? "SMOOTH ON MODERN SCREENS" : "AS THE EUROPEAN PS2";
        if (i == 6) return !useGpu ? "ONLY WITH THE VIDEO CARD" : vsync ? "IN STEP WITH THE SCREEN" : "SHOWN AS SOON AS READY";
        if (i == 7) return !useGpu ? "ONLY WITH THE VIDEO CARD" : showOsd ? "FPS AND STUTTERS ON SCREEN" : "CLEAN SCREEN";
        if (i == 8) return vignette ? "AS ON THE PS2: GRAIN AND HAZE" : "NO GRAIN, NO HAZE";
        if (i == 9) return fov == 100 ? "AS ON THE PS2" : fov == 110 ? "A LITTLE WIDER" : fov == 120 ? "WIDER, SEE MORE AROUND" : "WIDEST, EDGES MAY SHOW FLAWS";
        if (i == 10) return Changed ? "SAVE FOR THE NEXT PLAY" : "NOTHING CHANGED";
        return Changed ? "WITHOUT SAVING" : "";
    }

    // launcher.ini: one key=value per line (iso, graphics, scale).
    string ReadIni(string key)
    {
        if (File.Exists(IniPath))
            foreach (var line in File.ReadAllLines(IniPath))
                if (line.StartsWith(key + "=")) return line.Substring(key.Length + 1).Trim();
        return "";
    }

    // Sets one key (null: removes it) and keeps the others.
    void WriteIni(string key, string value)
    {
        var lines = new List<string>();
        if (File.Exists(IniPath))
            foreach (var line in File.ReadAllLines(IniPath))
                if (!line.StartsWith(key + "=") && line.Trim() != "") lines.Add(line);
        if (value != null) lines.Add(key + "=" + value);
        File.WriteAllLines(IniPath, lines);
    }

    string ReadIso() { return ReadIni("iso"); }

    string Exe { get { return Path.Combine(dir, "sor.exe"); } }
    // disco\ next to the exe (the ready-made package), or ..\disco when the game was built with scripts/setup.sh
    string Disco { get { string up = Path.GetFullPath(Path.Combine(dir, @"..\disco")); return File.Exists(Path.Combine(up, "SLES_529.50.pc")) ? up : Path.Combine(dir, "disco"); } }
    string Elf { get { return Path.Combine(Disco, "SLES_529.50.pc"); } }

    // What stops Play before it asks for the disc, or "" if nothing does.
    string Missing()
    {
        if (!File.Exists(Exe)) return "THE GAME FILE (SOR.EXE) IS MISSING  ·  DOWNLOAD AND UNZIP THE PACKAGE AGAIN";
        if (!IsProgram(Exe)) return "THE GAME FILE (SOR.EXE) IS DAMAGED  ·  DOWNLOAD AND UNZIP THE PACKAGE AGAIN";
        return "";
    }

    // A Windows 64-bit program starts with "MZ" and has a "PE" header for x64: checked before starting it, so a
    // damaged file gives our message instead of Windows' own error window.
    static bool IsProgram(string path)
    {
        try
        {
            using (var f = new FileStream(path, FileMode.Open, FileAccess.Read))
            {
                var h = ReadAt(f, 0, 64);
                if (h[0] != 'M' || h[1] != 'Z') return false;
                int pe = BitConverter.ToInt32(h, 0x3c);
                if (pe <= 0 || pe > f.Length - 6) return false;
                var p = ReadAt(f, pe, 6);
                return p[0] == 'P' && p[1] == 'E' && p[2] == 0 && p[3] == 0 && BitConverter.ToUInt16(p, 4) == 0x8664;
            }
        }
        catch (Exception) { return false; }
    }

    // The SHA-1 of SLES_529.50 on the European disc (PAL, SLES-52950) the port was made on: the code in sor.exe
    // was translated from it, so another version of the game cannot work with it.
    const string ElfSha1 = "e56561c4b73fd6e01f63c1157bb7ce457fdb65d0";

    // Lists the files of an ISO9660 image: full path in capitals -> (first sector, size).
    static Dictionary<string, long[]> IsoFiles(FileStream f)
    {
        var all = new Dictionary<string, long[]>();
        var pvd = ReadAt(f, 16 * 2048L, 2048);
        if (pvd[1] != 'C' || pvd[2] != 'D' || pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1') throw new InvalidDataException("NOT A DISC IMAGE (ISO9660)");
        IsoDir(f, BitConverter.ToUInt32(pvd, 156 + 2), BitConverter.ToUInt32(pvd, 156 + 10), "", all, 0);
        return all;
    }

    static void IsoDir(FileStream f, long lba, long size, string prefix, Dictionary<string, long[]> all, int depth)
    {
        if (depth > 8) return;
        var d = ReadAt(f, lba * 2048, (int)Math.Min(size, 1 << 20));
        int i = 0;
        while (i < d.Length)
        {
            int len = d[i];
            if (len == 0) { i = (i / 2048 + 1) * 2048; continue; }
            if (i + 33 > d.Length) break;
            long elba = BitConverter.ToUInt32(d, i + 2), esize = BitConverter.ToUInt32(d, i + 10);
            int flags = d[i + 25], nl = d[i + 32];
            string name = System.Text.Encoding.ASCII.GetString(d, i + 33, Math.Min(nl, d.Length - i - 33));
            i += len;
            if (nl == 1 && (name[0] == '\0' || name[0] == '\x01')) continue;
            name = prefix + "/" + name.Split(';')[0].ToUpperInvariant();
            if ((flags & 2) != 0) IsoDir(f, elba, esize, name, all, depth + 1);
            else all[name] = new[] { elba, esize };
        }
    }

    static byte[] ReadAt(FileStream f, long pos, int n)
    {
        var b = new byte[n];
        f.Seek(pos, SeekOrigin.Begin);
        int got = 0;
        while (got < n) { int r = f.Read(b, got, n - got); if (r <= 0) break; got += r; }
        return b;
    }

    // Reads from the disc image into disco\ the files the game needs; "" when done, or what went wrong.
    // The executable is checked against the PAL disc, then saved with its empty segment past the PS2's 32 MB
    // turned off (PT_NULL), as strumenti/elf_per_pc.py does: the runtime's loader refuses that segment.
    static string ReadDisc(string iso, string disco)
    {
        try
        {
            using (var f = new FileStream(iso, FileMode.Open, FileAccess.Read))
            {
                var all = IsoFiles(f);
                if (!all.ContainsKey("/SLES_529.50")) return "THIS IS NOT THE EUROPEAN DISC (SLES-52950)  ·  OTHER VERSIONS DO NOT WORK YET";
                var e = all["/SLES_529.50"];
                var elf = ReadAt(f, e[0] * 2048, (int)e[1]);
                string sha;
                using (var s = SHA1.Create()) sha = BitConverter.ToString(s.ComputeHash(elf)).Replace("-", "").ToLowerInvariant();
                if (sha != ElfSha1) return "THIS DISC'S GAME IS A DIFFERENT VERSION  ·  ONLY THE EUROPEAN DISC (SLES-52950) WORKS";
                uint phoff = BitConverter.ToUInt32(elf, 0x1c);
                int phentsize = BitConverter.ToUInt16(elf, 0x2a), phnum = BitConverter.ToUInt16(elf, 0x2c);
                for (int i = 0; i < phnum; i++)
                {
                    int o = (int)phoff + i * phentsize;
                    uint type = BitConverter.ToUInt32(elf, o), vaddr = BitConverter.ToUInt32(elf, o + 8);
                    uint filesz = BitConverter.ToUInt32(elf, o + 16), memsz = BitConverter.ToUInt32(elf, o + 20);
                    if (type == 1 && filesz == 0 && (ulong)vaddr + memsz > 0x2000000) { elf[o] = elf[o + 1] = elf[o + 2] = elf[o + 3] = 0; }
                }
                Directory.CreateDirectory(Path.Combine(disco, "MODULES"));
                foreach (var kv in all)
                {
                    string dest = null;
                    if (kv.Key == "/SYSTEM.CNF") dest = Path.Combine(disco, "SYSTEM.CNF");
                    else if (kv.Key.StartsWith("/MODULES/")) dest = Path.Combine(disco, "MODULES", kv.Key.Substring(9));
                    if (dest != null) File.WriteAllBytes(dest, ReadAt(f, kv.Value[0] * 2048, (int)kv.Value[1]));
                }
                string tmp = Path.Combine(disco, "SLES_529.50.pc.part");
                File.WriteAllBytes(tmp, elf);
                string pc = Path.Combine(disco, "SLES_529.50.pc");
                if (File.Exists(pc)) File.Delete(pc);
                File.Move(tmp, pc);
            }
            return "";
        }
        catch (InvalidDataException) { return "THIS FILE IS NOT A DISC IMAGE OR IS DAMAGED  ·  CHOOSE YOUR SHADOW OF ROME .ISO"; }
        catch (Exception) { return "COULD NOT READ THE DISC  ·  CLOSE PROGRAMS THAT USE IT, OR COPY THE .ISO TO THIS PC AND RETRY"; }
    }

    void Play()
    {
        message = Missing();
        if (message != "") { Invalidate(); return; }
        string iso = ReadIso();
        if (iso == "" || !File.Exists(iso))
        {
            using (var d = new OpenFileDialog { Title = "Choose your Shadow of Rome disc image (PAL, SLES-52950)", Filter = "Disc image (*.iso)|*.iso|All files|*.*" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) { message = "THE GAME NEEDS YOUR OWN COPY OF THE DISC"; Invalidate(); return; }
                iso = d.FileName;
            }
        }
        if (!File.Exists(Elf))
        {   // the first Play: read the game's files from the disc image (a few seconds)
            message = "READING YOUR DISC  ·  ONLY THE FIRST TIME";
            Invalidate(); Update();
            Cursor = Cursors.WaitCursor;
            message = ReadDisc(iso, Disco);
            Cursor = Cursors.Default;
            if (message != "") { Invalidate(); return; }
        }
        WriteIni("iso", iso);
        var psi = new ProcessStartInfo(Exe, "\"" + Elf + "\" \"" + iso + "\" 0") { WorkingDirectory = dir, UseShellExecute = false };
        psi.EnvironmentVariables["SOR_FINESTRA"] = "1";
        psi.EnvironmentVariables["SOR_GRAPHICS"] = SavedGpu ? "gpu " + SavedScale + " " + SavedApi : "cpu";
        psi.EnvironmentVariables["SOR_WIDESCREEN"] = SavedWide.ToString(); // 0 4:3, 1 16:9, 2 21:9, 3 32:9
        psi.EnvironmentVariables["SOR_FULLSCREEN"] = SavedFull ? "1" : "0";
        psi.EnvironmentVariables["SORGS_MODS"] = SavedMods ? "1" : "0";
        psi.EnvironmentVariables["SOR_PAD_SWAP"] = SavedSwap ? "1" : "0";        // the controller's cross and circle
        psi.EnvironmentVariables["SOR_PAD_INVERT_Y"] = SavedInvY ? "1" : "0";    // the right stick (camera)
        psi.EnvironmentVariables["SOR_PAD_INVERT_X"] = SavedInvX ? "1" : "0";
        psi.EnvironmentVariables["SOR_MOUSE_LOOK"] = SavedMouseLook ? "1" : "0";     // pc/sor_mouse.cpp: the mouse turns the camera
        psi.EnvironmentVariables["SOR_MOUSE_SPEED"] = SavedMouseSpeed.ToString();
        psi.EnvironmentVariables["SOR_MOUSE_INVERT_Y"] = SavedMouseInvY ? "1" : "0";
        psi.EnvironmentVariables["SOR_TOGGLE"] = SavedToggle == 0 ? "off" : MapNames[SavedToggle - 1];   // pc/sor_tasti.cpp: CONTROLS > TOGGLE
        EnsureGameVols();
        psi.EnvironmentVariables["SOR_KEYS"] = MapEnv(false);          // CONTROLS > KEYBOARD: only the changed keys ("" = standard)
        psi.EnvironmentVariables["SOR_PAD_BUTTONS"] = MapEnv(true);    // CONTROLS > CONTROLLER: only the changed buttons
        psi.EnvironmentVariables["SOR_60HZ"] = Saved60 ? "1" : "0"; // the game's video mode byte: 1 = 60 Hz
        psi.EnvironmentVariables["SORGS_SHARPNESS"] = SavedSharp.ToString(); // sor-gs.dll: 0 as the PS2, 1 low, 2 high
        psi.EnvironmentVariables["SORGS_FXAA"] = SavedFxaa ? "1" : "0"; // sor-gs.dll: PCSX2's FXAA antialiasing (video card only)
        psi.EnvironmentVariables["SORGS_VSYNC"] = SavedVsync ? "1" : "0"; // sor-gs.dll: every picture in step with the screen
        if (!SavedVignette) psi.EnvironmentVariables["SOR_NO_VIGNETTE"] = "1"; // sor.exe: skip the game's grain/haze filters
        if (SavedFov != 100) psi.EnvironmentVariables["SOR_FOV"] = SavedFov.ToString(); // sor.exe: the gameplay camera wider (GRAPHICS > FIELD OF VIEW)
        psi.EnvironmentVariables["SORGS_OSD"] = SavedOsd ? "1" : "0"; // read by sor-gs.dll (video card only)
        psi.EnvironmentVariables["SOR_AUDIO_STRETCH"] = SavedSmooth ? "1" : "0"; // sor.exe, pc/sor_audio.cpp: OPTIONS > SMOOTH AUDIO, 0 = the old sound
        int sorVu1Mode = SavedVu1; psi.EnvironmentVariables["SOR_VU1"] = sorVu1Mode == 1 ? "pcsx2" : sorVu1Mode == 2 ? "confronto" : "nostro"; // sor.exe, pc/sor_vu1.cpp: SPEED TESTS > VU1 ENGINE
        if (SavedUpFix > 0) { psi.EnvironmentVariables["SORGS_HALFPIXEL"] = UpHp[SavedUpFix]; psi.EnvironmentVariables["SORGS_NATIVESCALING"] = UpNs[SavedUpFix]; } // SPEED TESTS > UPSCALE FIX
        if (SavedTimings) psi.EnvironmentVariables["SOR_TEMPI"] = "1"; // sor.exe: [sor-time] lines (VU1, VIF1, GIF, IOP), SPEED TESTS > TIMINGS
        psi.EnvironmentVariables["SOR_LOG"] = NextLog();   // always on: the last 5 runs, for reports and for us
        psi.EnvironmentVariables["SOR_CHEATS"] = DebugCheats();                  // hidden DEBUG page: "" = no cheats
        psi.EnvironmentVariables["SOR_DEBUG_PANEL"] = SavedDbgPanel ? "1" : "0"; // hidden DEBUG page: F12 engine panel
        psi.EnvironmentVariables["SOR_FREECAM"] = SavedDbgFreecam ? "1" : "0";   // hidden DEBUG page: F8 free camera
        psi.EnvironmentVariables["SORGS_WIREFRAME"] = SavedDbg(DWire) ? "1" : "0"; // DEBUG page: F7 polygons as wires (sor-gs.dll, DirectX 11)
        Process p;
        try { p = Process.Start(psi); }
        catch (Exception) { message = "COULD NOT START THE GAME  ·  CHECK YOUR ANTIVIRUS OR DOWNLOAD THE PACKAGE AGAIN"; Invalidate(); return; }
        // If the game closes within 3 seconds, something is wrong: come back and say so instead of vanishing,
        // and forget the disc image, so that the next Play asks for it again.
        Hide();
        if (p.WaitForExit(3000))
        {
            try { WriteIni("iso", null); } catch (Exception) { }   // the graphics stay
            System.Threading.Thread.Sleep(300); ScrubLogs();
            Show();
            message = ClosedMessage(p.ExitCode);
            Invalidate();
            return;
        }
        Close();
    }

    // GenericTypographic alone measures a space as 0 wide ("COMING SOON" came out "COMINGSOON").
    static readonly StringFormat Typo = TypoWithSpaces();
    static StringFormat TypoWithSpaces()
    {
        var s = (StringFormat)StringFormat.GenericTypographic.Clone();
        s.FormatFlags |= StringFormatFlags.MeasureTrailingSpaces;
        return s;
    }

    static void Spaced(Graphics g, string s, Font f, Brush b, float x, float y, float gap)
    {
        foreach (char c in s)
        {
            string t = c.ToString();
            g.DrawString(t, f, b, x, y, StringFormat.GenericTypographic);
            x += g.MeasureString(t, f, PointF.Empty, Typo).Width + gap;
        }
    }

    static float SpacedWidth(Graphics g, string s, Font f, float gap)
    {
        float w = 0;
        foreach (char c in s) w += g.MeasureString(c.ToString(), f, PointF.Empty, Typo).Width + gap;
        return w - gap;
    }

    void Highlight(Graphics g, Rectangle b, bool strong, Brush gold)
    {
        using (var hl = new LinearGradientBrush(b, Color.FromArgb(strong ? 90 : 35, 120, 22, 14), Color.FromArgb(0, 120, 22, 14), 0f))
            g.FillRectangle(hl, b);
        g.FillPolygon(gold, new[] { new PointF(b.X - 16, b.Y + 14), new PointF(b.X - 9, b.Y + 21), new PointF(b.X - 16, b.Y + 28), new PointF(b.X - 23, b.Y + 21) });
    }

    // The graphics page: each row with its value on the right (arrows: it changes) and what it means below.
    void PaintGraphics(Graphics g, Brush gold, Brush bone, Brush ash, Brush ashText, Brush dimGold)
    {
        for (int i = 0; i < GItems.Length; i++)
        {
            var b = boxes[i];
            bool on = i == sel;
            bool live = i == 0 ? GpuInstalled : i == 1 || i == 2 || i == 3 || i == 6 || i == 7 ? useGpu : i == 10 ? Changed : true;
            if (on) Highlight(g, b, live, gold);
            Spaced(g, GItems[i], itemFont, live ? (on ? gold : bone) : ash, b.X + 6, b.Y + 4, 3);
            string value = i == 0 ? (!useGpu ? "PROCESSOR" : api == 2 ? "DIRECTX 12" : api == 3 ? "VULKAN" : "DIRECTX 11") : i == 1 ? scale + "X" : i == 2 ? (sharp == 0 ? "OFF" : sharp == 1 ? "LOW" : "HIGH") : i == 3 ? (fxaa ? "FXAA" : "OFF") : i == 4 ? ScreenNames[wide] : i == 5 ? (hz60 ? "60 FPS" : "50 FPS") : i == 6 ? (vsync ? "ON" : "OFF") : i == 7 ? (showOsd ? "ON" : "OFF") : i == 8 ? (vignette ? "ON" : "OFF") : i == 9 ? (fov == 100 ? "ORIGINAL" : "+" + (fov - 100) + "%") : "";
            if (value != "")
            {
                string v = live ? "\u2039 " + value + " \u203A" : value;
                Spaced(g, v, valueFont, live ? (on ? gold : bone) : ash, b.Right - 6 - SpacedWidth(g, v, valueFont, 2), b.Y + 9, 2);
            }
            string d = Describe(i);
            Spaced(g, GLatin[i] + (d != "" ? "  \u00B7  " + d : ""), latinFont, live ? dimGold : ashText, b.X + 8, b.Y + 26, 2);   // 29 px rows
        }
    }

    // ---- ash and embers falling behind the menu (the author, 9/11: Rome burning, no music) ----
    class Flake { public float X, Y, VX, VY, Size, Phase; public bool Ember; }
    static readonly Random rnd = new Random();
    readonly Flake[] Cinders = new Flake[70];
    Flake NewAsh()
    {
        var f = new Flake { X = (float)(rnd.NextDouble() * 960), Y = -10, Ember = rnd.NextDouble() < 0.18 };
        f.Size = (float)(f.Ember ? 1.5 + rnd.NextDouble() * 1.5 : 1.2 + rnd.NextDouble() * 2.3);
        f.VY = (float)(0.25 + rnd.NextDouble() * 0.6); f.VX = (float)(0.1 + rnd.NextDouble() * 0.3);
        f.Phase = (float)(rnd.NextDouble() * 6.28); return f;
    }
    void StepAsh()
    {
        for (int i = 0; i < Cinders.Length; i++)
        {
            var f = Cinders[i]; f.Phase += 0.03f;
            f.X += f.VX + (float)Math.Sin(f.Phase) * 0.35f; f.Y += f.VY;
            if (f.Y > 550 || f.X > 975) Cinders[i] = NewAsh();
        }
    }
    void PaintAsh(Graphics g)
    {
        foreach (var f in Cinders)
        {
            int a = (int)(f.Ember ? 150 + 90 * Math.Abs(Math.Sin(f.Phase * 2)) : 70 + 40 * Math.Abs(Math.Sin(f.Phase)));
            var c = f.Ember ? Color.FromArgb(a, 255, 120 + (int)(60 * Math.Sin(f.Phase)), 40) : Color.FromArgb(a, 150, 140, 130);
            if (f.Ember) using (var glow = new SolidBrush(Color.FromArgb(a / 6, 255, 90, 20))) g.FillEllipse(glow, f.X - f.Size * 2, f.Y - f.Size * 2, f.Size * 5, f.Size * 5);
            using (var b = new SolidBrush(c)) g.FillEllipse(b, f.X, f.Y, f.Size, f.Size);
        }
    }

    // ---- menu sounds (the author, 9/11): made here, quiet and short; a soft metal tick to move, a dull thud to choose ----
    static IntPtr MakeSound(int ms, Func<double, double> wave)
    {
        const int rate = 22050; int n = rate * ms / 1000;
        var m = new MemoryStream(); var w = new BinaryWriter(m);
        w.Write(0x46464952); w.Write(36 + n * 2); w.Write(0x45564157); w.Write(0x20746D66); w.Write(16);
        w.Write((short)1); w.Write((short)1); w.Write(rate); w.Write(rate * 2); w.Write((short)2); w.Write((short)16);
        w.Write(0x61746164); w.Write(n * 2);
        for (int i = 0; i < n; i++) { double t = (double)i / rate, v = wave(t) * Math.Min(1, t * 2000); w.Write((short)(Math.Max(-1, Math.Min(1, v)) * 32767)); }
        var bytes = m.ToArray(); var h = System.Runtime.InteropServices.GCHandle.Alloc(bytes, System.Runtime.InteropServices.GCHandleType.Pinned); return h.AddrOfPinnedObject();
    }
    static readonly Random noise = new Random(7);
    static readonly IntPtr TickSound = MakeSound(60, t => 0.07 * Math.Exp(-t * 90) * (Math.Sin(2 * Math.PI * 2350 * t) + 0.6 * Math.Sin(2 * Math.PI * 3720 * t) + 0.3 * Math.Sin(2 * Math.PI * 5110 * t)));
    static readonly IntPtr ClickSound = MakeSound(160, t => Math.Exp(-t * 32) * (0.30 * Math.Sin(2 * Math.PI * (150 - 200 * t) * t) + 0.05 * (noise.NextDouble() * 2 - 1) * Math.Exp(-t * 60)) + 0.05 * Math.Exp(-t * 70) * Math.Sin(2 * Math.PI * 880 * t));
    // PlaySound async from memory: a new sound cuts the old one, so fast moves never pile up
    [System.Runtime.InteropServices.DllImport("winmm.dll")] static extern bool PlaySound(IntPtr data, IntPtr mod, uint flags);
    static void Play(IntPtr snd) { try { PlaySound(snd, IntPtr.Zero, 0x0004 | 0x0001 | 0x0002); } catch (Exception) { } }   // SND_MEMORY|SND_ASYNC|SND_NODEFAULT
    int lastTickMs;
    int lastSel = -1, lastPage = -1, lastScroll; bool skipTick;
    void PlayClick() { if (!Visible || !UseSnd) return; Play(ClickSound); lastTickMs = Environment.TickCount + 120; skipTick = true; }
    void CheckTick()
    {
        if (sel == lastSel && page == lastPage && mapScroll == lastScroll) return;   // mapScroll: the key mapping's list moved
        bool first = lastSel < 0; lastSel = sel; lastPage = page; lastScroll = mapScroll;
        if (first || skipTick || !Visible) { skipTick = false; return; }
        if (!UseSnd || Environment.TickCount - lastTickMs < 95) return; lastTickMs = Environment.TickCount; Play(TickSound);
    }

    // ---- Rome burning far away (the author, 9/11): a red glow low on the horizon and the city in black against it ----
    static GraphicsPath romePath;
    static GraphicsPath RomeSkyline()
    {
        var p = new GraphicsPath(); var r = new Random(11);
        var pts = new List<PointF>(); pts.Add(new PointF(0, 540));
        float x = 0;
        while (x < 960)
        {
            if (x > 530 && x < 810) { x = 810; continue; }   // room for the Colosseum
            float w = 18 + (float)r.NextDouble() * 34, h = 492 + (float)r.NextDouble() * 26;
            if (r.NextDouble() < 0.15) { pts.Add(new PointF(x, h)); pts.Add(new PointF(x + w / 2, h - 10)); pts.Add(new PointF(x + w, h)); }   // a pitched roof
            else { pts.Add(new PointF(x, h)); pts.Add(new PointF(x + w, h)); }
            x += w;
        }
        pts.Add(new PointF(960, 540)); p.AddPolygon(pts.ToArray());
        p.FillMode = FillMode.Winding; return p;
    }
    void PaintRome(Graphics g)
    {
        var glowRect = new RectangleF(-200, 380, 1360, 320);
        using (var path = new GraphicsPath())
        {
            path.AddEllipse(glowRect);
            using (var glow = new PathGradientBrush(path))
            {
                glow.CenterPoint = new PointF(660, 540);
                glow.CenterColor = Color.FromArgb(70, 200, 70, 20);
                glow.SurroundColors = new[] { Color.FromArgb(0, 200, 70, 20) };
                g.FillRectangle(glow, 0, 380, 960, 160);
            }
        }
        if (romePath == null) romePath = RomeSkyline();
        using (var b = new SolidBrush(Color.FromArgb(235, 6, 4, 4))) g.FillPath(b, romePath);
        PaintColosseum(g);
    }

    // The Colosseum: an oval drum seen from the side, three rows of round arches and the attic on top; the right part
    // of the outer ring has fallen, so the top steps down there, as in the real one.
    static float ColTop(float x)
    {
        if (x < 690) return 418;
        if (x < 715) return 432;
        if (x < 745) return 452;
        if (x < 770) return 470;
        return 486;
    }
    void PaintColosseum(Graphics g)
    {
        const float left = 545, right = 800, cx = (left + right) / 2, rad = (right - left) / 2;
        var outline = new List<PointF> { new PointF(left, 540) };
        for (float x = left; x <= right; x += 1) outline.Add(new PointF(x, ColTop(x) + (x < left + 6 || x > right - 6 ? 6 : 0)));
        outline.Add(new PointF(right, 540));
        using (var b = new SolidBrush(Color.FromArgb(245, 6, 4, 4))) g.FillPolygon(b, outline.ToArray());
        using (var light = new SolidBrush(Color.FromArgb(120, 190, 80, 25)))
        using (var dim = new SolidBrush(Color.FromArgb(70, 160, 65, 20)))
        using (var cornice = new SolidBrush(Color.FromArgb(55, 200, 110, 50)))
        {
            float[] tierTop = { 436, 466, 496 };
            for (int t = 0; t < 3; t++)
            {
                float y = tierTop[t];
                for (int k = -7; k <= 7; k++)
                {
                    double th = k * 0.2;
                    float x = cx + rad * (float)Math.Sin(th) * 0.96f, w = 11f * (float)Math.Cos(th);
                    if (w < 4 || ColTop(x) > y - 3) continue;
                    using (var arch = new GraphicsPath())
                    {
                        arch.AddArc(x - w / 2, y, w, w, 180, 180);
                        arch.AddLine(x + w / 2, y + w / 2, x + w / 2, y + 20);
                        arch.AddLine(x - w / 2, y + 20, x - w / 2, y + w / 2);
                        arch.CloseFigure();
                        g.FillPath(t == 2 ? dim : light, arch);
                    }
                }
                for (float x = left + 3; x < right - 3; x += 1) if (ColTop(x) < y - 5) g.FillRectangle(cornice, x, y - 4, 1, 1.2f);   // the cornice
            }
            for (int k = -7; k <= 2; k += 2)   // the attic: small windows on the standing part
            {
                float x = cx + rad * (float)Math.Sin(k * 0.2) * 0.96f;
                if (ColTop(x) <= 420) g.FillRectangle(dim, x - 2.5f, 424, 5, 6);
            }
        }
    }

    // ---- a quiet fire far away, looped on its own channel (winmm waveOut), so the menu clicks never cut it ----
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    struct WaveFormat { public short tag, ch; public int rate, bytes; public short align, bits, size; }
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    struct WaveHdr { public IntPtr data; public int len, recorded; public IntPtr user; public int flags, loops; public IntPtr next, reserved; }
    [System.Runtime.InteropServices.DllImport("winmm.dll")] static extern int waveOutOpen(out IntPtr h, int dev, ref WaveFormat f, IntPtr cb, IntPtr inst, int flags);
    [System.Runtime.InteropServices.DllImport("winmm.dll")] static extern int waveOutPrepareHeader(IntPtr h, IntPtr hdr, int size);
    [System.Runtime.InteropServices.DllImport("winmm.dll")] static extern int waveOutWrite(IntPtr h, IntPtr hdr, int size);
    [System.Runtime.InteropServices.DllImport("winmm.dll")] static extern int waveOutPause(IntPtr h);
    [System.Runtime.InteropServices.DllImport("winmm.dll")] static extern int waveOutRestart(IntPtr h);
    IntPtr fireOut = IntPtr.Zero; bool firePlaying;
    void StartFire()
    {
        try
        {
            const int rate = 22050, secs = 8, n = rate * secs;
            var pcm = new short[n]; var r = new Random(5);
            double lp = 0, lp2 = 0;
            var raw = new double[n];
            for (int i = 0; i < n; i++)
            {
                double w = r.NextDouble() * 2 - 1;
                lp += (w - lp) * 0.02; lp2 += (lp - lp2) * 0.05;          // a low roar
                raw[i] = lp2 * 1.5 + lp * 0.1;   // kept low: it sounded like wind (the author, 9/11)
            }
            for (int k = 0; k < secs * 9; k++)                           // crackles: short dry pops
            {
                int at = r.Next(n - 2000); double amp = 0.08 + r.NextDouble() * 0.18;
                for (int j = 0; j < 400; j++) raw[at + j] += amp * Math.Exp(-j / 40.0) * (r.NextDouble() * 2 - 1);
            }
            int fade = rate / 2;                                          // the end fades into the start: no seam
            for (int i = 0; i < fade; i++) { double t = (double)i / fade; raw[i] = raw[i] * t + raw[n - fade + i] * (1 - t); }
            for (int i = 0; i < n - fade; i++) pcm[i] = (short)(Math.Max(-1, Math.Min(1, raw[i] * 0.03)) * 32767);
            int len = (n - fade) * 2;
            var data = System.Runtime.InteropServices.Marshal.AllocHGlobal(len);
            System.Runtime.InteropServices.Marshal.Copy(pcm, 0, data, n - fade);
            var fmt = new WaveFormat { tag = 1, ch = 1, rate = rate, bytes = rate * 2, align = 2, bits = 16, size = 0 };
            if (waveOutOpen(out fireOut, -1, ref fmt, IntPtr.Zero, IntPtr.Zero, 0) != 0) { fireOut = IntPtr.Zero; return; }
            var hdr = new WaveHdr { data = data, len = len, flags = 0x4 | 0x8, loops = int.MaxValue };   // WHDR_BEGINLOOP | WHDR_ENDLOOP
            int hs = System.Runtime.InteropServices.Marshal.SizeOf(typeof(WaveHdr));
            var hp = System.Runtime.InteropServices.Marshal.AllocHGlobal(hs);
            System.Runtime.InteropServices.Marshal.StructureToPtr(hdr, hp, false);
            waveOutPrepareHeader(fireOut, hp, hs); waveOutWrite(fireOut, hp, hs); firePlaying = true;
        }
        catch (Exception) { fireOut = IntPtr.Zero; }
    }
    void UpdateFire()
    {   // the fire follows MENU SOUNDS and stops while the window is minimized
        bool want = UseSnd && WindowState != FormWindowState.Minimized && Visible;
        if (want && fireOut == IntPtr.Zero) StartFire();
        else if (fireOut != IntPtr.Zero && want != firePlaying) { if (want) waveOutRestart(fireOut); else waveOutPause(fireOut); firePlaying = want; }
    }

    // ---- the game's log (the author, 9/11): logs/run-1.txt is the last run, up to run-5. Not shown to players; read when
    // something goes wrong. The Windows user name is taken out of the paths, so a log can be attached to a public report.
    string LogDir { get { return Path.Combine(dir, "logs"); } }
    string NextLog()
    {
        try
        {
            Directory.CreateDirectory(LogDir);
            ScrubLogs();
            Func<int, string> F = n => Path.Combine(LogDir, "run-" + n + ".txt");
            if (File.Exists(F(5))) File.Delete(F(5));
            for (int n = 4; n >= 1; n--) if (File.Exists(F(n))) File.Move(F(n), F(n + 1));
            return F(1);
        }
        catch (Exception) { return ""; }   // no log rather than no game
    }
    void ScrubLogs()
    {
        try
        {
            if (!Directory.Exists(LogDir)) return;
            string user = Environment.UserName, home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            foreach (var f in Directory.GetFiles(LogDir, "run-*.txt"))
            {
                string t = File.ReadAllText(f), u = t;
                if (home.Length > 3) u = System.Text.RegularExpressions.Regex.Replace(u, System.Text.RegularExpressions.Regex.Escape(home), "%USERPROFILE%", System.Text.RegularExpressions.RegexOptions.IgnoreCase);
                if (user.Length > 0) u = System.Text.RegularExpressions.Regex.Replace(u, @"([\/]Users[\/])" + System.Text.RegularExpressions.Regex.Escape(user) + @"(?=[\/""\s]|$)", "$1<user>", System.Text.RegularExpressions.RegexOptions.IgnoreCase);
                if (u != t) File.WriteAllText(f, u);
            }
        }
        catch (Exception) { }
    }

    // ---- the title bar ----
    const int TitleH = 34;
    int titleHover = -1;
    [System.Runtime.InteropServices.DllImport("user32.dll")] static extern bool ReleaseCapture();
    [System.Runtime.InteropServices.DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, int msg, IntPtr w, IntPtr l);
    Rectangle TitleButtonRect(int i) { return new Rectangle(ClientSize.Width - (2 - i) * 46, 0, 46, TitleH); }
    int TitleButton(Point p) { for (int i = 0; i < 2; i++) if (TitleButtonRect(i).Contains(p)) return i; return -1; }
    protected override CreateParams CreateParams
    {   // a borderless window that still shows in the taskbar and minimizes from it
        get { var cp = base.CreateParams; cp.Style |= 0x00020000 /* WS_MINIMIZEBOX */ | 0x00080000 /* WS_SYSMENU */; return cp; }
    }

    void PaintTitle(Graphics g)
    {
        var bar = new Rectangle(0, 0, ClientSize.Width, TitleH);
        using (var bg = new LinearGradientBrush(bar, Color.FromArgb(30, 16, 11), Color.FromArgb(14, 9, 7), 0f)) g.FillRectangle(bg, bar);
        using (var line = new Pen(Color.FromArgb(110, 90, 50))) g.DrawLine(line, 0, TitleH - 1, ClientSize.Width, TitleH - 1);
        if (Icon != null) using (var ic = new Icon(Icon, 20, 20)) g.DrawIcon(ic, new Rectangle(10, 7, 20, 20));
        using (var b = new SolidBrush(Color.FromArgb(201, 162, 92))) Spaced(g, "SHADOW OF ROME RECOMPILED", latinFont, b, 40, 11, 3);
        for (int i = 0; i < 2; i++)
        {
            var r = TitleButtonRect(i);
            if (titleHover == i) using (var hb = new SolidBrush(i == 1 ? Color.FromArgb(140, 40, 25) : Color.FromArgb(60, 42, 26))) g.FillRectangle(hb, r);
            using (var p = new Pen(titleHover == i ? Color.FromArgb(240, 217, 168) : Color.FromArgb(201, 162, 92), 1.4f))
            {
                int cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
                if (i == 0) g.DrawLine(p, cx - 6, cy + 1, cx + 6, cy + 1);
                else { g.DrawLine(p, cx - 5, cy - 5, cx + 5, cy + 5); g.DrawLine(p, cx - 5, cy + 5, cx + 5, cy - 5); }
            }
        }
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
        CheckTick();
        PaintTitle(g);
        // a thin gold frame around the whole window
        using (var fr = new Pen(Color.FromArgb(110, 90, 50))) g.DrawRectangle(fr, 0, 0, ClientSize.Width - 1, ClientSize.Height - 1);
        g.SetClip(new Rectangle(1, TitleH, ClientSize.Width - 2, ClientSize.Height - TitleH - 1));
        g.TranslateTransform(0, TitleH);
        var r = new Rectangle(0, 0, 960, 540);
        using (var bg = new LinearGradientBrush(r, Color.FromArgb(24, 10, 8), Color.FromArgb(5, 4, 4), 50f)) g.FillRectangle(bg, r);
        PaintRome(g);
        if (UseAsh) PaintAsh(g);

        // the game's logo on the right, faded into the dark at the edges
        var lr = new Rectangle(470, 100, 450, 339);
        if (logo != null)
        {
            var cm = new ColorMatrix { Matrix33 = 0.92f };
            using (var ia = new ImageAttributes()) { ia.SetColorMatrix(cm); g.DrawImage(logo, lr, 0, 0, logo.Width, logo.Height, GraphicsUnit.Pixel, ia); }
        }
        else
        {
            using (var gold = new SolidBrush(Gold)) Spaced(g, "SHADOW OF ROME", titleFont, gold, 500, 190, 4);
        }

        using (var gold = new SolidBrush(Gold))
        using (var bone = new SolidBrush(Bone))
        using (var ash = new SolidBrush(Ash))
        using (var ashText = new SolidBrush(AshText))
        using (var dimGold = new SolidBrush(Color.FromArgb(150, 196, 158, 92)))
        using (var rule = new Pen(Color.FromArgb(110, 196, 158, 92), 1))
        {
            Spaced(g, "SHADOW OF ROME", subFont, dimGold, 92, 62, 3);
            Spaced(g, page == 1 ? "GRAPHICS  \u00B7  SPECIES" : page == 2 ? "OPTIONS  \u00B7  OPTIONES" : page == 3 ? "MODS  \u00B7  MVTATIONES" : page == 4 ? "CONTROLS  \u00B7  IMPERIVM" : page == 5 ? "DEBUG  \u00B7  INSPECTIO" : page == 7 ? "SPEED TESTS  \u00B7  PROBATIO" : page == 6 ? (mapPad ? "CONTROLLER  \u00B7  MANVS" : "KEYBOARD  \u00B7  CLAVES") : "PC PORT", subFont, page >= 1 ? dimGold : ashText, 92, 88, 3);
            g.DrawLine(rule, 92, 122, 420, 122);
            if (page == 1) PaintGraphics(g, gold, bone, ash, ashText, dimGold);
            else if (page == 6) PaintMap(g, gold, bone, ash, ashText, dimGold);
            else if (page >= 2) PaintSmall(g, gold, bone, ash, ashText, dimGold);
            else for (int i = 0; i < Items.Length; i++)
            {
                var b = boxes[i];
                bool on = i == sel;
                if (on) Highlight(g, b, Ready[i], gold);
                Spaced(g, Items[i], itemFont, Ready[i] ? (on ? gold : bone) : ash, b.X + 6, b.Y + 4, 3);
                Spaced(g, Latin[i] + (Ready[i] ? "" : "  ·  COMING SOON"), latinFont, Ready[i] ? dimGold : ashText, b.X + 8, b.Y + 29, 2);
            }
            // the message above the line, the signature under it (the author, 9/11)
            if (message != "") Spaced(g, message, latinFont, dimGold, 92, 482, 2);
            g.DrawLine(rule, 92, 502, 420, 502);
            using (var tiny = new SolidBrush(Color.FromArgb(70, 66, 62)))
            {
                g.DrawString("by Korvonordico", smallFont, tiny, 92, 510);
            }
            // the version in a plain font: the serif ones may draw 0 as a small o
            using (var ver = new SolidBrush(Gold))
            using (var verFont = new Font("Segoe UI", 10f, FontStyle.Bold))
            using (var right = new StringFormat { Alignment = StringAlignment.Far })
                g.DrawString("VERSION " + Version, verFont, ver, new RectangleF(600, 510, 320, 24), right);
        }
    }

    [STAThread]
    static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        if (args.Length == 3 && args[0] == "--read-disc")
        {   // reads the disc image into a folder and exits (used by the tests); exit code 1 and the reason on failure
            string why = ReadDisc(args[1], args[2]);
            if (why != "") { File.WriteAllText(Path.Combine(args[2], "read-disc-error.txt"), why); Environment.Exit(1); }
            return;
        }
        var f = new Launcher();
        if (args.Length >= 2 && args[0] == "--picture")
        {   // draws the window into a PNG (used for docs/launcher.png) and exits;
            // with a third word "play", draws what Play would say before asking for the disc
            if (args.Length == 3 && args[2] == "play") f.message = f.Missing() != "" ? f.Missing() : "READY TO PLAY";
            if (args.Length == 3 && args[2] == "closed") f.message = f.ClosedMessage(3);
            // "graphics": the graphics page as saved; then "cpu" (processor chosen), "sharp" (high, chosen), "fxaa" (antialiasing on, chosen), "novsync" (off, chosen), "osd" (on, chosen) or a scale (chosen, row 2)
            if (args.Length >= 3 && (args[2] == "options" || args[2] == "mods" || args[2] == "controls")) f.Choose(args[2] == "options" ? 1 : args[2] == "controls" ? 3 : 4);
            // "keyboard" / "controller": the mapping page; then "capture" (waiting for a key, row 2), "dup" (a key already used),
            // "reserved" (F9 refused), "end" (scrolled to the last rows), "changed" (two keys / buttons swapped, SWAP on)
            if (args.Length >= 3 && (args[2] == "keyboard" || args[2] == "controller"))
            {
                f.Choose(3); f.OpenMap(args[2] == "controller");
                string w = args.Length == 4 ? args[3] : "";
                if (w == "capture") f.ChooseMap(1);
                else if (w == "dup") { f.ChooseMap(0); if (f.mapPad) f.SetPad("B"); else f.SetKey("C"); }
                else if (w == "reserved") { f.ChooseMap(0); f.message = Refused("F9"); }
                else if (w == "end") for (int k = 0; k < f.MapCount - 1; k++) f.MapMove(1);   // down to the last action
                else if (w == "changed") { f.mapKeys[0] = "V"; f.mapKeys[3] = "X,SPACE"; f.mapPads[0] = "B"; f.mapPads[1] = "A"; f.swap = true; f.sel = 0; }
                // "mouse": mouse buttons set as keys (as clicks and wheel notches would set them); "mousedup": a click already used
                else if (w == "mouse" || w == "mousedup")
                {
                    string[] m = { "MOUSE_LEFT", "MOUSE_RIGHT", "MOUSE_X1", "MOUSE_X2", "WHEEL_UP", "WHEEL_DOWN", "MOUSE_MIDDLE" };
                    for (int k = 0; k < m.Length; k++) { f.capture = k; f.SetKey(m[k]); }
                    f.sel = 6;
                    if (w == "mousedup") { f.ChooseMap(1); f.SetKey("MOUSE_LEFT"); }
                }
            }
            if (args.Length >= 3 && args[2] == "debug") { f.OpenDebug(); if (args.Length == 4 && args[3] == "changed") { f.dbg[0] = !f.dbg[0]; f.dbg[3] = !f.dbg[3]; f.dbg[DPanel] = !f.dbg[DPanel]; f.sel = 9; } }
            if (args.Length == 4 && args[3] == "changed") { f.full = !f.full; f.mods = !f.mods; }
            if (args.Length >= 3 && args[2] == "graphics")
            {
                f.OpenGraphics();
                int s;
                if (args.Length == 4 && args[3] == "cpu") f.useGpu = false;
                else if (args.Length == 4 && args[3] == "dx12") f.api = 2;
                else if (args.Length == 4 && args[3] == "wide") { f.wide = 1; f.sel = 4; }
                else if (args.Length == 4 && args[3] == "21") { f.useGpu = true; f.wide = 2; f.sel = 4; f.message = "NOT MADE FOR THIS GAME: SOME VIEWS MAY SHOW FLAWS  ·  FILMS STAY 4:3, MENUS STRETCHED"; }
                else if (args.Length == 4 && args[3] == "50") { f.hz60 = false; f.sel = 5; }
                else if (args.Length == 4 && args[3] == "sharp") { f.useGpu = true; f.sharp = 2; f.sel = 2; }
                else if (args.Length == 4 && args[3] == "fxaa") { f.useGpu = true; f.fxaa = true; f.sel = 3; }
                else if (args.Length == 4 && args[3] == "novsync") { f.useGpu = true; f.vsync = false; f.sel = 6; }
                else if (args.Length == 4 && args[3] == "osd") { f.useGpu = true; f.showOsd = true; f.sel = 7; }
                else if (args.Length == 4 && int.TryParse(args[3], out s)) { f.scale = s; f.sel = 1; f.message = f.Hint(); }
            }
            using (var bmp = new Bitmap(f.ClientSize.Width, f.ClientSize.Height))
            {
                using (var g = Graphics.FromImage(bmp)) f.OnPaint(new PaintEventArgs(g, f.ClientRectangle));
                bmp.Save(args[1], ImageFormat.Png);
            }
            return;
        }
        f.liveSnd = f.SavedMenuSnd; f.liveAsh = f.SavedAsh;
        if (f.DebugOn) f.message = "DEBUG TOOLS ON  ·  CTRL+D TO CHANGE THEM";   // so they are not forgotten
        Application.Run(f);
    }
}
