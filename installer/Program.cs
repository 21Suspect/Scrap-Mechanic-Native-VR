using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using Microsoft.Win32;

[assembly: AssemblyTitle("Scrap Mechanic VR Chapter 2 Beta Patcher")]
[assembly: AssemblyDescription("Installer, verifier, repair manager, and restorer for Scrap Mechanic Chapter 2 VR")]
[assembly: AssemblyCompany("Scrap Mechanic VR Community Project")]
[assembly: AssemblyProduct("Scrap Mechanic VR Chapter 2")]
[assembly: AssemblyVersion("0.3.1.0")]
[assembly: AssemblyFileVersion("0.3.1.0")]

namespace ScrapMechanicVRPatcher
{
    internal static class BuildInfo
    {
        internal const string Version = "0.3.1-chapter2-beta-20260829";
        internal const string GameBuild = "24529696";
        internal const string GameExeHash = "5D663BA2EC5DC8C7ABEFCC5C9344AE86F0A066C4069A91F54833524AC9A5B4F5";
        internal const string AddonHash = "4ED7EEA9409145FD7CAB406FF1A51C56E49B019B9D48B248E7C5051EB8E877AC";
        internal const string DxgiHash = "EC9245D05C11751F2AC0D2256E6921AD8FB36BE9172EF6D587856591EB729A25";
        internal const string LoaderHash = "018C6519AFBDEADE6DA9E7D59C406068DD58674D87A65AE27353484A05E6674A";
        internal const string ManifestHash = "C1E92DDD4E1AA686B7CDE6DCD982DBDFA883866CEC3F3D44B1D79EF12DEA4779";
        internal const string PatcherHash = "1EFFC0087C231808CDB825918D710C2577B0B664564EE6B5200386AA9A8684C2";
        internal const int ManagedFileCount = 28;
        internal const string ResourceName = "ScrapMechanicVR.Payload.zip";
    }

    internal static class Hashing
    {
        internal static string Sha256(string path)
        {
            // ReShade and the loaded native add-on may keep their files open while
            // the manager refreshes. Read through compatible sharing flags so a
            // healthy running installation is not reported as incomplete.
            using (FileStream stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete))
            using (SHA256 sha = SHA256.Create())
            {
                byte[] hash = sha.ComputeHash(stream);
                StringBuilder text = new StringBuilder(hash.Length * 2);
                for (int i = 0; i < hash.Length; i++)
                    text.Append(hash[i].ToString("X2"));
                return text.ToString();
            }
        }

