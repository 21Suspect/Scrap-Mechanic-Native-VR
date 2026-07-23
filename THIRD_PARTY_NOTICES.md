# Third-party notices

The native project includes or interoperates with the following components. Their licenses apply independently of the project's MIT-licensed original code.

## ReShade 6.7.3

- Project: https://reshade.me/ and https://github.com/crosire/reshade
- Included: API headers and 64-bit loader (`payload/Release/dxgi.dll`)
- Binary SHA-256: `EC9245D05C11751F2AC0D2256E6921AD8FB36BE9172EF6D587856591EB729A25`
- License: `source/NativeVR/third_party/RESHade-LICENSE.md`

## Khronos OpenXR

- Project: https://github.com/KhronosGroup/OpenXR-SDK
- Included: OpenXR API headers and 64-bit OpenXR loader 1.0.6 (`payload/Release/openxr_loader.dll`)
- Binary SHA-256: `F7D6EB54C79BD923E9F008B81B89D4B0B5893FD33599E9591FF62BECBA936DAC`
- License: `source/NativeVR/third_party/OpenXR-LICENSE`

## Zig 0.16.0

- Project: https://ziglang.org/
- Use: pinned native build tool downloaded by `Get-Dependencies.ps1` after checksum verification
- Distribution: not embedded in the repository release installer

## Microsoft redistributable components

Microsoft's `d3dcompiler_47.dll` is used at runtime to compile the small hand/tool shaders. The patcher does not replace the tested game's existing copy when its hash matches.

## Scrap Mechanic-derived compatibility content

Generated mechanic-hand and tool geometry plus compatibility scripts are derived from content in a user's installed copy of Scrap Mechanic. Scrap Mechanic and its assets remain the property of Axolot Games or their respective rights holders and are not relicensed under MIT. The mod requires a legitimate copy of Scrap Mechanic and cannot run independently.

See `LEGAL.md` and Axolot's official modding guidelines: https://scrapmechanic.com/modding-guidelines
