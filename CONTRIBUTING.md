# Contributing

Contributions are welcome. The most useful changes are reproducible fixes, headset/runtime compatibility reports, careful performance work, and clear documentation.

## Before opening an issue

1. Confirm the exact Scrap Mechanic Steam build.
2. Run the patcher's **Verify** action.
3. Reproduce on a clean, supported installation when possible.
4. Remove access tokens, account identifiers, personal paths, and save data from screenshots and logs.
5. Search existing issues for the same symptom.

Include:

- headset and connection method;
- active OpenXR runtime;
- GPU and Windows version;
- whether Touch controllers or optical hand tracking were active;
- exact reproduction steps;
- the relevant tail of `Release\ScrapNativeVR.log` or the installer log.

## Pull requests

1. Fork the repository and create a focused branch.
2. Keep native gameplay input deterministic and bounded.
3. Preserve server-authoritative Survival behavior.
4. Do not patch `ScrapMechanic.exe`, DRM, ownership checks, anti-cheat, or save files.
5. Do not broaden the supported game-build guard without testing the new executable.
6. Update `manifest.json` hashes only for intentional payload changes.
7. Run the checks below and describe real hardware testing honestly.

```powershell
.\Patcher.ps1 -Action ValidatePayload
.\Get-Dependencies.ps1
.\source\NativeVR\Build.ps1
.\Build-OneFileInstaller.ps1
.\ScrapMechanicVR-Patcher.exe --self-test
```

Do not commit local backups, logs, build directories, credentials, or the generated root installer executable. Release executables belong in GitHub Releases.

By contributing original work, you agree that it may be distributed under the MIT License. Do not submit content you do not have permission to share.