        internal static string Sha256Text(string value)
        {
            using (SHA256 sha = SHA256.Create())
            {
                byte[] hash = sha.ComputeHash(Encoding.UTF8.GetBytes(value));
                StringBuilder text = new StringBuilder(hash.Length * 2);
                for (int i = 0; i < hash.Length; i++)
                    text.Append(hash[i].ToString("X2"));
                return text.ToString();
            }
        }
    }

    internal static class PackageManager
    {
        internal static readonly string StateRoot = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ScrapMechanicVR-Chapter2");

        internal static readonly string PackageDirectory = Path.Combine(
            StateRoot, "packages", BuildInfo.Version);

        internal static string PatcherPath
        {
            get { return Path.Combine(PackageDirectory, "Patcher.ps1"); }
        }

        private static bool PackageIsValid()
        {
            string manifest = Path.Combine(PackageDirectory, "manifest.json");
            string addon = Path.Combine(PackageDirectory, "payload", "Release", "smvr_native_vr_v1.addon64");
            return File.Exists(PatcherPath) && File.Exists(manifest) && File.Exists(addon) &&
                   String.Equals(Hashing.Sha256(PatcherPath), BuildInfo.PatcherHash, StringComparison.OrdinalIgnoreCase) &&
                   String.Equals(Hashing.Sha256(manifest), BuildInfo.ManifestHash, StringComparison.OrdinalIgnoreCase) &&
                   String.Equals(Hashing.Sha256(addon), BuildInfo.AddonHash, StringComparison.OrdinalIgnoreCase);
        }

        internal static void EnsureExtracted()
        {
            if (PackageIsValid())
                return;

            Directory.CreateDirectory(Path.Combine(StateRoot, "packages"));
            string temporaryDirectory = Path.Combine(
                StateRoot, "packages", ".extract-" + Guid.NewGuid().ToString("N"));
            string temporaryZip = temporaryDirectory + ".zip";

            try
            {
                using (Stream resource = Assembly.GetExecutingAssembly().GetManifestResourceStream(BuildInfo.ResourceName))
                {
                    if (resource == null)
                        throw new InvalidOperationException("The embedded VR payload is missing from this installer.");
                    using (FileStream output = File.Create(temporaryZip))
                        resource.CopyTo(output);
                }

                Directory.CreateDirectory(temporaryDirectory);
                ZipFile.ExtractToDirectory(temporaryZip, temporaryDirectory);

                string stagedManifest = Path.Combine(temporaryDirectory, "manifest.json");
                string stagedAddon = Path.Combine(temporaryDirectory, "payload", "Release", "smvr_native_vr_v1.addon64");
                string stagedPatcher = Path.Combine(temporaryDirectory, "Patcher.ps1");
                if (!File.Exists(stagedPatcher) ||
                    !File.Exists(stagedManifest) ||
                    !File.Exists(stagedAddon) ||
                    !String.Equals(Hashing.Sha256(stagedPatcher), BuildInfo.PatcherHash, StringComparison.OrdinalIgnoreCase) ||
                    !String.Equals(Hashing.Sha256(stagedManifest), BuildInfo.ManifestHash, StringComparison.OrdinalIgnoreCase) ||
                    !String.Equals(Hashing.Sha256(stagedAddon), BuildInfo.AddonHash, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("The embedded VR payload failed its integrity check.");

                if (Directory.Exists(PackageDirectory))
                    Directory.Delete(PackageDirectory, true);
                Directory.Move(temporaryDirectory, PackageDirectory);
            }
            finally
            {
                if (File.Exists(temporaryZip))
                    File.Delete(temporaryZip);
                if (Directory.Exists(temporaryDirectory))
                    Directory.Delete(temporaryDirectory, true);
            }
        }
    }

    internal static class GameLocator
    {
        internal static readonly string SavedGamePath = Path.Combine(PackageManager.StateRoot, "installed-game.txt");

        private static void AddCandidate(List<string> candidates, string candidate)
        {
            if (String.IsNullOrWhiteSpace(candidate))
                return;
            try
            {
                string full = Path.GetFullPath(Environment.ExpandEnvironmentVariables(candidate.Trim()));
                if (!candidates.Exists(delegate(string item) {
                    return String.Equals(item, full, StringComparison.OrdinalIgnoreCase);
                }))
                    candidates.Add(full);
            }
            catch { }
        }

        private static string RegistryString(RegistryHive hive, RegistryView view, string subKey, string name)
        {
            try
            {
                using (RegistryKey baseKey = RegistryKey.OpenBaseKey(hive, view))
                using (RegistryKey key = baseKey.OpenSubKey(subKey))
                {
                    object value = key == null ? null : key.GetValue(name);
                    return value == null ? null : Environment.ExpandEnvironmentVariables(value.ToString());
                }
            }
            catch { return null; }
        }

        private static void AddSteamRoot(List<string> candidates, string steamRoot)
        {
            if (String.IsNullOrWhiteSpace(steamRoot))
                return;

            AddCandidate(candidates, Path.Combine(steamRoot, "steamapps", "common", "Scrap Mechanic"));
            string libraries = Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf");
            if (!File.Exists(libraries))
                return;

            try
            {
                string text = File.ReadAllText(libraries);
                MatchCollection matches = Regex.Matches(text, "\\\"path\\\"\\s+\\\"([^\\\"]+)\\\"", RegexOptions.IgnoreCase);
                foreach (Match match in matches)
                {
                    string library = match.Groups[1].Value.Replace("\\\\", "\\");
                    AddCandidate(candidates, Path.Combine(library, "steamapps", "common", "Scrap Mechanic"));
                }
            }
            catch { }
        }

        internal static string Find()
        {
            List<string> candidates = new List<string>();

            try
            {
                if (File.Exists(SavedGamePath))
                    AddCandidate(candidates, File.ReadAllText(SavedGamePath).Trim());
            }
            catch { }

            string uninstallKey = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 387990";
            foreach (RegistryView view in new RegistryView[] { RegistryView.Registry64, RegistryView.Registry32 })
            {
                AddCandidate(candidates, RegistryString(RegistryHive.LocalMachine, view, uninstallKey, "InstallLocation"));
                AddCandidate(candidates, RegistryString(RegistryHive.CurrentUser, view, uninstallKey, "InstallLocation"));
            }

            AddSteamRoot(candidates, RegistryString(RegistryHive.CurrentUser, RegistryView.Registry64, @"Software\Valve\Steam", "SteamPath"));
            AddSteamRoot(candidates, RegistryString(RegistryHive.CurrentUser, RegistryView.Registry32, @"Software\Valve\Steam", "SteamPath"));
            AddSteamRoot(candidates, RegistryString(RegistryHive.LocalMachine, RegistryView.Registry32, @"SOFTWARE\Valve\Steam", "InstallPath"));
            AddSteamRoot(candidates, RegistryString(RegistryHive.LocalMachine, RegistryView.Registry64, @"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"));

            string programFilesX86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
            string programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
            AddCandidate(candidates, Path.Combine(programFilesX86, "Steam", "steamapps", "common", "Scrap Mechanic"));
            AddCandidate(candidates, Path.Combine(programFiles, "Steam", "steamapps", "common", "Scrap Mechanic"));

            foreach (string candidate in candidates)
                if (IsGameRoot(candidate))
                    return candidate;
            return String.Empty;
        }

        internal static bool IsGameRoot(string root)
        {
            return !String.IsNullOrWhiteSpace(root) &&
                   File.Exists(Path.Combine(root, "Release", "ScrapMechanic.exe")) &&
                   Directory.Exists(Path.Combine(root, "Data")) &&
                   Directory.Exists(Path.Combine(root, "Survival"));
        }

        internal static string GameHash(string root)
        {
            if (!IsGameRoot(root))
                return String.Empty;
            return Hashing.Sha256(Path.Combine(root, "Release", "ScrapMechanic.exe"));
        }

        internal static string ActiveOpenXrRuntime()
        {
            foreach (RegistryView view in new RegistryView[] { RegistryView.Registry64, RegistryView.Registry32 })
            {
                string value = RegistryString(RegistryHive.LocalMachine, view, @"SOFTWARE\Khronos\OpenXR\1", "ActiveRuntime");
                if (!String.IsNullOrWhiteSpace(value))
                    return value;
            }
            return String.Empty;
        }

        internal static bool LooksInstalled(string root)
        {
            try
            {
                string addon = Path.Combine(root, "Release", "smvr_native_vr_v1.addon64");
                string dxgi = Path.Combine(root, "Release", "dxgi.dll");
                string loader = Path.Combine(root, "Release", "libopenxr_loader.dll");
                string cxx = Path.Combine(root, "Release", "libc++.dll");
                string unwind = Path.Combine(root, "Release", "libunwind.dll");
                string reshadeIni = Path.Combine(root, "Release", "ReShade.ini");
                string vrIni = Path.Combine(root, "Release", "ScrapMechanicVR.ini");
                return File.Exists(addon) && File.Exists(dxgi) && File.Exists(loader) &&
                       File.Exists(cxx) && File.Exists(unwind) && File.Exists(reshadeIni) && File.Exists(vrIni) &&
                       String.Equals(Hashing.Sha256(addon), BuildInfo.AddonHash, StringComparison.OrdinalIgnoreCase) &&
                       String.Equals(Hashing.Sha256(dxgi), BuildInfo.DxgiHash, StringComparison.OrdinalIgnoreCase) &&
                       String.Equals(Hashing.Sha256(loader), BuildInfo.LoaderHash, StringComparison.OrdinalIgnoreCase);
            }
            catch { return false; }
        }

        internal static bool HasManagedInstall(string root)
        {
            try
            {
                string normalized = Path.GetFullPath(root).TrimEnd(
                    Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar).ToLowerInvariant();
                string key = Hashing.Sha256Text(normalized).Substring(0, 16);
                return File.Exists(Path.Combine(PackageManager.StateRoot, "install-state-" + key + ".json"));
            }
            catch { return false; }
        }

        internal static void Save(string root)
        {
            Directory.CreateDirectory(PackageManager.StateRoot);
            File.WriteAllText(SavedGamePath, root, new UTF8Encoding(false));
        }
    }

    internal sealed class CommandResult
    {
        internal int ExitCode;
        internal string Output;
        internal string LogPath;
    }

    internal static class PatcherRunner
    {
        private static string Quote(string value)
        {
            return "\"" + value.Replace("\"", "\\\"") + "\"";
        }

        internal static CommandResult RunVisible(string action, string gameRoot, bool elevated)
        {
            PackageManager.EnsureExtracted();
            string logDirectory = Path.Combine(PackageManager.StateRoot, "logs");
            Directory.CreateDirectory(logDirectory);
            string logPath = Path.Combine(logDirectory,
                DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + action.ToLowerInvariant() + ".log");
            string command =
                "$ErrorActionPreference='Stop'; $log=" + PowerShellLiteral(logPath) + "; try { & " +
                PowerShellLiteral(PackageManager.PatcherPath) + " -Action " +
                PowerShellLiteral(action) + " -GamePath " + PowerShellLiteral(gameRoot) +
                " *>&1 | Tee-Object -FilePath $log; exit 0 } catch { " +
                "$errorLine=('ERROR: ' + $_.Exception.Message); " +
                "$detail=($_ | Format-List * -Force | Out-String); " +
                "$errorLine | Out-File -LiteralPath $log -Append -Encoding Unicode; " +
                "$detail | Out-File -LiteralPath $log -Append -Encoding Unicode; " +
                "Write-Host $errorLine -ForegroundColor Red; Write-Host $detail -ForegroundColor DarkRed; exit 1 }";
            string encoded = Convert.ToBase64String(Encoding.Unicode.GetBytes(command));
            ProcessStartInfo info = new ProcessStartInfo();
            info.FileName = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
                @"WindowsPowerShell\v1.0\powershell.exe");
            info.Arguments = "-NoProfile -ExecutionPolicy Bypass -EncodedCommand " + encoded;
            info.WorkingDirectory = PackageManager.PackageDirectory;
            info.UseShellExecute = true;
            info.WindowStyle = ProcessWindowStyle.Normal;
            if (elevated)
                info.Verb = "runas";
            using (Process process = Process.Start(info))
            {
                process.WaitForExit();
                string output = File.Exists(logPath) ? File.ReadAllText(logPath) :
                    "No elevated action log was produced. The operation may have been cancelled before PowerShell started.";
                return new CommandResult { ExitCode = process.ExitCode, Output = output, LogPath = logPath };
            }
        }

        internal static string FailureMessage(string action, CommandResult result)
        {
            string output = String.IsNullOrWhiteSpace(result.Output) ? "No PowerShell diagnostic output was captured." : result.Output.Trim();
            string[] lines = output.Split(new string[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries);
            string errorLine = null;
            for (int i = lines.Length - 1; i >= 0; i--)
            {
                string candidate = lines[i].Trim();
                if (candidate.StartsWith("ERROR:", StringComparison.OrdinalIgnoreCase))
                {
                    errorLine = candidate;
                    break;
                }
            }
            if (String.IsNullOrWhiteSpace(errorLine))
                errorLine = "ERROR: PowerShell did not provide a concise exception message; see the log tail below.";
            int first = Math.Max(0, lines.Length - 10);
            string tail = String.Join(Environment.NewLine, lines, first, lines.Length - first);
            return action + " failed with exit code " + result.ExitCode + "." +
                   Environment.NewLine + Environment.NewLine + errorLine +
                   Environment.NewLine + Environment.NewLine + "Technical log tail:" +
                   Environment.NewLine + tail +
                   Environment.NewLine + Environment.NewLine + "Detailed log: " + result.LogPath;
        }

        internal static CommandResult RunCaptured(string action, string gameRoot)
        {
            PackageManager.EnsureExtracted();
            ProcessStartInfo info = new ProcessStartInfo();
            info.FileName = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
                @"WindowsPowerShell\v1.0\powershell.exe");
            info.Arguments = "-NoProfile -ExecutionPolicy Bypass -File " + Quote(PackageManager.PatcherPath) +
                             " -Action " + action + " -GamePath " + Quote(gameRoot);
            info.WorkingDirectory = PackageManager.PackageDirectory;
            info.UseShellExecute = false;
            info.CreateNoWindow = true;
            info.RedirectStandardOutput = true;
            info.RedirectStandardError = true;
            using (Process process = Process.Start(info))
            {
                string output = process.StandardOutput.ReadToEnd();
                string error = process.StandardError.ReadToEnd();
                process.WaitForExit();
                return new CommandResult {
                    ExitCode = process.ExitCode,
                    Output = output + (String.IsNullOrWhiteSpace(error) ? String.Empty : Environment.NewLine + error)
                };
            }
        }

        private static string PowerShellLiteral(string value)
        {
            return "'" + value.Replace("'", "''") + "'";
        }

        internal static void StartDetached(string gameRoot)
        {
            PackageManager.EnsureExtracted();
            string command =
                "$ErrorActionPreference='Stop'; try { & " + PowerShellLiteral(PackageManager.PatcherPath) +
                " -Action Start -GamePath " + PowerShellLiteral(gameRoot) +
                "; exit 0 } catch { Write-Host ''; Write-Host $_ -ForegroundColor Red; " +
                "[void](Read-Host 'VR launch failed. Press Enter to close'); exit 1 }";
            string encoded = Convert.ToBase64String(Encoding.Unicode.GetBytes(command));
            ProcessStartInfo info = new ProcessStartInfo();
            info.FileName = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
                @"WindowsPowerShell\v1.0\powershell.exe");
            info.Arguments = "-NoProfile -ExecutionPolicy Bypass -EncodedCommand " + encoded;
            info.WorkingDirectory = PackageManager.PackageDirectory;
            info.UseShellExecute = true;
            info.WindowStyle = ProcessWindowStyle.Normal;
            Process.Start(info);
        }
    }

    internal static class Shortcuts
    {
        internal static readonly string ManagerExecutable = Path.Combine(
            PackageManager.StateRoot, "ScrapMechanicVR-Chapter2.exe");

        private static string DesktopShortcut
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Start Scrap Mechanic VR - Chapter 2.lnk"); }
        }

        private static string StartMenuDirectory
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "Scrap Mechanic VR - Chapter 2"); }
        }

        private static void CreateShortcut(string path, string target, string arguments, string description, string icon)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            if (shellType == null)
                throw new InvalidOperationException("Windows Script Host is unavailable; shortcuts could not be created.");
            object shell = Activator.CreateInstance(shellType);
            object shortcut = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path });
            Type shortcutType = shortcut.GetType();
            shortcutType.InvokeMember("TargetPath", BindingFlags.SetProperty, null, shortcut, new object[] { target });
            shortcutType.InvokeMember("Arguments", BindingFlags.SetProperty, null, shortcut, new object[] { arguments });
            shortcutType.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, shortcut, new object[] { PackageManager.StateRoot });
            shortcutType.InvokeMember("Description", BindingFlags.SetProperty, null, shortcut, new object[] { description });
            if (File.Exists(icon))
                shortcutType.InvokeMember("IconLocation", BindingFlags.SetProperty, null, shortcut, new object[] { icon + ",0" });
            shortcutType.InvokeMember("Save", BindingFlags.InvokeMethod, null, shortcut, null);
        }

        internal static void Install(string gameRoot)
        {
            Directory.CreateDirectory(PackageManager.StateRoot);
            string current = Assembly.GetExecutingAssembly().Location;
            if (!String.Equals(Path.GetFullPath(current), Path.GetFullPath(ManagerExecutable), StringComparison.OrdinalIgnoreCase))
                File.Copy(current, ManagerExecutable, true);

            string gameIcon = Path.Combine(gameRoot, "Release", "ScrapMechanic.exe");
            CreateShortcut(DesktopShortcut, ManagerExecutable, "--start", "Start Scrap Mechanic Chapter 2 in native OpenXR VR", gameIcon);
            CreateShortcut(Path.Combine(StartMenuDirectory, "Start Scrap Mechanic VR - Chapter 2.lnk"), ManagerExecutable, "--start", "Start Scrap Mechanic Chapter 2 in native OpenXR VR", gameIcon);
            CreateShortcut(Path.Combine(StartMenuDirectory, "Manage or Uninstall Scrap Mechanic VR - Chapter 2.lnk"), ManagerExecutable, "", "Manage, verify, or uninstall the Chapter 2 VR snapshot", gameIcon);
        }

        internal static void Remove()
        {
            try { if (File.Exists(DesktopShortcut)) File.Delete(DesktopShortcut); } catch { }
            try { if (Directory.Exists(StartMenuDirectory)) Directory.Delete(StartMenuDirectory, true); } catch { }
        }
    }

    internal sealed class MainForm : Form
    {
        private static readonly Color AppBackground = Color.FromArgb(24, 26, 29);
        private static readonly Color CardBackground = Color.FromArgb(36, 39, 43);
        private static readonly Color FieldBackground = Color.FromArgb(27, 29, 32);
        private static readonly Color Accent = Color.FromArgb(255, 187, 35);
        private static readonly Color AccentHover = Color.FromArgb(255, 204, 82);
        private static readonly Color Teal = Color.FromArgb(44, 207, 190);
        private static readonly Color Success = Color.FromArgb(104, 211, 145);
        private static readonly Color Warning = Color.FromArgb(255, 183, 77);
        private static readonly Color Danger = Color.FromArgb(255, 105, 97);
        private static readonly Color PrimaryText = Color.FromArgb(241, 243, 245);
        private static readonly Color MutedText = Color.FromArgb(160, 166, 174);
        private static readonly Color Border = Color.FromArgb(67, 72, 78);

        private readonly TextBox gamePath;
        private readonly Label gameStatus;
        private readonly Label openXrStatus;
        private readonly Label modStatus;
        private readonly Button installButton;
        private readonly Button startButton;
        private readonly Button verifyButton;
        private readonly Button repairButton;
        private readonly Button uninstallButton;
        private readonly Button logsButton;
        private readonly RichTextBox details;

        internal MainForm()
        {
            Text = "Scrap Mechanic VR Chapter 2 - Beta";
            ClientSize = new Size(900, 700);
            MinimumSize = new Size(916, 739);
            StartPosition = FormStartPosition.CenterScreen;
            Font = new Font("Segoe UI", 9F);
            BackColor = AppBackground;
            ForeColor = PrimaryText;
            DoubleBuffered = true;

            Panel accentBar = new Panel();
            accentBar.BackColor = Accent;
            accentBar.Dock = DockStyle.Top;
            accentBar.Height = 5;
            Controls.Add(accentBar);

            Label brand = new Label();
            brand.Text = "SM" + Environment.NewLine + "VR";
            brand.TextAlign = ContentAlignment.MiddleCenter;
            brand.Font = new Font("Segoe UI Black", 12F, FontStyle.Bold);
            brand.ForeColor = Color.FromArgb(22, 24, 27);
            brand.BackColor = Accent;
            brand.Location = new Point(24, 23);
            brand.Size = new Size(58, 58);
            Controls.Add(brand);

            Label title = new Label();
            title.Text = "Scrap Mechanic Native VR — Chapter 2 Beta";
            title.Font = new Font("Segoe UI Semibold", 21F, FontStyle.Bold);
            title.ForeColor = PrimaryText;
            title.AutoSize = true;
            title.Location = new Point(98, 19);
            Controls.Add(title);

            Label subtitle = new Label();
            subtitle.Text = "CHAPTER 2 BETA  /  SCRAP MECHANIC 1.0  /  META QUEST 3";
            subtitle.AutoSize = true;
            subtitle.Font = new Font("Segoe UI Semibold", 8.5F, FontStyle.Bold);
            subtitle.ForeColor = MutedText;
            subtitle.Location = new Point(101, 61);
            Controls.Add(subtitle);

            Label version = new Label();
            version.Text = "BUILD  " + BuildInfo.Version;
            version.TextAlign = ContentAlignment.MiddleCenter;
            version.Font = new Font("Segoe UI Semibold", 8.5F, FontStyle.Bold);
            version.ForeColor = Accent;
            version.BackColor = CardBackground;
            version.Location = new Point(710, 31);
            version.Size = new Size(166, 32);
            version.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            Controls.Add(version);

            Panel setupCard = CreateCard(24, 105, 852, 184);
            setupCard.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            Controls.Add(setupCard);

            Label pathLabel = new Label();
            pathLabel.Text = "GAME INSTALLATION";
            pathLabel.Font = new Font("Segoe UI Semibold", 8.5F, FontStyle.Bold);
            pathLabel.ForeColor = Accent;
            pathLabel.AutoSize = true;
            pathLabel.Location = new Point(18, 15);
            setupCard.Controls.Add(pathLabel);

            gamePath = new TextBox();
            gamePath.Location = new Point(20, 42);
            gamePath.Size = new Size(700, 27);
            gamePath.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            gamePath.BackColor = FieldBackground;
            gamePath.ForeColor = PrimaryText;
            gamePath.BorderStyle = BorderStyle.FixedSingle;
            gamePath.Font = new Font("Segoe UI", 9.5F);
            setupCard.Controls.Add(gamePath);

            Button browse = new Button();
            browse.Text = "BROWSE";
            browse.Location = new Point(732, 40);
            browse.Size = new Size(100, 31);
            browse.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            StyleButton(browse, ButtonKind.Neutral);
            browse.Click += BrowseClicked;
            setupCard.Controls.Add(browse);

            gameStatus = CreateStatusLabel(setupCard, 86);
            openXrStatus = CreateStatusLabel(setupCard, 113);
            modStatus = CreateStatusLabel(setupCard, 140);

            Panel actionsCard = CreateCard(24, 305, 852, 116);
            actionsCard.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            Controls.Add(actionsCard);

            installButton = CreateActionButton(actionsCard, "INSTALL VR MOD", 18, 17, 170, ButtonKind.Primary);
            installButton.Click += InstallClicked;

            startButton = CreateActionButton(actionsCard, "START VR", 198, 17, 120, ButtonKind.Success);
            startButton.Click += StartClicked;

            verifyButton = CreateActionButton(actionsCard, "VERIFY", 328, 17, 110, ButtonKind.Neutral);
            verifyButton.Click += VerifyClicked;

            uninstallButton = CreateActionButton(actionsCard, "UNINSTALL / RESTORE", 448, 17, 170, ButtonKind.Danger);
            uninstallButton.Click += UninstallClicked;

            logsButton = CreateActionButton(actionsCard, "OPEN LOGS", 628, 17, 204, ButtonKind.Neutral);
            logsButton.Click += LogsClicked;

            repairButton = CreateActionButton(actionsCard, "REPAIR / CLEAN OLD VR FILES", 18, 67, 250, ButtonKind.Warning);
            repairButton.Click += RepairClicked;

            Label safety = new Label();
            safety.Text = "GUARDED INSTALL  •  BACKUPS VERIFIED  •  GAME EXE & SAVES UNTOUCHED";
            safety.AutoSize = false;
            safety.TextAlign = ContentAlignment.MiddleRight;
            safety.Font = new Font("Segoe UI Semibold", 8F, FontStyle.Bold);
            safety.ForeColor = MutedText;
            safety.Location = new Point(280, 69);
            safety.Size = new Size(552, 32);
            safety.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            actionsCard.Controls.Add(safety);

            Label activityLabel = new Label();
            activityLabel.Text = "ACTIVITY LOG";
            activityLabel.Font = new Font("Segoe UI Semibold", 8.5F, FontStyle.Bold);
            activityLabel.ForeColor = Accent;
            activityLabel.AutoSize = true;
            activityLabel.Location = new Point(28, 443);
            Controls.Add(activityLabel);

            details = new RichTextBox();
            details.Location = new Point(24, 468);
            details.Size = new Size(852, 207);
            details.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            details.ReadOnly = true;
            details.BorderStyle = BorderStyle.None;
            details.BackColor = Color.FromArgb(18, 20, 22);
            details.ForeColor = Color.FromArgb(202, 207, 213);
            details.Font = new Font("Consolas", 9F);
            details.Text = "[READY] Installation status loaded. Choose an action above.\n";
            Controls.Add(details);

            gamePath.Text = GameLocator.Find();
            if (GameLocator.IsGameRoot(gamePath.Text))
            {
                try { Icon = Icon.ExtractAssociatedIcon(Path.Combine(gamePath.Text, "Release", "ScrapMechanic.exe")); }
                catch { }
            }
            gamePath.TextChanged += delegate { RefreshState(); };
            Shown += delegate
            {
                RefreshState();
                if (startButton.Enabled) startButton.Focus();
                else if (installButton.Enabled) installButton.Focus();
            };
        }

        private enum ButtonKind { Primary, Success, Neutral, Warning, Danger }

        private static Panel CreateCard(int left, int top, int width, int height)
        {
            Panel panel = new Panel();
            panel.Location = new Point(left, top);
            panel.Size = new Size(width, height);
            panel.BackColor = CardBackground;
            return panel;
        }

        private Label CreateStatusLabel(Control parent, int top)
        {
            Label label = new Label();
            label.AutoSize = false;
            label.Location = new Point(20, top);
            label.Size = new Size(812, 23);
            label.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            label.Font = new Font("Segoe UI", 9F);
            label.ForeColor = MutedText;
            parent.Controls.Add(label);
            return label;
        }

        private Button CreateActionButton(Control parent, string text, int left, int top, int width, ButtonKind kind)
        {
            Button button = new Button();
            button.Text = text;
            button.Location = new Point(left, top);
            button.Size = new Size(width, 38);
            button.Font = new Font("Segoe UI Semibold", 8.5F, FontStyle.Bold);
            StyleButton(button, kind);
            parent.Controls.Add(button);
            return button;
        }

        private static void StyleButton(Button button, ButtonKind kind)
        {
            Color normal;
            Color hover;
            Color text;
            Color border;
            switch (kind)
            {
                case ButtonKind.Primary:
                    normal = Accent; hover = AccentHover; text = Color.FromArgb(25, 27, 29); border = Accent; break;
                case ButtonKind.Success:
                    normal = Teal; hover = Color.FromArgb(86, 225, 211); text = Color.FromArgb(20, 30, 30); border = Teal; break;
                case ButtonKind.Warning:
                    normal = Color.FromArgb(63, 55, 38); hover = Color.FromArgb(83, 69, 40); text = Warning; border = Color.FromArgb(126, 96, 45); break;
                case ButtonKind.Danger:
                    normal = CardBackground; hover = Color.FromArgb(68, 43, 44); text = Danger; border = Color.FromArgb(126, 62, 62); break;
                default:
                    normal = Color.FromArgb(49, 53, 58); hover = Color.FromArgb(62, 67, 73); text = PrimaryText; border = Border; break;
            }
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderSize = 1;
            button.FlatAppearance.BorderColor = border;
            button.FlatAppearance.MouseOverBackColor = hover;
            button.FlatAppearance.MouseDownBackColor = hover;
            button.BackColor = normal;
            button.ForeColor = text;
            button.UseVisualStyleBackColor = false;
            button.Cursor = Cursors.Hand;
            button.EnabledChanged += delegate
            {
                if (button.Enabled)
                {
                    button.BackColor = normal;
                    button.ForeColor = text;
                    button.FlatAppearance.BorderColor = border;
                    button.Cursor = Cursors.Hand;
                }
                else
                {
                    button.BackColor = Color.FromArgb(42, 45, 49);
                    button.ForeColor = Color.FromArgb(105, 111, 118);
                    button.FlatAppearance.BorderColor = Color.FromArgb(55, 59, 64);
                    button.Cursor = Cursors.Default;
                }
            };
        }

        private void SetBusy(bool busy)
        {
            UseWaitCursor = busy;
            installButton.Enabled = !busy && installButton.Enabled;
            startButton.Enabled = !busy && startButton.Enabled;
            verifyButton.Enabled = !busy && verifyButton.Enabled;
            repairButton.Enabled = !busy && repairButton.Enabled;
            uninstallButton.Enabled = !busy && uninstallButton.Enabled;
            Application.DoEvents();
        }

        private void Append(string text)
        {
            details.AppendText("[" + DateTime.Now.ToString("HH:mm:ss") + "] " + text.TrimEnd() + Environment.NewLine);
            details.SelectionStart = details.TextLength;
            details.ScrollToCaret();
        }

        private void RefreshState()
        {
            string root = gamePath.Text.Trim();
            bool valid = GameLocator.IsGameRoot(root);
            string hash = String.Empty;
            bool compatible = false;
            if (valid)
            {
                try
                {
                    hash = GameLocator.GameHash(root);
                    compatible = String.Equals(hash, BuildInfo.GameExeHash, StringComparison.OrdinalIgnoreCase);
                }
                catch { }
            }

            gameStatus.Text = !valid
                ? "●  GAME     Not found — choose the folder containing Data, Survival, and Release"
                : compatible
                    ? "●  GAME     Supported Steam build " + BuildInfo.GameBuild
                    : "●  GAME     Unsupported build — no files will be modified";
            gameStatus.ForeColor = compatible ? Success : Danger;

            string runtime = GameLocator.ActiveOpenXrRuntime();
            bool runtimeReady = !String.IsNullOrWhiteSpace(runtime) && File.Exists(runtime);
            openXrStatus.Text = runtimeReady
                ? "●  OPENXR   " + runtime
                : "●  OPENXR   No active 64-bit runtime — select Meta Quest Link in Meta Horizon settings";
            openXrStatus.ForeColor = runtimeReady ? Success : Warning;

            bool installed = compatible && GameLocator.LooksInstalled(root);
            bool managedInstall = compatible && GameLocator.HasManagedInstall(root);
            modStatus.Text = installed && managedInstall
                ? "●  VR MOD   Unconfirmed feature candidate installed — " + BuildInfo.ManagedFileCount + " managed files"
                : installed
                    ? "●  VR MOD   Matching manual feature candidate found — Install will adopt it safely"
                : managedInstall
                    ? "●  VR MOD   Installation is incomplete or modified — Restore & Reinstall is available"
                    : "●  VR MOD   Not installed";
            modStatus.ForeColor = installed && managedInstall ? Success : installed || managedInstall ? Warning : MutedText;

            installButton.Text = managedInstall && !installed ? "RESTORE & REINSTALL" : "INSTALL VR MOD";
            installButton.Enabled = compatible && (!installed || !managedInstall);
            startButton.Enabled = installed;
            verifyButton.Enabled = compatible;
            repairButton.Text = managedInstall ? "FORCE RESET / REINSTALL" : "REPAIR / CLEAN OLD VR FILES";
            repairButton.Enabled = compatible;
            uninstallButton.Enabled = managedInstall;
            logsButton.Enabled = valid;
        }

        private void BrowseClicked(object sender, EventArgs e)
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = "Select the Scrap Mechanic folder containing Data, Survival, and Release.";
                dialog.SelectedPath = Directory.Exists(gamePath.Text) ? gamePath.Text : String.Empty;
                dialog.ShowNewFolderButton = false;
                if (dialog.ShowDialog(this) == DialogResult.OK)
                    gamePath.Text = dialog.SelectedPath;
            }
        }

        private void InstallClicked(object sender, EventArgs e)
        {
            string root = gamePath.Text.Trim();
            if (MessageBox.Show(this,
                "Install the Chapter 2 VR beta into:\n\n" + root +
                "\n\nAny existing managed installation will be restored first. Modified managed files are preserved under LocalAppData before originals are restored. ScrapMechanic.exe and saves are never modified.",
                "Install Scrap Mechanic VR — Chapter 2", MessageBoxButtons.OKCancel, MessageBoxIcon.Information) != DialogResult.OK)
                return;

            try
            {
                SetBusy(true);
                Append("Validating the embedded payload and requesting installation privileges...");
                CommandResult result = PatcherRunner.RunVisible("Install", root, true);
                Append(result.Output);
                if (result.ExitCode != 0)
                    throw new InvalidOperationException(PatcherRunner.FailureMessage("Installation", result));
                GameLocator.Save(root);
                Shortcuts.Install(root);
                Append("Installation completed and desktop/Start Menu launchers were created.");
                MessageBox.Show(this,
                    "The Chapter 2 VR beta is installed.\n\nStart Meta Quest Link, connect the headset, then use the new 'Start Scrap Mechanic VR - Chapter 2' shortcut.",
                    "Installation complete", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Win32Exception ex)
            {
                Append("Installation was cancelled or elevation failed: " + ex.Message);
            }
            catch (Exception ex)
            {
                Append("INSTALL FAILED: " + ex.Message);
                MessageBox.Show(this, ex.Message, "Installation failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                RefreshState();
                SetBusy(false);
            }
        }

        private void StartClicked(object sender, EventArgs e)
        {
            try
            {
                string root = gamePath.Text.Trim();
                GameLocator.Save(root);
                Append("Starting Scrap Mechanic Chapter 2 through Steam with the active OpenXR runtime...");
                PatcherRunner.StartDetached(root);
            }
            catch (Exception ex)
            {
                Append("START FAILED: " + ex.Message);
                MessageBox.Show(this, ex.Message, "VR launch failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void VerifyClicked(object sender, EventArgs e)
        {
            try
            {
                SetBusy(true);
                Append("Running full payload and installed-file verification...");
                CommandResult result = PatcherRunner.RunCaptured("Verify", gamePath.Text.Trim());
                Append(result.Output);
                if (result.ExitCode == 0)
                    MessageBox.Show(this, "All installed VR files match the manifest.", "Verification passed", MessageBoxButtons.OK, MessageBoxIcon.Information);
                else
                    MessageBox.Show(this, "Verification did not pass. Review the detailed output in the manager window.", "Verification failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
            catch (Exception ex)
            {
                Append("VERIFY FAILED: " + ex.Message);
            }
            finally
            {
                RefreshState();
                SetBusy(false);
            }
        }

        private void RepairClicked(object sender, EventArgs e)
        {
            string root = gamePath.Text.Trim();
            bool managedInstall = GameLocator.HasManagedInstall(root);
            if (managedInstall)
            {
                if (MessageBox.Show(this,
                    "Force Reset / Reinstall will restore every recognized current or legacy VR file, preserve unknown old state records without touching their files, and then install this version.\n\n" +
                    "Game directory:\n" + root +
                    "\n\nThe supported game-build check and original-file hash checks remain enforced. Continue?",
                    "Force Reset / Reinstall", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
                    return;

                try
                {
                    SetBusy(true);
                    Append("Requesting Force Reset / Reinstall privileges and migrating legacy install state...");
                    CommandResult result = PatcherRunner.RunVisible("ForceInstall", root, true);
                    Append(result.Output);
                    if (result.ExitCode != 0)
                    {
                        if (result.Output.IndexOf("STEAM_REPAIR_REQUIRED", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            ProcessStartInfo steam = new ProcessStartInfo();
                            steam.FileName = "steam://validate/387990";
                            steam.UseShellExecute = true;
                            Process.Start(steam);
                        }
                        throw new InvalidOperationException(PatcherRunner.FailureMessage("Force Reset / Reinstall", result));
                    }
                    GameLocator.Save(root);
                    Shortcuts.Install(root);
                    Append("Force Reset / Reinstall completed and launch shortcuts were refreshed.");
                    MessageBox.Show(this,
                        "The stale installation state was migrated and Scrap Mechanic VR was reinstalled successfully.",
                        "Force reinstall complete", MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
                catch (Win32Exception ex)
                {
                    Append("Force Reset / Reinstall was cancelled or elevation failed: " + ex.Message);
                }
                catch (Exception ex)
                {
                    Append("FORCE RESET / REINSTALL FAILED: " + ex.Message);
                    MessageBox.Show(this, ex.Message, "Force reinstall failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
                finally
                {
                    RefreshState();
                    SetBusy(false);
                }
                return;
            }

            if (MessageBox.Show(this,
                    "Back up and remove stale files only from the current VR-managed paths and three explicitly recognized legacy VR paths in:\n\n" + root +
                    "\n\nModified original files will be removed so Steam can download clean copies. Existing ReShade files at these exact VR paths may also be removed, but every removed file is quarantined under LocalAppData.\n\nContinue?",
                "Repair old or conflicting VR files", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
                return;

            try
            {
                SetBusy(true);
                Append("Requesting repair privileges and quarantining stale VR-managed files...");
                CommandResult result = PatcherRunner.RunVisible("Repair", root, true);
                Append(result.Output);
                if (result.ExitCode != 0)
                    throw new InvalidOperationException(PatcherRunner.FailureMessage("Repair", result));
                Shortcuts.Remove();
                Append("Repair cleanup completed. Opening Steam file verification for Scrap Mechanic...");
                ProcessStartInfo steam = new ProcessStartInfo();
                steam.FileName = "steam://validate/387990";
                steam.UseShellExecute = true;
                Process.Start(steam);
                MessageBox.Show(this,
                    "Steam verification has been opened. Wait until Steam finishes restoring Scrap Mechanic, then return here and click Install VR Mod.",
                    "Repair cleanup complete", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Win32Exception ex)
            {
                Append("Repair was cancelled or elevation/Steam launch failed: " + ex.Message);
            }
            catch (Exception ex)
            {
                Append("REPAIR FAILED: " + ex.Message);
                MessageBox.Show(this, ex.Message, "Repair failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                RefreshState();
                SetBusy(false);
            }
        }

        private void UninstallClicked(object sender, EventArgs e)
        {
            string root = gamePath.Text.Trim();
            if (MessageBox.Show(this,
                "Restore every patcher-managed game file to its exact pre-install state?\n\nSave files are not touched.",
                "Uninstall Scrap Mechanic VR", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
                return;
            try
            {
                SetBusy(true);
                Append("Requesting restore privileges and verifying backups...");
                CommandResult result = PatcherRunner.RunVisible("Uninstall", root, true);
                Append(result.Output);
                if (result.ExitCode != 0)
                    throw new InvalidOperationException(PatcherRunner.FailureMessage("Uninstall / restore", result));
                Shortcuts.Remove();
                Append("Uninstall completed; game originals were hash-verified after restore.");
                MessageBox.Show(this, "The VR mod was removed and original files were restored.", "Restore complete", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Win32Exception ex)
            {
                Append("Uninstall was cancelled or elevation failed: " + ex.Message);
            }
            catch (Exception ex)
            {
                Append("UNINSTALL FAILED: " + ex.Message);
                MessageBox.Show(this, ex.Message, "Uninstall failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                RefreshState();
                SetBusy(false);
            }
        }

        private void LogsClicked(object sender, EventArgs e)
        {
            string actionLogs = Path.Combine(PackageManager.StateRoot, "logs");
            if (Directory.Exists(actionLogs))
            {
                Process.Start("explorer.exe", QuoteForExplorer(actionLogs));
                return;
            }
            string release = Path.Combine(gamePath.Text.Trim(), "Release");
            if (Directory.Exists(release))
                Process.Start("explorer.exe", QuoteForExplorer(release));
        }

        private static string QuoteForExplorer(string path)
        {
            return "\"" + path.Replace("\"", "") + "\"";
        }
    }

    internal static class Program
    {
        private static void RunSelfTest()
        {
            PackageManager.EnsureExtracted();
            string root = GameLocator.Find();
            if (!GameLocator.IsGameRoot(root))
                throw new InvalidOperationException("Scrap Mechanic was not discovered in any registered Steam library.");
            string hash = GameLocator.GameHash(root);
            if (!String.Equals(hash, BuildInfo.GameExeHash, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("The discovered game executable is not supported: " + hash);
            string runtime = GameLocator.ActiveOpenXrRuntime();
            if (String.IsNullOrWhiteSpace(runtime) || !File.Exists(runtime))
                throw new InvalidOperationException("No active 64-bit OpenXR runtime was found.");

            Directory.CreateDirectory(PackageManager.StateRoot);
            File.WriteAllText(Path.Combine(PackageManager.StateRoot, "self-test.log"),
                "PASS\r\nGame=" + root + "\r\nBuild=" + BuildInfo.GameBuild +
                "\r\nOpenXR=" + runtime + "\r\nPackage=" + PackageManager.PackageDirectory + "\r\n",
                new UTF8Encoding(false));
        }

        [STAThread]
        private static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            bool selfTest = args.Length > 0 && String.Equals(args[0], "--self-test", StringComparison.OrdinalIgnoreCase);
            try
            {
                if (selfTest)
                {
                    RunSelfTest();
                    Environment.ExitCode = 0;
                    return;
                }
                PackageManager.EnsureExtracted();
                if (args.Length > 0 && String.Equals(args[0], "--start", StringComparison.OrdinalIgnoreCase))
                {
                    string root = GameLocator.Find();
                    if (!GameLocator.IsGameRoot(root) || !GameLocator.LooksInstalled(root))
                        throw new InvalidOperationException("The managed Chapter 2 VR installation could not be found. Run this manager and reinstall or verify it.");
                    PatcherRunner.StartDetached(root);
                    return;
                }
                Application.Run(new MainForm());
            }
            catch (Exception ex)
            {
                if (selfTest)
                {
                    try
                    {
                        Directory.CreateDirectory(PackageManager.StateRoot);
                        File.WriteAllText(Path.Combine(PackageManager.StateRoot, "self-test.log"),
                            "FAIL\r\n" + ex + "\r\n", new UTF8Encoding(false));
                    }
                    catch { }
                    Environment.ExitCode = 1;
                    return;
                }
                MessageBox.Show(ex.Message, "Scrap Mechanic VR — Chapter 2", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }
}
