# Build and deployment

## Build

The preserved test binary was built with LLVM-MinGW UCRT x86-64 (`llvm-mingw-20260616`) and Ninja. The source uses ReShade API 18, MinHook 1.3.3 source, OpenXR 1.1.60 headers, and the matching MinGW OpenXR loader import library included in `third_party`.

Example:

```powershell
.\Build.ps1 `
  -CCompiler 'C:\path\to\llvm-mingw\bin\cc.exe' `
  -CxxCompiler 'C:\path\to\llvm-mingw\bin\c++.exe' `
  -Ninja 'C:\path\to\ninja.exe'
```

The exact headset-tested binary is preserved at `payload/Release/smvr_native_vr_v1.addon64`. A local rebuild is not expected to be byte-identical because PE timestamps and toolchain paths may change; verify behavior separately before replacing the preserved payload.

## Test payload

`payload/Release` contains the exact native add-on and runtime dependency binaries used for the confirmed visual snapshot, plus portable pass-through ReShade and VR configuration files.

Only use it with the exact executable hash documented in `README.md`. Stop the game before copying files. Do not install the legacy branch's `scrap_native_vr.addon64`, `openxr_loader.dll`, Lua scripts, installer, or gameplay patches alongside this branch.

The portable `ReShade.ini` points effect, texture, and intermediate-cache search paths at empty directories inside `Release/smvr-empty`. It disables effects, overlay shortcuts, and input handling. It does not modify Scrap Mechanic's desktop resolution settings.

## Runtime files

- `dxgi.dll`: ReShade 6.7.3 full add-on build.
- `smvr_native_vr_v1.addon64`: exact Chapter 2 renderer snapshot.
- `libopenxr_loader.dll`: Khronos OpenXR loader 1.1.60.
- `libc++.dll` and `libunwind.dll`: LLVM-MinGW runtime dependencies of the preserved add-on.
- `ScrapMechanicVR.ini`: enables VR and disables the high-resolution PC diagnostic probe.

Validate all payload hashes against `SHA256SUMS.txt` before copying them.

## One-file installer

The release manager is built from `installer/Program.cs`, embeds the exact `payload`, and calls the guarded transaction backend in `Patcher.ps1`.

```powershell
.\Build-OneFileInstaller.ps1
```

This produces `dist/ScrapMechanicVR-Chapter2-Patcher.exe`. The build first validates all seven payload hashes. The resulting executable validates its embedded manifest and native add-on before extracting, discovers the Steam installation and active 64-bit OpenXR runtime, and refuses any game executable other than the documented build.

The current installer version is `0.1.2-chapter2-20260825`, SHA-256 `90F79D30AD1B747E024F87999C643F749524EDEC2047477A12C9069B406769B4`. Its backend was tested in isolated game trees for install, exact existing-payload adoption, verify, runtime-mutated configuration, rollback, uninstall/restore, empty-directory cleanup, and rejection of legacy files. The compiled EXE's extraction and environment self-test also passed against the supported installed game and Meta OpenXR runtime.
