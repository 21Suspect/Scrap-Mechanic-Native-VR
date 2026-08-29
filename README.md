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
4. Connect Quest Link, then use the new **Start Scrap Mechanic VR - Chapter 2** shortcut.

The installer automatically finds Steam, verifies the supported game build, creates backups, and includes Verify, Repair, Start, and Uninstall actions. It is currently unsigned, so Windows SmartScreen may ask for confirmation.

## Features

- Native stereo OpenXR rendering at the headset-recommended `2064 × 2272` resolution per eye.
- Six-degree-of-freedom head tracking, correct stereo depth and perspective, exact runtime FOV, and the VR seam fix.
- Quest Touch controllers and optional Meta Quest optical hand tracking.
- Tracked mechanic gloves with animated fingers and no artificial arms.
- VR locomotion, turning, jump, crouch, sprint, interaction, hotbar controls, seated zoom, pause, and recentering.
- Tracked hammer, connection tool, paint tool, weld tool, lift, potato weapons, Chapter 2 scrap spudgun, potato launcher, and clay gun.
- Physical hammer swings and VR-aimed gunfire from each tracked weapon barrel.
- Calibrated Chapter 2 clay-gun grip and moving parts, with an included live calibration helper.
- World-locked startup and in-game spatial menus using Scrap Mechanic's live native UI.
- Right-hand menu laser, native hover/click/hold/drag input, and smooth transparent menu presentation.
- Restrained controller haptics for UI, tools, weapons, interactions, and recentering.
- Normal PC mode keeps the standard first-person viewmodel; VR mode removes it.
- Chapter 2 VR Mod branding on both the desktop and VR main menus.
- Guarded one-file installation with hash verification, repair, backups, and complete restore.

## Controls

| Quest control | Action |
| --- | --- |
| Left stick | Move |
| Right stick | Turn; scroll an open menu |
| A | Jump; click, hold, or drag in a menu |
| B | Use / interact |
| Right trigger | Primary action |
| Left trigger | Secondary action |
| X / Y while standing | Previous / next hotbar item |
| X + Y while standing | Open backpack |
| X / Y while seated | Zoom in / out |
| Left controller menu button | Pause / resume |
| Hold both thumbsticks for one second | Recenter view and floor |
| Optical pinch | Primary tool or menu interaction |
| Physical hammer swing | Hammer attack |

## Requirements

- Windows 10 or Windows 11, x64.
- Steam Scrap Mechanic `1.0.5.876`, Steam build `24529696`.
- A 64-bit OpenXR runtime. Meta Quest 3 with Quest Link is the tested setup.

## Legacy game version

Looking for the mod made for Scrap Mechanic before Chapter 2 / 1.0? It is preserved on the **[legacy-pre-1.0 branch](https://github.com/21Suspect/Scrap-Mechanic-Native-VR/tree/legacy-pre-1.0)**.

Do not mix files or installers from the legacy and current builds.

## Build and support

Build instructions are in [BUILD.md](BUILD.md). Installer and payload checksums are recorded in [SHA256SUMS.txt](SHA256SUMS.txt).

If you enjoy the project, you can [buy 21Suspect a coffee](https://buymeacoffee.com/21suspect).

This is an unofficial community project and is not affiliated with Axolot Games, Meta, Valve, Khronos, or ReShade. Original project code is under the [MIT License](LICENSE); third-party and Scrap Mechanic-derived assets retain their respective rights. See [LEGAL.md](LEGAL.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
