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
- `Release/ScrapMechanicVR.ini`: VR configuration and profile-aware controller bindings. The installer exposes it through **Open Bindings**. The right index trigger plus middle-finger grip also queues the game's **F** force-build action.
- `Release/ScrapMechanicVR-ClayCalibration.exe` and `.ini`: live clay-gun pose/pivot/axis calibration helper.
- `Release/ScrapMechanicVR-HeldCalibration.exe` and `.ini`: live grouped held-item position/rotation/scale calibration helper. It can be copied to another PC, auto-connects to a running game installation (including when the game starts after the helper), and exports a shareable plain-text pose file.
- The wrist HUD uses a fixed compact smartwatch pose built into the native renderer; no calibration helper or extra runtime files are required.

The documented executable hash remains the only supported configuration. The installer can explicitly override a newer game-build mismatch after a prominent warning, but compatibility is not guaranteed. Do not mix this payload with the legacy branch's native add-on, OpenXR loader, Lua scripts, or installer.

## One-file installer

`installer/Program.cs` embeds the complete payload and invokes the guarded transaction backend in `Patcher.ps1`.

```powershell
.\Build-OneFileInstaller.ps1
```

The public artifact is `dist/ScrapMechanicVR-Installer.exe`. It validates its embedded patcher, manifest, native add-on, branded UI assets, soundtrack, and the managed payload before installation. Its five user actions are Install VR Mod, Uninstall VR Mod, Start VR, Open Logs, and Open Bindings. Install automatically migrates a managed older/current build and verifies the completed installation. Uninstall removes managed current/older builds, restores backups, and verifies the result. Start VR requires the active OpenXR runtime to report a connected headset.

Current prerelease: `1.4.11-pre.1` for Scrap Mechanic `1.0.7.894` / Steam test build `26302409`

The installer SHA-256 is recorded in `SHA256SUMS.txt`.

Focused regression checks for palette input, fertilizer ownership, and startup retries:

```powershell
c++.exe -std=c++20 tools/tests/ui_trigger_gate.cpp -o build/ui_trigger_gate_test.exe
.\build\ui_trigger_gate_test.exe
c++.exe -std=c++20 tools/tests/launch_retry.cpp -o build/launch_retry_test.exe
.\build\launch_retry_test.exe
c++.exe -std=c++20 tools/tests/vr_projection.cpp -o build/vr_projection_test.exe
.\build\vr_projection_test.exe
python tools/tests/test_fertilizer_effect.py --lua-dll '<game directory>/Release/lua51.dll'
```

The Lua test executes the shipped fertilizer script with mocked tool owners. The C++ tests exercise the production input gate and launch retry policy. These checks do not replace an in-headset multiplayer or VDXR test.

This prerelease uses `compiledDataCachePolicy: rebuild`. It retires the previous release's cache seed and invalidates `Cache/Bundle/core_data.cbo` on install/uninstall. The engine must rebuild it from 1.0.7 files on first launch; user-authored INI files remain preserved. Earlier builds used a seed to avoid cold-cache startup problems, so first-launch rebuilding is specifically awaiting runtime verification on 1.0.7.

For the 1.0.7 native ABI checks, install Python `pefile` and run:

```powershell
python tools/tests/test_game_107_layout.py '<game directory>/Release/ScrapMechanic.exe'
powershell -NoProfile -ExecutionPolicy Bypass -File tools/tests/test_upgrade_restore.ps1
```

The executable test verifies the guarded render/camera/raycast entry points, call targets, result fraction offset and all 16 input instruction guards. The new input event is 0x48 bytes, with its payload at +0x30; 1.0.6's 0x40-byte event is incompatible. Headset validation is separate from these checks.

## Release checks

Before publishing:

1. Build the native add-on successfully.
2. Ensure `manifest.json` matches every file in `payload`.
3. Run `Patcher.ps1 -Action ValidatePayload`.
4. Build the one-file installer and run its self-test.
5. Run `Verify-Snapshot.ps1`.
6. Regenerate `SHA256SUMS.txt` and keep `snapshot.json` validation specific to this candidate.
7. Publish `v1.4.11-pre.1` from `compat/sm-1.0.7` with GitHub's prerelease flag and `make_latest: false`. Keep `main` and the stable 1.4.10 release unchanged.
