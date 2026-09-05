# Chapter 2 release status

Release candidate: `1.3.15-chapter2-20260905`

Branch: `main`

Supported game: Scrap Mechanic `1.0.5.876`, Steam build `24529696`

## Current build

- Palette/menu selection consumes the trigger until physical release before allowing a new gameplay action, including the native mouse queue, Lua hand state, and force-build chord (issue #12).
- Fertilizer use from remote players retains the remote character's hosted effect; only a locally owned tool reads the local VR hand pose (issue #13).
- Start VR begins its headset retry budget at the first OpenXR attempt and completes it when the session starts. Launch requests survive delayed add-on loading when the game process started promptly. Failure diagnostics include OpenXR result names and GPU adapter identities.
- Focused input, Lua ownership, and cold-start retry regression checks pass. The reported VDXR-only-flatscreen case still requires the affected machine's native VR and ReShade logs to identify its failing stage; it has not been reproduced locally.
- Native stereo OpenXR rendering at `2064 × 2272` per eye with six-degree-of-freedom head tracking.
- Exact runtime FOV, correct depth and perspective, VR seam correction, and normal Scrap Mechanic color and contrast.
- Standing VR keeps the world upright, preserves smooth desktop/controller pitch, and removes only pitch-orbit height movement; seated VR uses the original game camera behavior.
- OpenXR 1.1 Meta Touch, legacy Oculus Touch, Valve Index, and generic OpenXR controller profiles, plus optional Meta optical hand tracking with tracked mechanic gloves.
- Profile-aware controller bindings keep Quest 3 defaults intact while giving SteamVR/Valve Index a trackpad menu path and grip sprint default.
- Tracked tools and Chapter 2 weapons, including stock trigger-driven hammer attacks aimed from the right OpenXR hand, OpenXR tool targeting, and gunfire from the separately calibrated VR barrel pose.
- Complete grouped held-item geometry with a live position/rotation calibration helper, including blocks, parts, buckets, glowsticks, cornades, loose clay, extinguisher, planting, fertilizer, food and drink, feeder food, soil, keys and power cores, resources, arbitrary carried objects, and the logbook.
- The held-item helper follows the running game installation and exports every grouped pose as a shareable plain-text file for review.
- Held-item actions use the right-hand VR pose for throwing, spraying, placing, targeting, inserting, dropping, and use interactions.
- Right-controller B keeps Scrap Mechanic's stock Use interaction and redirects only its selection ray to the OpenXR right hand, including normal hold-to-refine behavior; a laser-free amber marker identifies the exact selected surface.
- Grip plus A performs Scrap Mechanic's native Shift-click quick transfer in inventory/chest menus, and right grip plus right-stick vertical raises or lowers the lift.
- World-locked startup and in-game spatial UI with the live native game menus, transparent composition, hover, click, hold, and drag.
- Keyboard and pointer events are queued directly into Scrap Mechanic's input manager without Windows input simulation or a foreground-window dependency.
- Restrained native OpenXR haptics for controller interaction.
- Always-visible compact curved smartwatch-style wrist HUD: left glove vitals/health, a dynamic blue underwater oxygen bar, and in-game time; right glove a world-space compass with cardinal markers and live quest, beacon, raid, enemy, and event waypoints. The pose is fixed in the native renderer for consistent placement.
- Right index trigger plus right middle-finger grip queues Scrap Mechanic's native **F** force-build action as a guarded chord; existing Quest 3 mappings remain unchanged.
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

`dist/ScrapMechanicVR-Installer.exe` embeds and validates the managed payload, branded installer artwork, soundtrack, and a verified first-launch game-data cache. It starts music at 50% with a compact volume control and presents Install VR Mod, Uninstall VR Mod, Start VR, Open Logs, and Open Bindings. Install and uninstall detect current/older managed versions, explain the exact operation before asking for approval, migrate or restore safely, and verify automatically. Known prior-release payloads are migrated by exact path and SHA-256, same-version metadata refreshes retain their verified restore authority, and active calibration helpers are reported before any transaction begins. The wrist HUD is fixed in the native renderer, so no HUD calibration helper is installed. Start VR requires a connected headset reported by the active OpenXR runtime.

Installer SHA-256: `42D0B8CC7AA0B35542FF59B376FA67B3C1639FAC5A99FDD2DF1C94ADCEC2791D`. Installer and payload hashes are recorded in `SHA256SUMS.txt`.
