# Chapter 2 release status

Release candidate: `1.3.6-chapter2-20260902`

Branch: `main`

Supported game: Scrap Mechanic `1.0.5.876`, Steam build `24529696`

## Current build

- Native stereo OpenXR rendering at `2064 × 2272` per eye with six-degree-of-freedom head tracking.
- Exact runtime FOV, correct depth and perspective, VR seam correction, and normal Scrap Mechanic color and contrast.
- Standing VR keeps the world upright, preserves smooth desktop/controller pitch, and removes only pitch-orbit height movement; seated VR uses the original game camera behavior.
- OpenXR 1.1 Meta Touch, legacy Oculus Touch, Valve Index, and generic OpenXR controller profiles, plus optional Meta optical hand tracking with tracked mechanic gloves.
- Tracked tools and Chapter 2 weapons, including stock trigger-driven hammer attacks aimed from the right OpenXR hand, OpenXR tool targeting, and gunfire from the separately calibrated VR barrel pose.
- Complete grouped held-item geometry with a live position/rotation calibration helper, including blocks, parts, buckets, glowsticks, cornades, loose clay, extinguisher, planting, fertilizer, food and drink, feeder food, soil, keys and power cores, resources, arbitrary carried objects, and the logbook.
- Held-item actions use the right-hand VR pose for throwing, spraying, placing, targeting, inserting, dropping, and use interactions.
- Right-controller B keeps Scrap Mechanic's stock Use interaction and redirects only its selection ray to the OpenXR right hand, including normal hold-to-refine behavior; a laser-free amber marker identifies the exact selected surface.
- Grip plus A performs Scrap Mechanic's native Shift-click quick transfer in inventory/chest menus, and right grip plus right-stick vertical raises or lowers the lift.
- World-locked startup and in-game spatial UI with the live native game menus, transparent composition, hover, click, hold, and drag.
- Keyboard and pointer events are queued directly into Scrap Mechanic's input manager without Windows input simulation or a foreground-window dependency.
- Restrained native OpenXR haptics for controller interaction.
- Normal PC mode retains its standard viewmodel; VR mode removes it.
- Seat, headset-focus, tool-switching, primary-action, and lift-placement state recover automatically after transitions.
- Survival, Creative, and Custom Game content contexts use the same tracked-hand and spatial-UI bridge.
- Steam discovery ignores stale or offline library drives and prioritizes the game directory already validated by the installer.
- Guarded migration recognizes path-specific payload hashes from prior Chapter 2 releases, carries verified restore state forward, and never mistakes known older VR files for unrelated modifications.
- The Start VR headset probe resolves its bundled OpenXR loader dependencies from the package directory, independent of PATH, and works with Meta Quest Link, Virtual Desktop VDXR, and SteamVR runtimes.

## Rendering architecture

- Two high-level engine scene renders per OpenXR frame: left eye and right eye.
- OpenXR swapchains use the runtime-recommended extent and an sRGB format.
- The centered engine source is `2565 × 2711` and is cropped to the runtime FOV without a rotational guard band.
- The user's desktop resolution setting remains independent from the VR eye resolution.
- The live menu compositor captures native UI on transitions and interactions while its cached stereo panel, head tracking, laser input, and haptics continue at headset rate.

## Installer

`dist/ScrapMechanicVR-Installer.exe` embeds and validates all 47 managed payload files, the branded installer artwork, its soundtrack, and a verified first-launch game-data cache. It starts music at 50% with a compact volume control and presents only Install VR Mod, Uninstall VR Mod, Start VR, and Open Logs. Install and uninstall detect current/older managed versions, explain the exact operation before asking for approval, migrate or restore safely, and verify automatically. Known prior-release payloads are migrated by exact path and SHA-256, same-version metadata refreshes retain their verified restore authority, and open calibration helpers are reported before any transaction begins. Start VR requires a connected headset reported by the active OpenXR runtime.

Installer SHA-256: `721C708913E64AA90FE63D4455799AA3C1E7151402E4EBA2E07AA5C2D50A4B42`. Installer and payload hashes are recorded in `SHA256SUMS.txt`.
