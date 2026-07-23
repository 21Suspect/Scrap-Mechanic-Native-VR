# Troubleshooting

## The installer says the game build is unsupported

The current native hooks support Steam build `22163681` only. Do not bypass this guard. Restore the mod before a Steam update and wait for a build-specific port if the executable changes.

## The game folder is not detected

Click **Browse** and select the folder containing `Data`, `Survival`, `Release`, and `ScrapMechanic.exe`. Steam libraries can be on any drive.

## Installation is incomplete or modified

1. Close Scrap Mechanic.
2. Click **Verify** and read the per-file status.
3. Use **Force Reset / Reinstall** for a recognized older managed installation.
4. If the patcher identifies true Steam corruption, use **Repair / Clean Old VR Files**, allow Steam to verify the game, and install again.

Post-install edits to managed files are preserved under `%LOCALAPPDATA%\ScrapMechanicVR\conflicts`; they are not silently overwritten.

## Install or uninstall fails with exit code 1

Use **Open Logs**. Installer transaction logs are under `%LOCALAPPDATA%\ScrapMechanicVR\logs` and contain a plain `ERROR:` line plus the PowerShell diagnostic tail. Common causes are:

- Scrap Mechanic or ReShade still locking a managed file;
- a manually deleted backup;
- an install-state file belonging to a different Steam library;
- an unsupported or externally modified original file;
- administrator elevation being cancelled.

The patcher validates state ownership, executable identity, paths, and hashes before restoring anything.

## Headset shows a blue screen or no stereo image

- Confirm Quest Link/Air Link is active before launching.
- In Meta Quest Link, set Meta Quest Link as the active OpenXR runtime.
- Start the game with the generated **Start Scrap Mechanic VR** shortcut.
- Check `Release\ScrapNativeVR.log` for OpenXR session and swapchain errors.
- Disable unrelated overlays/injectors temporarily.

## Movement changes direction or turning tilts

The current release uses a freshly projected, normalized, yaw-only HMD basis each frame and does not accumulate headset/controller orientation. Hold both thumbsticks for one second to establish a fresh origin. If drift returns, attach the relevant `VR LOCOMOTION BASIS` and tracking log lines to an issue.

## Controllers work on the desktop instead of only in-game

The runtime targets the Scrap Mechanic window and releases injected input when focus changes. If another application receives input, stop the game and attach a log with exact reproduction steps.

## Menus are visible but cannot be clicked or dragged

Point with the right hand/controller laser. Use the right trigger, optical pinch, or A to click and hold. The laser endpoint must be on the floating panel. Menu input is sent to Scrap Mechanic's internal UI queue rather than the Windows desktop cursor.

## Tools, hands, or lasers are missing

- Verify the installation.
- Confirm `Release\scrap_native_vr.addon64` matches the manifest.
- Check `Release\ScrapNativeVR.log` for shader compilation or asset initialization failures.
- Restore altered ReShade settings with **Force Reset / Reinstall** if necessary.

## Performance is poor

VR renders one view per eye. Reduce headset render resolution or refresh rate first, then lower in-game shadows, foliage density, draw distance, SSAO, and reflection quality. Avoid forcing an unstable frame rate; use the Meta runtime's supported refresh-rate options.

## Restoring the original game

Close the game and click **Uninstall / Restore**. Every required original is hash-verified after restoration. If a managed file was modified, the patcher preserves it in the conflicts directory first.
