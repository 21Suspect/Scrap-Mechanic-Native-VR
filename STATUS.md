# Chapter 2 release status

Release: `1.0.0-chapter2-20260830`

Branch: `main`

Supported game: Scrap Mechanic `1.0.5.876`, Steam build `24529696`

## Headset-confirmed build

- Native stereo OpenXR rendering at `2064 × 2272` per eye with six-degree-of-freedom head tracking.
- Exact runtime FOV, correct depth and perspective, VR seam correction, and normal Scrap Mechanic color and contrast.
- Quest Touch controllers and optional Meta optical hand tracking with tracked mechanic gloves.
- Tracked tools and Chapter 2 weapons, including physical hammer swings and gunfire from the VR barrel pose.
- World-locked startup and in-game spatial UI with the live native game menus, transparent composition, hover, click, hold, and drag.
- UI events are queued directly into Scrap Mechanic's input manager without Windows mouse simulation.
- Restrained native OpenXR haptics for controller interaction.
- Normal PC mode retains its standard viewmodel; VR mode removes it.
- Seat, headset-focus, tool-switching, primary-action, and lift-placement state recover automatically after transitions.

## Rendering architecture

- Two high-level engine scene renders per OpenXR frame: left eye and right eye.
- OpenXR swapchains use the runtime-recommended extent and an sRGB format.
- The centered engine source is `2565 × 2711` and is cropped to the runtime FOV without a rotational guard band.
- The user's desktop resolution setting remains independent from the VR eye resolution.
- The live menu compositor uses an adaptive capture scheduler while stereo placement, head tracking, laser input, and haptics continue at headset rate.

## Installer

`dist/ScrapMechanicVR-Installer.exe` embeds and validates all 28 managed payload files, verifies the supported game executable, backs up replaced files, refreshes generated Lua cache when needed, and supports install, start, verify, repair, and uninstall.

Installer and payload hashes are recorded in `SHA256SUMS.txt`.
