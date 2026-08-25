# Scrap Mechanic Native VR — Chapter 2 / 1.0 branch

This branch is a separate visual-only OpenXR implementation for the Scrap Mechanic Chapter 2 / 1.0 renderer. It does **not** use the legacy renderer addresses, native binary, installer, controller code, or gameplay patches from this repository's `main` branch.

> Status: Quest 3 visual test snapshot, not a final stable release. Use only with the exact supported Steam build below. The user has confirmed the core stereo result in the headset, but performance, desktop mirroring, UI, and headset display-mode changes still need work.

## Exact compatibility

- Steam app: `387990`
- Steam build: `24529696`
- Game version: `1.0.5.876`
- `Release/ScrapMechanic.exe` SHA-256: `5D663BA2EC5DC8C7ABEFCC5C9344AE86F0A066C4069A91F54833524AC9A5B4F5`
- Architecture: Windows x86-64 / D3D11
- Tested headset/runtime: Meta Quest 3 through Quest Link, Meta OpenXR runtime
- ReShade: full add-on build 6.7.3

The add-on validates the executable image size, PE timestamp, and hook byte prefixes. If any exact-build check fails, it must not install the renderer hooks.

## Human-confirmed in Quest 3

- Two real engine-rendered eyes with correct depth and perspective.
- Immediate six-degree-of-freedom head tracking.
- Correct runtime FOV without the rejected rotational guard band.
- Runtime-recommended `2064 × 2272` submitted resolution per eye.
- Clouds, lighting, shadows, roads/decals, and foliage remain spatially correct.
- Correct color/contrast through sRGB OpenXR swapchains.
- First-person viewmodel is removed only while VR rendering is active; normal PC mode retains it.

These statements refer to the user's headset observations for this exact snapshot. Compilation, logs, or a monitor mirror are not treated as visual proof.

## Known limitations

- In-world performance is currently around 20 FPS on the test PC and needs optimization.
- The PC eye mirror may freeze once the headset becomes active. This does not imply a third scene render; VR output remains the priority.
- The new swapchain-reset handling survived PC-only fullscreen/windowed transitions, but the equivalent headset transition was interrupted before user confirmation. Treat fullscreen-with-HMD as unconfirmed.
- Main-menu UI is currently absent in VR.
- Hands, controllers, tools, weapons, locomotion, and gameplay interaction are intentionally out of scope for this snapshot.
- No installer/repair/uninstall workflow from the legacy branch is reused here.

See [STATUS.md](STATUS.md) for the evidence boundary and [BUILD.md](BUILD.md) for build/deployment details.

## Repository separation

- `main`: legacy implementation for Steam build `22163681`.
- `chapter2-1.0`: this clean Chapter 2 / 1.0 visual renderer snapshot for Steam build `24529696`.

Never mix payloads, hook addresses, or installers between these branches.

## License

Original project code is under the [MIT License](LICENSE). Third-party components keep their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LEGAL.md](LEGAL.md). This project is unofficial and is not affiliated with Axolot Games, Meta, Valve, Khronos, or ReShade.
