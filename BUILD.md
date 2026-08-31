# Build and deployment

## Native add-on

The Chapter 2 release uses LLVM-MinGW UCRT x86-64 (`llvm-mingw-20260616`) and Ninja. Its dependencies are ReShade API 18, MinHook 1.3.3 source, OpenXR 1.1.60 headers, and the matching MinGW OpenXR loader import library in `third_party`.

```powershell
.\Build.ps1 `
  -CCompiler 'C:\path\to\llvm-mingw\bin\cc.exe' `
  -CxxCompiler 'C:\path\to\llvm-mingw\bin\c++.exe' `
  -Ninja 'C:\path\to\ninja.exe'
```

The tested add-on must be copied to `payload/Release/smvr_native_vr_v1.addon64`, its hash recorded in `manifest.json`, and the payload validator rerun before packaging.

## Payload

`payload` contains the native add-on, portable pass-through ReShade host, OpenXR loader/runtime dependencies, VR configuration and calibration assets, Chapter 2 scripts, desktop-logo assets, and startup-menu art.

Important runtime files:

- `Release/dxgi.dll`: ReShade 6.7.3 full add-on host.
- `Release/smvr_native_vr_v1.addon64`: Chapter 2 native OpenXR renderer and interaction add-on.
- `Release/libopenxr_loader.dll`: Khronos OpenXR loader 1.1.60.
- `Release/libc++.dll` and `Release/libunwind.dll`: LLVM-MinGW dependencies.
- `Release/ScrapMechanicVR.ini`: VR configuration.
- `Release/ScrapMechanicVR-ClayCalibration.exe` and `.ini`: live clay-gun pose/pivot/axis calibration helper.

Only use the payload with the exact executable hash documented in `README.md`. Do not mix it with the legacy branch's native add-on, OpenXR loader, Lua scripts, or installer.

## One-file installer

`installer/Program.cs` embeds the complete payload and invokes the guarded transaction backend in `Patcher.ps1`.

```powershell
.\Build-OneFileInstaller.ps1
```

The public artifact is `dist/ScrapMechanicVR-Installer.exe`. It validates its embedded patcher, manifest, native add-on, branded UI assets, soundtrack, and all 47 managed payload files before installation. Its four user actions are Install VR Mod, Uninstall VR Mod, Start VR, and Open Logs. Install automatically migrates a managed older/current build and verifies the completed installation. Uninstall removes managed current/older builds, restores backups, and verifies the result. Start VR requires the active OpenXR runtime to report a connected headset.

Current version: `1.3.2-chapter2-20260831`

The installer SHA-256 is recorded in `SHA256SUMS.txt`.

The payload includes a verified `Cache/Bundle/core_data.cbo` seed for the supported game build. This avoids Scrap Mechanic's unstable cold-cache compiler path on the first VR launch; the cache remains runtime-mutable and is backed up/restored like every other managed file.

## Release checks

Before publishing:

1. Build the native add-on successfully.
2. Ensure `manifest.json` matches every file in `payload`.
3. Run `Patcher.ps1 -Action ValidatePayload`.
4. Build the one-file installer and run its self-test.
5. Run `Verify-Snapshot.ps1`.
6. Regenerate `SHA256SUMS.txt` and record the installer hash in `README.md` and this file.
7. Publish the current Chapter 2 release to `main`; preserve the pre-1.0 build on `legacy-pre-1.0`.
