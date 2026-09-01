using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using Microsoft.Win32;

[assembly: AssemblyTitle("Scrap Mechanic VR Chapter 2 Installer")]
[assembly: AssemblyDescription("Installer, verifier, repair manager, and restorer for Scrap Mechanic Chapter 2 VR")]
[assembly: AssemblyCompany("Scrap Mechanic VR Community Project")]
[assembly: AssemblyProduct("Scrap Mechanic VR Chapter 2")]
[assembly: AssemblyVersion("1.3.3.0")]
[assembly: AssemblyFileVersion("1.3.3.0")]

namespace ScrapMechanicVRPatcher
{
    internal static class BuildInfo
    {
        internal const string Version = "1.3.3-chapter2-20260901";
        internal const string GameBuild = "24529696";
        internal const string GameExeHash = "5D663BA2EC5DC8C7ABEFCC5C9344AE86F0A066C4069A91F54833524AC9A5B4F5";
        internal const string AddonHash = "1292B0C0FF8845C2AC297C7435D6CD189D0A6B952131E0E5136B1A80CC28197C";
        internal const string DxgiHash = "EC9245D05C11751F2AC0D2256E6921AD8FB36BE9172EF6D587856591EB729A25";
        internal const string LoaderHash = "018C6519AFBDEADE6DA9E7D59C406068DD58674D87A65AE27353484A05E6674A";
        internal const string MusicHash = "02E8E98721A899C2731ED8AFDF6378DB98DC09BB87FA5896FBA911CE5D875660";
        internal const string LogoHash = "C692A16C8CB01B94618951C09F64A156D7DD6A71D349B91E023B018165504C34";
        internal const string ManifestHash = "ADDBA6CE30B00C11DDC0FDD02EEB5484B1B81F9A3C1A11BB6C185C1281C28E4B";
        internal const string PatcherHash = "7DA69B2FB835E7166C2E58A4FD4CF4D758BC8BF56EA3D3BEE212CB0A3A2CD911";
        internal const int ManagedFileCount = 47;
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

        internal static string MusicPath
        {
            get { return Path.Combine(PackageDirectory, "media", "BOOMBOX MIX NEW.mp3"); }
        }

        internal static string LogoPath
        {
            get { return Path.Combine(PackageDirectory, "media", "ScrapMechanicVR-Logo.png"); }
        }

        internal static string OpenXrLoaderPath
        {
            get { return Path.Combine(PackageDirectory, "payload", "Release", "libopenxr_loader.dll"); }
        }

        private static bool PackageIsValid()
        {
            string manifest = Path.Combine(PackageDirectory, "manifest.json");
            string addon = Path.Combine(PackageDirectory, "payload", "Release", "smvr_native_vr_v1.addon64");
            return File.Exists(PatcherPath) && File.Exists(manifest) && File.Exists(addon) &&
                   File.Exists(MusicPath) && File.Exists(LogoPath) &&
                   String.Equals(Hashing.Sha256(PatcherPath), BuildInfo.PatcherHash, StringComparison.OrdinalIgnoreCase) &&
                   String.Equals(Hashing.Sha256(manifest), BuildInfo.ManifestHash, StringComparison.OrdinalIgnoreCase) &&
                   String.Equals(Hashing.Sha256(addon), BuildInfo.AddonHash, StringComparison.OrdinalIgnoreCase) &&
                   String.Equals(Hashing.Sha256(MusicPath), BuildInfo.MusicHash, StringComparison.OrdinalIgnoreCase) &&
                   String.Equals(Hashing.Sha256(LogoPath), BuildInfo.LogoHash, StringComparison.OrdinalIgnoreCase);
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
                string stagedMusic = Path.Combine(temporaryDirectory, "media", "BOOMBOX MIX NEW.mp3");
                string stagedLogo = Path.Combine(temporaryDirectory, "media", "ScrapMechanicVR-Logo.png");
                if (!File.Exists(stagedPatcher) ||
                    !File.Exists(stagedManifest) ||
                    !File.Exists(stagedAddon) ||
                    !File.Exists(stagedMusic) ||
                    !File.Exists(stagedLogo) ||
                    !String.Equals(Hashing.Sha256(stagedPatcher), BuildInfo.PatcherHash, StringComparison.OrdinalIgnoreCase) ||
                    !String.Equals(Hashing.Sha256(stagedManifest), BuildInfo.ManifestHash, StringComparison.OrdinalIgnoreCase) ||
                    !String.Equals(Hashing.Sha256(stagedAddon), BuildInfo.AddonHash, StringComparison.OrdinalIgnoreCase) ||
                    !String.Equals(Hashing.Sha256(stagedMusic), BuildInfo.MusicHash, StringComparison.OrdinalIgnoreCase) ||
                    !String.Equals(Hashing.Sha256(stagedLogo), BuildInfo.LogoHash, StringComparison.OrdinalIgnoreCase))
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
            return File.Exists(ManagedStatePath(root));
        }

