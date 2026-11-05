// Shadow of Rome PC port - launcher.
// Build: build.bat, or csc -target:winexe -win32icon:ShadowOfRome.ico -out:ShadowOfRome.exe Launcher.cs
// (the .NET Framework compiler that ships with Windows).
// Only "Play" works for now; the other entries are greyed out as "coming soon".
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
    const string Version = "0.0.7";   // the version of the whole port (CHANGELOG.md)
    static readonly string[] Items = { "PLAY", "OPTIONS", "GRAPHICS", "CONTROLS", "MODS", "CREDITS", "QUIT" };
    static readonly string[] Latin = { "INCIPE", "OPTIONES", "SPECIES", "IMPERIVM", "MVTATIONES", "AVCTORES", "EXI" };
    static readonly bool[] Ready = { true, false, false, false, false, false, true };
    int sel = 0, hover = -1;
    string message = "";
    readonly string dir = AppDomain.CurrentDomain.BaseDirectory;
    string IniPath { get { return Path.Combine(dir, "launcher.ini"); } }
    readonly Rectangle[] boxes = new Rectangle[Items.Length];
    readonly Font titleFont, subFont, itemFont, latinFont, smallFont;
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
        smallFont = new Font("Segoe UI", 8.5f);
        string lp = Path.Combine(dir, "logo.png");
        if (File.Exists(lp)) using (var f = Image.FromFile(lp)) logo = new Bitmap(f);
        for (int i = 0; i < Items.Length; i++)
            boxes[i] = new Rectangle(90, 150 + i * 46, 330, 42);
        MouseMove += (s, e) => { int h = Hit(e.Location); if (h != hover) { hover = h; if (h >= 0) sel = h; Invalidate(); } };
        MouseClick += (s, e) => { int h = Hit(e.Location); if (h >= 0) Choose(h); };
        KeyDown += OnKey;
    }

    static string FirstFont(params string[] names)
    {
        using (var f = new InstalledFontCollection())
            foreach (var n in names)
                foreach (var fam in f.Families) if (fam.Name == n) return n;
        return "Georgia";
    }

    int Hit(Point p) { for (int i = 0; i < boxes.Length; i++) if (boxes[i].Contains(p)) return i; return -1; }

    void OnKey(object s, KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Down) { sel = (sel + 1) % Items.Length; message = ""; Invalidate(); }
        else if (e.KeyCode == Keys.Up) { sel = (sel + Items.Length - 1) % Items.Length; message = ""; Invalidate(); }
        else if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space || e.KeyCode == Keys.X) Choose(sel);
        else if (e.KeyCode == Keys.Escape) Close();
    }

    void Choose(int i)
    {
        if (Items[i] == "QUIT") { Close(); return; }
        if (!Ready[i]) { message = Items[i] + " - COMING SOON"; Invalidate(); return; }
        Play();
    }

    string ReadIso()
    {
        if (File.Exists(IniPath))
            foreach (var line in File.ReadAllLines(IniPath))
                if (line.StartsWith("iso=")) return line.Substring(4).Trim();
        return "";
    }

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
        File.WriteAllText(IniPath, "iso=" + iso + Environment.NewLine);
        var psi = new ProcessStartInfo(Exe, "\"" + Elf + "\" \"" + iso + "\" 0") { WorkingDirectory = dir, UseShellExecute = false };
        psi.EnvironmentVariables["SOR_FINESTRA"] = "1";
        Process p;
        try { p = Process.Start(psi); }
        catch (Exception ex) { message = "COULD NOT START THE GAME: " + ex.Message; Invalidate(); return; }
        // If the game closes within 3 seconds, something is wrong: come back and say so instead of vanishing,
        // and forget the disc image, so that the next Play asks for it again.
        Hide();
        if (p.WaitForExit(3000))
        {
            try { File.Delete(IniPath); } catch (Exception) { }
            Show();
            message = "THE GAME CLOSED AT ONCE (CODE " + p.ExitCode + ")  ·  PLAY ASKS FOR THE DISC IMAGE AGAIN";
            Invalidate();
            return;
        }
        Close();
    }

    static void Spaced(Graphics g, string s, Font f, Brush b, float x, float y, float gap)
    {
        foreach (char c in s)
        {
            string t = c.ToString();
            g.DrawString(t, f, b, x, y, StringFormat.GenericTypographic);
            x += g.MeasureString(t, f, PointF.Empty, StringFormat.GenericTypographic).Width + gap;
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
            Spaced(g, "PC PORT  ·  EXPERIMENTAL", subFont, ashText, 92, 88, 3);
            g.DrawLine(rule, 92, 122, 420, 122);
            for (int i = 0; i < Items.Length; i++)
            {
                var b = boxes[i];
                bool on = i == sel;
                if (on)
                {
                    using (var hl = new LinearGradientBrush(b, Color.FromArgb(Ready[i] ? 90 : 35, 120, 22, 14), Color.FromArgb(0, 120, 22, 14), 0f))
                        g.FillRectangle(hl, b);
                    g.FillPolygon(gold, new[] { new PointF(b.X - 16, b.Y + 14), new PointF(b.X - 9, b.Y + 21), new PointF(b.X - 16, b.Y + 28), new PointF(b.X - 23, b.Y + 21) });
                }
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
            using (var ver = new SolidBrush(Color.FromArgb(120, 112, 102)))
            using (var right = new StringFormat { Alignment = StringAlignment.Far })
                g.DrawString("Version " + Version, smallFont, ver, new RectangleF(600, 514, 320, 20), right);
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
