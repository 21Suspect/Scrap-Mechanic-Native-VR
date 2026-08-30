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

set(SMVR_RUNTIME_SOURCES
  src/native_vr.cpp
  src/feature_engine_input.cpp
  src/feature_input.cpp
  src/feature_startup_menu.cpp
  source/NativeVR/src/custom_content_bridge.cpp
  source/NativeVR/src/vr_hands.cpp
  source/NativeVR/src/vr_tools.cpp
)

# VR pointer input must enter through Scrap Mechanic's private event queue.
# Fail configuration if a Windows mouse-injection path is ever reintroduced.
set(SMVR_FORBIDDEN_MOUSE_TOKENS
  "INPUT_MOUSE"
  "MOUSEEVENTF_"
  "mouse_event("
  "SetCursorPos("
  "PostMessageW("
  "PostMessageA("
  "WM_MOUSEMOVE"
  "WM_LBUTTON"
  "WM_RBUTTON"
  "WM_MBUTTON"
  "WM_XBUTTON"
  "WM_MOUSEWHEEL"
)
foreach(source IN LISTS SMVR_RUNTIME_SOURCES)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${source}" source_text)
  foreach(token IN LISTS SMVR_FORBIDDEN_MOUSE_TOKENS)
    string(FIND "${source_text}" "${token}" token_offset)
    if(NOT token_offset EQUAL -1)
      message(FATAL_ERROR
        "Windows mouse simulation is forbidden: found '${token}' in ${source}")
    endif()
  endforeach()
endforeach()

add_library(smvr_native_vr_v1 SHARED ${SMVR_RUNTIME_SOURCES})
target_include_directories(smvr_native_vr_v1 PRIVATE
  source/NativeVR/src
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
  minhook d3d11 dxgi windowscodecs ole32
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openxr/lib/libopenxr_loader.dll.a"
)
set_target_properties(smvr_native_vr_v1 PROPERTIES
  PREFIX ""
  SUFFIX ".addon64"
  OUTPUT_NAME "smvr_native_vr_v1"
)
