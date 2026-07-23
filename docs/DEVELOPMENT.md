# Development and architecture

## Safety model

The patcher supports one explicitly identified Steam executable and validates both original and patched hashes. It creates timestamped backups before replacement, uses transaction state under `%LOCALAPPDATA%\ScrapMechanicVR`, preserves post-install conflicts, and verifies restoration.

Never disable the executable or original-file guards merely to support a new game update. Port and test the native hooks first.

## Components

- `source/NativeVR/src/vr_runtime.cpp` owns OpenXR session/input state, controller and optical hand tracking, recentering, locomotion, VR UI interaction, and hand/tool presentation.
- `source/NativeVR/src/engine_hooks.cpp` synchronizes the game camera and stereo render passes.
- `source/NativeVR/src/vr_tools.cpp` reads game-published tool, seat, and first-person state.
- `SurvivalPlayer.lua` publishes bounded hand/tool state and consumes VR interactions using APIs exposed by the supported game build.
- Tool Lua files redirect local targeting/effects while retaining ordinary desktop and remote-player behavior.
- `Patcher.ps1` performs validation, install, migration, repair, verification, and restore.
- `installer/Program.cs` embeds the payload and provides the Windows manager UI.

## Dependencies

Run:

```powershell
.\Get-Dependencies.ps1
```

This downloads the pinned Zig toolchain after checksum verification. ReShade and OpenXR headers plus their license texts are kept under `source/NativeVR/third_party`.

## Native build

```powershell
.\source\NativeVR\Build.ps1
```

Build and deploy to a local installation:

```powershell
.\Build-And-Deploy.ps1 -GamePath "C:\Program Files (x86)\Steam\steamapps\common\Scrap Mechanic"
```

Use `-UpdateDistribution` only after hardware testing when the new add-on should replace the payload copy and update its manifest hash.

## Intentional Lua or data changes

Synchronize only manifest-managed files:

```powershell
.\Sync-From-Game.ps1 -GamePath "C:\Program Files (x86)\Steam\steamapps\common\Scrap Mechanic"
```

Review every diff. The live game may contain user experiments that do not belong in the distribution.

## Validation

```powershell
.\Patcher.ps1 -Action ValidatePayload
.\Build-OneFileInstaller.ps1
.\ScrapMechanicVR-Patcher.exe --self-test
```

Also parse changed Lua files, compile native source from a clean dependency directory, install through the patcher, verify all managed files, test a new and existing Survival world, test restore, and record actual Quest hardware results.

## Release checklist

1. Bump `manifest.json` and `installer/Program.cs` versions.
2. Validate original and installed hashes.
3. Build the native add-on and installer from a clean checkout.
4. Exercise install, verify, start, force reset, and uninstall.
5. Create `SHA256SUMS.txt`.
6. Publish the installer as a GitHub Release asset rather than committing it.
7. Document known engine constraints and game-build compatibility.
