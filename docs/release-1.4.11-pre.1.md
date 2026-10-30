# Scrap Mechanic VR 1.4.11-pre.1 — Patch 1.0.7 Test

Prerelease for **Scrap Mechanic 1.0.7.894, Steam test build 26302409**. Select the game's **test** branch in Steam before installing. This does not replace the stable v1.4.10 release for 1.0.6.

- Updated the native rendering, camera, interaction-ray and controller-input bindings for 1.0.7.
- Adapted the input-event structure to the game's IME changes, including menu scrolling. Build and instruction checks remain enabled.
- Rebased the VR scripts onto 1.0.7, retaining the updated seat, elevator, carry, player, planter and logbook behavior alongside seated VR firing and tracked-hand actions.
- Invalidates the compiled game-data cache so 1.0.7 rebuilds it from the installed game and VR files on the first launch. No older cache seed is bundled.
- Fixed upgrade restoration so an older mod backup cannot overwrite a current game original installed by Steam.

Close Scrap Mechanic before installing. The first launch may take longer while the cache rebuilds. If Steam has not restored the required current game files, the installer will identify them and request verification before continuing.

The existing Quest/Meta rendering path, controller bindings, hand/tool lighting and late hand-pose sampling are retained. This prerelease needs headset testing on 1.0.7; please report the headset/runtime and attach the VR log if anything fails. Known lighting limitations remain approximate cover shading and no colored creation-headlight illumination on VR hands/tools.

Validation: native build and projection tests, executable hook/input-layout checks, Lua syntax and lighting checks, seven shader compilations, fertilizer-effect ownership regression, and isolated install/verify/uninstall with settings preservation and cross-build upgrade checks. An actual game/headset startup has not yet been verified for this prerelease.