        internal static string ManagedStatePath(string root)
        {
            try
            {
                string normalized = Path.GetFullPath(root).TrimEnd(
                    Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar).ToLowerInvariant();
                string key = Hashing.Sha256Text(normalized).Substring(0, 16);
                return Path.Combine(PackageManager.StateRoot, "install-state-" + key + ".json");
            }
            catch { return String.Empty; }
        }

        internal static string ManagedVersion(string root)
        {
            try
            {
                string statePath = ManagedStatePath(root);
                if (String.IsNullOrWhiteSpace(statePath) || !File.Exists(statePath))
                    return String.Empty;
                Match match = Regex.Match(File.ReadAllText(statePath),
                    "\\\"patchVersion\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"", RegexOptions.IgnoreCase);
                return match.Success ? match.Groups[1].Value : "unknown managed version";
            }
            catch { return "unknown managed version"; }
        }

        internal static bool HasVrTraces(string root)
        {
            if (!IsGameRoot(root))
                return false;
            string[] paths = new string[] {
                @"Release\smvr_native_vr_v1.addon64",
                @"Release\ScrapMechanicVR.ini",
                @"Release\ScrapMechanicVR-StartupMenu.png",
                @"Release\ScrapMechanicVR-HeldItems.bin",
                @"Release\scrap_native_vr.addon64",
                @"Release\openxr_loader.dll",
                @"NativeVR\Start-NativeVR.ps1"
            };
            foreach (string relative in paths)
                if (File.Exists(Path.Combine(root, relative)))
                    return true;
            return HasManagedInstall(root);
        }

        internal static void Save(string root)
        {
            Directory.CreateDirectory(PackageManager.StateRoot);
            File.WriteAllText(SavedGamePath, root, new UTF8Encoding(false));
        }
    }

    internal static class OpenXrProbe
    {
        private const int XrTypeInstanceCreateInfo = 3;
        private const int XrTypeSystemGetInfo = 4;
        private const int XrFormFactorHeadMountedDisplay = 1;
        private const int XrErrorFormFactorUnavailable = -35;
        private const ulong XrApiVersion10 = 0x0001000000000000UL;
        private const uint LoadLibrarySearchDllLoadDir = 0x00000100;
        private const uint LoadLibrarySearchDefaultDirs = 0x00001000;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        private struct XrApplicationInfo
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
            internal string applicationName;
            internal uint applicationVersion;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
            internal string engineName;
            internal uint engineVersion;
            internal ulong apiVersion;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct XrInstanceCreateInfo
        {
            internal int type;
            internal IntPtr next;
            internal ulong createFlags;
            internal XrApplicationInfo applicationInfo;
            internal uint enabledApiLayerCount;
            internal IntPtr enabledApiLayerNames;
            internal uint enabledExtensionCount;
            internal IntPtr enabledExtensionNames;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct XrSystemGetInfo
        {
            internal int type;
            internal IntPtr next;
            internal int formFactor;
        }

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int XrCreateInstanceDelegate(ref XrInstanceCreateInfo createInfo, out IntPtr instance);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int XrGetSystemDelegate(IntPtr instance, ref XrSystemGetInfo getInfo, out ulong systemId);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int XrDestroyInstanceDelegate(IntPtr instance);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibrary(string path);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetDllDirectory(string path);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);

