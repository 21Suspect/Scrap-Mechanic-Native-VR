# Scrap Mechanic Native VR — Chapter 2 Beta

This branch is the native OpenXR VR mod for Scrap Mechanic Chapter 2 / 1.0. It is separate from the legacy implementation on `main` and must not be mixed with files from that branch.

> Beta release: tested on Meta Quest 3 through Quest Link with Scrap Mechanic `1.0.5.876`, Steam build `24529696`. Other game builds are rejected by the installer.

## Easy installation

1. Close Scrap Mechanic.
2. Download [ScrapMechanicVR-Chapter2-Patcher.exe](https://github.com/21Suspect/Scrap-Mechanic-Native-VR/raw/chapter2-1.0/dist/ScrapMechanicVR-Chapter2-Patcher.exe).
3. Verify the installer SHA-256: `E434B408B30ECE40A22690C1E1D0AFD039D49AC5C4E5878408FF8BD9199E1F7C`.
4. Run it, confirm Steam build `24529696`, and select **Install VR Mod**.
5. Keep Quest Link active, set Meta as the active OpenXR runtime, and select **Start Scrap Mechanic**.

The installer is currently unsigned, so Windows may show a SmartScreen warning. It validates the supported game executable and all packaged files, backs up replaced game files, detects incompatible legacy-mod files, and provides Verify, Repair/Restore, and Uninstall actions. It does not rewrite saves or desktop-resolution settings.

## Exact compatibility

- Steam app: `387990`
- Steam build: `24529696`
- Game version: `1.0.5.876`
- `Release/ScrapMechanic.exe` SHA-256: `5D663BA2EC5DC8C7ABEFCC5C9344AE86F0A066C4069A91F54833524AC9A5B4F5`
- Platform: Windows x86-64 / Direct3D 11
- Tested headset/runtime: Meta Quest 3, Quest Link, Meta OpenXR runtime
- ReShade host: full add-on build 6.7.3

## Current features

- Native stereo rendering with two engine-rendered eyes, six-degree-of-freedom head tracking, correct depth and perspective, and the runtime-recommended `2064 × 2272` resolution per eye.
- Exact runtime FOV with the cross-shaped image seam fixed and without the rejected rotational guard band.
- Correct sRGB color, contrast, lighting, clouds, decals, shadows, and foliage in VR.
- The first-person PC viewmodel remains unchanged in normal PC mode and is removed only while VR is active.
- Quest Touch controller input and optional Meta optical hand tracking.
- Tracked, arm-free mechanic gloves with tuned finger opening and closing.
- VR locomotion and common actions, including turning, jump, crouch, sprint, use, hotbar, backpack, pause, seated zoom, and recenter.
- Tracked hammer, connection tool, paint tool, weld tool, potato weapons, Chapter 2 scrap spudgun, potato launcher, and clay gun.
- User-tuned clay-gun grip, moving-part pivots, and rotation axes, plus the included live calibration helper.
- Thin white aiming lasers on VR guns and tools.
- World-locked main-menu and submenu display using the game's exact live native UI, including hover states and confirmation dialogs.
- Right-hand laser interaction for controllers and optical hands. Input is queued directly into Scrap Mechanic's own input manager; Windows mouse simulation is never used.
- Native menu fade/blur backgrounds are keyed transparent so the 3D menu scene remains visible.
- Chapter 2 VR Mod logo on both the VR and desktop main menus.
- Guarded one-file installer, verification, repair, and uninstall workflow.

## Known limitations

- Gun projectiles intentionally use Scrap Mechanic's stock PC crosshair origin and direction. Earlier experimental VR-barrel projectile rewrites were removed because they were unreliable; the white barrel lasers are visual aiming guides only.
- The desktop mirror can freeze on its first left-eye frame while the headset is active. Headset rendering continues normally.
- Fullscreen/windowed switching with an active headset has not completed the same level of user testing as normal windowed play.
- The floating native UI currently covers the startup/main-menu flow. A complete in-world spatial HUD/backpack rework is not included.
- This is a beta tied to one exact game executable. A Scrap Mechanic update requires a new compatible build.
- The installer is not code-signed.

See [STATUS.md](STATUS.md) for the validation boundary and [BUILD.md](BUILD.md) for build and packaging details. `SHA256SUMS.txt` contains the machine-readable checksums.

## Repository separation

- `main`: legacy mod for Scrap Mechanic Steam build `22163681`.
- `chapter2-1.0`: current Chapter 2 / 1.0 beta for Steam build `24529696`.
- `chapter2-feature-port`: development history used to prepare this beta.

Never mix payloads, hook addresses, Lua scripts, or installers between the legacy and Chapter 2 branches.

## License

Original project code is under the [MIT License](LICENSE). Third-party components retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LEGAL.md](LEGAL.md). This unofficial project is not affiliated with Axolot Games, Meta, Valve, Khronos, or ReShade.
