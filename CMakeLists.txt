cmake_minimum_required(VERSION 3.24)
project(smvr_native_vr_chapter2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde64.c
)
target_include_directories(minhook
  PUBLIC third_party/minhook/include
  PRIVATE third_party/minhook/src third_party/minhook/src/hde
)

add_library(smvr_native_vr_v1 SHARED src/native_vr.cpp)
target_include_directories(smvr_native_vr_v1 PRIVATE
  third_party/reshade
  third_party/openxr/include
  third_party/minhook/include
)
target_compile_definitions(smvr_native_vr_v1 PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX
  XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11
  RESHADE_API_VERSION=18
)
target_compile_options(smvr_native_vr_v1 PRIVATE -Wall -Wextra)
target_link_libraries(smvr_native_vr_v1 PRIVATE
  minhook d3d11 dxgi
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openxr/lib/libopenxr_loader.dll.a"
)
set_target_properties(smvr_native_vr_v1 PROPERTIES
  PREFIX ""
  SUFFIX ".addon64"
  OUTPUT_NAME "smvr_native_vr_v1"
)
