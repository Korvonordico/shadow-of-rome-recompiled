// Shadow of Rome PC port - launcher.
// Build: build.bat, or csc -target:winexe -win32icon:ShadowOfRome.ico -out:ShadowOfRome.exe Launcher.cs
// (the .NET Framework compiler that ships with Windows).
// Every entry works: Play, Options, Graphics, Controls, Mods and Quit.
// Graphics: the video card (PCSX2's GS, from sor-gs.dll next to sor.exe) at 1x to 4x the PS2's size, or the
// processor (the port's own drawing, as in 0.0.7). Saved in launcher.ini only by Apply; Play passes it to the game
// (SOR_GRAPHICS: "gpu <scale> <api>" or "cpu"; SOR_WIDESCREEN, SOR_60HZ; SORGS_VSYNC: every picture in step with
// the screen, vsync= in launcher.ini; SORGS_OSD: the video card's FPS and frame-time statistics on screen, osd=; SORGS_SHARPNESS: 0-2, sharpness=).
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
    const string Version = "1.9";     // the version of the whole port (CHANGELOG.md)
    static readonly string[] Items = { "PLAY", "OPTIONS", "GRAPHICS", "CONTROLS", "MODS", "QUIT" };
    static readonly string[] Latin = { "INCIPE", "OPTIONES", "SPECIES", "IMPERIVM", "MVTATIONES", "EXI" };
    static readonly bool[] Ready = { true, true, true, true, true, true };
    int sel = 0, hover = -1;
    string message = "";
    int page = 0;                      // 0: the main menu, 1: graphics, 2: options, 3: mods, 4: controls
    static readonly string[] OItems = { "DISPLAY", "APPLY", "BACK" };
    static readonly string[] OLatin = { "SPECTACVLVM", "CONFIRMA", "REDI" };
    static readonly string[] MItems = { "TEXTURE MODS", "MODS FOLDER", "APPLY", "BACK" };
    static readonly string[] MLatin = { "MVTATIONES", "APERI", "CONFIRMA", "REDI" };
    static readonly string[] CItems = { "BUTTONS", "VERTICAL", "HORIZONTAL", "APPLY", "BACK" };
    static readonly string[] CLatin = { "CLAVES", "SVRSVM DEORSVM", "DEXTRA SINISTRA", "CONFIRMA", "REDI" };
    bool swap, invY, invX;             // controls (controller only): cross and circle swapped; the camera's right stick inverted
    bool full; bool mods = true;       // options: full screen; mods: replacement textures used
    string[] PageItems { get { return page == 1 ? GItems : page == 2 ? OItems : page == 3 ? MItems : page == 4 ? CItems : Items; } }
    // Rows 0-5 change a value (VSYNC: every picture in step with the screen; OSD: the video card's on-screen
    // statistics), 6 saves, 7 goes back without saving.
    static readonly string[] GItems = { "RENDERER", "RESOLUTION", "SHARPNESS", "SCREEN", "FRAME RATE", "VSYNC", "OSD", "APPLY", "BACK" };
    static readonly string[] GLatin = { "PICTOR", "MAGNITVDO", "ACIES", "FORMA", "CELERITAS", "CONCENTVS", "NVNTIVS", "CONFIRMA", "REDI" };
    const int MaxScale = 4;
    bool hz60;                         // 60 Hz: the game's own 60 Hz mode, chosen at start
    static readonly string[] ScreenNames = { "4:3", "16:9", "21:9", "32:9" };
    int wide;                          // 0: 4:3, 1: widescreen 16:9 (any renderer), 2: 21:9 (video card only)
    bool useGpu; int scale = 3; int api = 1; bool showOsd; bool vsync = true; int sharp; // sharp: 0 off (as the PS2), 1 low, 2 high; api: 1 DirectX 11, 2 DirectX 12, 3 Vulkan
           // what the graphics page shows; launcher.ini changes only with APPLY
    readonly string dir = AppDomain.CurrentDomain.BaseDirectory;
    string IniPath { get { return Path.Combine(dir, "launcher.ini"); } }
    readonly Rectangle[] menuBoxes = new Rectangle[Items.Length];
    readonly Rectangle[] graphicsBoxes = new Rectangle[GItems.Length]; // 9 rows: a little closer together
    Rectangle[] boxes { get { return page == 1 ? graphicsBoxes : menuBoxes; } }
    readonly Font titleFont, subFont, itemFont, latinFont, smallFont, valueFont;
    readonly Image logo;

    static readonly Color Gold = Color.FromArgb(196, 158, 92);
    static readonly Color Bone = Color.FromArgb(214, 204, 186);
    static readonly Color Ash = Color.FromArgb(64, 60, 56);      // greyed entries: dark, they recede
    static readonly Color AshText = Color.FromArgb(82, 76, 70);

    Launcher()
    {
        Text = "Shadow of Rome Recompiled " + Version;
        ClientSize = new Size(960, 540);
        FormBorderStyle = FormBorderStyle.FixedSingle;
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
            graphicsBoxes[i] = new Rectangle(90, 140 + i * 37, 330, 37);
        MouseMove += (s, e) => { int h = Hit(e.Location); if (h != hover) { hover = h; if (h >= 0) sel = h; Invalidate(); } };
        MouseClick += (s, e) =>
        {
            int h = Hit(e.Location);
            if (h < 0) return;
            if (page == 1) ChooseGraphics(h, e.Button == MouseButtons.Right ? -1 : 1);   // right click: the value before
            else if (page >= 2) ChooseSmall(h);
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

    int Hit(Point p) { int n = PageItems.Length; for (int i = 0; i < n; i++) if (boxes[i].Contains(p)) return i; return -1; }

    void OnKey(object s, KeyEventArgs e)
    {
        if (page == 1) { OnGraphicsKey(e); return; }
        if (page >= 2) { OnSmallKey(e); return; }
        if (e.KeyCode == Keys.Down) { sel = (sel + 1) % Items.Length; message = ""; Invalidate(); }
        else if (e.KeyCode == Keys.Up) { sel = (sel + Items.Length - 1) % Items.Length; message = ""; Invalidate(); }
        else if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space || e.KeyCode == Keys.X) Choose(sel);
        else if (e.KeyCode == Keys.Escape) Close();
    }

    void Choose(int i)
    {
        if (Items[i] == "QUIT") { Close(); return; }
        if (!Ready[i]) { message = Items[i] + " - COMING SOON"; Invalidate(); return; }
        if (Items[i] == "GRAPHICS") { OpenGraphics(); return; }
        if (Items[i] == "CONTROLS") { page = 4; swap = SavedSwap; invY = SavedInvY; invX = SavedInvX; sel = 0; hover = -1; message = "FOR THE CONTROLLER  \u00B7  THE KEYBOARD KEYS ARE IN THE README"; Invalidate(); return; }
        if (Items[i] == "OPTIONS" || Items[i] == "MODS") { page = Items[i] == "OPTIONS" ? 2 : 3; full = SavedFull; mods = SavedMods; sel = 0; hover = -1; message = ""; Invalidate(); return; }
        Play();
    }

    // ---- graphics ----
    bool GpuInstalled { get { return File.Exists(Path.Combine(dir, "sor-gs.dll")); } }
    // The saved choice; with nothing saved, the video card at 3x when sor-gs.dll is there.
    bool SavedGpu { get { return GpuInstalled && ReadIni("graphics") != "cpu"; } }
    int SavedApi { get { int a; return int.TryParse(ReadIni("api"), out a) && a >= 1 && a <= 3 ? a : 1; } }
    int SavedScale { get { int s; return int.TryParse(ReadIni("scale"), out s) && s >= 1 && s <= MaxScale ? s : 3; } }
    int SavedWide { get { string s = ReadIni("screen"); int w = s == "16:9" ? 1 : s == "21:9" ? 2 : s == "32:9" ? 2 : 0; return w >= 2 && !SavedGpu ? 1 : w; } }
    bool SavedFull { get { return ReadIni("display") == "fullscreen"; } }
    bool SavedMods { get { return ReadIni("mods") != "0"; } }   // on, unless turned off
    bool SavedSwap { get { return ReadIni("swap") == "1"; } }
    bool SavedInvY { get { return ReadIni("invert_y") == "1"; } }
    bool SavedInvX { get { return ReadIni("invert_x") == "1"; } }
    string ModsDir { get { return Path.Combine(dir, @"textures\SLES-52950\replacements"); } }
        bool Saved60 { get { return ReadIni("framerate") != "50"; } }
        bool SavedOsd { get { return ReadIni("osd") == "1"; } }
        bool SavedVsync { get { return ReadIni("vsync") != "0"; } } // on, unless turned off
        int SavedSharp { get { int s; return int.TryParse(ReadIni("sharpness"), out s) && s >= 0 && s <= 2 ? s : 1; } } // LOW at the first start
        bool Changed { get { return useGpu != SavedGpu || (useGpu && (scale != SavedScale || api != SavedApi)) || wide != SavedWide || hz60 != Saved60 || vsync != SavedVsync || sharp != SavedSharp || showOsd != SavedOsd; } }

    void OpenGraphics() { page = 1; useGpu = SavedGpu; scale = SavedScale; api = SavedApi; wide = SavedWide; hz60 = Saved60; vsync = SavedVsync; sharp = SavedSharp; showOsd = SavedOsd; sel = 0; hover = -1; message = ""; Invalidate(); }

    void Back(string why) { sel = page == 2 ? 1 : page == 3 ? 4 : page == 4 ? 3 : 2; page = 0; hover = -1; message = why; Invalidate(); }

    // ---- options and mods: one value, then APPLY and BACK (mods: also the folder) ----
    bool SmallChanged { get { return page == 2 ? full != SavedFull : page == 4 ? swap != SavedSwap || invY != SavedInvY || invX != SavedInvX : mods != SavedMods; } }
    int ApplyRow { get { return page == 3 ? 2 : page == 4 ? 3 : 1; } }
    bool IsToggle(int i) { return page == 4 ? i <= 2 : i == 0; }
    void Toggle(int i)
    {
        if (page == 2) full = !full;
        else if (page == 3) mods = !mods;
        else if (i == 0) swap = !swap;
        else if (i == 1) invY = !invY;
        else invX = !invX;
    }

    void ChooseSmall(int i)
    {
        sel = i;
        int apply = ApplyRow;
        if (IsToggle(i)) { Toggle(i); message = ""; Invalidate(); return; }
        if (page == 3 && i == 1)
        {
            try { Directory.CreateDirectory(ModsDir); Process.Start("explorer.exe", "\"" + ModsDir + "\""); message = "PUT PCSX2 TEXTURE PACKS FOR SLES-52950 HERE"; }
            catch (Exception ex) { message = "COULD NOT OPEN THE FOLDER: " + ex.Message.ToUpperInvariant(); }
            Invalidate(); return;
        }
        if (i == apply && SmallChanged)
        {
            try
            {
                if (page == 2) WriteIni("display", full ? "fullscreen" : "window");
                else if (page == 3) WriteIni("mods", mods ? "1" : "0");
                else { WriteIni("swap", swap ? "1" : "0"); WriteIni("invert_y", invY ? "1" : "0"); WriteIni("invert_x", invX ? "1" : "0"); }
            }
            catch (Exception ex) { message = "COULD NOT SAVE: " + ex.Message.ToUpperInvariant(); Invalidate(); return; }
            Back((page == 2 ? "OPTIONS" : page == 4 ? "CONTROLS" : "MODS") + " SAVED  \u00B7  USED FROM THE NEXT PLAY");
            return;
        }
        Back(i == apply + 1 && SmallChanged ? "NOT SAVED  \u00B7  NOTHING CHANGED" : "");
    }

    void OnSmallKey(KeyEventArgs e)
    {
        int n = PageItems.Length;
        if (e.KeyCode == Keys.Down) { sel = (sel + 1) % n; Invalidate(); }
        else if (e.KeyCode == Keys.Up) { sel = (sel + n - 1) % n; Invalidate(); }
        else if ((e.KeyCode == Keys.Left || e.KeyCode == Keys.Right) && IsToggle(sel)) ChooseSmall(sel);
        else if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space || e.KeyCode == Keys.X) ChooseSmall(sel);
        else if (e.KeyCode == Keys.Escape) ChooseSmall(n - 1);
    }

    string SmallDescribe(int i)
    {
        int apply = ApplyRow;
        if (page == 4 && i == 0) return swap ? "CROSS AND CIRCLE CHANGE PLACES" : "CROSS AND CIRCLE AS ON THE PS2";
        if (page == 4 && i <= 2) return "CAMERA, RIGHT STICK";
        if (i == 0 && page == 2) return "F11 SWITCHES WHILE PLAYING";
        if (i == 0) return mods ? "TEXTURE PACKS IN THE FOLDER" : "ORIGINAL TEXTURES ONLY";
        if (page == 3 && i == 1) return Directory.Exists(ModsDir) && Directory.GetFiles(ModsDir, "*", SearchOption.AllDirectories).Length > 0 ? "OPEN IT  \u00B7  TEXTURES FOUND" : "OPEN IT  \u00B7  EMPTY FOR NOW";
        if (i == apply) return SmallChanged ? "SAVE FOR THE NEXT PLAY" : "NOTHING CHANGED";
        return SmallChanged ? "WITHOUT SAVING" : "";
    }

    void PaintSmall(Graphics g, Brush gold, Brush bone, Brush ash, Brush ashText, Brush dimGold)
    {
        var items = PageItems; var latin = page == 2 ? OLatin : page == 4 ? CLatin : MLatin;
        int apply = ApplyRow;
        for (int i = 0; i < items.Length; i++)
        {
            var b = boxes[i];
            bool on = i == sel, live = i != apply || SmallChanged;
            if (on) Highlight(g, b, live, gold);
            Spaced(g, items[i], itemFont, live ? (on ? gold : bone) : ash, b.X + 6, b.Y + 4, 3);
            if (IsToggle(i))
            {
                string v = "\u2039 " + (page == 2 ? (full ? "FULL SCREEN" : "WINDOW") : page == 4 ? (i == 0 ? (swap ? "SWAPPED" : "AS PS2") : (i == 1 ? invY : invX) ? "INVERTED" : "NORMAL") : (mods ? "ON" : "OFF")) + " \u203A";
                Spaced(g, v, valueFont, on ? gold : bone, b.Right - 6 - SpacedWidth(g, v, valueFont, 2), b.Y + 9, 2);
            }
            string d = SmallDescribe(i);
            Spaced(g, latin[i] + (d != "" ? "  \u00B7  " + d : ""), latinFont, live ? dimGold : ashText, b.X + 8, b.Y + 29, 2);
        }
    }

    string ClosedMessage(int code)
    {
        return "THE GAME CLOSED AT ONCE (CODE " + code + ")  \u00B7  "
            + (SavedGpu ? "TRY GRAPHICS: PROCESSOR, OR CHOOSE THE DISC AGAIN" : "PLAY ASKS FOR THE DISC IMAGE AGAIN");
    }

    string Hint() { return sel == 1 && useGpu ? "HIGHER IS SHARPER  \u00B7  IF THE GAME SLOWS DOWN, CHOOSE A LOWER ONE" : ""; }

    // Changes the value on a row: d = 1 the next one, -1 the one before (round).
    void Step(int row, int d)
    {
        if (row == 0 && GpuInstalled)
        {   // DirectX 11 > DirectX 12 > Vulkan > processor > DirectX 11 ...
            int s = ((useGpu ? api - 1 : 3) + d + 4) % 4;
            useGpu = s < 3; if (useGpu) api = s + 1;
        }
        else if (row == 1 && useGpu) scale = (scale - 1 + d + MaxScale) % MaxScale + 1;
        else if (row == 2 && useGpu) sharp = (sharp + d + 3) % 3;   // PCSX2's sharpening and texture filtering
        else if (row == 3) { wide = useGpu ? (wide + d + 3) % 3 : 1 - Math.Min(wide, 1);   // 21:9 only with the video card; 32:9 (wide 3) is in sor.exe but broke the view in a test
 }
        else if (row == 4) hz60 = !hz60;
        else if (row == 5 && useGpu) vsync = !vsync;       // in step with the screen: done by the video card's GS
        else if (row == 6 && useGpu) showOsd = !showOsd;   // the statistics are drawn by the video card's GS
        message = row == 3 && wide >= 2 ? "NOT MADE FOR THIS GAME: SOME VIEWS MAY SHOW FLAWS  ·  FILMS AND MENUS STAY 4:3" : Hint();
        Invalidate();
    }

    void ChooseGraphics(int i, int d)
    {
        sel = i;
        if (i <= 6) { Step(i, d); return; }
        if (i == 7 && Changed)
        {
            try { WriteIni("graphics", useGpu ? "gpu" : "cpu"); WriteIni("api", api.ToString()); WriteIni("scale", scale.ToString()); WriteIni("screen", ScreenNames[wide]); WriteIni("framerate", hz60 ? "60" : "50"); WriteIni("vsync", vsync ? "1" : "0"); WriteIni("sharpness", sharp.ToString()); WriteIni("osd", showOsd ? "1" : "0"); }
            catch (Exception ex) { message = "COULD NOT SAVE: " + ex.Message.ToUpperInvariant(); Invalidate(); return; }
            Back("GRAPHICS SAVED  \u00B7  USED FROM THE NEXT PLAY");
            return;
        }
        Back(i == 8 && Changed ? "NOT SAVED  \u00B7  THE GRAPHICS STAY AS THEY WERE" : "");
    }

    void OnGraphicsKey(KeyEventArgs e)
    {
        int n = GItems.Length;
        if (e.KeyCode == Keys.Down) { sel = (sel + 1) % n; message = Hint(); Invalidate(); }
        else if (e.KeyCode == Keys.Up) { sel = (sel + n - 1) % n; message = Hint(); Invalidate(); }
        else if (e.KeyCode == Keys.Left && sel <= 6) Step(sel, -1);
        else if (e.KeyCode == Keys.Right && sel <= 6) Step(sel, 1);
        else if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space || e.KeyCode == Keys.X) ChooseGraphics(sel, 1);
        else if (e.KeyCode == Keys.Escape) ChooseGraphics(8, 1);
    }

    // What a row's second line says about its value.
    string Describe(int i)
    {
        if (i == 0)
            return !GpuInstalled ? "NO VIDEO CARD: SOR-GS.DLL MISSING" : useGpu ? "PCSX2'S GS  \u00B7  SHARP, UPSCALED" : "THE PORT'S OWN  \u00B7  PS2 SIZE";
        if (i == 1)
            return !useGpu ? "ONLY WITH THE VIDEO CARD" : (680 * scale) + "\u00D7" + (510 * scale) + (scale == 1 ? "  \u00B7  AS ON THE PS2" : "");
        if (i == 2) return !useGpu ? "ONLY WITH THE VIDEO CARD" : sharp == 0 ? "AS ON THE PS2" : sharp == 1 ? "CRISPER TEXTURES AND EDGES" : "CRISPEST, A MODERN LOOK";
        if (i == 3) return wide >= 2 ? "NOT MADE FOR IT, MAY SHOW FLAWS" : wide == 1 ? "WIDER VIEW, FOR WIDE MONITORS" : "AS ON THE PS2";
        if (i == 4) return hz60 ? "SMOOTH ON MODERN SCREENS" : "AS THE EUROPEAN PS2";
        if (i == 5) return !useGpu ? "ONLY WITH THE VIDEO CARD" : vsync ? "IN STEP WITH THE SCREEN" : "SHOWN AS SOON AS READY";
        if (i == 6) return !useGpu ? "ONLY WITH THE VIDEO CARD" : showOsd ? "FPS AND STUTTERS ON SCREEN" : "CLEAN SCREEN";
        if (i == 7) return Changed ? "SAVE FOR THE NEXT PLAY" : "NOTHING CHANGED";
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
        if (!File.Exists(Exe)) return "SOR.EXE NOT FOUND  ·  BUILD THE GAME FIRST: SEE THE README";
        return "";
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
        catch (Exception ex) { return "COULD NOT READ THE DISC IMAGE: " + ex.Message.ToUpperInvariant(); }
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
        psi.EnvironmentVariables["SOR_60HZ"] = Saved60 ? "1" : "0"; // the game's video mode byte: 1 = 60 Hz
        psi.EnvironmentVariables["SORGS_SHARPNESS"] = SavedSharp.ToString(); // sor-gs.dll: 0 as the PS2, 1 low, 2 high
        psi.EnvironmentVariables["SORGS_VSYNC"] = SavedVsync ? "1" : "0"; // sor-gs.dll: every picture in step with the screen
        psi.EnvironmentVariables["SORGS_OSD"] = SavedOsd ? "1" : "0"; // read by sor-gs.dll (video card only)
        Process p;
        try { p = Process.Start(psi); }
        catch (Exception ex) { message = "COULD NOT START THE GAME: " + ex.Message; Invalidate(); return; }
        // If the game closes within 3 seconds, something is wrong: come back and say so instead of vanishing,
        // and forget the disc image, so that the next Play asks for it again.
        Hide();
        if (p.WaitForExit(3000))
        {
            try { WriteIni("iso", null); } catch (Exception) { }   // the graphics stay
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
            bool live = i == 0 ? GpuInstalled : i == 1 || i == 2 || i == 5 || i == 6 ? useGpu : i == 7 ? Changed : true;
            if (on) Highlight(g, b, live, gold);
            Spaced(g, GItems[i], itemFont, live ? (on ? gold : bone) : ash, b.X + 6, b.Y + 4, 3);
            string value = i == 0 ? (!useGpu ? "PROCESSOR" : api == 2 ? "DIRECTX 12" : api == 3 ? "VULKAN" : "DIRECTX 11") : i == 1 ? scale + "X" : i == 2 ? (sharp == 0 ? "OFF" : sharp == 1 ? "LOW" : "HIGH") : i == 3 ? ScreenNames[wide] : i == 4 ? (hz60 ? "60 FPS" : "50 FPS") : i == 5 ? (vsync ? "ON" : "OFF") : i == 6 ? (showOsd ? "ON" : "OFF") : "";
            if (value != "")
            {
                string v = live ? "\u2039 " + value + " \u203A" : value;
                Spaced(g, v, valueFont, live ? (on ? gold : bone) : ash, b.Right - 6 - SpacedWidth(g, v, valueFont, 2), b.Y + 9, 2);
            }
            string d = Describe(i);
            Spaced(g, GLatin[i] + (d != "" ? "  \u00B7  " + d : ""), latinFont, live ? dimGold : ashText, b.X + 8, b.Y + 29, 2);
        }
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
        var r = ClientRectangle;
        using (var bg = new LinearGradientBrush(r, Color.FromArgb(24, 10, 8), Color.FromArgb(5, 4, 4), 50f)) g.FillRectangle(bg, r);

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
            Spaced(g, page == 1 ? "GRAPHICS  \u00B7  SPECIES" : page == 2 ? "OPTIONS  \u00B7  OPTIONES" : page == 3 ? "MODS  \u00B7  MVTATIONES" : page == 4 ? "CONTROLS  \u00B7  IMPERIVM" : "PC PORT", subFont, page >= 1 ? dimGold : ashText, 92, 88, 3);
            g.DrawLine(rule, 92, 122, 420, 122);
            if (page == 1) PaintGraphics(g, gold, bone, ash, ashText, dimGold);
            else if (page >= 2) PaintSmall(g, gold, bone, ash, ashText, dimGold);
            else for (int i = 0; i < Items.Length; i++)
            {
                var b = boxes[i];
                bool on = i == sel;
                if (on) Highlight(g, b, Ready[i], gold);
                Spaced(g, Items[i], itemFont, Ready[i] ? (on ? gold : bone) : ash, b.X + 6, b.Y + 4, 3);
                Spaced(g, Latin[i] + (Ready[i] ? "" : "  ·  COMING SOON"), latinFont, Ready[i] ? dimGold : ashText, b.X + 8, b.Y + 29, 2);
            }
            g.DrawLine(rule, 92, 482, 420, 482);
            if (message != "") Spaced(g, message, latinFont, dimGold, 92, 492, 2);
            using (var tiny = new SolidBrush(Color.FromArgb(70, 66, 62)))
            {
                g.DrawString("by Korvonordico", smallFont, tiny, 92, 514);
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
            // "graphics": the graphics page as saved; then "cpu" (processor chosen), "sharp" (high, chosen), "novsync" (off, chosen), "osd" (on, chosen) or a scale (chosen, row 2)
            if (args.Length >= 3 && (args[2] == "options" || args[2] == "mods" || args[2] == "controls")) f.Choose(args[2] == "options" ? 1 : args[2] == "controls" ? 3 : 4);
            if (args.Length == 4 && args[3] == "changed") { f.full = !f.full; f.mods = !f.mods; }
            if (args.Length >= 3 && args[2] == "graphics")
            {
                f.OpenGraphics();
                int s;
                if (args.Length == 4 && args[3] == "cpu") f.useGpu = false;
                else if (args.Length == 4 && args[3] == "dx12") f.api = 2;
                else if (args.Length == 4 && args[3] == "wide") { f.wide = 1; f.sel = 3; }
                else if (args.Length == 4 && args[3] == "21") { f.useGpu = true; f.wide = 2; f.sel = 3; f.message = "NOT MADE FOR THIS GAME: SOME VIEWS MAY SHOW FLAWS  ·  FILMS AND MENUS STAY 4:3"; }
                else if (args.Length == 4 && args[3] == "50") { f.hz60 = false; f.sel = 4; }
                else if (args.Length == 4 && args[3] == "sharp") { f.useGpu = true; f.sharp = 2; f.sel = 2; }
                else if (args.Length == 4 && args[3] == "novsync") { f.useGpu = true; f.vsync = false; f.sel = 5; }
                else if (args.Length == 4 && args[3] == "osd") { f.useGpu = true; f.showOsd = true; f.sel = 6; }
                else if (args.Length == 4 && int.TryParse(args[3], out s)) { f.scale = s; f.sel = 1; f.message = f.Hint(); }
            }
            using (var bmp = new Bitmap(f.ClientSize.Width, f.ClientSize.Height))
            {
                using (var g = Graphics.FromImage(bmp)) f.OnPaint(new PaintEventArgs(g, f.ClientRectangle));
                bmp.Save(args[1], ImageFormat.Png);
            }
            return;
        }
        Application.Run(f);
    }
}
