using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Windows.Forms;

internal static class Installer
{
    internal const string AssetName = "MechAssault-Rev1-Windows-x64.zip";

    internal static string Stage(string install, string url, string digest, long size, Version expectedVersion)
    {
        Uri asset;
        if (!Uri.TryCreate(url, UriKind.Absolute, out asset) ||
            asset.Scheme != Uri.UriSchemeHttps || asset.Host != "github.com" ||
            !asset.AbsolutePath.StartsWith("/Ss7GoKu2000/MechAssault-PC-Recomp/releases/download/", StringComparison.Ordinal) ||
            !asset.AbsolutePath.EndsWith("/" + AssetName, StringComparison.Ordinal))
            throw new InvalidDataException("The release does not point to this project's Windows ZIP.");
        if (String.IsNullOrEmpty(digest) || !digest.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase) ||
            digest.Length != 71 || size <= 0 || size > 512L * 1024 * 1024)
            throw new InvalidDataException("GitHub did not provide a valid ZIP size and SHA-256 digest.");

        string work = Path.Combine(install, ".mech-update-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(work);
        try
        {
            string zip = Path.Combine(work, "update.zip");
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            var request = WebRequest.CreateHttp(asset);
            request.UserAgent = "MechAssault-PC-Recomp";
            request.Timeout = 20000;
            request.ReadWriteTimeout = 20000;
            using (var response = request.GetResponse())
            using (var input = response.GetResponseStream())
            using (var file = File.Create(zip))
            {
                if (response.ResponseUri.Scheme != Uri.UriSchemeHttps)
                    throw new InvalidDataException("The release download redirected away from HTTPS.");
                byte[] buffer = new byte[65536];
                int read;
                while ((read = input.Read(buffer, 0, buffer.Length)) > 0)
                {
                    if (file.Length + read > size) throw new InvalidDataException("The ZIP exceeds its advertised size.");
                    file.Write(buffer, 0, read);
                }
                if (file.Length != size) throw new InvalidDataException("The update download is incomplete.");
            }

            using (var hash = SHA256.Create())
            using (var file = File.OpenRead(zip))
            {
                string actual = "sha256:" + BitConverter.ToString(hash.ComputeHash(file)).Replace("-", "");
                if (!String.Equals(actual, digest, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("The download failed the GitHub SHA-256 check.");
            }

            string contents = Path.Combine(work, "files");
            Extract(zip, contents, expectedVersion);
            File.Delete(zip);
            return work;
        }
        catch
        {
            Directory.Delete(work, true);
            throw;
        }
    }

    private static bool IsPackageFile(string name)
    {
        var parts = name.Split('/');
        if (parts.Any(part => part.Length == 0 || part == "." || part == ".." ||
            part.EndsWith(".") || part.EndsWith(" ") || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0))
            return false;
        if (parts.Length == 1)
            return new[] { "MechAssault.exe", "version.txt", "README.md", "SDK-LICENSE", "SDK-THIRD_PARTY.txt" }.Contains(name);
        if (parts[0] != "game") return false;
        if (parts.Length == 2)
            return new[] { "MechAssault.exe", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll" }.Contains(parts[1]);
        if (parts[1] != "compiler") return false;
        if (parts.Length == 3)
            return new[] { "clang.exe", "lld-link.exe", "xbe_recomp.exe", "MechAssault.lib", "LLVM-LICENSE.txt",
                           "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll" }.Contains(parts[2]);
        return parts[2] == "include" || parts[2] == "xdk" ||
               (parts.Length == 4 && parts[2] == "config" && parts[3] == "game.toml");
    }

    private static void Extract(string zip, string contents, Version expectedVersion)
    {
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        long total = 0;
        using (var archive = ZipFile.OpenRead(zip))
        {
            foreach (var entry in archive.Entries)
            {
                string name = entry.FullName;
                if (!IsPackageFile(name) || !names.Add(name) || entry.Length > 768L * 1024 * 1024 - total)
                    throw new InvalidDataException("Unexpected item in the Windows update ZIP: " + name);
                total += entry.Length;
                string target = Path.Combine(contents, name.Replace('/', Path.DirectorySeparatorChar));
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                entry.ExtractToFile(target);
            }
        }
        foreach (string name in new[] { "MechAssault.exe", "game/MechAssault.exe", "game/compiler/xbe_recomp.exe", "version.txt" })
            if (!names.Contains(name)) throw new InvalidDataException("The update ZIP is missing " + name);
        Version version;
        if (!Version.TryParse(File.ReadAllText(Path.Combine(contents, "version.txt")).Trim(), out version) ||
            version != expectedVersion)
            throw new InvalidDataException("The ZIP version does not match the release tag.");
    }

    internal static void Start(string install, string work)
    {
        // A separate copy can replace the original launcher after it exits.
        string helper = Path.Combine(Path.GetTempPath(), "MechAssault-Update-" + Guid.NewGuid().ToString("N") + ".exe");
        File.Copy(Application.ExecutablePath, helper);
        try
        {
            int pid = Process.GetCurrentProcess().Id;
            Process.Start(new ProcessStartInfo(helper, "--apply-update " + pid + " \"" + install.TrimEnd('\\') +
                "\" \"" + work + "\"") { UseShellExecute = false });
        }
        catch { File.Delete(helper); throw; }
    }

    private static void CheckInstallPath(string path)
    {
        for (string item = path; !String.IsNullOrEmpty(item); item = Path.GetDirectoryName(item))
            if ((File.Exists(item) || Directory.Exists(item)) &&
                (File.GetAttributes(item) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Cannot install through a linked file or directory: " + item);
    }

    internal static void Apply(string[] args)
    {
        string install = Path.GetFullPath(args[2]);
        string work = Path.GetFullPath(args[3]);
        if (!String.Equals(Path.GetDirectoryName(work), install, StringComparison.OrdinalIgnoreCase) ||
            !Path.GetFileName(work).StartsWith(".mech-update-", StringComparison.Ordinal))
            throw new InvalidDataException("Invalid update staging directory.");

        Process launcher = null;
        try { launcher = Process.GetProcessById(Int32.Parse(args[1])); }
        catch (ArgumentException) { }
        using (launcher)
            if (launcher != null && !launcher.WaitForExit(15000))
                throw new IOException("Close MechAssault before installing the update.");

        string contents = Path.Combine(work, "files");
        string backup = Path.Combine(work, "backup");
        var applied = new List<string>();
        string[] files = Directory.GetFiles(contents, "*", SearchOption.AllDirectories)
            .Select(file => file.Substring(contents.Length + 1))
            .OrderBy(name => name == "version.txt" ? 1 : 0).ToArray();
        foreach (string name in files)
        {
            if (!IsPackageFile(name.Replace('\\', '/'))) throw new InvalidDataException("Unexpected staged update file.");
            CheckInstallPath(Path.Combine(install, name));
            CheckInstallPath(Path.Combine(contents, name));
        }
        try
        {
            foreach (string name in files)
            {
                string destination = Path.Combine(install, name);
                string original = Path.Combine(backup, name);
                string replacement = Path.Combine(contents, name);
                Directory.CreateDirectory(Path.GetDirectoryName(destination));
                if (File.Exists(destination))
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(original));
                    File.Move(destination, original);
                }

                applied.Add(name);
                File.Move(replacement, destination);
            }
        }
        catch
        {
            foreach (string name in applied.AsEnumerable().Reverse())
            {
                string destination = Path.Combine(install, name);
                string original = Path.Combine(backup, name);
                if (File.Exists(destination)) File.Delete(destination);
                if (File.Exists(original)) File.Move(original, destination);
            }
            throw;
        }
        Directory.Delete(work, true);
        Process.Start(new ProcessStartInfo(Path.Combine(install, "MechAssault.exe")) { WorkingDirectory = install });
    }
}
