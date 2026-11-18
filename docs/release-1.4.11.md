# Scrap Mechanic VR 1.4.11

Supports Scrap Mechanic **1.0.7a.903**, public Steam build **26702088**. Use the default Steam branch.

- Updated the renderer, camera, and interaction hooks for the public executable.
- Updated controller keyboard and mouse input for the hotfix's input queue, including menu scrolling.
- Rebased Creative mode on the public scripts so the new `/unanchor` command remains available.

The first launch after installing may take longer while the game rebuilds its cache.

Tested on the public Steam build. The installer self-test and in-headset smoke test passed, and the game log confirmed successful stereo frame submission through Meta OpenXR.
