# Chapter 2 beta status

Release: `0.3.0-chapter2-beta-20260829`

Branch: `chapter2-1.0`

Supported game: Scrap Mechanic `1.0.5.876`, Steam build `24529696`

## Headset-confirmed results

- Native stereo, correct depth and perspective, six-degree-of-freedom head tracking, and correct color/contrast on Meta Quest 3 through Quest Link.
- Runtime-recommended `2064 × 2272` submitted resolution per eye.
- Exact runtime FOV with no rotational guard band and no cross-shaped missing/duplicated-pixel seam.
- VR-only first-person viewmodel removal; normal PC mode retains the standard viewmodel.
- Arm-free mechanic gloves with corrected textures and tuned finger range.
- Tracked tool and weapon visuals, including the Chapter 2 clay gun and its calibrated moving parts.
- World-locked native startup menu with the Chapter 2 VR Mod logo, exact live Scrap Mechanic submenus, native hover/click behavior, controller/hand-tracking laser input, and transparent native fade/blur backgrounds.
- UI input is sent through Scrap Mechanic's own private input-event queue. The mod does not synthesize Windows mouse motion or mouse-button events.

## Rendering architecture

- Exactly two high-level engine scene renders per OpenXR frame: left and right.
- No neutral third scene render.
- OpenXR eye swapchains use the runtime-recommended `2064 × 2272` extent and an sRGB format.
- The centered engine source is `2565 × 2711`; each eye is cropped to the runtime FOV without a rotational guard band.
- The user's desktop resolution setting is not rewritten for the VR render size.
- The first-person viewmodel patch is active only while OpenXR reports `shouldRender=true` and is restored on stop, idle, session loss, disable, and add-on destruction.
- The menu compositor captures the game's live native UI before mirror composition and removes the native full-screen fade/blur matte for transparent presentation in the VR world.

## Gameplay status

- Quest Touch and optional Meta optical hand tracking are integrated.
- Locomotion, turning, jump, crouch, sprint, use, hotbar, backpack, pause, seated zoom, and recenter paths are present.
- Mechanic gloves, tools, weapons, lasers, physical hammer, touch controls, and the clay calibration helper are included.
- Gun projectiles and muzzle effects deliberately use stock Scrap Mechanic 1.0 calculations. The unsuccessful experimental tracked-barrel projectile override was removed, so the PC crosshair still determines shot direction. Gun lasers are visual only.

## Known limitations

- The desktop left-eye mirror may freeze after the headset becomes active; this does not stop headset rendering.
- Fullscreen/windowed switching with an active HMD is not as thoroughly confirmed as normal windowed operation.
- Startup/main-menu UI is covered, but a complete spatial in-world HUD/backpack implementation is not included.
- Compatibility is locked to one executable hash and must be updated after a game patch.
- The installer is unsigned.

## Installer status

The public one-file installer is `dist/ScrapMechanicVR-Chapter2-Patcher.exe`, version `0.3.0-chapter2-beta-20260829`. It embeds and validates all 28 managed payload files, rejects unsupported game executable hashes, backs up replaced files, invalidates the game's generated Lua cache when necessary, detects legacy-mod conflicts, and supports install, verify, repair/restore, and uninstall.

The installer and payload hashes are recorded in `SHA256SUMS.txt`. The former local-only feature-port installer has been retired to avoid ambiguity.

## Evidence boundary

“Confirmed” means the user observed the behavior in Quest 3 on the supported build. Compilation, logs, a desktop mirror, or a passing installer check are supporting evidence but are not substitutes for headset observation. Known defects remain documented rather than being presented as completed features.
