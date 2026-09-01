# Scrap Mechanic Native VR — Chapter 2

![Scrap Mechanic VR gameplay](docs/images/scrap-vr-demo.gif)

Native OpenXR VR for the current Scrap Mechanic Chapter 2 / 1.0 release, made by [21Suspect](https://github.com/21Suspect).

[![Latest release](https://img.shields.io/github/v/release/21Suspect/Scrap-Mechanic-Native-VR?display_name=tag&sort=semver)](https://github.com/21Suspect/Scrap-Mechanic-Native-VR/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/21Suspect/Scrap-Mechanic-Native-VR/total)](https://github.com/21Suspect/Scrap-Mechanic-Native-VR/releases)
[![License: MIT](https://img.shields.io/badge/original_code-MIT-ffc124.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-45d6c7.svg)](#requirements)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy_Me_a_Coffee-Support-FFDD00?logo=buy-me-a-coffee&logoColor=000000)](https://buymeacoffee.com/21suspect)

## Easy installation

1. Close Scrap Mechanic.
2. Download and run **[ScrapMechanicVR-Installer.exe](https://github.com/21Suspect/Scrap-Mechanic-Native-VR/raw/refs/heads/main/dist/ScrapMechanicVR-Installer.exe)**.
3. Click **Install VR Mod** and approve the administrator prompt.
4. Start your OpenXR runtime, connect and wake the headset, then click **Start VR** or use the new desktop shortcut.

For Virtual Desktop, select **VDXR** as the OpenXR runtime in Virtual Desktop Streamer before connecting the headset. You can also select **SteamVR**, provided SteamVR is configured as the active OpenXR runtime. The installer supports both paths and checks the connected headset through the selected runtime.

The installer has four clear actions: **Install VR Mod**, **Uninstall VR Mod**, **Start VR**, and **Open Logs**. Install automatically removes a detected older/current managed build before upgrading, and both install and uninstall verify their result automatically. Start VR checks that the active OpenXR runtime reports a connected headset. The installer is currently unsigned, so Windows SmartScreen may ask for confirmation.

## Features

- Native stereo OpenXR rendering at the headset-recommended `2064 × 2272` resolution per eye.
- Six-degree-of-freedom head tracking, correct stereo depth and perspective, exact runtime FOV, and the VR seam fix.
- A level standing horizon with mouse/controller pitch-height movement removed, while seats retain Scrap Mechanic's original camera orbit.
- Quest Touch and Valve Index controllers, plus optional Meta Quest optical hand tracking.
- Tracked mechanic gloves with animated fingers and no artificial arms.
- VR locomotion, turning, jump, crouch, sprint, interaction, hotbar controls, seated zoom, pause, and recentering.
- Tracked hammer, connection tool, paint tool, weld tool, lift, handbook, potato weapons, Chapter 2 scrap spudgun, potato launcher, and clay gun.
- Complete grouped held-item geometry for blocks, parts, buckets and their contents, glowsticks, cornades, loose clay, extinguisher, seed packets, fertilizer, every food and drink, feeder food, soil bags, keycards, power cores, resources, carried objects, and the logbook.
- VR-hand action origins for throwing, spraying, placing, targeting, inserting, dropping, eating, and using held items.
- Physical hammer swings and VR-aimed gunfire from each tracked weapon barrel.
- Calibrated Chapter 2 clay-gun grip and moving parts, with an included live calibration helper.
- World-locked startup and in-game spatial menus using Scrap Mechanic's live native UI.
- Right-hand menu laser, native hover/click/hold/drag input, and smooth transparent menu presentation.
- Restrained controller haptics for UI, tools, weapons, interactions, and recentering.
- Normal PC mode keeps the standard first-person viewmodel; VR mode removes it.
- Survival, Creative, and Custom Games share the same tracked-hand, held-item, and spatial-menu integration.
- Chapter 2 VR Mod branding on both the desktop and VR main menus.
- Guarded one-file installation with automatic hash verification, backups, version migration, and complete restore.

## Controls

| Quest / Valve Index control | Action |
| --- | --- |
| Left stick | Move |
| Right stick | Turn; scroll an open menu |
| Quest A / Index right A | Jump; click, hold, or drag in a menu |
| Quest B / Index right B | Use / interact |
| Right trigger | Primary action |
| Left trigger | Secondary action |
| Quest X / Y or Index left A / B while standing | Previous / next hotbar item |
| Quest X + Y or Index left A + B while standing | Open backpack |
| Quest X / Y or Index left A / B while seated | Zoom in / out |
| Quest left menu / Index left trackpad press | Pause / resume |
| Hold both thumbsticks for one second | Recenter view and floor |
| Optical pinch | Primary tool or menu interaction |
| Physical hammer swing | Hammer attack |

## Requirements

- Windows 10 or Windows 11, x64.
- Steam Scrap Mechanic `1.0.5.876`, Steam build `24529696`.
- A 64-bit OpenXR runtime, such as Meta Quest Link, Virtual Desktop VDXR, or SteamVR OpenXR.

## Legacy game version

Looking for the mod made for Scrap Mechanic before Chapter 2 / 1.0? It is preserved on the **[legacy-pre-1.0 branch](https://github.com/21Suspect/Scrap-Mechanic-Native-VR/tree/legacy-pre-1.0)**.

Do not mix files or installers from the legacy and current builds.

## Build and support

Build instructions are in [BUILD.md](BUILD.md). Installer and payload checksums are recorded in [SHA256SUMS.txt](SHA256SUMS.txt).

If you enjoy the project, you can [buy 21Suspect a coffee](https://buymeacoffee.com/21suspect).

This is an unofficial community project and is not affiliated with Axolot Games, Meta, Valve, Khronos, or ReShade. Original project code is under the [MIT License](LICENSE); third-party and Scrap Mechanic-derived assets retain their respective rights. See [LEGAL.md](LEGAL.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
