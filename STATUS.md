# Chapter 2 visual snapshot status

Snapshot name: `chapter2-v0.1.0-test`

## Evidence levels reached

1. Renderer hook reached: yes.
2. Engine scene target identified and acquired: yes.
3. Two engine scene renders completed per submitted VR frame: yes in diagnostics.
4. Both OpenXR eye images rendered and released: yes in diagnostics.
5. `xrEndFrame` returned `XR_SUCCESS`: yes in current diagnostics.
6. Human visual confirmation in Quest 3: yes for stereo, depth, perspective, tracking, world effects, color, resolution, and VR-only viewmodel removal.

The snapshot does not claim that unconfirmed items work. In particular, headset fullscreen/windowed switching remains unconfirmed even though the stale swapchain-reference fix passed PC-only transitions.

## Rendering architecture

- Exactly two high-level engine scene renders per OpenXR frame: left and right.
- No native/neutral third scene render.
- PC mirror uses the already-rendered left-eye texture when available.
- OpenXR eye swapchains use the runtime-recommended `2064 × 2272` extent and an sRGB format.
- The engine offscreen source is centered at `2565 × 2711`; each eye is cropped to the runtime FOV without adding a rotational guard band.
- The game user's desktop resolution setting is not rewritten for the VR resolution.
- The first-person viewmodel branch is patched only while OpenXR reports `shouldRender=true`, and restored on stop, idle, loss, disable, and add-on destruction.

## Preserved hashes

- Source `src/native_vr.cpp`: `678F882BBE9DED0611D2EA01B2DF8DDC4F4BC391D258F499B50CC7CCB92C383A`
- Installed/tested add-on: `7CFB36E13C824641D3088213BF5C632FC91E3D99C3AE4993878FF66CD7B387F8`
- ReShade 6.7.3 DXGI proxy: `EC9245D05C11751F2AC0D2256E6921AD8FB36BE9172EF6D587856591EB729A25`
- OpenXR loader 1.1.60: `018C6519AFBDEADE6DA9E7D59C406068DD58674D87A65AE27353484A05E6674A`
- LLVM-MinGW `libc++.dll`: `952B41B1370DDB5E7299CC9FCEFCE2AD8C2021409948B057EC4D903C03760B6C`
- LLVM-MinGW `libunwind.dll`: `8D9F60801F50C0A6CB7BEB7FD2E121F98BF39EBE40E6C7E07A8A39FCCB817C03`

`SHA256SUMS.txt` is the machine-readable payload checksum set.
