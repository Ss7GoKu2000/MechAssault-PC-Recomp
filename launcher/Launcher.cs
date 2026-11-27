using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Net;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;

internal sealed class Launcher : Form
{
    private const string Repository = "Ss7GoKu2000/MechAssault-PC-Recomp";
    private const string ReleasePage = "https://github.com/" + Repository + "/releases";
    private const string ReleaseEndpoint = "repos/" + Repository + "/releases?per_page=30";
    private static readonly Color PanelColor = Color.FromArgb(31, 38, 47);
    private static readonly Color AccentColor = Color.FromArgb(93, 213, 179);
    private readonly string install = AppDomain.CurrentDomain.BaseDirectory;
    private readonly string version;
    private readonly TextBox source = new TextBox();
    private readonly Label status = new Label();
    private readonly Button check;

    [STAThread]
    private static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        if (args.Length == 4 && args[0] == "--apply-update")
        {
            try { Installer.Apply(args); }
            catch (Exception error) { MessageBox.Show(error.Message, "MechAssault update", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            return;
        }
        Application.Run(new Launcher(args.FirstOrDefault()));
    }

    private Launcher(string selected)
    {
        version = ReadLocal("version.txt").Trim();
        Text = "MechAssault";
        ClientSize = new Size(620, 318);
        AutoScaleMode = AutoScaleMode.Dpi;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Segoe UI", 10);
        BackColor = Color.FromArgb(19, 24, 31);
        ForeColor = Color.WhiteSmoke;

        AddLabel("MECHASSAULT", 26, 18, 568, 48, 26, FontStyle.Bold, AccentColor);
        AddLabel("PC Recomp  /  Rev 1  /  " + version, 29, 70, 562, 25, 10, FontStyle.Regular, ForeColor);
        AddLabel("Game source", 29, 106, 562, 24, 10, FontStyle.Bold, ForeColor);

        source.SetBounds(29, 135, 562, 28);
        source.Text = selected ?? ReadLocal("last-game-source.txt");
        source.BackColor = PanelColor;
        source.ForeColor = ForeColor;
        Controls.Add(source);

        AddButton("Choose XISO", 29, 177, 133, BrowseIso);
        AddButton("Choose folder", 171, 177, 145, BrowseFolder);
        AcceptButton = AddButton("Play", 466, 177, 125, Play);
        ((Button)AcceptButton).BackColor = AccentColor;
        ((Button)AcceptButton).ForeColor = Color.FromArgb(15, 28, 26);

        check = AddButton("Check for updates", 29, 232, 166, CheckUpdates);
        AddButton("GitHub releases", 204, 232, 156, delegate { OpenReleases(); });
        status.SetBounds(29, 276, 562, 37);
        status.ForeColor = Color.Gainsboro;
        Controls.Add(status);
    }

    private string ReadLocal(string name)
    {
        string path = Path.Combine(install, name);
        return File.Exists(path) ? File.ReadAllText(path) : "";
    }

    private void AddLabel(string text, int x, int y, int width, int height, float size, FontStyle style, Color color)
    {
        var label = new Label { Text = text, ForeColor = color, Font = new Font("Segoe UI", size, style) };
        label.SetBounds(x, y, width, height);
        Controls.Add(label);
    }

    private Button AddButton(string text, int x, int y, int width, EventHandler action)
    {
        var button = new Button { Text = text, BackColor = PanelColor, ForeColor = ForeColor, FlatStyle = FlatStyle.Flat };
        button.FlatAppearance.BorderSize = 0;
        button.SetBounds(x, y, width, 36);
        button.Click += action;
        Controls.Add(button);
        return button;
    }

    private void BrowseIso(object sender, EventArgs args)
    {
        using (var dialog = new OpenFileDialog { Filter = "Xbox disc images (*.iso)|*.iso" })
            if (dialog.ShowDialog(this) == DialogResult.OK) source.Text = dialog.FileName;
    }

    private void BrowseFolder(object sender, EventArgs args)
    {
        using (var dialog = new FolderBrowserDialog {
            Description = "Choose the folder containing default.xbe.", ShowNewFolderButton = false
        })
            if (dialog.ShowDialog(this) == DialogResult.OK) source.Text = dialog.SelectedPath;
    }

    private void Play(object sender, EventArgs args)
    {
        try
        {
            string path = source.Text.Trim().Trim('"');
            if (!File.Exists(Path.Combine(path, "default.xbe")) &&
                !(File.Exists(path) && String.Equals(Path.GetExtension(path), ".iso", StringComparison.OrdinalIgnoreCase)))
                throw new InvalidOperationException("Choose a disc image or the folder containing default.xbe.");

            string runtime = Path.Combine(install, "game");
            var launch = new ProcessStartInfo(Path.Combine(runtime, "MechAssault.exe")) {
                WorkingDirectory = runtime, UseShellExecute = false
            };
            launch.EnvironmentVariables["MECHASSAULT_GAME_SOURCE"] = Path.GetFullPath(path);
            File.WriteAllText(Path.Combine(install, "last-game-source.txt"), path);
            Process.Start(launch);
            Close();
        }
        catch (Exception error)
        {
            MessageBox.Show(this, error.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private async void CheckUpdates(object sender, EventArgs args)
    {
        check.Enabled = false;
        status.Text = "Checking GitHub releases...";
        try
        {
            string releases = await Task.Run((Func<string>)ReadReleases);
            var list = new JavaScriptSerializer().Deserialize<Release[]>(releases);
            var latest = list.Where(release => !release.draft)
                .Select(release => new {
                    Version = ParseVersion(release.tag_name),
                    Asset = (release.assets ?? new ReleaseAsset[0]).FirstOrDefault(asset => asset.name == Installer.AssetName)
                })
                .Where(item => item.Version != null && item.Asset != null)
                .OrderByDescending(item => item.Version).FirstOrDefault();
            Version installed = ParseVersion(version);
            if (latest == null) status.Text = "No Windows release ZIP is available.";
            else if (installed == null) status.Text = "Installed version unknown. Open GitHub releases.";
            else if (latest.Version <= installed) status.Text = "You're on the latest available build.";
            else if (MessageBox.Show(this, "Download and install v" + latest.Version +
                     "? The launcher will restart after the update.",
                     Text, MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
            {
                Enabled = false;
                status.Text = "Downloading and verifying v" + latest.Version + "...";
                string work = await Task.Run(() => Installer.Stage(install, latest.Asset.browser_download_url,
                    latest.Asset.digest, latest.Asset.size, latest.Version));
                Installer.Start(install, work);
                Close();
            }
            else status.Text = "Update available: v" + latest.Version;
        }
        catch (WebException error)
        {
            var response = error.Response as HttpWebResponse;
            status.Text = response != null && response.StatusCode == HttpStatusCode.NotFound
                ? "Private repo. Sign in with GitHub CLI or open GitHub releases."
                : "Couldn't reach GitHub. Try again or open GitHub releases.";
        }
        catch (Exception error)
        {
            status.Text = error.Message;
        }
        finally { if (!IsDisposed) { Enabled = true; check.Enabled = true; } }
    }

    private static Version ParseVersion(string tag)
    {
        Version result;
        return Version.TryParse((tag ?? "").TrimStart('v', 'V'), out result) ? result : null;
    }

    private sealed class Release
    {
        public string tag_name { get; set; }
        public bool draft { get; set; }
        public ReleaseAsset[] assets { get; set; }
    }

    private sealed class ReleaseAsset
    {
        public string name { get; set; }
        public string browser_download_url { get; set; }
        public string digest { get; set; }
        public long size { get; set; }
    }

    private static string ReadReleases()
    {
        // Use a tester's existing GitHub CLI session, if present, for private releases.
        try
        {
            var command = new ProcessStartInfo("gh.exe", "api " + ReleaseEndpoint) {
                UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true
            };
            command.EnvironmentVariables["GH_PROMPT_DISABLED"] = "1";
            using (var cli = Process.Start(command))
            {
                var output = cli.StandardOutput.ReadToEndAsync();
                cli.BeginErrorReadLine();
                if (cli.WaitForExit(10000))
                {
                    if (cli.ExitCode == 0) return output.GetAwaiter().GetResult();
                }
                else cli.Kill();
            }
        }
        catch (System.ComponentModel.Win32Exception) { }

        ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
        var request = WebRequest.CreateHttp("https://api.github.com/" + ReleaseEndpoint);
        request.UserAgent = "MechAssault-PC-Recomp";
        request.Accept = "application/vnd.github+json";
        request.Timeout = 10000;
        request.ReadWriteTimeout = 10000;
        using (var response = request.GetResponse())
        using (var reader = new StreamReader(response.GetResponseStream()))
            return reader.ReadToEnd();
    }

    private void OpenReleases()
    {
        try { Process.Start(new ProcessStartInfo(ReleasePage) { UseShellExecute = true }); }
        catch (Exception error) { MessageBox.Show(this, error.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error); }
    }
}
