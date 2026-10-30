// Shadow of Rome PC port - launcher.
// Build: csc -target:winexe -out:ShadowOfRome.exe Launcher.cs  (the .NET Framework compiler that ships with Windows)
// Only "Play" works for now; the other entries are greyed out as "coming soon".
// logo.png (next to the exe) is optional and NOT part of the repository: it is the project's logo (docs/logo.png
// in the repository). Without it the title is drawn in text.
using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.Windows.Forms;

class Launcher : Form
{
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
        Text = "Shadow of Rome";
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

    void Play()
    {
        string exe = Path.Combine(dir, "sor.exe");
        string elf = Path.Combine(dir, @"..\disco\SLES_529.50.pc");
        if (!File.Exists(exe)) { message = "SOR.EXE NOT FOUND NEXT TO THE LAUNCHER"; Invalidate(); return; }
        string iso = ReadIso();
        if (iso == "" || !File.Exists(iso))
        {
            using (var d = new OpenFileDialog { Title = "Choose your Shadow of Rome disc image (PAL, SLES-52950)", Filter = "Disc image (*.iso)|*.iso|All files|*.*" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) { message = "THE GAME NEEDS YOUR OWN COPY OF THE DISC"; Invalidate(); return; }
                iso = d.FileName;
                File.WriteAllText(IniPath, "iso=" + iso + Environment.NewLine);
            }
        }
        var psi = new ProcessStartInfo(exe, "\"" + elf + "\" \"" + iso + "\" 0") { WorkingDirectory = dir, UseShellExecute = false };
        psi.EnvironmentVariables["SOR_FINESTRA"] = "1";
        try { Process.Start(psi); Close(); }
        catch (Exception ex) { message = "COULD NOT START THE GAME: " + ex.Message; Invalidate(); }
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
        }
    }

    [STAThread]
    static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        var f = new Launcher();
        if (args.Length == 2 && args[0] == "--picture")
        {   // draws the window into a PNG (used for docs/launcher.png) and exits
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
