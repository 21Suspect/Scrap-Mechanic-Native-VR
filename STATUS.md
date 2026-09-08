# Chapter 2 release status

Current release: `1.4.8`

Branch: `main`

Supported game: Scrap Mechanic `1.0.5.876`, Steam build `24529696`

## Current build

- Asymmetric per-eye FOVs now choose a shared render extent large enough for both eyes, fixing the Bigscreen Beyond/SteamVR `eye_camera_build` failure without changing mirrored Quest/Meta projection behavior (issue #28).
- Headset removal hands rendering and input back to ordinary desktop mode after a short focus transition while pumping empty OpenXR frames; refocusing rebuilds the VR anchor on Meta, VDXR, and SteamVR runtimes (issue #27).
- The validated native player-state bridge now drives the gameplay/menu marker in base Survival as well as Custom Games, preventing a stale marker from trapping motion-controller input in the floating UI (issue #25).
- Installer upgrades refresh the derived `core_data.cbo` seed while continuing to preserve user-authored INI settings, and uninstall safely adopts legacy runtime-mutable records from 1.4.0 (issue #26).
- Seated VR firing uses the first-person effect set, restoring muzzle sound and particles for all six supported firearms, including the clay gun.
- Gun effects originate from the calibrated native barrel pose, including the stock-forward offset.
- Palette/menu selection consumes the trigger until physical release before allowing a new gameplay action, including the native mouse queue, Lua hand state, and force-build chord (issue #12).
- Fertilizer use from remote players retains the remote character's hosted effect; only a locally owned tool reads the local VR hand pose (issue #13).
- Start VR begins its headset retry budget at the first OpenXR attempt and completes it when the session starts. Launch requests survive delayed add-on loading when the game process started promptly. Failure diagnostics include OpenXR result names and GPU adapter identities.
- Focused input, Lua ownership, cold-start retry, asymmetric-projection, and installer runtime-cache regression checks pass.
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
- The centered engine source is derived from the larger requirement of both reported eye FOVs and is cropped to each runtime eye extent without a rotational guard band.
- The user's desktop resolution setting remains independent from the VR eye resolution.
- The live menu compositor captures native UI on transitions and interactions while its cached stereo panel, head tracking, laser input, and haptics continue at headset rate.

## Installer

`dist/ScrapMechanicVR-Installer.exe` embeds and validates the managed payload, branded installer artwork, soundtrack, and a verified first-launch game-data cache. It starts music at 50% with a compact volume control and presents Install VR Mod, Uninstall VR Mod, Start VR, Open Logs, and Open Bindings. Install and uninstall detect current/older managed versions, explain the exact operation before asking for approval, migrate or restore safely, and verify automatically. Known prior-release payloads are migrated by exact path and SHA-256, same-version metadata refreshes retain their verified restore authority, and active calibration helpers are reported before any transaction begins. The wrist HUD is fixed in the native renderer, so no HUD calibration helper is installed. Start VR requires a connected headset reported by the active OpenXR runtime.

Installer SHA-256: `2B1ABFE7602AB041871363C875F0D09ED3D53EB46E5CF7E7291F1B1C183A3FB0`. Installer and payload hashes are recorded in `SHA256SUMS.txt`.
