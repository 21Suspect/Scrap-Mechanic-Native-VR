# Native add-on source

This is the complete source used to build `payload/Release/scrap_native_vr.addon64`.

- `src/addon.cpp`: ReShade add-on entry points, frame lifecycle, desktop mirror, and OpenXR bootstrap.
- `src/vr_runtime.*`: OpenXR session, eye views, controllers, hand tracking, input, recentering, transparent UI-only capture/interaction, and in-eye panel composition.
- `src/engine_hooks.*`: build-guarded Scrap Mechanic renderer/camera hooks and hand-state publication.
- `src/vr_hands.*`: native articulated hand rendering and the baked Touch-controller grip transform shared by hands, tools, and gameplay rays.
- `src/vr_tools.*`: native held tools, lighting, laser rendering, and Gatling animation.
- `src/mechanic_hands_asset.hpp` and `src/native_tool_asset.hpp`: generated embedded render data.
- `third_party`: pinned ReShade/OpenXR headers and licenses.
- `tools`: the two asset generators used to regenerate the embedded hand and tool meshes.

Run the distribution-root `Get-Dependencies.ps1`, then `Build-And-Deploy.ps1`. All engine detours are specific to executable SHA-256 `9D8B5F97171413DB1C284FC36B983173CB08E272AF3DF427F7DCDA360A3FD1FE`; do not weaken those guards for another build.