        [DllImport("kernel32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FreeLibrary(IntPtr module);

        private static T LoadFunction<T>(IntPtr module, string name) where T : class
        {
            IntPtr address = GetProcAddress(module, name);
            if (address == IntPtr.Zero)
                throw new InvalidOperationException("The OpenXR loader does not export " + name + ".");
            return (T)(object)Marshal.GetDelegateForFunctionPointer(address, typeof(T));
        }

        internal static bool HeadsetAvailable(string loaderPath, out string detail)
        {
            detail = String.Empty;
            if (String.IsNullOrWhiteSpace(loaderPath) || !File.Exists(loaderPath))
            {
                detail = "OpenXR loader is missing";
                return false;
            }

            // The packaged Khronos loader depends on libc++.dll and
            // libunwind.dll beside it. LoadLibrary(fullPath) does not reliably
            // search that directory for transitive dependencies when the
            // installer is launched from Downloads without development tools
            // on PATH. Use a
            // scoped DLL-load directory so the probe behaves exactly like the
            // installed game and never depends on the user's PATH.
            IntPtr module = LoadLibraryEx(loaderPath, IntPtr.Zero,
                LoadLibrarySearchDllLoadDir | LoadLibrarySearchDefaultDirs);
            if (module == IntPtr.Zero && Marshal.GetLastWin32Error() == 87)
            {
                // Compatibility fallback for Windows installations that do not
                // support the LOAD_LIBRARY_SEARCH_* flags. The headset probe is
                // an isolated worker process, so this temporary process-wide
                // search directory cannot affect the installer UI.
                string loaderDirectory = Path.GetDirectoryName(loaderPath);
                if (SetDllDirectory(loaderDirectory))
                {
                    try { module = LoadLibrary(loaderPath); }
                    finally { SetDllDirectory(null); }
                }
            }
            if (module == IntPtr.Zero)
            {
                detail = "OpenXR loader could not be opened (Windows error " + Marshal.GetLastWin32Error() + ")";
                return false;
            }

            IntPtr instance = IntPtr.Zero;
            try
            {
                XrCreateInstanceDelegate createInstance = LoadFunction<XrCreateInstanceDelegate>(module, "xrCreateInstance");
                XrGetSystemDelegate getSystem = LoadFunction<XrGetSystemDelegate>(module, "xrGetSystem");
                XrInstanceCreateInfo createInfo = new XrInstanceCreateInfo();
                createInfo.type = XrTypeInstanceCreateInfo;
                createInfo.applicationInfo = new XrApplicationInfo {
                    applicationName = "Scrap Mechanic VR Installer",
                    applicationVersion = 1,
                    engineName = "Scrap Mechanic VR",
                    engineVersion = 1,
                    apiVersion = XrApiVersion10
                };
                int createResult = createInstance(ref createInfo, out instance);
                if (createResult < 0 || instance == IntPtr.Zero)
                {
                    detail = "OpenXR runtime is not ready (result " + createResult + ")";
                    return false;
                }

                XrSystemGetInfo systemInfo = new XrSystemGetInfo();
                systemInfo.type = XrTypeSystemGetInfo;
                systemInfo.formFactor = XrFormFactorHeadMountedDisplay;
                ulong systemId;
                int systemResult = getSystem(instance, ref systemInfo, out systemId);
                if (systemResult >= 0 && systemId != 0)
                {
                    detail = "headset connected";
                    return true;
                }
                detail = systemResult == XrErrorFormFactorUnavailable
                    ? "runtime ready, but no headset is connected"
                    : "headset check failed (OpenXR result " + systemResult + ")";
                return false;
            }
            catch (Exception ex)
            {
                detail = ex.Message;
                return false;
            }
            finally
            {
                if (instance != IntPtr.Zero)
                {
                    try
                    {
                        XrDestroyInstanceDelegate destroyInstance = LoadFunction<XrDestroyInstanceDelegate>(module, "xrDestroyInstance");
                        destroyInstance(instance);
                    }
                    catch { }
                }
                FreeLibrary(module);
            }
        }

        internal static bool HeadsetAvailableWithTimeout(out string detail)
        {
            string diagnosticRoot = Path.Combine(PackageManager.StateRoot, "diagnostics");
            Directory.CreateDirectory(diagnosticRoot);
            string resultPath = Path.Combine(diagnosticRoot, "headset-probe-" + Guid.NewGuid().ToString("N") + ".txt");
            try
            {
                ProcessStartInfo info = new ProcessStartInfo();
                info.FileName = Assembly.GetExecutingAssembly().Location;
                info.Arguments = "--headset-probe-worker \"" + resultPath.Replace("\"", String.Empty) + "\"";
                info.UseShellExecute = false;
                info.CreateNoWindow = true;
                using (Process worker = Process.Start(info))
                {
                    if (!worker.WaitForExit(5000))
                    {
                        try { worker.Kill(); } catch { }
                        try { worker.WaitForExit(1000); } catch { }
                        detail = "OpenXR did not answer within five seconds; connect and wake the headset";
                        return false;
                    }
                }

                if (!File.Exists(resultPath))
                {
                    detail = "OpenXR headset probe returned no result";
                    return false;
                }
                string[] lines = File.ReadAllLines(resultPath);
                detail = lines.Length > 1 ? lines[1] : "OpenXR headset probe returned an incomplete result";
                return lines.Length > 0 && lines[0] == "CONNECTED";
            }
            catch (Exception ex)
            {
                detail = "OpenXR headset probe failed: " + ex.Message;
                return false;
            }
            finally
            {
                try { if (File.Exists(resultPath)) File.Delete(resultPath); } catch { }
            }
        }

        internal static bool RunWorker(string resultPath)
        {
            string diagnosticRoot = Path.GetFullPath(Path.Combine(PackageManager.StateRoot, "diagnostics"))
                .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
            string fullResultPath = Path.GetFullPath(resultPath);
            if (!fullResultPath.StartsWith(diagnosticRoot, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Unsafe OpenXR probe result path.");
            Directory.CreateDirectory(diagnosticRoot);
            string detail;
            bool available = HeadsetAvailable(PackageManager.OpenXrLoaderPath, out detail);
            File.WriteAllText(fullResultPath,
                (available ? "CONNECTED" : "UNAVAILABLE") + Environment.NewLine + detail,
                new UTF8Encoding(false));
            return available;
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
        private static string WindowsPowerShellBootstrap()
        {
            // Do not inherit a PowerShell 7-only PSModulePath when the manager
            // was launched by a developer shell.  Windows PowerShell needs its
            // own inbox modules for commands such as Get-FileHash.
            return "$env:PSModulePath=$env:WINDIR+'\\System32\\WindowsPowerShell\\v1.0\\Modules;'+" +
                   "$env:ProgramFiles+'\\WindowsPowerShell\\Modules'; ";
        }

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
                "$ErrorActionPreference='Stop'; " + WindowsPowerShellBootstrap() +
                "$log=" + PowerShellLiteral(logPath) + "; try { & " +
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
            string logDirectory = Path.Combine(PackageManager.StateRoot, "logs");
            Directory.CreateDirectory(logDirectory);
            string logPath = Path.Combine(logDirectory,
                DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + action.ToLowerInvariant() + "-captured.log");
            ProcessStartInfo info = new ProcessStartInfo();
            info.FileName = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
                @"WindowsPowerShell\v1.0\powershell.exe");
            string command = "$ErrorActionPreference='Stop'; " + WindowsPowerShellBootstrap() +
                "$log=" + PowerShellLiteral(logPath) + "; try { & " +
                PowerShellLiteral(PackageManager.PatcherPath) + " -Action " +
                PowerShellLiteral(action) + " -GamePath " + PowerShellLiteral(gameRoot) +
                " *>&1 | Out-File -LiteralPath $log -Encoding Unicode; exit 0 } catch { " +
                "$errorLine=('ERROR: ' + $_.Exception.Message); " +
                "$detail=($_ | Format-List * -Force | Out-String); " +
                "$errorLine | Out-File -LiteralPath $log -Append -Encoding Unicode; " +
                "$detail | Out-File -LiteralPath $log -Append -Encoding Unicode; exit 1 }";
            string encoded = Convert.ToBase64String(Encoding.Unicode.GetBytes(command));
            info.Arguments = "-NoProfile -ExecutionPolicy Bypass -EncodedCommand " + encoded;
            info.WorkingDirectory = PackageManager.PackageDirectory;
            info.UseShellExecute = false;
            info.CreateNoWindow = true;
            using (Process process = Process.Start(info))
            {
                process.WaitForExit();
                string output = File.Exists(logPath) ? File.ReadAllText(logPath) :
                    "No captured action log was produced.";
                return new CommandResult {
                    ExitCode = process.ExitCode,
                    Output = output,
                    LogPath = logPath
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
                "$ErrorActionPreference='Stop'; " + WindowsPowerShellBootstrap() +
                "try { & " + PowerShellLiteral(PackageManager.PatcherPath) +
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
            CreateShortcut(Path.Combine(StartMenuDirectory, "Manage or Uninstall Scrap Mechanic VR - Chapter 2.lnk"), ManagerExecutable, "", "Install, start, or uninstall Scrap Mechanic VR Chapter 2", gameIcon);
        }

        internal static void Remove()
        {
            try { if (File.Exists(DesktopShortcut)) File.Delete(DesktopShortcut); } catch { }
            try { if (Directory.Exists(StartMenuDirectory)) Directory.Delete(StartMenuDirectory, true); } catch { }
        }
    }

    internal static class InstallerMusic
    {
        private const string Alias = "ScrapMechanicVRInstallerMusic";

        [DllImport("winmm.dll", CharSet = CharSet.Unicode)]
        private static extern int mciSendString(
            string command, StringBuilder returnValue, int returnLength, IntPtr callback);

        internal static bool Start(string path, int volumePercent)
        {
            Close();
            if (!File.Exists(path))
                return false;

            string safePath = path.Replace("\"", String.Empty);
            int result = mciSendString(
                "open \"" + safePath + "\" type mpegvideo alias " + Alias,
                null, 0, IntPtr.Zero);
            if (result != 0)
                return false;

            SetVolume(volumePercent);
            result = mciSendString("play " + Alias + " repeat", null, 0, IntPtr.Zero);
            if (result == 0)
                return true;

            Close();
            return false;
        }

        internal static void SetVolume(int volumePercent)
        {
            int safePercent = Math.Max(0, Math.Min(100, volumePercent));
            mciSendString(
                "setaudio " + Alias + " volume to " + (safePercent * 10),
                null, 0, IntPtr.Zero);
        }

        internal static void Close()
        {
            mciSendString("close " + Alias, null, 0, IntPtr.Zero);
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
        private readonly Button uninstallButton;
        private readonly Button logsButton;
        private readonly RichTextBox details;
        private readonly TrackBar musicVolume;
        private readonly Label musicVolumeLabel;
        private bool automaticVerificationRunning;
        private bool? lastVerificationPassed;
        private string lastVerificationRoot = String.Empty;
        private bool? lastHeadsetReady;
        private string lastHeadsetDetail = String.Empty;

        internal MainForm()
        {
            Text = "Scrap Mechanic VR Chapter 2";
            ClientSize = new Size(900, 700);
            MinimumSize = new Size(916, 739);
            StartPosition = FormStartPosition.CenterScreen;
            Font = new Font("Segoe UI", 9F);
            BackColor = AppBackground;
            ForeColor = PrimaryText;
            DoubleBuffered = true;
            ShowIcon = false;

            Panel accentBar = new Panel();
            accentBar.BackColor = Accent;
            accentBar.Dock = DockStyle.Top;
            accentBar.Height = 5;
            Controls.Add(accentBar);

            PictureBox brand = new PictureBox();
            brand.BackColor = Color.Transparent;
            brand.Location = new Point(18, 11);
            brand.Size = new Size(122, 82);
            brand.SizeMode = PictureBoxSizeMode.Zoom;
            using (FileStream logoStream = new FileStream(PackageManager.LogoPath, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (Image logoSource = Image.FromStream(logoStream))
                brand.Image = new Bitmap(logoSource);
            Controls.Add(brand);

            Label title = new Label();
            title.Text = "Scrap Mechanic Native VR — Chapter 2";
            title.Font = new Font("Segoe UI Semibold", 21F, FontStyle.Bold);
            title.ForeColor = PrimaryText;
            title.AutoSize = true;
            title.Location = new Point(148, 19);
            Controls.Add(title);

            Label subtitle = new Label();
            subtitle.Text = "CHAPTER 2  /  OPENXR  /  QUEST + VALVE INDEX";
            subtitle.AutoSize = true;
            subtitle.Font = new Font("Segoe UI Semibold", 8.5F, FontStyle.Bold);
            subtitle.ForeColor = MutedText;
            subtitle.Location = new Point(151, 61);
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

            musicVolumeLabel = new Label();
            musicVolumeLabel.Text = "MUSIC 50%";
            musicVolumeLabel.TextAlign = ContentAlignment.MiddleRight;
            musicVolumeLabel.Font = new Font("Segoe UI Semibold", 8F, FontStyle.Bold);
            musicVolumeLabel.ForeColor = MutedText;
            musicVolumeLabel.Location = new Point(620, 68);
            musicVolumeLabel.Size = new Size(84, 24);
            musicVolumeLabel.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            Controls.Add(musicVolumeLabel);

            musicVolume = new TrackBar();
            musicVolume.Minimum = 0;
            musicVolume.Maximum = 100;
            musicVolume.Value = 50;
            musicVolume.SmallChange = 5;
            musicVolume.LargeChange = 10;
            musicVolume.TickStyle = TickStyle.None;
            musicVolume.BackColor = AppBackground;
            musicVolume.Location = new Point(710, 66);
            musicVolume.Size = new Size(166, 32);
            musicVolume.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            musicVolume.Scroll += delegate
            {
                musicVolumeLabel.Text = "MUSIC " + musicVolume.Value + "%";
                InstallerMusic.SetVolume(musicVolume.Value);
            };
            Controls.Add(musicVolume);

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

            installButton = CreateActionButton(actionsCard, "INSTALL VR MOD", 18, 17, 190, ButtonKind.Primary);
            installButton.Click += InstallClicked;

            uninstallButton = CreateActionButton(actionsCard, "UNINSTALL VR MOD", 218, 17, 190, ButtonKind.Danger);
            uninstallButton.Click += UninstallClicked;

            startButton = CreateActionButton(actionsCard, "START VR", 418, 17, 170, ButtonKind.Success);
            startButton.Click += StartClicked;

            logsButton = CreateActionButton(actionsCard, "OPEN LOGS", 598, 17, 234, ButtonKind.Neutral);
            logsButton.Click += LogsClicked;

            Label safety = new Label();
            safety.Text = "GUARDED INSTALL  •  AUTOMATIC VERIFICATION  •  GAME EXE & SAVES UNTOUCHED";
            safety.AutoSize = false;
            safety.TextAlign = ContentAlignment.MiddleRight;
            safety.Font = new Font("Segoe UI Semibold", 8F, FontStyle.Bold);
            safety.ForeColor = MutedText;
            safety.Location = new Point(18, 69);
            safety.Size = new Size(814, 32);
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
            details.Text = "[READY] Inspecting the game, installed VR version, and OpenXR runtime.\n";
            Controls.Add(details);

            gamePath.Text = GameLocator.Find();
            gamePath.TextChanged += delegate { RefreshState(); };
            InstallerMusic.Start(PackageManager.MusicPath, musicVolume.Value);
            FormClosed += delegate
            {
                InstallerMusic.Close();
                if (brand.Image != null) brand.Image.Dispose();
            };
            Shown += delegate
            {
                RefreshState();
                if (startButton.Enabled) startButton.Focus();
                else if (installButton.Enabled) installButton.Focus();
                BeginInvoke((MethodInvoker)delegate { AutomaticStartupVerification(); });
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
            uninstallButton.Enabled = !busy && uninstallButton.Enabled;
            logsButton.Enabled = !busy && logsButton.Enabled;
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
            openXrStatus.Text = !runtimeReady
                ? "●  OPENXR   No active 64-bit runtime"
                : lastHeadsetReady == true
                    ? "●  OPENXR   Headset connected — " + runtime
                    : lastHeadsetReady == false
                        ? "●  OPENXR   " + lastHeadsetDetail
                        : "●  OPENXR   Runtime ready — Start VR checks the headset connection";
            openXrStatus.ForeColor = lastHeadsetReady == true ? Success : Warning;

            bool installed = compatible && GameLocator.LooksInstalled(root);
            bool managedInstall = compatible && GameLocator.HasManagedInstall(root);
            bool traces = compatible && GameLocator.HasVrTraces(root);
            string managedVersion = managedInstall ? GameLocator.ManagedVersion(root) : String.Empty;
            bool verificationForRoot = String.Equals(lastVerificationRoot, root, StringComparison.OrdinalIgnoreCase);
            if (installed && managedInstall)
            {
                modStatus.Text = verificationForRoot && lastVerificationPassed == true
                    ? "●  VR MOD   " + managedVersion + " installed — automatic verification passed"
                    : verificationForRoot && lastVerificationPassed == false
                        ? "●  VR MOD   Managed files need attention — Install will replace them safely"
                        : "●  VR MOD   " + managedVersion + " installed — checking automatically";
            }
            else if (managedInstall)
                modStatus.Text = "●  VR MOD   Older, incomplete, or modified managed version detected: " + managedVersion;
            else if (traces)
                modStatus.Text = "●  VR MOD   Current or older unmanaged VR files detected";
            else
                modStatus.Text = "●  VR MOD   Not installed";
            modStatus.ForeColor = installed && managedInstall && verificationForRoot && lastVerificationPassed == true
                ? Success : traces || managedInstall ? Warning : MutedText;

            installButton.Text = "INSTALL VR MOD";
            installButton.Enabled = compatible;
            startButton.Enabled = installed;
            uninstallButton.Enabled = compatible && (managedInstall || traces);
            logsButton.Enabled = valid || Directory.Exists(Path.Combine(PackageManager.StateRoot, "logs"));
        }

        private void AutomaticStartupVerification()
        {
            if (automaticVerificationRunning)
                return;
            string root = gamePath.Text.Trim();
            if (!GameLocator.IsGameRoot(root) ||
                !String.Equals(GameLocator.GameHash(root), BuildInfo.GameExeHash, StringComparison.OrdinalIgnoreCase))
                return;

            bool currentFiles = GameLocator.LooksInstalled(root);
            bool managedInstall = GameLocator.HasManagedInstall(root);
            if (!currentFiles || !managedInstall)
            {
                if (managedInstall)
                    Append("Automatic startup inspection found managed version " + GameLocator.ManagedVersion(root) + ". Install will remove it with its verified backups before installing " + BuildInfo.Version + ".");
                else if (GameLocator.HasVrTraces(root))
                    Append("Automatic startup inspection found unmanaged current or older VR files. Install or Uninstall will explain the guarded cleanup before asking for approval.");
                else
                    Append("Automatic startup inspection passed: no VR mod is currently installed.");
                return;
            }

            try
            {
                automaticVerificationRunning = true;
                SetBusy(true);
                Append("Automatically verifying all " + BuildInfo.ManagedFileCount + " installed VR files...");
                CommandResult result = PatcherRunner.RunCaptured("Verify", root);
                lastVerificationRoot = root;
                lastVerificationPassed = result.ExitCode == 0;
                Append(result.Output);
                Append(result.ExitCode == 0
                    ? "Automatic startup verification passed."
                    : "Automatic startup verification found a problem. Install will restore the existing version and replace it safely after approval.");
            }
            catch (Exception ex)
            {
                lastVerificationRoot = root;
                lastVerificationPassed = false;
                Append("Automatic startup verification could not complete: " + ex.Message);
            }
            finally
            {
                automaticVerificationRunning = false;
                RefreshState();
                SetBusy(false);
            }
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
            bool managedInstall = GameLocator.HasManagedInstall(root);
            bool traces = GameLocator.HasVrTraces(root);
            string existing = managedInstall
                ? "Detected managed VR version: " + GameLocator.ManagedVersion(root) +
                  "\nIts verified state and backups will be used to restore and remove it first."
                : traces
                    ? "Detected current or older VR files without a managed restore state.\nRecognized files will be preserved before guarded cleanup; Steam verification may be required for original game files."
                    : "No existing VR mod was detected.";
            if (MessageBox.Show(this,
                existing + "\n\nAfter approval, the installer will:\n" +
                "1. Verify the supported game and all embedded files.\n" +
                "2. Remove any managed older/current VR version using its exact backups.\n" +
                "3. Back up required originals and install " + BuildInfo.ManagedFileCount + " files for " + BuildInfo.Version + ".\n" +
                "4. Automatically verify the completed installation and refresh the launch shortcuts.\n\n" +
                "Game directory:\n" + root + "\n\nScrapMechanic.exe and save files are never modified. Continue?",
                "Install Scrap Mechanic VR", MessageBoxButtons.YesNo, MessageBoxIcon.Information) != DialogResult.Yes)
            {
                Append("Install cancelled before any files were changed.");
                return;
            }

            try
            {
                SetBusy(true);
                Append("Install approved. Verifying the package, inspecting the existing version, and requesting installation privileges...");
                CommandResult result = PatcherRunner.RunVisible("Install", root, true);
                Append(result.Output);
                if (result.ExitCode != 0)
                {
                    if (result.Output.IndexOf("STEAM_REPAIR_REQUIRED", StringComparison.OrdinalIgnoreCase) >= 0)
                    {
                        OpenSteamVerification();
                        throw new InvalidOperationException("The older VR files were removed, but one or more original game files need Steam verification. Steam verification has been opened; let it finish, then click Install VR Mod again.\n\n" + PatcherRunner.FailureMessage("Installation", result));
                    }
                    throw new InvalidOperationException(PatcherRunner.FailureMessage("Installation", result));
                }
                GameLocator.Save(root);
                Shortcuts.Install(root);
                lastVerificationRoot = root;
                lastVerificationPassed = true;
                Append("Installation and automatic post-install verification passed. Desktop and Start Menu launchers were refreshed.");
                MessageBox.Show(this,
                    BuildInfo.Version + " is installed and all " + BuildInfo.ManagedFileCount + " managed files passed verification.\n\nStart your OpenXR runtime, connect the headset, then click Start VR. The game will not launch until OpenXR reports a connected headset.",
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

        private static void OpenSteamVerification()
        {
            ProcessStartInfo steam = new ProcessStartInfo();
            steam.FileName = "steam://validate/387990";
            steam.UseShellExecute = true;
            Process.Start(steam);
        }

        private void StartClicked(object sender, EventArgs e)
        {
            try
            {
                SetBusy(true);
                string root = gamePath.Text.Trim();
                string runtime = GameLocator.ActiveOpenXrRuntime();
                if (String.IsNullOrWhiteSpace(runtime) || !File.Exists(runtime))
                    throw new InvalidOperationException("No active 64-bit OpenXR runtime was found. Enable Meta Quest Link or SteamVR OpenXR, then try again.");
                Append("Checking whether the active OpenXR runtime reports a connected headset...");
                string headsetDetail;
                bool headsetReady = OpenXrProbe.HeadsetAvailableWithTimeout(out headsetDetail);
                lastHeadsetReady = headsetReady;
                lastHeadsetDetail = headsetDetail;
                if (!headsetReady)
                    throw new InvalidOperationException("Start VR was stopped because no OpenXR headset is ready: " + headsetDetail + ". Connect and wake the headset, then try again.");
                GameLocator.Save(root);
                Append("OpenXR reports a connected headset. Starting Scrap Mechanic Chapter 2 through Steam...");
                PatcherRunner.StartDetached(root);
            }
            catch (Exception ex)
            {
                Append("START FAILED: " + ex.Message);
                MessageBox.Show(this, ex.Message, "VR launch failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
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
            bool managedInstall = GameLocator.HasManagedInstall(root);
            string removalPlan = managedInstall
                ? "Detected managed VR version: " + GameLocator.ManagedVersion(root) +
                  "\nThe installer will validate its saved state and backups, preserve any user-modified managed files, restore original game files, remove mod-owned files and shortcuts, then verify the result."
                : "No managed restore state was found, but recognized current or older VR files are present.\nThe installer will preserve and remove those files. If original game files cannot be restored safely, Steam verification will open automatically.";
            if (MessageBox.Show(this,
                removalPlan + "\n\nGame directory:\n" + root +
                "\n\nScrapMechanic.exe and save files are never modified. Continue?",
                "Uninstall Scrap Mechanic VR", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
            {
                Append("Uninstall cancelled before any files were changed.");
                return;
            }
            try
            {
                SetBusy(true);
                Append("Uninstall approved. Inspecting the installed version, verifying restore data, and requesting privileges...");
                CommandResult result = PatcherRunner.RunVisible("Uninstall", root, true);
                Append(result.Output);
                if (result.ExitCode != 0)
                {
                    if (result.Output.IndexOf("STEAM_REPAIR_REQUIRED", StringComparison.OrdinalIgnoreCase) >= 0)
                    {
                        Shortcuts.Remove();
                        lastVerificationRoot = root;
                        lastVerificationPassed = null;
                        OpenSteamVerification();
                        Append("VR files were removed. Steam verification was opened to restore original game files that had no usable backup.");
                        MessageBox.Show(this,
                            "The VR mod files were removed. Steam verification has been opened to restore the remaining original Scrap Mechanic files. Let Steam finish before launching the game.",
                            "Steam verification required", MessageBoxButtons.OK, MessageBoxIcon.Information);
                        return;
                    }
                    throw new InvalidOperationException(PatcherRunner.FailureMessage("Uninstall / restore", result));
                }
                Shortcuts.Remove();
                lastVerificationRoot = root;
                lastVerificationPassed = null;
                Append("Uninstall and automatic post-uninstall verification passed. Original files were restored and no recognized VR payload remains.");
                MessageBox.Show(this, "The VR mod was removed. Original files were restored and verified automatically.", "Uninstall complete", MessageBoxButtons.OK, MessageBoxIcon.Information);
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

        private static void RunHeadlessAction(string action)
        {
            PackageManager.EnsureExtracted();
            string root = GameLocator.Find();
            if (!GameLocator.IsGameRoot(root))
                throw new InvalidOperationException("Scrap Mechanic was not discovered in any registered Steam library.");

            CommandResult result = PatcherRunner.RunCaptured(action, root);
            Directory.CreateDirectory(PackageManager.StateRoot);
            string logPath = Path.Combine(PackageManager.StateRoot,
                "installer-" + action.ToLowerInvariant() + "-test.log");
            File.WriteAllText(logPath,
                "Action=" + action + "\r\nGame=" + root + "\r\nExitCode=" + result.ExitCode +
                "\r\n\r\n" + result.Output,
                new UTF8Encoding(false));
            if (result.ExitCode != 0)
                throw new InvalidOperationException(PatcherRunner.FailureMessage(action, result));

            if (String.Equals(action, "Install", StringComparison.OrdinalIgnoreCase))
            {
                GameLocator.Save(root);
                Shortcuts.Install(root);
            }
            else if (String.Equals(action, "Uninstall", StringComparison.OrdinalIgnoreCase))
            {
                Shortcuts.Remove();
            }
        }

        [STAThread]
        private static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            bool selfTest = args.Length > 0 && String.Equals(args[0], "--self-test", StringComparison.OrdinalIgnoreCase);
            bool headsetProbeWorker = args.Length > 0 && String.Equals(args[0], "--headset-probe-worker", StringComparison.OrdinalIgnoreCase);
            string headlessAction = null;
            if (args.Length > 0)
            {
                if (String.Equals(args[0], "--install", StringComparison.OrdinalIgnoreCase))
                    headlessAction = "Install";
                else if (String.Equals(args[0], "--uninstall", StringComparison.OrdinalIgnoreCase))
                    headlessAction = "Uninstall";
                else if (String.Equals(args[0], "--verify", StringComparison.OrdinalIgnoreCase))
                    headlessAction = "Verify";
            }
            bool testCommand = selfTest || headsetProbeWorker || headlessAction != null;
            try
            {
                if (headsetProbeWorker)
                {
                    if (args.Length < 2)
                        throw new InvalidOperationException("The OpenXR probe result path is missing.");
                    PackageManager.EnsureExtracted();
                    Environment.ExitCode = OpenXrProbe.RunWorker(args[1]) ? 0 : 2;
                    return;
                }
                if (selfTest)
                {
                    RunSelfTest();
                    Environment.ExitCode = 0;
                    return;
                }
                if (headlessAction != null)
                {
                    RunHeadlessAction(headlessAction);
                    Environment.ExitCode = 0;
                    return;
                }
                PackageManager.EnsureExtracted();
                if (args.Length > 0 && String.Equals(args[0], "--start", StringComparison.OrdinalIgnoreCase))
                {
                    string root = GameLocator.Find();
                    if (!GameLocator.IsGameRoot(root) || !GameLocator.LooksInstalled(root))
                        throw new InvalidOperationException("The Chapter 2 VR installation could not be found or is incomplete. Open this manager and click Install VR Mod.");
                    string runtime = GameLocator.ActiveOpenXrRuntime();
                    if (String.IsNullOrWhiteSpace(runtime) || !File.Exists(runtime))
                        throw new InvalidOperationException("No active 64-bit OpenXR runtime was found. Enable Meta Quest Link or SteamVR OpenXR before using Start VR.");
                    string headsetDetail;
                    if (!OpenXrProbe.HeadsetAvailableWithTimeout(out headsetDetail))
                        throw new InvalidOperationException("Start VR was stopped because no OpenXR headset is ready: " + headsetDetail + ". Connect and wake the headset, then try again.");
                    PatcherRunner.StartDetached(root);
                    return;
                }
                Application.Run(new MainForm());
            }
            catch (Exception ex)
            {
                if (testCommand)
                {
                    try
                    {
                        Directory.CreateDirectory(PackageManager.StateRoot);
                        string failureLog = selfTest ? "self-test.log" :
                            headsetProbeWorker ? "headset-probe-worker.log" :
                            "installer-" + headlessAction.ToLowerInvariant() + "-test.log";
                        File.WriteAllText(Path.Combine(PackageManager.StateRoot, failureLog),
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
