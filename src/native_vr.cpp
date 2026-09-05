#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <reshade.hpp>
#include <MinHook.h>

#include "feature_input.hpp"
#include "feature_launch_retry.hpp"
#include "feature_startup_menu.hpp"
#include "custom_content_bridge.hpp"
#include "vr_hands.hpp"
#include "vr_tools.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

static_assert(RESHADE_API_VERSION == 18, "SMVR v1 requires ReShade API 18");

namespace smvr
{
constexpr uintptr_t kRenderSetupRva = 0x858E20;
constexpr uintptr_t kCameraBuildRva = 0x903F70;
constexpr uintptr_t kFirstPersonCameraCallerRva = 0x861320;
constexpr uintptr_t kViewmodelSkipBranchRva = 0x861324;
constexpr uintptr_t kRaycastRva = 0x478340;
constexpr uintptr_t kPlayerToolRaycastReturnRva = 0x456FF2;
constexpr uintptr_t kRaycastResultFractionStoreRva = 0x79B3F8;
constexpr uint32_t kGameImageSize = 0x1C1D000;
constexpr uint32_t kGameTimestamp = 0x6A7060DD;
constexpr uintptr_t kFrameRendererOffset = 0x2A0;
constexpr uintptr_t kLowRendererOffset = 0x8;
constexpr uintptr_t kDeviceOffset = 0x2F0;
constexpr uintptr_t kContextOffset = 0x2F8;
constexpr uintptr_t kBackbufferOffset = 0x360;
constexpr uintptr_t kFrameRenderTargetsOffset = 0x11538;
constexpr uintptr_t kMainFrameTargetOffset = 0x150;
constexpr uintptr_t kPresentFrameTargetOffset = 0xc8;
constexpr uintptr_t kTextureWrapperLowOffset = 0x8;
constexpr uintptr_t kTextureLowNativeOffset = 0x20;
constexpr uintptr_t kRenderWidthOffset = 0x4c;
constexpr uintptr_t kRenderHeightOffset = 0x50;
constexpr uintptr_t kRenderScaleOffset = 0x94;
constexpr uintptr_t kReportedWidthOffset = 0x8;
constexpr uintptr_t kReportedHeightOffset = 0xc;
constexpr uint8_t kRenderPrefix[] = {
    0x48,0x8B,0xC4,0x4C,0x89,0x48,0x20,0x4C,0x89,0x40,0x18,0x53,0x56,0x57,0x41,0x55
};
constexpr uint8_t kCameraBuildPrefix[] = {
    0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8D,0xA8,0xA8,0xFE,0xFF,0xFF
};
constexpr uint8_t kViewmodelConditionalBranch[] = { 0x0F,0x84,0x58,0x03,0x00,0x00 };
constexpr uint8_t kViewmodelUnconditionalSkip[] = { 0xE9,0x59,0x03,0x00,0x00,0x90 };
constexpr uint8_t kRaycastPrefix[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20
};
constexpr uint8_t kPlayerToolRaycastCall[] = {
    0x48,0x8D,0x87,0x30,0x04,0x00,0x00,0x48,0x89,0x44,0x24,0x28,
    0x4C,0x89,0x6C,0x24,0x20,0x4C,0x8D,0x4C,0x24,0x68,0x4C,0x8D,
    0x44,0x24,0x58,0x48,0x8B,0xCB,0xE8,0x4E,0x13,0x02,0x00
};
constexpr uint8_t kRaycastResultFractionStore[] = {
    0x8B,0x47,0x08,0x89,0x83,0xF0,0x00,0x00,0x00
};

using RenderSetupFn = void (__fastcall *)(void *, float, const float *, const float *, void *);
using CameraBuildFn = void (__fastcall *)(void *, const float *, const float *, float, float, float);
using RaycastFn = uint8_t (__fastcall *)(void *, void *, const float *, const float *, void *, void *);

// Scrap Mechanic executes equipped tools on independent Lua Logic Tasks. The
// engine's sm.json.open data cache is therefore not a safe transport for the
// frame-current projectile pose: a task can retain the first inactive packet
// even while the native bridge file advances on disk. Register a tiny read-only
// native Lua function in every task instead. This keeps projectile origin and
// direction tied to the exact tracked pose available when the tool fires.
struct lua_State;
using LuaCFunction = int (__cdecl *)(lua_State *);
using LuaPCallFn = int (__cdecl *)(lua_State *, int, int, int);
using LuaCallFn = void (__cdecl *)(lua_State *, int, int);
using LuaGetFieldFn = void (__cdecl *)(lua_State *, int, const char *);
using LuaLNewStateFn = lua_State * (__cdecl *)();
using LuaLLoadBufferXFn = int (__cdecl *)(lua_State *, const char *, size_t, const char *, const char *);
using LuaGetTopFn = int (__cdecl *)(lua_State *);
using LuaSetTopFn = void (__cdecl *)(lua_State *, int);
using LuaGetFenvFn = void (__cdecl *)(lua_State *, int);
using LuaTypeFn = int (__cdecl *)(lua_State *, int);
using LuaToPointerFn = const void * (__cdecl *)(lua_State *, int);
using LuaToBooleanFn = int (__cdecl *)(lua_State *, int);
using LuaToNumberFn = double (__cdecl *)(lua_State *, int);
using LuaPushCClosureFn = void (__cdecl *)(lua_State *, LuaCFunction, int);
using LuaPushNumberFn = void (__cdecl *)(lua_State *, double);
using LuaPushBooleanFn = void (__cdecl *)(lua_State *, int);
using LuaPushStringFn = void (__cdecl *)(lua_State *, const char *);
using LuaSetFieldFn = void (__cdecl *)(lua_State *, int, const char *);

constexpr int kLuaGlobalsIndex = -10002;
constexpr int kLuaTypeTable = 5;
constexpr char kLuaProjectilePoseFunction[] = "ScrapVRProjectilePoseNative";
constexpr char kLuaActionPoseFunction[] = "ScrapVRActionPoseNative";
constexpr char kLuaConnectionTargetFunction[] = "ScrapVRConnectionTargetNative";

HMODULE g_module = nullptr;
HMODULE g_game = nullptr;
uintptr_t g_game_base = 0;
std::atomic<void *> g_render_original{nullptr};
std::atomic<void *> g_camera_build_original{nullptr};
std::atomic<void *> g_raycast_original{nullptr};
std::atomic<void *> g_lua_pcall_original{nullptr};
std::atomic<void *> g_lua_call_original{nullptr};
std::atomic<void *> g_lua_getfield_original{nullptr};
std::atomic<void *> g_lua_newstate_original{nullptr};
std::atomic<void *> g_lua_loadbufferx_original{nullptr};
void *g_lua_pcall_target = nullptr;
void *g_lua_call_target = nullptr;
void *g_lua_getfield_target = nullptr;
void *g_lua_newstate_target = nullptr;
void *g_lua_loadbufferx_target = nullptr;
LuaGetTopFn g_lua_gettop = nullptr;
LuaSetTopFn g_lua_settop = nullptr;
LuaGetFenvFn g_lua_getfenv = nullptr;
LuaTypeFn g_lua_type = nullptr;
LuaToPointerFn g_lua_topointer = nullptr;
LuaToBooleanFn g_lua_toboolean = nullptr;
LuaToNumberFn g_lua_tonumber = nullptr;
LuaPushCClosureFn g_lua_pushcclosure = nullptr;
LuaPushNumberFn g_lua_pushnumber = nullptr;
LuaPushBooleanFn g_lua_pushboolean = nullptr;
LuaPushStringFn g_lua_pushstring = nullptr;
LuaSetFieldFn g_lua_setfield = nullptr;
std::atomic<ID3D11Device *> g_device{nullptr};
std::atomic<ID3D11DeviceContext *> g_context{nullptr};
std::atomic<ID3D11Texture2D *> g_final_target{nullptr};
std::atomic<uint64_t> g_backbuffer_handle{0};
std::atomic<IUnknown *> g_backbuffer_identity{nullptr};
std::atomic<reshade::api::device *> g_game_api_device{nullptr};
std::atomic<reshade::api::swapchain *> g_game_api_swapchain{nullptr};
std::atomic<bool> g_game_device_pinned{false};
std::atomic<uint64_t> g_observed_d3d11_devices{0};
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_hook_reached{false};
std::atomic<bool> g_target_found{false};
std::atomic<bool> g_failed{false};
constexpr uint64_t kOpenXrRetryDelayMs = 5000;
constexpr uint64_t kExplicitVrLaunchRetryWindowMs = 180000;
constexpr uint64_t kVrLaunchMarkerMaximumAge100ns = 300ull * 10000000ull;
std::atomic<uint64_t> g_openxr_retry_after_ms{0};
features::LaunchRetryWindow g_explicit_vr_launch{kExplicitVrLaunchRetryWindowMs};
std::atomic<bool> g_desktop_fallback_logged{false};
std::atomic<bool> g_openxr_runtime_logged{false};
std::atomic<bool> g_openxr_unavailable_logged{false};
std::atomic<uint64_t> g_engine_frames{0};
std::atomic<uint64_t> g_stereo_frames{0};
std::atomic<uint64_t> g_openxr_success_frames{0};
std::atomic<bool> g_camera_abi_logged{false};
std::atomic<bool> g_vr_upright_camera_logged{false};
std::atomic<uint64_t> g_camera_build_calls{0};
std::atomic<uint64_t> g_vr_camera_diagnostic_calls{0};
std::atomic<uint64_t> g_viewmodel_camera_hides{0};
std::atomic<bool> g_viewmodel_camera_hide_logged{false};
std::atomic<bool> g_viewmodel_pass_patched{false};
std::atomic<bool> g_vr_target_tracking_suppressed_logged{false};
std::atomic<bool> g_vr_target_tracking_locked{false};
std::atomic<uint64_t> g_vr_target_reacquires{0};
std::atomic<uint64_t> g_mirror_present_failures{0};
std::atomic<bool> g_mirror_present_failure_logged{false};
std::atomic<void *> g_last_render_manager{nullptr};
std::atomic<bool> g_target_wrapper_diagnostic_logged{false};
std::atomic<bool> g_highres_pc_probe_requested{false};
std::atomic<bool> g_highres_pc_probe_done{false};
bool g_feature_input_enabled = true;
bool g_feature_optical_hands_enabled = true;
bool g_feature_hands_enabled = true;
bool g_feature_startup_menu_enabled = true;
std::atomic<bool> g_hands_render_failure_logged{false};

void mark_openxr_failed(bool retry = true)
{
    g_vr_target_tracking_locked.store(false, std::memory_order_release);
    g_openxr_retry_after_ms.store(retry ? GetTickCount64() + kOpenXrRetryDelayMs : 0,
        std::memory_order_release);
    g_failed.store(true, std::memory_order_release);
}
uint64_t g_hand_bridge_sequence = 0;
uint64_t g_last_hand_bridge_publish_ms = 0;
std::atomic<bool> g_hand_bridge_logged{false};
std::atomic<bool> g_hand_bridge_inactive_published{false};
std::atomic<bool> g_hand_bridge_inactive_authoritative{false};
bool g_previous_right_interaction = false;
bool g_previous_gun_muzzle_active = false;
std::string g_previous_gun_muzzle_item;
bool g_right_hand_world_active = false;
XrVector3f g_right_hand_world{};
XrVector3f g_right_hand_forward{};
XrVector3f g_right_hand_up{};
uint64_t g_right_hand_world_ms = 0;
bool g_right_aim_world_active = false;
XrVector3f g_right_aim_world{};
XrVector3f g_right_aim_forward{};
XrVector3f g_right_aim_up{};
uint64_t g_right_aim_world_ms = 0;
std::atomic<float> g_right_action_target_distance{0.0f};
std::atomic<uint64_t> g_right_action_target_ms{0};
std::atomic<float> g_interaction_laser_target_distance{0.0f};
std::atomic<uint64_t> g_interaction_laser_target_ms{0};
std::atomic<uint32_t> g_interaction_laser_target_kind{0};
struct NativeProjectilePose
{
    bool authoritative = false;
    bool active = false;
    XrVector3f position{};
    XrVector3f direction{0.0f, 0.0f, -1.0f};
    std::string item;
    uint64_t sequence = 0;
};
std::mutex g_native_projectile_mutex;
NativeProjectilePose g_native_projectile_pose;
struct NativeActionPose
{
    bool authoritative = false;
    bool active = false;
    XrVector3f position{};
    XrVector3f direction{0.0f, 0.0f, -1.0f};
    XrVector3f up{0.0f, 0.0f, 1.0f};
    uint64_t sequence = 0;
};
std::mutex g_native_action_mutex;
NativeActionPose g_native_action_pose;
std::mutex g_lua_state_mutex;
std::vector<lua_State *> g_lua_registered_states;
std::vector<const void *> g_lua_registered_environments;
std::atomic<bool> g_lua_projectile_registration_logged{false};
std::atomic<bool> g_lua_projectile_call_logged{false};
std::atomic<bool> g_lua_action_call_logged{false};
std::atomic<bool> g_tool_raycast_logged{false};
std::atomic<bool> g_use_raycast_logged{false};
thread_local int g_active_eye = -1;
thread_local uint32_t g_eye_camera_build_index = 0;
std::mutex g_log_mutex;
std::mutex g_xr_mutex;
HANDLE g_log = INVALID_HANDLE_VALUE;
std::wstring g_ini_path;
features::InputBridge g_input;
features::StartupMenuUi g_startup_menu;

void log_line(const char *format, ...)
{
    char line[1024]{};
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(line, sizeof(line) - 3, format, args);
    va_end(args);
    if (n < 0) return;
    const size_t used = static_cast<size_t>(n) < sizeof(line) - 3 ? static_cast<size_t>(n) : sizeof(line) - 3;
    line[used] = '\r'; line[used + 1] = '\n'; line[used + 2] = 0;
    std::lock_guard lock(g_log_mutex);
    if (g_log == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(g_log, line, static_cast<DWORD>(used + 2), &written, nullptr);
}

void log_matrix(const char *name, const float *m)
{
    if (!m) return;
    log_line("%s=[%.6f,%.6f,%.6f,%.6f;%.6f,%.6f,%.6f,%.6f;%.6f,%.6f,%.6f,%.6f;%.6f,%.6f,%.6f,%.6f]",
        name, m[0],m[1],m[2],m[3], m[4],m[5],m[6],m[7],
        m[8],m[9],m[10],m[11], m[12],m[13],m[14],m[15]);
}

features::BindingInput parse_binding_input(const wchar_t *section, const wchar_t *key,
                                           features::BindingInput fallback)
{
    wchar_t value[64]{};
    const DWORD length = GetPrivateProfileStringW(section, key, L"", value,
        static_cast<DWORD>(std::size(value)), g_ini_path.c_str());
    if (length == 0 || length >= std::size(value)) return fallback;

    std::wstring name(value, length);
    size_t first = 0;
    while (first < name.size() && std::iswspace(name[first])) ++first;
    size_t last = name.size();
    while (last > first && std::iswspace(name[last - 1])) --last;
    name = name.substr(first, last - first);
    for (wchar_t &character : name)
    {
        if (character == L'-' || character == L' ' || character == L'.') character = L'_';
        character = static_cast<wchar_t>(std::towlower(character));
    }

    using features::BindingInput;
    if (name == L"none" || name == L"disabled" || name == L"off") return BindingInput::none;
    if (name == L"left_primary" || name == L"left_a") return BindingInput::left_primary;
    if (name == L"right_primary" || name == L"right_a") return BindingInput::right_primary;
    if (name == L"left_secondary" || name == L"left_b") return BindingInput::left_secondary;
    if (name == L"right_secondary" || name == L"right_b") return BindingInput::right_secondary;
    if (name == L"left_stick_click" || name == L"left_thumbstick_click") return BindingInput::left_stick_click;
    if (name == L"right_stick_click" || name == L"right_thumbstick_click") return BindingInput::right_stick_click;
    if (name == L"left_grip" || name == L"left_squeeze") return BindingInput::left_grip;
    if (name == L"right_grip" || name == L"right_squeeze") return BindingInput::right_grip;
    if (name == L"left_trigger") return BindingInput::left_trigger;
    if (name == L"right_trigger") return BindingInput::right_trigger;
    if (name == L"left_menu" || name == L"menu") return BindingInput::left_menu;
    if (name == L"right_menu") return BindingInput::right_menu;
    if (name == L"left_trackpad" || name == L"left_trackpad_click") return BindingInput::left_trackpad_click;
    if (name == L"right_trackpad" || name == L"right_trackpad_click") return BindingInput::right_trackpad_click;
    if (name == L"left_system" || name == L"system") return BindingInput::left_system;
    if (name == L"right_system") return BindingInput::right_system;

    log_line("VR_INPUT_BINDING_INVALID section=%ls key=%ls value=%ls using_default=1",
        section, key, value);
    return fallback;
}

features::ControllerBindings load_controller_bindings(const wchar_t *section,
                                                       const features::ControllerBindings &defaults)
{
    features::ControllerBindings bindings = defaults;
    bindings.menu = parse_binding_input(section, L"Menu", bindings.menu);
    bindings.sprint = parse_binding_input(section, L"Sprint", bindings.sprint);
    bindings.crouch = parse_binding_input(section, L"Crouch", bindings.crouch);
    bindings.jump = parse_binding_input(section, L"Jump", bindings.jump);
    bindings.use = parse_binding_input(section, L"Use", bindings.use);
    bindings.context = parse_binding_input(section, L"Context", bindings.context);
    bindings.hotbar_previous = parse_binding_input(section, L"HotbarPrevious", bindings.hotbar_previous);
    bindings.hotbar_next = parse_binding_input(section, L"HotbarNext", bindings.hotbar_next);
    bindings.inventory = parse_binding_input(section, L"Inventory", bindings.inventory);
    return bindings;
}

void consume_vr_launch_request()
{
    wchar_t local_app_data[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return;
    const std::wstring marker = std::wstring(local_app_data) +
        L"\\ScrapMechanicVR-Chapter2\\vr-launch-request.marker";
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(marker.c_str(), GetFileExInfoStandard, &attributes)) return;

    FILETIME current_time{};
    GetSystemTimeAsFileTime(&current_time);
    ULARGE_INTEGER current{}, modified{};
    current.LowPart = current_time.dwLowDateTime;
    current.HighPart = current_time.dwHighDateTime;
    modified.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    modified.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    FILETIME created{}, exited{}, kernel{}, user{};
    ULARGE_INTEGER process_start{};
    const bool have_process_start = GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE;
    process_start.LowPart = created.dwLowDateTime;
    process_start.HighPart = created.dwHighDateTime;
    // An add-on loaded late during cold startup still belongs to the game
    // process that was launched promptly after the user's Start VR request.
    const bool fresh = features::launch_marker_fresh(current.QuadPart, modified.QuadPart,
        have_process_start ? process_start.QuadPart : 0, kVrLaunchMarkerMaximumAge100ns);
    DeleteFileW(marker.c_str());
    if (!fresh)
    {
        log_line("VR_LAUNCH_HANDOFF ignored=1 reason=stale_marker");
        return;
    }
    // Cold game loading can take minutes before any 3D frame reaches OpenXR.
    // Start its retry budget at the first actual initialization attempt.
    g_explicit_vr_launch.request();
    log_line("VR_LAUNCH_HANDOFF active=1 retry_window_ms=%llu marker_consumed=1 budget_starts=first_xr_attempt",
        static_cast<unsigned long long>(kExplicitVrLaunchRetryWindowMs));
}

bool readable(const void *pointer, size_t size)
{
    if (!pointer || size == 0) return false;
    const auto *at = static_cast<const uint8_t *>(pointer);
    size_t left = size;
    while (left)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(at, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
        const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        const uintptr_t here = reinterpret_cast<uintptr_t>(at);
        if (end <= here) return false;
        const size_t available = static_cast<size_t>(end - here);
        if (available >= left) return true;
        at += available; left -= available;
    }
    return true;
}

bool write_executable_bytes(void *target, const uint8_t *bytes, size_t size)
{
    if (!target || !bytes || size == 0 || !readable(target, size)) return false;
    DWORD old_protect = 0;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old_protect)) return false;
    std::memcpy(target, bytes, size);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), target, size) != FALSE;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(target, size, old_protect, &ignored) != FALSE;
    return flushed && restored;
}

bool install_viewmodel_pass_patch()
{
    if (g_viewmodel_pass_patched.load(std::memory_order_acquire)) return true;
    if (!g_enabled.load(std::memory_order_acquire)) return true;
    auto *branch = reinterpret_cast<uint8_t *>(g_game_base + kViewmodelSkipBranchRva);
    if (!readable(branch, sizeof(kViewmodelConditionalBranch)) ||
        std::memcmp(branch, kViewmodelConditionalBranch, sizeof(kViewmodelConditionalBranch)) != 0)
        return false;
    if (!write_executable_bytes(branch, kViewmodelUnconditionalSkip, sizeof(kViewmodelUnconditionalSkip)))
        return false;
    g_viewmodel_pass_patched.store(true, std::memory_order_release);
    log_line("VR_VIEWMODEL_PASS_DISABLED rva=%llx method=skip_first_person_render_submission",
        static_cast<unsigned long long>(kViewmodelSkipBranchRva));
    return true;
}

bool restore_viewmodel_pass_patch()
{
    if (!g_viewmodel_pass_patched.exchange(false, std::memory_order_acq_rel)) return true;
    auto *branch = reinterpret_cast<uint8_t *>(g_game_base + kViewmodelSkipBranchRva);
    if (!readable(branch, sizeof(kViewmodelUnconditionalSkip)) ||
        std::memcmp(branch, kViewmodelUnconditionalSkip, sizeof(kViewmodelUnconditionalSkip)) != 0)
    {
        log_line("FAIL stage=restore_viewmodel_pass reason=bytes_changed");
        return false;
    }
    if (!write_executable_bytes(branch, kViewmodelConditionalBranch, sizeof(kViewmodelConditionalBranch)))
    {
        log_line("FAIL stage=restore_viewmodel_pass reason=write_failed");
        return false;
    }
    log_line("VR_VIEWMODEL_PASS_RESTORED rva=%llx",
        static_cast<unsigned long long>(kViewmodelSkipBranchRva));
    return true;
}

template <typename T> void release_atomic(std::atomic<T *> &slot)
{
    if (T *value = slot.exchange(nullptr, std::memory_order_acq_rel)) value->Release();
}

bool is_current_process_swapchain(reshade::api::swapchain *swapchain,
                                  const DXGI_SWAP_CHAIN_DESC &desc)
{
    if (!swapchain) return false;
    HWND window = static_cast<HWND>(swapchain->get_hwnd());
    if (!window) window = desc.OutputWindow;
    if (!window) return false;
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    return process_id == GetCurrentProcessId();
}

bool pin_game_device(reshade::api::device *api_device, const char *source)
{
    if (!api_device || api_device->get_api() != reshade::api::device_api::d3d11) return false;
    reshade::api::device *selected = g_game_api_device.load(std::memory_order_acquire);
    if (selected && selected != api_device) return false;

    auto *native = reinterpret_cast<ID3D11Device *>(static_cast<uintptr_t>(api_device->get_native()));
    if (!native) return false;
    native->AddRef();
    ID3D11DeviceContext *context = nullptr;
    native->GetImmediateContext(&context);
    if (!context)
    {
        native->Release();
        return false;
    }

    if (!selected)
    {
        reshade::api::device *expected = nullptr;
        if (!g_game_api_device.compare_exchange_strong(expected, api_device,
                std::memory_order_acq_rel, std::memory_order_acquire) && expected != api_device)
        {
            context->Release();
            native->Release();
            return false;
        }
    }

    ID3D11Device *old_device = g_device.exchange(native, std::memory_order_acq_rel);
    ID3D11DeviceContext *old_context = g_context.exchange(context, std::memory_order_acq_rel);
    if (old_context) old_context->Release();
    if (old_device) old_device->Release();
    const bool first_pin = !g_game_device_pinned.exchange(true, std::memory_order_acq_rel);
    if (first_pin)
        log_line("D3D11_GAME_DEVICE_PINNED source=%s api_device=%p device=%p context=%p",
            source ? source : "unknown", api_device, native, context);
    return true;
}

struct Mat4 { float m[16]{}; };

Mat4 multiply(const Mat4 &a, const Mat4 &b)
{
    Mat4 out{};
    for (int r = 0; r != 4; ++r)
        for (int c = 0; c != 4; ++c)
            for (int k = 0; k != 4; ++k)
                out.m[r * 4 + c] += a.m[r * 4 + k] * b.m[k * 4 + c];
    return out;
}

XrQuaternionf normalize(XrQuaternionf q)
{
    const float length = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (length <= 0.000001f) return {0,0,0,1};
    return {q.x/length, q.y/length, q.z/length, q.w/length};
}

XrQuaternionf conjugate(XrQuaternionf q) { return {-q.x,-q.y,-q.z,q.w}; }

XrQuaternionf yaw_only(XrQuaternionf q)
{
    q = normalize(q);
    const float sin_yaw = 2.0f * (q.w*q.y + q.x*q.z);
    const float cos_yaw = 1.0f - 2.0f * (q.y*q.y + q.z*q.z);
    const float half_yaw = std::atan2(sin_yaw, cos_yaw) * 0.5f;
    return {0.0f, std::sin(half_yaw), 0.0f, std::cos(half_yaw)};
}

XrQuaternionf quaternion_multiply_raw(XrQuaternionf a, XrQuaternionf b)
{
    return {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    };
}

XrQuaternionf quaternion_multiply(XrQuaternionf a, XrQuaternionf b) { return normalize(quaternion_multiply_raw(a, b)); }

XrVector3f rotate_vector(XrQuaternionf q, XrVector3f v)
{
    const XrQuaternionf p{v.x,v.y,v.z,0};
    const XrQuaternionf temp = quaternion_multiply_raw(q, p);
    const XrQuaternionf result = quaternion_multiply_raw(temp, conjugate(q));
    return {result.x,result.y,result.z};
}

bool world_to_view_matrix(const float *matrix);

XrVector3f view_point_to_world(const float world_to_view[16], const XrVector3f &point)
{
    const XrVector3f translated{
        point.x - world_to_view[12],
        point.y - world_to_view[13],
        point.z - world_to_view[14]
    };
    return {
        world_to_view[0] * translated.x + world_to_view[1] * translated.y + world_to_view[2] * translated.z,
        world_to_view[4] * translated.x + world_to_view[5] * translated.y + world_to_view[6] * translated.z,
        world_to_view[8] * translated.x + world_to_view[9] * translated.y + world_to_view[10] * translated.z
    };
}

XrVector3f view_vector_to_world(const float world_to_view[16], const XrVector3f &vector)
{
    return {
        world_to_view[0] * vector.x + world_to_view[1] * vector.y + world_to_view[2] * vector.z,
        world_to_view[4] * vector.x + world_to_view[5] * vector.y + world_to_view[6] * vector.z,
        world_to_view[8] * vector.x + world_to_view[9] * vector.y + world_to_view[10] * vector.z
    };
}

bool normalize_vector(XrVector3f &vector)
{
    const float length = std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
    if (!std::isfinite(length) || length < 0.0001f) return false;
    vector.x /= length; vector.y /= length; vector.z /= length;
    return true;
}

float world_heading_from_view(const float *world_to_view)
{
    if (!world_to_view_matrix(world_to_view)) return 0.0f;
    XrVector3f forward = view_vector_to_world(world_to_view, {0.0f, 0.0f, -1.0f});
    const float horizontal_length = std::sqrt(forward.x * forward.x + forward.y * forward.y);
    if (!std::isfinite(horizontal_length) || horizontal_length < 0.0001f) return 0.0f;
    // Scrap Mechanic uses +Z as world up and the X/Y plane for the ground.
    // Keep north at +Y and east at +X, using the normal clockwise compass
    // convention. Ignore camera pitch so looking up/down never changes the
    // displayed direction.
    forward.x /= horizontal_length;
    forward.y /= horizontal_length;
    return std::atan2(forward.x, forward.y);
}

float world_heading_from_view(const float *world_to_view,
                              const XrPosef &head_pose,
                              const XrPosef &reference_head)
{
    // The game camera matrix supplies the world-space heading (including mouse
    // and controller turning). Add only the headset's horizontal rotation since
    // the OpenXR pose itself is in tracking space, not Scrap Mechanic world
    // space. Reusing the same reference transform as the hand bridge makes the
    // recentered headset orientation the zero point and keeps pitch/roll out of
    // the compass calculation.
    if (!world_to_view_matrix(world_to_view)) return 0.0f;
    const XrQuaternionf inverse_reference = conjugate(normalize(reference_head.orientation));
    const XrQuaternionf relative_orientation = quaternion_multiply(
        inverse_reference, head_pose.orientation);
    const XrQuaternionf relative_yaw = yaw_only(relative_orientation);
    XrVector3f forward = view_vector_to_world(world_to_view,
        rotate_vector(relative_yaw, {0.0f, 0.0f, -1.0f}));
    const float horizontal_length = std::sqrt(forward.x * forward.x + forward.y * forward.y);
    if (!std::isfinite(horizontal_length) || horizontal_length < 0.0001f)
        return world_heading_from_view(world_to_view);
    forward.x /= horizontal_length;
    forward.y /= horizontal_length;
    return std::atan2(forward.x, forward.y);
}

XrVector3f cross_vector(const XrVector3f &a, const XrVector3f &b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

void set_native_projectile_pose(bool authoritative, bool active,
    const XrVector3f &position = {}, const XrVector3f &direction = {0.0f, 0.0f, -1.0f},
    const char *item = nullptr)
{
    std::lock_guard lock(g_native_projectile_mutex);
    g_native_projectile_pose.authoritative = authoritative;
    g_native_projectile_pose.active = active;
    g_native_projectile_pose.position = position;
    g_native_projectile_pose.direction = direction;
    g_native_projectile_pose.item = item ? item : "";
    ++g_native_projectile_pose.sequence;
}

void set_native_action_pose(bool authoritative, bool active,
    const XrVector3f &position = {}, const XrVector3f &direction = {0.0f, 0.0f, -1.0f},
    const XrVector3f &up = {0.0f, 0.0f, 1.0f})
{
    std::lock_guard lock(g_native_action_mutex);
    g_native_action_pose.authoritative = authoritative;
    g_native_action_pose.active = active;
    g_native_action_pose.position = position;
    g_native_action_pose.direction = direction;
    g_native_action_pose.up = up;
    ++g_native_action_pose.sequence;
}

void update_native_projectile_pose(bool right_active, bool projectile_tool_active,
    const XrVector3f &hand_position, XrVector3f forward, XrVector3f up,
    const XrVector3f &local_offset, const XrVector3f &local_direction, const char *item)
{
    // Rebuild the same orthonormal hand basis used by Chapter2VR.lua. Forward
    // is the controller/hand aim vector; local -Z therefore maps to forward.
    if (!right_active || !projectile_tool_active || !item || !normalize_vector(forward))
    {
        set_native_projectile_pose(true, false, {}, {}, item);
        return;
    }
    const float up_projection = up.x * forward.x + up.y * forward.y + up.z * forward.z;
    up.x -= forward.x * up_projection;
    up.y -= forward.y * up_projection;
    up.z -= forward.z * up_projection;
    if (!normalize_vector(up))
    {
        up = {0.0f, 0.0f, 1.0f};
        const float fallback_projection = up.x * forward.x + up.y * forward.y + up.z * forward.z;
        up.x -= forward.x * fallback_projection;
        up.y -= forward.y * fallback_projection;
        up.z -= forward.z * fallback_projection;
    }
    if (!normalize_vector(up))
    {
        set_native_projectile_pose(true, false, {}, {}, item);
        return;
    }
    XrVector3f right = cross_vector(forward, up);
    if (!normalize_vector(right))
    {
        set_native_projectile_pose(true, false, {}, {}, item);
        return;
    }
    const XrVector3f position{
        hand_position.x + right.x * local_offset.x + up.x * local_offset.y - forward.x * local_offset.z,
        hand_position.y + right.y * local_offset.x + up.y * local_offset.y - forward.y * local_offset.z,
        hand_position.z + right.z * local_offset.x + up.z * local_offset.y - forward.z * local_offset.z
    };
    XrVector3f direction{
        right.x * local_direction.x + up.x * local_direction.y - forward.x * local_direction.z,
        right.y * local_direction.x + up.y * local_direction.y - forward.y * local_direction.z,
        right.z * local_direction.x + up.z * local_direction.y - forward.z * local_direction.z
    };
    if (!normalize_vector(direction))
    {
        set_native_projectile_pose(true, false, {}, {}, item);
        return;
    }
    set_native_projectile_pose(true, true, position, direction, item);
}

int __cdecl lua_native_projectile_pose(lua_State *state)
{
    NativeProjectilePose pose;
    {
        std::lock_guard lock(g_native_projectile_mutex);
        pose = g_native_projectile_pose;
    }
    g_lua_pushboolean(state, pose.authoritative ? 1 : 0);
    g_lua_pushboolean(state, pose.active ? 1 : 0);
    g_lua_pushnumber(state, pose.position.x);
    g_lua_pushnumber(state, pose.position.y);
    g_lua_pushnumber(state, pose.position.z);
    g_lua_pushnumber(state, pose.direction.x);
    g_lua_pushnumber(state, pose.direction.y);
    g_lua_pushnumber(state, pose.direction.z);
    g_lua_pushstring(state, pose.item.c_str());
    g_lua_pushnumber(state, static_cast<double>(pose.sequence));
    if (!g_lua_projectile_call_logged.exchange(true))
        log_line("VR_LUA_PROJECTILE_BRIDGE_CALLED authoritative=%u active=%u item=%s",
            pose.authoritative ? 1u : 0u, pose.active ? 1u : 0u,
            pose.item.empty() ? "none" : pose.item.c_str());
    return 10;
}

int __cdecl lua_native_action_pose(lua_State *state)
{
    NativeActionPose pose;
    {
        std::lock_guard lock(g_native_action_mutex);
        pose = g_native_action_pose;
    }
    g_lua_pushboolean(state, pose.authoritative ? 1 : 0);
    g_lua_pushboolean(state, pose.active ? 1 : 0);
    g_lua_pushnumber(state, pose.position.x);
    g_lua_pushnumber(state, pose.position.y);
    g_lua_pushnumber(state, pose.position.z);
    g_lua_pushnumber(state, pose.direction.x);
    g_lua_pushnumber(state, pose.direction.y);
    g_lua_pushnumber(state, pose.direction.z);
    g_lua_pushnumber(state, pose.up.x);
    g_lua_pushnumber(state, pose.up.y);
    g_lua_pushnumber(state, pose.up.z);
    g_lua_pushnumber(state, static_cast<double>(pose.sequence));
    if (!g_lua_action_call_logged.exchange(true))
        log_line("VR_LUA_ACTION_BRIDGE_CALLED authoritative=%u active=%u",
            pose.authoritative ? 1u : 0u, pose.active ? 1u : 0u);
    return 12;
}

int __cdecl lua_native_connection_target(lua_State *state)
{
    const bool active = g_lua_toboolean(state, 1) != 0;
    const double distance = g_lua_tonumber(state, 2);
    if (active && std::isfinite(distance) && distance >= 0.05 && distance <= 8.0)
    {
        g_interaction_laser_target_distance.store(
            static_cast<float>(distance), std::memory_order_release);
        g_interaction_laser_target_kind.store(
            static_cast<uint32_t>(scrapvr::tools::InteractionLaserKind::connection),
            std::memory_order_release);
        g_interaction_laser_target_ms.store(GetTickCount64(), std::memory_order_release);
    }
    else if (g_interaction_laser_target_kind.load(std::memory_order_acquire) ==
        static_cast<uint32_t>(scrapvr::tools::InteractionLaserKind::connection))
    {
        g_interaction_laser_target_ms.store(0, std::memory_order_release);
    }
    return 0;
}

bool resolve_lua_projectile_api()
{
    HMODULE lua = GetModuleHandleW(L"lua51.dll");
    if (!lua) return false;
    g_lua_pcall_target = reinterpret_cast<void *>(GetProcAddress(lua, "lua_pcall"));
    g_lua_call_target = reinterpret_cast<void *>(GetProcAddress(lua, "lua_call"));
    g_lua_getfield_target = reinterpret_cast<void *>(GetProcAddress(lua, "lua_getfield"));
    g_lua_newstate_target = reinterpret_cast<void *>(GetProcAddress(lua, "luaL_newstate"));
    g_lua_loadbufferx_target = reinterpret_cast<void *>(GetProcAddress(lua, "luaL_loadbufferx"));
    g_lua_gettop = reinterpret_cast<LuaGetTopFn>(GetProcAddress(lua, "lua_gettop"));
    g_lua_settop = reinterpret_cast<LuaSetTopFn>(GetProcAddress(lua, "lua_settop"));
    g_lua_getfenv = reinterpret_cast<LuaGetFenvFn>(GetProcAddress(lua, "lua_getfenv"));
    g_lua_type = reinterpret_cast<LuaTypeFn>(GetProcAddress(lua, "lua_type"));
    g_lua_topointer = reinterpret_cast<LuaToPointerFn>(GetProcAddress(lua, "lua_topointer"));
    g_lua_toboolean = reinterpret_cast<LuaToBooleanFn>(GetProcAddress(lua, "lua_toboolean"));
    g_lua_tonumber = reinterpret_cast<LuaToNumberFn>(GetProcAddress(lua, "lua_tonumber"));
    g_lua_pushcclosure = reinterpret_cast<LuaPushCClosureFn>(GetProcAddress(lua, "lua_pushcclosure"));
    g_lua_pushnumber = reinterpret_cast<LuaPushNumberFn>(GetProcAddress(lua, "lua_pushnumber"));
    g_lua_pushboolean = reinterpret_cast<LuaPushBooleanFn>(GetProcAddress(lua, "lua_pushboolean"));
    g_lua_pushstring = reinterpret_cast<LuaPushStringFn>(GetProcAddress(lua, "lua_pushstring"));
    g_lua_setfield = reinterpret_cast<LuaSetFieldFn>(GetProcAddress(lua, "lua_setfield"));
    return g_lua_pcall_target && g_lua_call_target && g_lua_getfield_target && g_lua_newstate_target &&
        g_lua_loadbufferx_target && g_lua_gettop && g_lua_settop &&
        g_lua_getfenv && g_lua_type && g_lua_topointer && g_lua_toboolean && g_lua_tonumber &&
        g_lua_pushcclosure && g_lua_pushnumber && g_lua_pushboolean && g_lua_pushstring && g_lua_setfield;
}

void ensure_lua_projectile_api(lua_State *state, int argument_count)
{
    if (!state) return;
    bool register_global = false;
    {
        std::lock_guard lock(g_lua_state_mutex);
        if (std::find(g_lua_registered_states.begin(), g_lua_registered_states.end(), state) ==
            g_lua_registered_states.end())
        {
            g_lua_registered_states.push_back(state);
            register_global = true;
        }
    }

    const int top = g_lua_gettop(state);
    if (register_global)
    {
        g_lua_pushcclosure(state, lua_native_projectile_pose, 0);
        g_lua_setfield(state, kLuaGlobalsIndex, kLuaProjectilePoseFunction);
        g_lua_pushcclosure(state, lua_native_action_pose, 0);
        g_lua_setfield(state, kLuaGlobalsIndex, kLuaActionPoseFunction);
        g_lua_pushcclosure(state, lua_native_connection_target, 0);
        g_lua_setfield(state, kLuaGlobalsIndex, kLuaConnectionTargetFunction);
    }

    // Scrap Mechanic normally shares one global environment per Logic Task,
    // but also install into the current callback's sandbox environment when it
    // differs. This is balanced back to the exact original Lua stack height.
    if (argument_count >= 0 && top >= argument_count + 1)
    {
        const int function_index = top - argument_count;
        g_lua_getfenv(state, function_index);
        if (g_lua_type(state, -1) == kLuaTypeTable)
        {
            const void *environment = g_lua_topointer(state, -1);
            bool register_environment = environment != nullptr;
            if (register_environment)
            {
                std::lock_guard lock(g_lua_state_mutex);
                if (std::find(g_lua_registered_environments.begin(), g_lua_registered_environments.end(),
                        environment) != g_lua_registered_environments.end())
                    register_environment = false;
                else
                    g_lua_registered_environments.push_back(environment);
            }
            if (register_environment)
            {
                g_lua_pushcclosure(state, lua_native_projectile_pose, 0);
                g_lua_setfield(state, -2, kLuaProjectilePoseFunction);
                g_lua_pushcclosure(state, lua_native_action_pose, 0);
                g_lua_setfield(state, -2, kLuaActionPoseFunction);
                g_lua_pushcclosure(state, lua_native_connection_target, 0);
                g_lua_setfield(state, -2, kLuaConnectionTargetFunction);
            }
        }
        g_lua_settop(state, top);
    }
    if ((register_global || argument_count >= 0) && !g_lua_projectile_registration_logged.exchange(true))
        log_line("VR_LUA_NATIVE_API_REGISTERED transport=native_logic_task projectile_pose=1 action_pose=1 connection_target=1");
}

int __cdecl hk_lua_pcall(lua_State *state, int argument_count, int result_count, int error_function)
{
    auto original = reinterpret_cast<LuaPCallFn>(g_lua_pcall_original.load(std::memory_order_acquire));
    if (!original) __fastfail(FAST_FAIL_INVALID_ARG);
    ensure_lua_projectile_api(state, argument_count);
    return original(state, argument_count, result_count, error_function);
}

void __cdecl hk_lua_call(lua_State *state, int argument_count, int result_count)
{
    auto original = reinterpret_cast<LuaCallFn>(g_lua_call_original.load(std::memory_order_acquire));
    if (!original) __fastfail(FAST_FAIL_INVALID_ARG);
    ensure_lua_projectile_api(state, argument_count);
    original(state, argument_count, result_count);
}

void __cdecl hk_lua_getfield(lua_State *state, int index, const char *key)
{
    auto original = reinterpret_cast<LuaGetFieldFn>(g_lua_getfield_original.load(std::memory_order_acquire));
    if (!original) __fastfail(FAST_FAIL_INVALID_ARG);
    ensure_lua_projectile_api(state, -1);
    original(state, index, key);
}

lua_State * __cdecl hk_lua_newstate()
{
    auto original = reinterpret_cast<LuaLNewStateFn>(g_lua_newstate_original.load(std::memory_order_acquire));
    if (!original) __fastfail(FAST_FAIL_INVALID_ARG);
    lua_State *state = original();
    ensure_lua_projectile_api(state, -1);
    return state;
}

int __cdecl hk_lua_loadbufferx(lua_State *state, const char *buffer, size_t length,
    const char *name, const char *mode)
{
    auto original = reinterpret_cast<LuaLLoadBufferXFn>(
        g_lua_loadbufferx_original.load(std::memory_order_acquire));
    if (!original) __fastfail(FAST_FAIL_INVALID_ARG);
    ensure_lua_projectile_api(state, -1);
    return original(state, buffer, length, name, mode);
}

bool hand_bridge_paths(wchar_t (&path)[MAX_PATH], wchar_t (&temporary)[MAX_PATH], bool create_directory)
{
    wchar_t module_path[MAX_PATH]{};
    if (!g_module || GetModuleFileNameW(g_module, module_path, MAX_PATH) == 0) return false;
    wchar_t *slash = std::wcsrchr(module_path, L'\\');
    if (!slash) return false;
    *slash = L'\0';
    wchar_t directory[MAX_PATH]{};
    if (swprintf_s(directory, L"%s\\..\\Data\\NativeVR", module_path) < 0) return false;
    if (create_directory) CreateDirectoryW(directory, nullptr);
    if (swprintf_s(path, L"%s\\hand_physics.json", directory) < 0) return false;
    if (swprintf_s(temporary, L"%s\\hand_physics.tmp", directory) < 0) return false;
    return true;
}

bool write_atomic_file(const wchar_t *path, const wchar_t *temporary, const char *data, size_t length)
{
    HANDLE file = CreateFileW(temporary, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool wrote = WriteFile(file, data, static_cast<DWORD>(length), &written, nullptr) != FALSE &&
        written == static_cast<DWORD>(length);
    CloseHandle(file);
    if (!wrote)
    {
        DeleteFileW(temporary);
        return false;
    }
    if (!MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary);
        return false;
    }
    return true;
}

bool write_custom_content_hand_bridge(const char *data, size_t length)
{
    wchar_t path[MAX_PATH]{}, temporary[MAX_PATH]{};
    if (!scrapvr::custom_content_bridge::get_hand_bridge_paths(
        path, MAX_PATH, temporary, MAX_PATH)) return false;
    return write_atomic_file(path, temporary, data, length);
}

void reset_hand_bridge(bool reset_session = false)
{
    wchar_t path[MAX_PATH]{}, temporary[MAX_PATH]{};
    if (hand_bridge_paths(path, temporary, false))
    {
        // Preserve the last valid JSON until its atomic replacement is ready.
        // Deleting it here creates a short startup window in which sm.json.open
        // asks Scrap Mechanic to rebuild a missing data-cache entry.
        DeleteFileW(temporary);
    }
    if (reset_session)
    {
        g_hand_bridge_sequence = 0;
        g_previous_right_interaction = false;
    }
    g_previous_gun_muzzle_active = false;
    g_previous_gun_muzzle_item.clear();
    g_last_hand_bridge_publish_ms = 0;
    g_hand_bridge_logged.store(false, std::memory_order_release);
    g_hand_bridge_inactive_published.store(false, std::memory_order_release);
    g_hand_bridge_inactive_authoritative.store(false, std::memory_order_release);
    g_right_hand_world_active = false;
    g_right_hand_world_ms = 0;
    g_right_aim_world_active = false;
    g_right_aim_world_ms = 0;
    g_right_action_target_distance.store(0.0f, std::memory_order_release);
    g_right_action_target_ms.store(0, std::memory_order_release);
    g_interaction_laser_target_distance.store(0.0f, std::memory_order_release);
    g_interaction_laser_target_ms.store(0, std::memory_order_release);
    g_interaction_laser_target_kind.store(0, std::memory_order_release);
    set_native_projectile_pose(false, false);
    set_native_action_pose(false, false);
}

void deactivate_hand_bridge(bool vr_authoritative = false)
{
    g_previous_right_interaction = false;
    g_previous_gun_muzzle_active = false;
    g_previous_gun_muzzle_item.clear();
    g_last_hand_bridge_publish_ms = 0;
    g_hand_bridge_logged.store(false, std::memory_order_release);
    g_right_hand_world_active = false;
    g_right_hand_world_ms = 0;
    g_right_aim_world_active = false;
    g_right_aim_world_ms = 0;
    g_right_action_target_distance.store(0.0f, std::memory_order_release);
    g_right_action_target_ms.store(0, std::memory_order_release);
    g_interaction_laser_target_distance.store(0.0f, std::memory_order_release);
    g_interaction_laser_target_ms.store(0, std::memory_order_release);
    g_interaction_laser_target_kind.store(0, std::memory_order_release);
    set_native_projectile_pose(vr_authoritative, false);
    set_native_action_pose(vr_authoritative, false);

    // A static inactive file is sufficient until tracking becomes active. Do not
    // rewrite it on every OpenXR cleanup/retry: Scrap Mechanic treats each change
    // as data-cache invalidation and can stall its logic task while recompiling it.
    if (g_hand_bridge_inactive_published.load(std::memory_order_acquire) &&
        g_hand_bridge_inactive_authoritative.load(std::memory_order_acquire) == vr_authoritative) return;

    wchar_t path[MAX_PATH]{}, temporary[MAX_PATH]{};
    if (!hand_bridge_paths(path, temporary, true)) return;
    const uint64_t hand_sequence = ++g_hand_bridge_sequence;
    char json[1024]{};
    const int length = std::snprintf(json, sizeof(json),
        "{\"sequence\":%llu,\"vrActive\":false,\"vrAuthoritative\":%s,\"opticalGunTrigger\":false,"
        "\"gunMuzzle\":{\"active\":false,\"item\":\"\",\"x\":0,\"y\":0,\"z\":0},"
        "\"left\":{\"active\":false,\"interact\":false,\"optical\":false},"
        "\"right\":{\"active\":false,\"interact\":false,\"optical\":false}}",
        static_cast<unsigned long long>(hand_sequence),
        vr_authoritative ? "true" : "false");
    if (length > 0 && static_cast<size_t>(length) < sizeof(json))
    {
        const size_t json_length = static_cast<size_t>(length);
        const bool base_written = write_atomic_file(path, temporary, json, json_length);
        const bool custom_written = write_custom_content_hand_bridge(json, json_length);
        if (base_written || custom_written)
        {
            g_hand_bridge_inactive_authoritative.store(vr_authoritative, std::memory_order_release);
            g_hand_bridge_inactive_published.store(true, std::memory_order_release);
            log_line("VR_HAND_BRIDGE_INACTIVE authoritative=%u desktop_fallback=%u custom_content=%u",
                vr_authoritative ? 1u : 0u, vr_authoritative ? 0u : 1u,
                custom_written ? 1u : 0u);
        }
    }
}

void remove_world_state_bridge()
{
    wchar_t hand_path[MAX_PATH]{}, hand_temporary[MAX_PATH]{};
    if (!hand_bridge_paths(hand_path, hand_temporary, true)) return;
    wchar_t *slash=std::wcsrchr(hand_path,L'\\');
    if (!slash) return;
    *slash=L'\0';
    wchar_t path[MAX_PATH]{};
    if (swprintf_s(path,L"%s\\world_state.json",hand_path)>=0)
        DeleteFileW(path);
    if (swprintf_s(path,L"%s\\player_state.json",hand_path)>=0)
        DeleteFileW(path);
}

void publish_hand_bridge(const XrPosef &reference, const float *game_world_to_view)
{
    if (!g_feature_hands_enabled || !world_to_view_matrix(game_world_to_view)) return;
    const uint64_t now = GetTickCount64();

    XrPosef poses[2]{};
    bool optical[2]{}, interaction[2]{};
    bool active[2]{
        scrapvr::hands::get_pose(0, poses[0], optical[0], interaction[0]),
        scrapvr::hands::get_pose(1, poses[1], optical[1], interaction[1])
    };
    XrVector3f world[2]{}, forward[2]{}, up[2]{};
    const XrQuaternionf inverse_reference = conjugate(normalize(reference.orientation));
    for (uint32_t hand = 0; hand < 2; ++hand)
    {
        if (!active[hand]) continue;
        const XrVector3f delta{
            poses[hand].position.x - reference.position.x,
            poses[hand].position.y - reference.position.y,
            poses[hand].position.z - reference.position.z
        };
        world[hand] = view_point_to_world(game_world_to_view,
            rotate_vector(inverse_reference, delta));
        const XrQuaternionf relative_orientation = quaternion_multiply(
            inverse_reference, poses[hand].orientation);
        forward[hand] = view_vector_to_world(game_world_to_view,
            rotate_vector(relative_orientation, {0.0f, 0.0f, -1.0f}));
        up[hand] = view_vector_to_world(game_world_to_view,
            rotate_vector(relative_orientation, {0.0f, 1.0f, 0.0f}));
    }
    g_right_hand_world_active = active[1];
    if (active[1])
    {
        g_right_hand_world = world[1];
        g_right_hand_forward = forward[1];
        g_right_hand_up = up[1];
        g_right_hand_world_ms = now;
    }

    // The rendered glove uses a controller-to-mesh calibration. That transform
    // is correct for the model, but it is not OpenXR's pointing direction and
    // made generic tool raycasts land near the player's feet on some Touch
    // runtimes. Publish the runtime aim pose independently for interaction.
    g_right_aim_world_active = g_input.pointer_pose_active(1);
    if (g_right_aim_world_active)
    {
        const XrPosef &aim_pose = g_input.pointer_pose(1);
        const XrVector3f aim_delta{
            aim_pose.position.x - reference.position.x,
            aim_pose.position.y - reference.position.y,
            aim_pose.position.z - reference.position.z
        };
        g_right_aim_world = view_point_to_world(game_world_to_view,
            rotate_vector(inverse_reference, aim_delta));
        const XrQuaternionf aim_relative = quaternion_multiply(
            inverse_reference, aim_pose.orientation);
        g_right_aim_forward = view_vector_to_world(game_world_to_view,
            rotate_vector(aim_relative, {0.0f, 0.0f, -1.0f}));
        g_right_aim_up = view_vector_to_world(game_world_to_view,
            rotate_vector(aim_relative, {0.0f, 1.0f, 0.0f}));
        g_right_aim_world_ms = now;
    }
    if (g_right_aim_world_active)
    {
        XrVector3f direction = g_right_aim_forward;
        XrVector3f up = g_right_aim_up;
        const float projection = up.x * direction.x + up.y * direction.y + up.z * direction.z;
        up.x -= direction.x * projection;
        up.y -= direction.y * projection;
        up.z -= direction.z * projection;
        const bool valid = normalize_vector(direction) && normalize_vector(up);
        set_native_action_pose(true, valid, g_right_aim_world, direction, up);
    }
    else
    {
        set_native_action_pose(true, false);
    }

    XrVector3f gun_muzzle_offset{};
    XrVector3f gun_muzzle_direction{0.0f, 0.0f, -1.0f};
    const char *gun_muzzle_item = nullptr;
    const bool gun_tool_active = scrapvr::tools::get_gun_muzzle_offset(
        gun_muzzle_offset, gun_muzzle_direction, gun_muzzle_item);
    const bool gun_muzzle_active = active[1] && gun_tool_active && gun_muzzle_item;
    update_native_projectile_pose(active[1], gun_tool_active, world[1], forward[1], up[1],
        gun_muzzle_offset, gun_muzzle_direction, gun_muzzle_item);
    XrVector3f interaction_laser_offset{};
    XrVector3f interaction_laser_direction{0.0f, 0.0f, -1.0f};
    scrapvr::tools::InteractionLaserKind interaction_laser_kind =
        scrapvr::tools::InteractionLaserKind::none;
    const bool interaction_laser_tool_active = scrapvr::tools::get_interaction_laser_offset(
        interaction_laser_offset, &interaction_laser_direction, &interaction_laser_kind);
    const bool hammer_tool_active = scrapvr::tools::is_hammer_active();

    const bool right_interaction_changed = interaction[1] != g_previous_right_interaction;
    const std::string gun_item = gun_muzzle_item ? gun_muzzle_item : "";
    const bool gun_muzzle_changed = gun_muzzle_active != g_previous_gun_muzzle_active ||
        gun_item != g_previous_gun_muzzle_item;
    // Keep an equipped gun or hammer pose current even before the trigger moves.
    // Hammer Lua samples this ordinary action aim at the stock impact frame.
    const uint64_t publish_interval_ms =
        (gun_tool_active || hammer_tool_active || interaction_laser_tool_active)
        ? 20u : (interaction[1] ? 20u : 1000u);
    if (!right_interaction_changed && !gun_muzzle_changed &&
        now - g_last_hand_bridge_publish_ms < publish_interval_ms) return;

    wchar_t path[MAX_PATH]{}, temporary[MAX_PATH]{};
    if (!hand_bridge_paths(path, temporary, true)) return;
    const bool optical_gun_trigger = optical[1] && gun_tool_active && interaction[1];
    const uint64_t hand_sequence = ++g_hand_bridge_sequence;
    const bool action_aim_active = active[1] && g_right_aim_world_active;
    char json[2048]{};
    const int length = std::snprintf(json, sizeof(json),
        "{\"sequence\":%llu,\"vrActive\":true,\"vrAuthoritative\":true,\"opticalGunTrigger\":%s,"
        "\"gunMuzzle\":{\"active\":%s,\"item\":\"%s\",\"x\":%.5f,\"y\":%.5f,\"z\":%.5f,\"dx\":%.5f,\"dy\":%.5f,\"dz\":%.5f},"
        "\"toolLaser\":{\"active\":%s,\"kind\":%u,\"x\":%.5f,\"y\":%.5f,\"z\":%.5f,\"dx\":%.5f,\"dy\":%.5f,\"dz\":%.5f},"
        "\"actionAim\":{\"active\":%s,\"x\":%.5f,\"y\":%.5f,\"z\":%.5f,\"fx\":%.5f,\"fy\":%.5f,\"fz\":%.5f,\"ux\":%.5f,\"uy\":%.5f,\"uz\":%.5f},"
        "\"left\":{\"active\":%s,\"interact\":%s,\"optical\":%s,\"x\":%.5f,\"y\":%.5f,\"z\":%.5f,\"fx\":%.5f,\"fy\":%.5f,\"fz\":%.5f,\"ux\":%.5f,\"uy\":%.5f,\"uz\":%.5f},"
        "\"right\":{\"active\":%s,\"interact\":%s,\"optical\":%s,\"x\":%.5f,\"y\":%.5f,\"z\":%.5f,\"fx\":%.5f,\"fy\":%.5f,\"fz\":%.5f,\"ux\":%.5f,\"uy\":%.5f,\"uz\":%.5f}}",
        static_cast<unsigned long long>(hand_sequence),
        optical_gun_trigger ? "true" : "false",
        gun_muzzle_active ? "true" : "false", gun_item.c_str(),
        gun_muzzle_offset.x, gun_muzzle_offset.y, gun_muzzle_offset.z,
        gun_muzzle_direction.x, gun_muzzle_direction.y, gun_muzzle_direction.z,
        interaction_laser_tool_active ? "true" : "false",
        static_cast<unsigned>(interaction_laser_kind),
        interaction_laser_offset.x, interaction_laser_offset.y, interaction_laser_offset.z,
        interaction_laser_direction.x, interaction_laser_direction.y,
        interaction_laser_direction.z,
        action_aim_active ? "true" : "false",
        g_right_aim_world.x, g_right_aim_world.y, g_right_aim_world.z,
        g_right_aim_forward.x, g_right_aim_forward.y, g_right_aim_forward.z,
        g_right_aim_up.x, g_right_aim_up.y, g_right_aim_up.z,
        active[0] ? "true" : "false", interaction[0] ? "true" : "false", optical[0] ? "true" : "false",
        world[0].x, world[0].y, world[0].z, forward[0].x, forward[0].y, forward[0].z,
        up[0].x, up[0].y, up[0].z,
        active[1] ? "true" : "false", interaction[1] ? "true" : "false", optical[1] ? "true" : "false",
        world[1].x, world[1].y, world[1].z, forward[1].x, forward[1].y, forward[1].z,
        up[1].x, up[1].y, up[1].z);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(json)) return;
    g_previous_right_interaction = interaction[1];
    const size_t json_length = static_cast<size_t>(length);
    const bool base_written = write_atomic_file(path, temporary, json, json_length);
    const bool custom_written = write_custom_content_hand_bridge(json, json_length);
    if (!base_written && !custom_written) return;

    g_hand_bridge_inactive_published.store(false, std::memory_order_release);
    g_last_hand_bridge_publish_ms = now;
    g_previous_gun_muzzle_active = gun_muzzle_active;
    g_previous_gun_muzzle_item = gun_item;
    if (gun_muzzle_changed)
        log_line("VR_GUN_MUZZLE_BRIDGE active=%u right_tracked=%u tool_is_gun=%u item=%s",
            gun_muzzle_active ? 1u : 0u, active[1] ? 1u : 0u,
            gun_tool_active ? 1u : 0u, gun_item.empty() ? "none" : gun_item.c_str());
    if (!g_hand_bridge_logged.exchange(true))
    {
        log_line("VR_HAND_BRIDGE_ACTIVE path=content-aware world_space=1 action_aim=openxr_pointer gun_projectile_override=tracked_barrel hammer=stock_trigger_hand_ray use=stock_interaction_hand_ray");
    }
}

uint8_t __fastcall hk_raycast(void *world, void *unused, const float *origin,
                              const float *delta, void *ignore, void *result)
{
    auto original = reinterpret_cast<RaycastFn>(g_raycast_original.load(std::memory_order_acquire));
    if (!original) __fastfail(FAST_FAIL_INVALID_ARG);
    const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const uint64_t now = GetTickCount64();
    if (g_enabled.load(std::memory_order_acquire) &&
        caller == g_game_base + kPlayerToolRaycastReturnRva && origin && delta)
    {
        XrVector3f local_offset{0.0f, -0.035f, -0.120f};
        XrVector3f local_direction{0.0f, 0.0f, -1.0f};
        scrapvr::tools::InteractionLaserKind interaction_laser_kind =
            scrapvr::tools::InteractionLaserKind::none;
        const bool runtime_aim_fresh = g_right_aim_world_active &&
            now - g_right_aim_world_ms <= 250;
        const bool glove_pose_fresh = g_right_hand_world_active &&
            now - g_right_hand_world_ms <= 250;
        // B remains Scrap Mechanic's own Use key. While it is held, override
        // only the engine's stock player-selection ray with OpenXR's semantic
        // aim pose. This lets the untouched HarvestCore interaction refine the
        // pointed-at log while retaining its normal hold timing and server path.
        const bool use_hand_aim = g_input.right_use_down() && runtime_aim_fresh;
        const bool calibrated_laser = !use_hand_aim &&
            scrapvr::tools::get_interaction_laser_offset(
                local_offset, &local_direction, &interaction_laser_kind);
        if (!calibrated_laser && runtime_aim_fresh)
            local_offset = {0.0f, 0.0f, 0.0f};
        const XrVector3f ray_origin = !calibrated_laser && runtime_aim_fresh
            ? g_right_aim_world : g_right_hand_world;
        XrVector3f forward = !calibrated_laser && runtime_aim_fresh
            ? g_right_aim_forward : g_right_hand_forward;
        XrVector3f up = !calibrated_laser && runtime_aim_fresh
            ? g_right_aim_up : g_right_hand_up;
        const bool pose_fresh = (!calibrated_laser && runtime_aim_fresh) || glove_pose_fresh;
        if (pose_fresh && normalize_vector(forward))
        {
            const float projection = up.x * forward.x + up.y * forward.y + up.z * forward.z;
            up.x -= forward.x * projection; up.y -= forward.y * projection; up.z -= forward.z * projection;
            XrVector3f right{
                forward.y * up.z - forward.z * up.y,
                forward.z * up.x - forward.x * up.z,
                forward.x * up.y - forward.y * up.x
            };
            const float range_squared = delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
            if (normalize_vector(up) && normalize_vector(right) && std::isfinite(range_squared) &&
                range_squared >= 0.01f && range_squared <= 400.0f)
            {
                const float redirected_origin[3]{
                    ray_origin.x + right.x * local_offset.x + up.x * local_offset.y - forward.x * local_offset.z,
                    ray_origin.y + right.y * local_offset.x + up.y * local_offset.y - forward.y * local_offset.z,
                    ray_origin.z + right.z * local_offset.x + up.z * local_offset.y - forward.z * local_offset.z
                };
                const float range = std::sqrt(range_squared);
				XrVector3f redirected_direction{
					right.x * local_direction.x + up.x * local_direction.y - forward.x * local_direction.z,
					right.y * local_direction.x + up.y * local_direction.y - forward.y * local_direction.z,
					right.z * local_direction.x + up.z * local_direction.y - forward.z * local_direction.z
				};
				if (!normalize_vector(redirected_direction)) redirected_direction = forward;
                const float redirected_delta[3]{redirected_direction.x * range,
                    redirected_direction.y * range, redirected_direction.z * range};
                if (use_hand_aim && !g_use_raycast_logged.exchange(true))
                    log_line("VR_USE_RAY_ACTIVE input=right_B action=stock_use source=openxr_aim_pose caller_rva=%llx",
                        static_cast<unsigned long long>(kPlayerToolRaycastReturnRva));
                if (!g_tool_raycast_logged.exchange(true))
                    log_line("VR_ACTION_RAY_ACTIVE raycast_rva=%llx caller_rva=%llx generic_source=%s interaction_lasers=calibrated_visible_tool",
                        static_cast<unsigned long long>(kRaycastRva),
                        static_cast<unsigned long long>(kPlayerToolRaycastReturnRva),
                        calibrated_laser ? "calibrated_visible_tool" :
                            (runtime_aim_fresh ? "openxr_aim_pose" : "grip_fallback"));
                const uint8_t hit = original(
                    world, unused, redirected_origin, redirected_delta, ignore, result);
                // This build's validated player-ray result stores its normalized
                // hit fraction at +0xF0. Convert it back to distance along the
                // exact redirected OpenXR ray so the VR marker sits on the real
                // selected surface instead of floating at an arbitrary depth.
                uint8_t result_valid = 0;
                float fraction = 0.0f;
                if (hit != 0 && result)
                {
                    std::memcpy(&result_valid, result, sizeof(result_valid));
                    std::memcpy(&fraction,
                        reinterpret_cast<const uint8_t *>(result) + 0xF0,
                        sizeof(fraction));
                }
                const bool valid_target = hit != 0 && result_valid != 0 &&
                    std::isfinite(fraction) &&
                    fraction >= 0.0f && fraction <= 1.0f;
                if (calibrated_laser &&
                    interaction_laser_kind == scrapvr::tools::InteractionLaserKind::surface)
                {
                    if (valid_target)
                    {
                        g_interaction_laser_target_distance.store(
                            std::clamp(range * fraction, 0.05f, 8.0f),
                            std::memory_order_release);
                        g_interaction_laser_target_kind.store(
                            static_cast<uint32_t>(interaction_laser_kind),
                            std::memory_order_release);
                        g_interaction_laser_target_ms.store(now, std::memory_order_release);
                    }
                    else
                    {
                        g_interaction_laser_target_ms.store(0, std::memory_order_release);
                    }
                }
                else if (!calibrated_laser)
                {
                    if (valid_target)
                    {
                        g_right_action_target_distance.store(
                            std::max(0.05f, range * fraction), std::memory_order_release);
                        g_right_action_target_ms.store(now, std::memory_order_release);
                    }
                    else
                    {
                        g_right_action_target_ms.store(0, std::memory_order_release);
                    }
                }
                return hit;
            }
        }
    }
    return original(world, unused, origin, delta, ignore, result);
}

void quaternion_column_matrix(XrQuaternionf q, float matrix[16])
{
    q = normalize(q);
    const float x=q.x,y=q.y,z=q.z,w=q.w;
    std::memset(matrix, 0, sizeof(float) * 16);
    matrix[0]=1-2*(y*y+z*z); matrix[1]=2*(x*y+z*w); matrix[2]=2*(x*z-y*w);
    matrix[4]=2*(x*y-z*w); matrix[5]=1-2*(x*x+z*z); matrix[6]=2*(y*z+x*w);
    matrix[8]=2*(x*z+y*w); matrix[9]=2*(y*z-x*w); matrix[10]=1-2*(x*x+y*y);
    matrix[15]=1.0f;
}

void multiply_column_major(const float a[16], const float b[16], float out[16])
{
    for (int column = 0; column != 4; ++column)
        for (int row = 0; row != 4; ++row)
        {
            out[column * 4 + row] = 0.0f;
            for (int k = 0; k != 4; ++k)
                out[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
        }
}

bool finite_matrix(const float *matrix)
{
    if (!readable(matrix, sizeof(float) * 16)) return false;
    for (int i = 0; i != 16; ++i) if (!std::isfinite(matrix[i])) return false;
    return true;
}

bool world_to_view_matrix(const float *matrix)
{
    return finite_matrix(matrix) && std::fabs(matrix[3]) < 0.01f &&
        std::fabs(matrix[7]) < 0.01f && std::fabs(matrix[11]) < 0.01f &&
        std::fabs(matrix[15] - 1.0f) < 0.01f;
}

struct StandingCameraState
{
    bool sample_valid = false;
    XrVector3f previous_camera_position{};
    XrVector3f previous_forward{};
    float previous_output_height = 0.0f;
    bool orbit_distance_valid = false;
    float orbit_distance = 0.0f;
    float height_bias = 0.0f;
    float orbit_candidate = 0.0f;
    uint32_t orbit_candidate_samples = 0;
    bool orbit_logged = false;
    bool outlier_logged = false;

    void reset()
    {
        sample_valid = false;
        previous_camera_position = {};
        previous_forward = {};
        previous_output_height = 0.0f;
        orbit_distance_valid = false;
        orbit_distance = 0.0f;
        height_bias = 0.0f;
        orbit_candidate = 0.0f;
        orbit_candidate_samples = 0;
        orbit_logged = false;
        outlier_logged = false;
    }
};

// Scrap Mechanic's desktop camera owns player/world translation and stick/mouse
// turning. Its pitch camera moves along a small orbit, however, so applying the
// orientation while retaining the raw position makes looking up/down raise or
// lower the VR viewpoint. Learn that orbit radius from consecutive rigid camera
// samples and remove only its vertical translation. Preserve yaw and pitch, but
// rebuild the basis from world-up so accidental desktop roll can never tilt the
// VR horizon. Real player/world vertical motion still passes through. Seats use
// the untouched game matrix because their larger intentional orbit is correct.
bool build_upright_world_to_view(const float *source, StandingCameraState &state,
                                 float upright[16])
{
    if (!world_to_view_matrix(source) || !upright) return false;

    const XrVector3f camera_position = view_point_to_world(source, {0.0f, 0.0f, 0.0f});
    XrVector3f source_forward = view_vector_to_world(source, {0.0f, 0.0f, -1.0f});
    if (!normalize_vector(source_forward)) return false;

    float camera_height = camera_position.z;
    if (state.sample_valid)
    {
        const XrVector3f forward_delta{
            source_forward.x - state.previous_forward.x,
            source_forward.y - state.previous_forward.y,
            source_forward.z - state.previous_forward.z
        };
        const XrVector3f camera_delta{
            camera_position.x - state.previous_camera_position.x,
            camera_position.y - state.previous_camera_position.y,
            camera_position.z - state.previous_camera_position.z
        };
        const float forward_delta_squared = forward_delta.x * forward_delta.x +
            forward_delta.y * forward_delta.y + forward_delta.z * forward_delta.z;

        // Estimate only while pitch actually changes. Requiring the residual to
        // be tiny rejects ordinary walking, jumping and vehicle translation.
        if (std::fabs(forward_delta.z) >= 0.0005f && forward_delta_squared >= 0.00000025f)
        {
            const float candidate = -(camera_delta.x * forward_delta.x +
                camera_delta.y * forward_delta.y + camera_delta.z * forward_delta.z) /
                forward_delta_squared;
            const XrVector3f residual{
                camera_delta.x + candidate * forward_delta.x,
                camera_delta.y + candidate * forward_delta.y,
                camera_delta.z + candidate * forward_delta.z
            };
            const float residual_length = std::sqrt(residual.x * residual.x +
                residual.y * residual.y + residual.z * residual.z);
            const float orbit_motion = std::fabs(candidate) * std::sqrt(forward_delta_squared);
            const float residual_limit = std::max(0.008f, orbit_motion * 0.25f);
            if (std::isfinite(candidate) && candidate >= -0.05f && candidate <= 3.0f &&
                std::isfinite(residual_length) && residual_length <= residual_limit)
            {
                const float clamped_candidate = std::max(0.0f, candidate);
                if (!state.orbit_distance_valid)
                {
                    const float tolerance = std::max(0.08f,
                        std::max(state.orbit_candidate, clamped_candidate) * 0.20f);
                    if (state.orbit_candidate_samples == 0 ||
                        std::fabs(clamped_candidate - state.orbit_candidate) > tolerance)
                    {
                        state.orbit_candidate = clamped_candidate;
                        state.orbit_candidate_samples = 1;
                    }
                    else
                    {
                        state.orbit_candidate =
                            (state.orbit_candidate * state.orbit_candidate_samples +
                                clamped_candidate) /
                            static_cast<float>(state.orbit_candidate_samples + 1);
                        ++state.orbit_candidate_samples;
                    }
                    // Do not lock from a single combined move/look frame. Three
                    // agreeing samples identify the camera's rigid pitch orbit
                    // without mistaking player translation for a huge radius.
                    if (state.orbit_candidate_samples >= 3)
                    {
                        state.orbit_distance = state.orbit_candidate;
                        state.orbit_distance_valid = true;
                        state.height_bias = state.previous_output_height -
                            (state.previous_camera_position.z +
                                state.orbit_distance * state.previous_forward.z);
                    }
                }
                else
                {
                    const float tolerance = std::max(0.10f, state.orbit_distance * 0.25f);
                    if (std::fabs(clamped_candidate - state.orbit_distance) <= tolerance)
                    {
                        const float output_before_update = camera_position.z +
                            state.orbit_distance * source_forward.z + state.height_bias;
                        state.orbit_distance = state.orbit_distance * 0.97f +
                            clamped_candidate * 0.03f;
                        state.height_bias = output_before_update -
                            (camera_position.z + state.orbit_distance * source_forward.z);
                    }
                    else if (!state.outlier_logged)
                    {
                        state.outlier_logged = true;
                        log_line("VR_CAMERA_STANDING_HEIGHT_OUTLIER rejected=%.5f locked=%.5f",
                            clamped_candidate, state.orbit_distance);
                    }
                }
                if (state.orbit_distance_valid && !state.orbit_logged)
                {
                    state.orbit_logged = true;
                    log_line("VR_CAMERA_STANDING_HEIGHT_LOCK orbit_radius=%.5f "
                        "mouse_controller_pitch_height=discarded player_vertical_motion=preserved",
                        state.orbit_distance);
                }
            }
        }
    }
    if (state.orbit_distance_valid)
        camera_height = camera_position.z + state.orbit_distance * source_forward.z +
            state.height_bias;

    state.sample_valid = true;
    state.previous_camera_position = camera_position;
    state.previous_forward = source_forward;
    state.previous_output_height = camera_height;

    // World-up plus the camera forward vector defines a roll-free basis. Unlike
    // the previous upright implementation, source_forward.z remains intact, so
    // the right stick can look up/down smoothly without changing eye height.
    XrVector3f forward = source_forward;
    const float horizontal_length = std::sqrt(
        forward.x * forward.x + forward.y * forward.y);
    XrVector3f right{};
    if (std::isfinite(horizontal_length) && horizontal_length > 0.0001f)
    {
        right = {forward.y / horizontal_length, -forward.x / horizontal_length, 0.0f};
    }
    else
    {
        right = {source[0], source[4], 0.0f};
        const float right_length = std::sqrt(right.x * right.x + right.y * right.y);
        if (!std::isfinite(right_length) || right_length <= 0.0001f) return false;
        right.x /= right_length;
        right.y /= right_length;
    }
    XrVector3f camera_up{
        right.y * forward.z - right.z * forward.y,
        right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x
    };
    if (!normalize_vector(camera_up)) return false;
    const XrVector3f camera_position_locked{
        camera_position.x, camera_position.y, camera_height
    };
    std::memset(upright, 0, sizeof(float) * 16);

    // Column-major rigid world-to-view matrix. Rows are camera right, camera
    // up, and camera back (-forward), respectively.
    upright[0] = right.x;
    upright[4] = right.y;
    upright[8] = right.z;
    upright[1] = camera_up.x;
    upright[5] = camera_up.y;
    upright[9] = camera_up.z;
    upright[2] = -forward.x;
    upright[6] = -forward.y;
    upright[10] = -forward.z;
    upright[12] = -(right.x * camera_position_locked.x +
        right.y * camera_position_locked.y + right.z * camera_position_locked.z);
    upright[13] = -(camera_up.x * camera_position_locked.x +
        camera_up.y * camera_position_locked.y + camera_up.z * camera_position_locked.z);
    upright[14] = forward.x * camera_position_locked.x +
        forward.y * camera_position_locked.y + forward.z * camera_position_locked.z;
    upright[15] = 1.0f;

    if (!world_to_view_matrix(upright)) return false;
    if (!g_vr_upright_camera_logged.exchange(true, std::memory_order_acq_rel))
    {
        const XrVector3f source_up = view_vector_to_world(source, {0.0f, 1.0f, 0.0f});
        log_line("VR_CAMERA_UPRIGHT_LOCK active=1 game_world_up=+Z roll=discarded "
            "mouse_controller_pitch=preserved pitch_orbit_height=discarded "
            "mouse_yaw=preserved source_up=%.5f,%.5f,%.5f",
            source_up.x, source_up.y, source_up.z);
    }
    return true;
}

bool perspective_matrix(const float *matrix)
{
    return finite_matrix(matrix) && std::fabs(matrix[0]) > 0.01f &&
        std::fabs(matrix[5]) > 0.01f && std::fabs(matrix[11] + 1.0f) < 0.1f &&
        std::fabs(matrix[15]) < 0.01f;
}

bool identity_view_matrix(const float *matrix)
{
    if (!world_to_view_matrix(matrix)) return false;
    constexpr float identity[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    for (int i = 0; i != 16; ++i)
        if (std::fabs(matrix[i] - identity[i]) > 0.001f) return false;
    return true;
}

void __fastcall hk_camera_build(void *state, const float *world_to_view, const float *projection,
                                float scalar0, float scalar1, float scalar2)
{
    auto original = reinterpret_cast<CameraBuildFn>(g_camera_build_original.load(std::memory_order_acquire));
    if (!original) __fastfail(FAST_FAIL_INVALID_ARG);

    const uint64_t sequence = g_camera_build_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool view_valid = world_to_view_matrix(world_to_view);
    const bool perspective = perspective_matrix(projection);
    const bool camera_relative = identity_view_matrix(world_to_view);
    const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const uintptr_t caller_rva = caller >= g_game_base ? caller - g_game_base : 0;
    const uint32_t call_in_eye = g_active_eye >= 0 ? ++g_eye_camera_build_index : 0;

    const uint64_t vr_diagnostic = g_active_eye >= 0
        ? g_vr_camera_diagnostic_calls.fetch_add(1, std::memory_order_relaxed) + 1 : 0;
    if (sequence <= 160 || (g_active_eye >= 0 && vr_diagnostic <= 240))
    {
        log_line("CAMERA_BUILD seq=%llu eye=%d eye_call=%u caller_rva=%llx view_valid=%u perspective=%u identity_view=%u view_t=%.6f,%.6f,%.6f projection=%.6f,%.6f,%.6f,%.6f scalars=%.6f,%.6f,%.6f",
            static_cast<unsigned long long>(sequence), g_active_eye, call_in_eye,
            static_cast<unsigned long long>(caller_rva), view_valid ? 1u : 0u,
            perspective ? 1u : 0u, camera_relative ? 1u : 0u,
            view_valid ? world_to_view[12] : 0.0f, view_valid ? world_to_view[13] : 0.0f,
            view_valid ? world_to_view[14] : 0.0f, perspective ? projection[0] : 0.0f,
            perspective ? projection[5] : 0.0f, perspective ? projection[8] : 0.0f,
            perspective ? projection[9] : 0.0f, scalar0, scalar1, scalar2);
    }

    // Scrap Mechanic's first-person arms/tool layer is the only perspective
    // camera built from an identity view at this exact caller. The caller's
    // following conditional block submits that pass. It is skipped by a
    // validated code patch while VR is enabled, so leave this camera ABI and
    // projection untouched and use this hook only to prove the path was reached.
    if (g_active_eye >= 0 && caller_rva == kFirstPersonCameraCallerRva &&
        perspective && camera_relative)
    {
        const uint64_t hidden = g_viewmodel_camera_hides.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!g_viewmodel_camera_hide_logged.exchange(true, std::memory_order_acq_rel))
            log_line("VR_VIEWMODEL_CAMERA_IDENTIFIED eye=%d caller_rva=%llx first=%llu pass_patch=%u",
                g_active_eye, static_cast<unsigned long long>(caller_rva),
                static_cast<unsigned long long>(hidden),
                g_viewmodel_pass_patched.load(std::memory_order_acquire) ? 1u : 0u);
    }

    original(state, world_to_view, projection, scalar0, scalar1, scalar2);
}

bool build_tracking_view(const XrPosef &reference, const XrPosef &eye, float transform[16])
{
    const XrQuaternionf inverse_reference = conjugate(normalize(reference.orientation));
    const XrQuaternionf relative_orientation = quaternion_multiply(inverse_reference, eye.orientation);
    const XrQuaternionf view_orientation = conjugate(relative_orientation);
    const XrVector3f position_delta{
        eye.position.x - reference.position.x,
        eye.position.y - reference.position.y,
        eye.position.z - reference.position.z
    };
    const XrVector3f relative_position = rotate_vector(inverse_reference, position_delta);
    const XrVector3f inverse_position{-relative_position.x,-relative_position.y,-relative_position.z};
    const XrVector3f view_position = rotate_vector(view_orientation, inverse_position);
    quaternion_column_matrix(view_orientation, transform);
    transform[12] = view_position.x;
    transform[13] = view_position.y;
    transform[14] = view_position.z;
    return true;
}

struct EyeRenderMapping
{
    D3D11_BOX source_box{};
    int32_t width = 0;
    int32_t height = 0;
};

constexpr uint32_t kVrRenderTargetAlignment = 16;
// A hidden culling margin was tested here, but the same foliage LOD transitions
// are visible in the unmodified PC renderer at maximum foliage/draw distance.
// Keep it disabled: it did not affect that engine-native behaviour and only
// increased the two eye render costs. The aligned source target remains required
// to avoid the confirmed odd-dimension post-process seam.
constexpr uint32_t kVrCullingMarginPixels = 0;

uint32_t align_up(uint32_t value, uint32_t alignment)
{
    if (alignment == 0 || value > UINT32_MAX - (alignment - 1)) return 0;
    return (value + alignment - 1) / alignment * alignment;
}

bool build_eye_projection(const XrFovf &fov, const float *game_projection,
                           uint32_t source_width, uint32_t source_height,
                           uint32_t target_width, uint32_t target_height,
                           float projection[16], EyeRenderMapping &mapping)
{
    if (!perspective_matrix(game_projection)) return false;
    const float tan_left = std::tan(fov.angleLeft);
    const float tan_right = std::tan(fov.angleRight);
    const float tan_down = std::tan(fov.angleDown);
    const float tan_up = std::tan(fov.angleUp);
    if (!std::isfinite(tan_left) || !std::isfinite(tan_right) ||
        !std::isfinite(tan_down) || !std::isfinite(tan_up) ||
        tan_left >= -0.001f || tan_right <= 0.001f ||
        tan_down >= -0.001f || tan_up <= 0.001f ||
        source_width < 16 || source_height < 16 ||
        target_width < 16 || target_height < 16 ||
        target_width > source_width || target_height > source_height) return false;

    // Scrap Mechanic 1.0's cloud reconstruction and cascade-shadow setup assume
    // a centered projection. The first implementation used the smallest possible
    // centered target (2565x2711 on Quest 3). Those odd dimensions flow through
    // the engine's half/quarter-resolution post passes and produce a duplicated or
    // missing center row/column. Keep the source target 16-pixel aligned and widen
    // the centered frustum just enough that the exact runtime eye extent remains a
    // 1:1 integer crop. This removes the odd-size post-process seam without a
    // scaling pass, a third scene render, or a change to the submitted OpenXR FOV.
    const float symmetric_x = (tan_right - tan_left) * static_cast<float>(source_width) /
        (2.0f * static_cast<float>(target_width));
    const float symmetric_y = (tan_up - tan_down) * static_cast<float>(source_height) /
        (2.0f * static_cast<float>(target_height));
    const float minimum_x = (std::max)(-tan_left, tan_right);
    const float minimum_y = (std::max)(-tan_down, tan_up);
    if (symmetric_x < minimum_x || symmetric_y < minimum_y ||
        symmetric_x < 0.001f || symmetric_y < 0.001f) return false;
    std::memset(projection, 0, sizeof(float) * 16);
    projection[0] = 1.0f / symmetric_x;
    projection[5] = 1.0f / symmetric_y;
    projection[8] = 0.0f;
    projection[9] = 0.0f;
    projection[10] = game_projection[10];
    projection[11] = game_projection[11];
    projection[14] = game_projection[14];
    projection[15] = game_projection[15];

    const float ndc_left = tan_left / symmetric_x;
    const float ndc_up = tan_up / symmetric_y;
    const auto clamp_u32 = [](long value, uint32_t maximum) -> uint32_t {
        if (value < 0) return 0;
        if (static_cast<uint64_t>(value) > maximum) return maximum;
        return static_cast<uint32_t>(value);
    };
    uint32_t left = clamp_u32(std::lround((ndc_left + 1.0f) * 0.5f * static_cast<float>(source_width)), source_width - 1);
    uint32_t top = clamp_u32(std::lround((1.0f - ndc_up) * 0.5f * static_cast<float>(source_height)), source_height - 1);
    if (left > source_width - target_width || top > source_height - target_height) return false;
    const uint32_t right = left + target_width;
    const uint32_t bottom = top + target_height;
    mapping.source_box = {left, top, 0, right, bottom, 1};
    mapping.width = static_cast<int32_t>(target_width);
    mapping.height = static_cast<int32_t>(target_height);
    return true;
}

int32_t eye_crop_width(const XrFovf &fov, uint32_t source_width)
{
    const float left = std::tan(fov.angleLeft);
    const float right = std::tan(fov.angleRight);
    const float symmetric = (std::max)(-left, right);
    if (!std::isfinite(left) || !std::isfinite(right) || symmetric <= 0.001f) return -1;
    const long first = std::lround(((left / symmetric) + 1.0f) * 0.5f * source_width);
    const long last = std::lround(((right / symmetric) + 1.0f) * 0.5f * source_width);
    return static_cast<int32_t>(last - first);
}

int32_t eye_crop_height(const XrFovf &fov, uint32_t source_height)
{
    const float down = std::tan(fov.angleDown);
    const float up = std::tan(fov.angleUp);
    const float symmetric = (std::max)(-down, up);
    if (!std::isfinite(down) || !std::isfinite(up) || symmetric <= 0.001f) return -1;
    const long first = std::lround((1.0f - (up / symmetric)) * 0.5f * source_height);
    const long last = std::lround((1.0f - (down / symmetric)) * 0.5f * source_height);
    return static_cast<int32_t>(last - first);
}

struct EyeSwapchain
{
    XrSwapchain handle = XR_NULL_HANDLE;
    uint32_t width = 0, height = 0;
    int64_t format = 0;
    std::vector<XrSwapchainImageD3D11KHR> images;
    std::vector<ID3D11RenderTargetView *> render_targets;
};

bool choose_vr_source_size(const std::array<XrView,2> &views, const EyeSwapchain (&eyes)[2],
                           uint32_t &width, uint32_t &height)
{
    width = height = 0;
    for (uint32_t candidate = 16; candidate <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION; ++candidate)
    {
        const int32_t left = eye_crop_width(views[0].fov, candidate);
        const int32_t right = eye_crop_width(views[1].fov, candidate);
        if (left <= 0 || right <= 0) return false;
        if (left <= static_cast<int32_t>(eyes[0].width) && right <= static_cast<int32_t>(eyes[1].width))
            width = candidate;
        else break;
    }
    for (uint32_t candidate = 16; candidate <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION; ++candidate)
    {
        const int32_t left = eye_crop_height(views[0].fov, candidate);
        const int32_t right = eye_crop_height(views[1].fov, candidate);
        if (left <= 0 || right <= 0) return false;
        if (left <= static_cast<int32_t>(eyes[0].height) && right <= static_cast<int32_t>(eyes[1].height))
            height = candidate;
        else break;
    }
    if (width < eyes[0].width || height < eyes[0].height) return false;
    if (width > UINT32_MAX - 2 * kVrCullingMarginPixels ||
        height > UINT32_MAX - 2 * kVrCullingMarginPixels) return false;
    width = align_up(width + 2 * kVrCullingMarginPixels, kVrRenderTargetAlignment);
    height = align_up(height + 2 * kVrCullingMarginPixels, kVrRenderTargetAlignment);
    return width >= eyes[0].width && height >= eyes[0].height &&
        width <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION &&
        height <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
}

ID3D11Texture2D *refresh_present_frame_target(void *manager, uint32_t width, uint32_t height,
                                               DXGI_FORMAT format);

// The mirror is also drawn immediately after the left-eye copy. The outer game
// renderer still owns this context, so preserve every state slot touched by the
// scaling pass. This keeps the direct pre-present path safe across window modes
// without adding a third scene render.
struct MirrorContextState
{
    ID3D11DeviceContext *context = nullptr;
    ID3D11RenderTargetView *targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView *depth = nullptr;
    ID3D11BlendState *blend = nullptr;
    float blend_factor[4]{};
    UINT sample_mask = 0xffffffffu;
    ID3D11DepthStencilState *depth_state = nullptr;
    UINT stencil_reference = 0;
    ID3D11RasterizerState *rasterizer = nullptr;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT scissor_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11InputLayout *input_layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer *vertex_buffer = nullptr;
    UINT vertex_stride = 0, vertex_offset = 0;
    ID3D11VertexShader *vertex_shader = nullptr;
    ID3D11GeometryShader *geometry_shader = nullptr;
    ID3D11HullShader *hull_shader = nullptr;
    ID3D11DomainShader *domain_shader = nullptr;
    ID3D11PixelShader *pixel_shader = nullptr;
    ID3D11Buffer *vertex_constant = nullptr;
    ID3D11ShaderResourceView *pixel_resource = nullptr;
    ID3D11SamplerState *pixel_sampler = nullptr;

    explicit MirrorContextState(ID3D11DeviceContext *value) : context(value)
    {
        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depth);
        context->OMGetBlendState(&blend, blend_factor, &sample_mask);
        context->OMGetDepthStencilState(&depth_state, &stencil_reference);
        context->RSGetState(&rasterizer);
        context->RSGetViewports(&viewport_count, viewports);
        context->RSGetScissorRects(&scissor_count, scissors);
        context->IAGetInputLayout(&input_layout);
        context->IAGetPrimitiveTopology(&topology);
        context->IAGetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
        context->VSGetShader(&vertex_shader, nullptr, nullptr);
        context->GSGetShader(&geometry_shader, nullptr, nullptr);
        context->HSGetShader(&hull_shader, nullptr, nullptr);
        context->DSGetShader(&domain_shader, nullptr, nullptr);
        context->PSGetShader(&pixel_shader, nullptr, nullptr);
        context->VSGetConstantBuffers(0, 1, &vertex_constant);
        context->PSGetShaderResources(0, 1, &pixel_resource);
        context->PSGetSamplers(0, 1, &pixel_sampler);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
    }

    ~MirrorContextState()
    {
        context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depth);
        context->OMSetBlendState(blend, blend_factor, sample_mask);
        context->OMSetDepthStencilState(depth_state, stencil_reference);
        context->RSSetState(rasterizer);
        context->RSSetViewports(viewport_count, viewports);
        context->RSSetScissorRects(scissor_count, scissors);
        context->IASetInputLayout(input_layout);
        context->IASetPrimitiveTopology(topology);
        context->IASetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
        context->VSSetShader(vertex_shader, nullptr, 0);
        context->GSSetShader(geometry_shader, nullptr, 0);
        context->HSSetShader(hull_shader, nullptr, 0);
        context->DSSetShader(domain_shader, nullptr, 0);
        context->PSSetShader(pixel_shader, nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &vertex_constant);
        context->PSSetShaderResources(0, 1, &pixel_resource);
        context->PSSetSamplers(0, 1, &pixel_sampler);
        if (pixel_sampler) pixel_sampler->Release();
        if (pixel_resource) pixel_resource->Release();
        if (vertex_constant) vertex_constant->Release();
        if (pixel_shader) pixel_shader->Release();
        if (domain_shader) domain_shader->Release();
        if (hull_shader) hull_shader->Release();
        if (geometry_shader) geometry_shader->Release();
        if (vertex_shader) vertex_shader->Release();
        if (vertex_buffer) vertex_buffer->Release();
        if (input_layout) input_layout->Release();
        if (rasterizer) rasterizer->Release();
        if (depth_state) depth_state->Release();
        if (blend) blend->Release();
        if (depth) depth->Release();
        for (auto *target : targets) if (target) target->Release();
    }
};

struct OpenXrState
{
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false;
    uint64_t last_focused_ms = 0;
    bool initialized = false;
    ID3D11Device *graphics_device = nullptr;
    ID3D11DeviceContext *graphics_context = nullptr;
    bool foreign_context_logged = false;
    IDXGISwapChain *game_swapchain = nullptr;
    bool anchor_valid = false;
    bool eye_math_logged = false;
    bool camera_mode_known = false;
    bool camera_mode_seated = false;
    StandingCameraState standing_camera{};
    XrPosef anchor_head{{0,0,0,1},{0,0,0}};
    Mat4 anchor_game{};
    EyeSwapchain eyes[2];
    uint32_t source_width = 0, source_height = 0;
    uint32_t desktop_width = 0, desktop_height = 0;
    DXGI_FORMAT source_format = DXGI_FORMAT_UNKNOWN;
    PFN_xrGetD3D11GraphicsRequirementsKHR get_requirements = nullptr;
    ID3D11Texture2D *mirror_texture = nullptr;
    ID3D11ShaderResourceView *mirror_view = nullptr;
    ID3D11VertexShader *mirror_vertex_shader = nullptr;
    ID3D11PixelShader *mirror_pixel_shader = nullptr;
    ID3D11SamplerState *mirror_sampler = nullptr;
    ID3D11Buffer *mirror_constants = nullptr;
    ID3D11RasterizerState *mirror_rasterizer = nullptr;
    ID3D11RenderTargetView *mirror_backbuffer_target = nullptr;
    IUnknown *mirror_backbuffer_identity = nullptr;
    bool mirror_ready = false;
    bool mirror_logged = false;
    bool mirror_failed = false;
    bool startup_desktop_ui_logged = false;
    bool render_size_override_logged = false;
    bool render_size_restore_logged = false;
    uint64_t mirror_source_copies = 0;
    uint64_t mirror_direct_draws = 0;
    uint64_t mirror_present_draws = 0;
    uint64_t mirror_manual_presents = 0;
    uint64_t mirror_present_busy = 0;
    bool mirror_manual_present_failed = false;
    uint8_t *render_size_base = nullptr;
    uint32_t original_render_width = 0;
    uint32_t original_render_height = 0;
    uint32_t override_render_width = 0;
    uint32_t override_render_height = 0;
    float original_render_scale = 1.0f;
    float original_reported_width = 0.0f;
    float original_reported_height = 0.0f;
    bool render_size_overridden = false;
    uint32_t pending_desktop_width = 0;
    uint32_t pending_desktop_height = 0;
    bool frame_pending = false;
    bool pending_startup_menu_composited = false;
    XrTime pending_display_time = 0;
    std::array<XrView,2> pending_views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};
    std::array<XrCompositionLayerProjectionView,2> pending_projection_views{{
        {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
        {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}
    }};
    uint32_t pending_indices[2]{};
    bool pending_acquired[2]{};
    bool pending_rendered[2]{};
    double pending_eye_ms[2]{};
    LARGE_INTEGER pending_render_begin{};
    LARGE_INTEGER pending_frequency{};

    void set_game_swapchain(IDXGISwapChain *value)
    {
        if (value == game_swapchain) return;
        if (value) value->AddRef();
        if (game_swapchain) game_swapchain->Release();
        game_swapchain = value;
    }

    ID3D11DeviceContext *bound_context(ID3D11DeviceContext *candidate, const char *stage)
    {
        if (!graphics_context) return nullptr;
        if (candidate && candidate != graphics_context && !foreign_context_logged)
        {
            foreign_context_logged = true;
            log_line("D3D11_FOREIGN_CONTEXT_IGNORED stage=%s candidate=%p bound=%p",
                stage ? stage : "unknown", candidate, graphics_context);
        }
        return graphics_context;
    }

    void wait_for_gpu_idle_before_destroy()
    {
        if (!graphics_device || !graphics_context) return;
        D3D11_QUERY_DESC query_desc{};
        query_desc.Query = D3D11_QUERY_EVENT;
        ID3D11Query *query = nullptr;
        HRESULT hr = graphics_device->CreateQuery(&query_desc, &query);
        if (FAILED(hr) || !query)
        {
            graphics_context->Flush();
            log_line("D3D11_GPU_IDLE_WAIT_UNAVAILABLE stage=xr_destroy hr=%08x",
                static_cast<unsigned>(hr));
            return;
        }

        const uint64_t started = GetTickCount64();
        graphics_context->End(query);
        graphics_context->Flush();
        BOOL complete = FALSE;
        for (;;)
        {
            hr = graphics_context->GetData(query, &complete, sizeof(complete),
                D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr == S_OK && complete) break;
            if (FAILED(hr) || GetTickCount64() - started >= 2000)
            {
                const HRESULT removed = graphics_device->GetDeviceRemovedReason();
                log_line("D3D11_GPU_IDLE_WAIT_FAILED stage=xr_destroy hr=%08x removed=%08x elapsed_ms=%llu",
                    static_cast<unsigned>(hr), static_cast<unsigned>(removed),
                    static_cast<unsigned long long>(GetTickCount64() - started));
                break;
            }
            SwitchToThread();
        }
        query->Release();
    }

    void restore_render_size_override()
    {
        if (!render_size_overridden) return;
        const bool fields_readable = render_size_base &&
            readable(render_size_base + kReportedWidthOffset, sizeof(float) * 2) &&
            readable(render_size_base + kRenderWidthOffset, sizeof(uint32_t) * 2) &&
            readable(render_size_base + kRenderScaleOffset, sizeof(float));
        if (!fields_readable)
        {
            log_line("FAIL stage=restore_vr_render_size reason=renderer_unreadable");
            mark_openxr_failed();
        }
        else
        {
            std::memcpy(render_size_base + kReportedWidthOffset, &original_reported_width,
                sizeof(original_reported_width));
            std::memcpy(render_size_base + kReportedHeightOffset, &original_reported_height,
                sizeof(original_reported_height));
            std::memcpy(render_size_base + kRenderWidthOffset, &original_render_width,
                sizeof(original_render_width));
            std::memcpy(render_size_base + kRenderHeightOffset, &original_render_height,
                sizeof(original_render_height));
            std::memcpy(render_size_base + kRenderScaleOffset, &original_render_scale,
                sizeof(original_render_scale));
            if (!render_size_restore_logged)
            {
                render_size_restore_logged=true;
                log_line("VR_RENDER_SIZE_RESTORED desktop=%ux%u scale=%.6f",
                    original_render_width, original_render_height, original_render_scale);
            }
        }
        render_size_base = nullptr;
        override_render_width = override_render_height = 0;
        render_size_overridden = false;
    }

    bool apply_render_size_override(void *renderer, uint32_t width, uint32_t height)
    {
        auto *base = static_cast<uint8_t *>(renderer);
        if (!base || width < 16 || height < 16 ||
            !readable(base + kReportedWidthOffset, sizeof(float) * 2) ||
            !readable(base + kRenderWidthOffset, sizeof(uint32_t) * 2) ||
            !readable(base + kRenderScaleOffset, sizeof(float))) return false;

        if (render_size_overridden &&
            (render_size_base != base || override_render_width != width || override_render_height != height))
            restore_render_size_override();

        if (render_size_overridden && pending_desktop_width != 0 && pending_desktop_height != 0)
        {
            // The desktop swapchain changed mode or monitor resolution while VR
            // owned these fields. Preserve that new desktop extent for the later
            // restore without changing the OpenXR eye swapchains or VR source.
            original_render_width = pending_desktop_width;
            original_render_height = pending_desktop_height;
            desktop_width = pending_desktop_width;
            desktop_height = pending_desktop_height;
            pending_desktop_width = pending_desktop_height = 0;
            log_line("VR_DESKTOP_RESOLUTION_UPDATED size=%ux%u vr_source=%ux%u",
                original_render_width, original_render_height, width, height);
        }

        if (!render_size_overridden)
        {
            std::memcpy(&original_reported_width, base + kReportedWidthOffset,
                sizeof(original_reported_width));
            std::memcpy(&original_reported_height, base + kReportedHeightOffset,
                sizeof(original_reported_height));
            std::memcpy(&original_render_width, base + kRenderWidthOffset,
                sizeof(original_render_width));
            std::memcpy(&original_render_height, base + kRenderHeightOffset,
                sizeof(original_render_height));
            std::memcpy(&original_render_scale, base + kRenderScaleOffset,
                sizeof(original_render_scale));
            render_size_base = base;
            override_render_width = width;
            override_render_height = height;
            render_size_overridden = true;
            if (!render_size_override_logged)
            {
                render_size_override_logged=true;
                log_line("VR_RENDER_SIZE_PERSISTENT desktop=%ux%u vr=%ux%u desktop_setting_unchanged=1",
                    original_render_width, original_render_height, width, height);
            }
        }

        constexpr float one = 1.0f;
        std::memcpy(base + kRenderWidthOffset, &width, sizeof(width));
        std::memcpy(base + kRenderHeightOffset, &height, sizeof(height));
        std::memcpy(base + kRenderScaleOffset, &one, sizeof(one));
        return true;
    }

    void release_mirror_backbuffer()
    {
        if (mirror_backbuffer_identity) mirror_backbuffer_identity->Release();
        if (mirror_backbuffer_target) mirror_backbuffer_target->Release();
        mirror_backbuffer_identity = nullptr;
        mirror_backbuffer_target = nullptr;
    }

    void release_mirror()
    {
        release_mirror_backbuffer();
        if (mirror_rasterizer) mirror_rasterizer->Release();
        if (mirror_constants) mirror_constants->Release();
        if (mirror_sampler) mirror_sampler->Release();
        if (mirror_pixel_shader) mirror_pixel_shader->Release();
        if (mirror_vertex_shader) mirror_vertex_shader->Release();
        if (mirror_view) mirror_view->Release();
        if (mirror_texture) mirror_texture->Release();
        mirror_rasterizer = nullptr;
        mirror_constants = nullptr;
        mirror_sampler = nullptr;
        mirror_pixel_shader = nullptr;
        mirror_vertex_shader = nullptr;
        mirror_view = nullptr;
        mirror_texture = nullptr;
        mirror_ready = false;
        mirror_logged = false;
    }

    bool initialize_mirror(ID3D11Device *device)
    {
        if (mirror_texture && mirror_view && mirror_vertex_shader && mirror_pixel_shader &&
            mirror_sampler && mirror_constants && mirror_rasterizer) return true;
        if (mirror_failed || !device || eyes[0].width == 0 || eyes[0].height == 0) return false;

        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = eyes[0].width;
        texture.Height = eyes[0].height;
        texture.MipLevels = 1;
        texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        texture.SampleDesc.Count = 1;
        texture.Usage = D3D11_USAGE_DEFAULT;
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device->CreateTexture2D(&texture, nullptr, &mirror_texture);
        D3D11_SHADER_RESOURCE_VIEW_DESC source_view{};
        source_view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        source_view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        source_view.Texture2D.MostDetailedMip = 0;
        source_view.Texture2D.MipLevels = 1;
        if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(mirror_texture, &source_view, &mirror_view);

        const char *shader = R"(
            cbuffer MirrorConstants : register(b0) { float4 uvTransform; };
            struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
            VSOut vs_main(uint id : SV_VertexID) {
                float2 uv = float2((id << 1) & 2, id & 2);
                VSOut output;
                output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
                output.uv = uv * uvTransform.xy + uvTransform.zw;
                return output;
            }
            Texture2D eyeTexture : register(t0);
            SamplerState eyeSampler : register(s0);
            float4 ps_main(VSOut input) : SV_TARGET {
                return eyeTexture.SampleLevel(eyeSampler, input.uv, 0.0);
            }
        )";
        HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
        using CompileFn = HRESULT (WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *,
            ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
        auto compile = compiler ? reinterpret_cast<CompileFn>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
        ID3DBlob *vertex_blob = nullptr;
        ID3DBlob *pixel_blob = nullptr;
        ID3DBlob *errors = nullptr;
        if (SUCCEEDED(hr) && compile)
            hr = compile(shader, std::strlen(shader), "smvr_left_eye_mirror", nullptr, nullptr,
                "vs_main", "vs_5_0", 0, 0, &vertex_blob, &errors);
        else if (SUCCEEDED(hr)) hr = E_NOINTERFACE;
        if (FAILED(hr) && errors)
            log_line("DESKTOP_MIRROR_SHADER_FAIL stage=vertex hr=%08x message=%s",
                static_cast<unsigned>(hr), static_cast<const char *>(errors->GetBufferPointer()));
        if (errors) { errors->Release(); errors = nullptr; }
        if (SUCCEEDED(hr))
            hr = compile(shader, std::strlen(shader), "smvr_left_eye_mirror", nullptr, nullptr,
                "ps_main", "ps_5_0", 0, 0, &pixel_blob, &errors);
        if (FAILED(hr) && errors)
            log_line("DESKTOP_MIRROR_SHADER_FAIL stage=pixel hr=%08x message=%s",
                static_cast<unsigned>(hr), static_cast<const char *>(errors->GetBufferPointer()));
        if (errors) errors->Release();
        if (compiler) FreeLibrary(compiler);

        if (SUCCEEDED(hr)) hr = device->CreateVertexShader(vertex_blob->GetBufferPointer(),
            vertex_blob->GetBufferSize(), nullptr, &mirror_vertex_shader);
        if (SUCCEEDED(hr)) hr = device->CreatePixelShader(pixel_blob->GetBufferPointer(),
            pixel_blob->GetBufferSize(), nullptr, &mirror_pixel_shader);
        if (vertex_blob) vertex_blob->Release();
        if (pixel_blob) pixel_blob->Release();

        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_BUFFER_DESC constants{};
        constants.ByteWidth = 16;
        constants.Usage = D3D11_USAGE_DEFAULT;
        constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = D3D11_CULL_NONE;
        rasterizer.DepthClipEnable = TRUE;
        if (SUCCEEDED(hr)) hr = device->CreateSamplerState(&sampler, &mirror_sampler);
        if (SUCCEEDED(hr)) hr = device->CreateBuffer(&constants, nullptr, &mirror_constants);
        if (SUCCEEDED(hr)) hr = device->CreateRasterizerState(&rasterizer, &mirror_rasterizer);
        if (FAILED(hr))
        {
            log_line("DESKTOP_MIRROR_INIT_FAIL hr=%08x", static_cast<unsigned>(hr));
            release_mirror();
            mirror_failed = true;
            return false;
        }
        log_line("DESKTOP_MIRROR_READY source=%ux%u format=R8G8B8A8_UNORM", eyes[0].width, eyes[0].height);
        return true;
    }

    bool mirror_left_eye(ID3D11Device *device, ID3D11DeviceContext *context,
                         IDXGISwapChain *swapchain, bool from_present_callback)
    {
        if (!mirror_ready || mirror_failed || !device || !context || !swapchain ||
            !mirror_view || !mirror_vertex_shader || !mirror_pixel_shader) return false;

        ID3D11Texture2D *backbuffer = nullptr;
        HRESULT hr = swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
        if (FAILED(hr) || !backbuffer) return false;
        D3D11_TEXTURE2D_DESC description{};
        backbuffer->GetDesc(&description);
        IUnknown *identity = nullptr;
        hr = backbuffer->QueryInterface(IID_PPV_ARGS(&identity));
        if (FAILED(hr) || !identity || description.SampleDesc.Count != 1 ||
            description.Width == 0 || description.Height == 0)
        {
            if (identity) identity->Release();
            backbuffer->Release();
            return false;
        }

        if (identity != mirror_backbuffer_identity)
        {
            if (mirror_backbuffer_identity) mirror_backbuffer_identity->Release();
            if (mirror_backbuffer_target) mirror_backbuffer_target->Release();
            mirror_backbuffer_identity = nullptr;
            mirror_backbuffer_target = nullptr;
            D3D11_RENDER_TARGET_VIEW_DESC target_desc{};
            target_desc.Format = description.Format;
            if (target_desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
                target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            target_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            target_desc.Texture2D.MipSlice = 0;
            hr = device->CreateRenderTargetView(backbuffer, &target_desc, &mirror_backbuffer_target);
            if (FAILED(hr) || !mirror_backbuffer_target)
            {
                identity->Release();
                backbuffer->Release();
                return false;
            }
            mirror_backbuffer_identity = identity;
        }
        else
        {
            identity->Release();
        }
        backbuffer->Release();

        const float source_aspect = static_cast<float>(eyes[0].width) / static_cast<float>(eyes[0].height);
        const float target_aspect = static_cast<float>(description.Width) / static_cast<float>(description.Height);
        float uv_transform[4] = {1.0f, 1.0f, 0.0f, 0.0f};
        if (target_aspect > source_aspect)
        {
            uv_transform[1] = source_aspect / target_aspect;
            uv_transform[3] = (1.0f - uv_transform[1]) * 0.5f;
        }
        else
        {
            uv_transform[0] = target_aspect / source_aspect;
            uv_transform[2] = (1.0f - uv_transform[0]) * 0.5f;
        }

        MirrorContextState preserved_state(context);
        context->UpdateSubresource(mirror_constants, 0, nullptr, uv_transform, 0, 0);
        context->OMSetRenderTargets(1, &mirror_backbuffer_target, nullptr);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
        context->OMSetDepthStencilState(nullptr, 0);
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(description.Width),
            static_cast<float>(description.Height), 0.0f, 1.0f};
        context->RSSetViewports(1, &viewport);
        context->RSSetState(mirror_rasterizer);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer *no_vertex_buffer = nullptr;
        UINT zero = 0;
        context->IASetVertexBuffers(0, 1, &no_vertex_buffer, &zero, &zero);
        context->VSSetShader(mirror_vertex_shader, nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &mirror_constants);
        context->PSSetShader(mirror_pixel_shader, nullptr, 0);
        context->PSSetShaderResources(0, 1, &mirror_view);
        context->PSSetSamplers(0, 1, &mirror_sampler);
        context->Draw(3, 0);
        ID3D11ShaderResourceView *no_source = nullptr;
        context->PSSetShaderResources(0, 1, &no_source);
        context->OMSetRenderTargets(0, nullptr, nullptr);

        if (!mirror_logged)
        {
            mirror_logged = true;
            log_line("DESKTOP_LEFT_EYE_MIRROR_ACTIVE source=%ux%u backbuffer=%ux%u third_scene_render=0",
                eyes[0].width, eyes[0].height, description.Width, description.Height);
        }
        uint64_t &draw_count = from_present_callback ? mirror_present_draws : mirror_direct_draws;
        ++draw_count;
        if (draw_count % 300 == 0)
            log_line("DESKTOP_LEFT_EYE_MIRROR_PROGRESS route=%s draws=%llu source_copies=%llu",
                from_present_callback ? "present_callback" : "direct_pre_present",
                static_cast<unsigned long long>(draw_count),
                static_cast<unsigned long long>(mirror_source_copies));
        return true;
    }

    // With the persistent VR render extent, Scrap Mechanic's internal present
    // buffer remains eye-sized. The engine correctly refuses to copy that
    // 2565x2711 resource into an unrelated desktop-sized backbuffer, which means
    // it never reaches a useful desktop Present while the headset is active.
    // The mirror pass above has already scaled the completed left eye into the
    // real swapchain buffer, so present that buffer directly. This is one cheap
    // desktop composite and one swapchain flip, never a third scene render.
    // DO_NOT_WAIT keeps desktop refresh from throttling the OpenXR render loop.
    bool present_left_eye_mirror()
    {
        if (!game_swapchain || !mirror_ready) return false;
        const HRESULT hr = game_swapchain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
        {
            ++mirror_present_busy;
            return true;
        }
        if (FAILED(hr))
        {
            if (!mirror_manual_present_failed)
            {
                mirror_manual_present_failed = true;
                log_line("DESKTOP_LEFT_EYE_MIRROR_PRESENT_FAIL hr=%08x",
                    static_cast<unsigned>(hr));
            }
            return false;
        }
        ++mirror_manual_presents;
        if (mirror_manual_presents <= 4 || (mirror_manual_presents % 300) == 0)
            log_line("DESKTOP_LEFT_EYE_MIRROR_PRESENT_OK count=%llu busy=%llu sync=0 do_not_wait=1 third_scene_render=0",
                static_cast<unsigned long long>(mirror_manual_presents),
                static_cast<unsigned long long>(mirror_present_busy));
        return true;
    }

    bool finish_pending_frame(ID3D11DeviceContext *context, bool from_present_callback = false)
    {
        if (!frame_pending) return true;
        context = bound_context(context, "finish_pending_frame");
        if (!context) return fail("bound_d3d11_context_missing", XR_ERROR_GRAPHICS_DEVICE_INVALID);
        const bool startup_menu_composited = pending_startup_menu_composited;
        if (context && mirror_texture && pending_acquired[0])
        {
            context->CopyResource(mirror_texture, eyes[0].images[pending_indices[0]].texture);
            mirror_ready = true;
            ++mirror_source_copies;
            ID3D11Device *device = graphics_device;
            if (device && game_swapchain)
            {
                // Only the game's real Present boundary is after its UI pass.
                // Capturing here from the render hook reads an incomplete/stale
                // backbuffer (and produced the cropped submenu artifacts). In
                // startup-menu mode the frame is deferred to on_present, where
                // the full desktop UI is copied before the VR mirror replaces it.
                if (from_present_callback && g_feature_startup_menu_enabled &&
                    g_startup_menu.visible() && g_startup_menu.native_capture_due())
                    g_startup_menu.capture_native_menu(context,game_swapchain);
                mirror_left_eye(device, context, game_swapchain, from_present_callback);
            }
        }
        if (context) context->Flush();
        if (!from_present_callback && mirror_ready && game_swapchain &&
            !present_left_eye_mirror())
        {
            const uint64_t failures = g_mirror_present_failures.fetch_add(1, std::memory_order_relaxed) + 1;
            if (!g_mirror_present_failure_logged.exchange(true, std::memory_order_acq_rel))
                log_line("DESKTOP_LEFT_EYE_MIRROR_FAIL route=manual_present first=%llu",
                    static_cast<unsigned long long>(failures));
        }

        XrResult release_failure = XR_SUCCESS;
        for (uint32_t i = 0; i < 2; ++i)
        {
            if (!pending_acquired[i]) continue;
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            const XrResult result = xrReleaseSwapchainImage(eyes[i].handle, &release);
            if (XR_FAILED(result) && XR_SUCCEEDED(release_failure)) release_failure = result;
            pending_acquired[i] = false;
        }

        const bool both_rendered = pending_rendered[0] && pending_rendered[1];
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = space;
        layer.viewCount = 2;
        layer.views = pending_projection_views.data();
        const XrCompositionLayerBaseHeader *layers[] = {
            reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer)
        };
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = pending_display_time;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = both_rendered && XR_SUCCEEDED(release_failure) ? 1u : 0u;
        end.layers = end.layerCount ? layers : nullptr;
        const XrResult end_result = xrEndFrame(session, &end);
        frame_pending = false;
        pending_startup_menu_composited = false;
        pending_display_time = 0;
        pending_rendered[0] = pending_rendered[1] = false;

        if (XR_FAILED(release_failure)) return fail("xrReleaseSwapchainImage", release_failure);
        if (!both_rendered) return fail("both_eyes_not_rendered", XR_ERROR_RUNTIME_FAILURE);
        if (XR_FAILED(end_result)) return fail("xrEndFrame", end_result);

        LARGE_INTEGER frame_render_end{};
        QueryPerformanceCounter(&frame_render_end);
        const double render_ms = pending_frequency.QuadPart > 0
            ? static_cast<double>(frame_render_end.QuadPart - pending_render_begin.QuadPart) * 1000.0 /
                static_cast<double>(pending_frequency.QuadPart)
            : 0.0;
        const uint64_t completed = g_openxr_success_frames.fetch_add(1) + 1;
        if (completed <= 10 || (completed % 120) == 0)
            log_line("OPENXR_FRAME_SUCCESS frame=%llu left_rendered=1 right_rendered=1 xrEndFrame=0 render_ms=%.3f eye_ms=%.3f,%.3f startup_menu=%u",
                static_cast<unsigned long long>(completed), render_ms,
                pending_eye_ms[0], pending_eye_ms[1], startup_menu_composited ? 1u : 0u);
        return true;
    }

    void abandon_pending_frame()
    {
        if (!frame_pending) return;
        for (uint32_t i = 0; i < 2; ++i)
        {
            if (!pending_acquired[i]) continue;
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xrReleaseSwapchainImage(eyes[i].handle, &release);
            pending_acquired[i] = false;
        }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = pending_display_time;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        xrEndFrame(session, &end);
        frame_pending = false;
        pending_display_time = 0;
        pending_rendered[0] = pending_rendered[1] = false;
        pending_startup_menu_composited = false;
        g_startup_menu.reset_state();
        log_line("VR_PENDING_FRAME_ABANDONED layers=0");
    }

    void destroy()
    {
        g_vr_target_tracking_locked.store(false, std::memory_order_release);
        abandon_pending_frame();
        // xrDestroySwapchain requires all graphics commands that reference its
        // images to have completed. Keep this blocking wait on teardown only;
        // per-frame image release is intentionally asynchronous.
        wait_for_gpu_idle_before_destroy();
        // Keep a valid inactive bridge on disk. Deleting it makes the Lua side
        // call sm.json.open on a missing file every update while no HMD exists.
        deactivate_hand_bridge();
        restore_render_size_override();
        restore_viewmodel_pass_patch();
        release_mirror();
        mirror_failed = false;
        startup_desktop_ui_logged = false;
        render_size_override_logged = false;
        render_size_restore_logged = false;
        g_startup_menu.shutdown();
        g_input.shutdown();
        scrapvr::hands::shutdown();
        if (running && session != XR_NULL_HANDLE) xrEndSession(session);
        running = false;
        for (auto &eye : eyes)
        {
            for (auto *target : eye.render_targets) if (target) target->Release();
            if (eye.handle != XR_NULL_HANDLE) xrDestroySwapchain(eye.handle);
            eye = {};
        }
        if (space != XR_NULL_HANDLE) xrDestroySpace(space);
        if (session != XR_NULL_HANDLE) xrDestroySession(session);
        if (instance != XR_NULL_HANDLE) xrDestroyInstance(instance);
        set_game_swapchain(nullptr);
        if (graphics_context) graphics_context->Release();
        if (graphics_device) graphics_device->Release();
        graphics_context = nullptr;
        graphics_device = nullptr;
        foreign_context_logged = false;
        instance = XR_NULL_HANDLE; system = XR_NULL_SYSTEM_ID; session = XR_NULL_HANDLE; space = XR_NULL_HANDLE;
        initialized = false; anchor_valid = false; eye_math_logged = false; state = XR_SESSION_STATE_UNKNOWN;
        camera_mode_known = false; camera_mode_seated = false; standing_camera.reset();
        last_focused_ms = 0;
        source_width = source_height = desktop_width = desktop_height = 0;
        pending_desktop_width = pending_desktop_height = 0;
    }

    bool fail(const char *stage, XrResult result)
    {
        char result_name[XR_MAX_RESULT_STRING_SIZE]{};
        if (instance != XR_NULL_HANDLE) xrResultToString(instance, result, result_name);
        log_line("FAIL stage=%s xr=%d result=%s", stage, static_cast<int>(result), result_name);
        mark_openxr_failed();
        return false;
    }

    bool initialize(ID3D11Device *device, const D3D11_TEXTURE2D_DESC &source)
    {
        if (g_explicit_vr_launch.begin_attempt(GetTickCount64()))
        {
            log_line("VR_LAUNCH_HANDOFF retry_budget_started=1");
        }
        if (!device) return fail("d3d11_graphics_device_missing", XR_ERROR_GRAPHICS_DEVICE_INVALID);
        device->AddRef();
        graphics_device = device;
        device->GetImmediateContext(&graphics_context);
        if (!graphics_context)
            return fail("d3d11_graphics_context_missing", XR_ERROR_GRAPHICS_DEVICE_INVALID);
        log_line("XR_D3D11_BINDING_READY device=%p context=%p source_format=%u",
            graphics_device, graphics_context, static_cast<unsigned>(source.Format));

        uint32_t extension_count = 0;
        XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extension_count, nullptr);
        if (XR_FAILED(result)) return fail("xrEnumerateInstanceExtensionProperties.count", result);
        std::vector<XrExtensionProperties> extension_properties(
            extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
        result = xrEnumerateInstanceExtensionProperties(
            nullptr, extension_count, &extension_count, extension_properties.data());
        if (XR_FAILED(result)) return fail("xrEnumerateInstanceExtensionProperties", result);
        bool hand_tracking_supported = false;
        bool generic_controller_supported = false;
        for (const auto &extension : extension_properties)
        {
            if (std::strcmp(extension.extensionName, XR_EXT_HAND_TRACKING_EXTENSION_NAME) == 0)
                hand_tracking_supported = true;
            else if (std::strcmp(extension.extensionName,
                         XR_KHR_GENERIC_CONTROLLER_EXTENSION_NAME) == 0)
                generic_controller_supported = true;
        }
        std::vector<const char *> extensions{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
        if (g_feature_input_enabled && g_feature_optical_hands_enabled && hand_tracking_supported)
            extensions.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        if (g_feature_input_enabled && generic_controller_supported)
            extensions.push_back(XR_KHR_GENERIC_CONTROLLER_EXTENSION_NAME);
        XrInstanceCreateInfo instance_info{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(instance_info.applicationInfo.applicationName, "Scrap Mechanic Native VR v1");
        instance_info.applicationInfo.applicationVersion = 1;
        strcpy_s(instance_info.applicationInfo.engineName, "Scrap Mechanic 1.0");
        instance_info.applicationInfo.engineVersion = 876;
        bool openxr_11_enabled = true;
        instance_info.applicationInfo.apiVersion = XR_MAKE_VERSION(1,1,0);
        instance_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instance_info.enabledExtensionNames = extensions.data();
        result = xrCreateInstance(&instance_info, &instance);
        if (result == XR_ERROR_API_VERSION_UNSUPPORTED)
        {
            openxr_11_enabled = false;
            instance_info.applicationInfo.apiVersion = XR_MAKE_VERSION(1,0,34);
            result = xrCreateInstance(&instance_info, &instance);
        }
        if (XR_FAILED(result)) return fail("xrCreateInstance", result);
        log_line("XR_API_SELECTED version=%u.%u generic_controller_extension=%u",
            XR_VERSION_MAJOR(instance_info.applicationInfo.apiVersion),
            XR_VERSION_MINOR(instance_info.applicationInfo.apiVersion),
            generic_controller_supported ? 1u : 0u);

        XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
        result = xrGetInstanceProperties(instance, &properties);
        if (XR_FAILED(result)) return fail("xrGetInstanceProperties", result);
        if (!g_openxr_runtime_logged.exchange(true, std::memory_order_acq_rel))
            log_line("XR_RUNTIME name=%s version=%u.%u.%u", properties.runtimeName,
                XR_VERSION_MAJOR(properties.runtimeVersion), XR_VERSION_MINOR(properties.runtimeVersion), XR_VERSION_PATCH(properties.runtimeVersion));

        XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
        system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        result = xrGetSystem(instance, &system_info, &system);
        if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE)
        {
            const uint64_t now = GetTickCount64();
            const uint64_t retry_deadline =
                g_explicit_vr_launch.deadline();
            const bool retry = retry_deadline != 0 && now < retry_deadline;
            if (!g_openxr_unavailable_logged.exchange(true, std::memory_order_acq_rel))
                log_line("XR_UNAVAILABLE reason=no_hmd desktop_fallback=1 auto_retry=%u "
                    "explicit_start_vr=%u retry_window_remaining_ms=%llu",
                    retry ? 1u : 0u, retry_deadline != 0 ? 1u : 0u,
                    static_cast<unsigned long long>(retry ? retry_deadline - now : 0));
            mark_openxr_failed(retry);
            return false;
        }
        if (XR_FAILED(result)) return fail("xrGetSystem", result);
        result = xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction *>(&get_requirements));
        if (XR_FAILED(result) || !get_requirements) return fail("xrGetD3D11GraphicsRequirementsKHR.address", result);
        XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        result = get_requirements(instance, system, &requirements);
        if (XR_FAILED(result)) return fail("xrGetD3D11GraphicsRequirementsKHR", result);

        IDXGIDevice *dxgi_device = nullptr;
        IDXGIAdapter *adapter = nullptr;
        DXGI_ADAPTER_DESC adapter_desc{};
        HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
        if (SUCCEEDED(hr)) hr = dxgi_device->GetAdapter(&adapter);
        if (SUCCEEDED(hr)) hr = adapter->GetDesc(&adapter_desc);
        if (adapter) adapter->Release();
        if (dxgi_device) dxgi_device->Release();
        if (FAILED(hr) || std::memcmp(&adapter_desc.AdapterLuid, &requirements.adapterLuid, sizeof(LUID)) != 0)
        {
            log_line("FAIL stage=adapter_luid_match hr=%08x game_gpu=%ls game_luid=%08x:%08x runtime_luid=%08x:%08x",
                static_cast<unsigned>(hr), adapter_desc.Description,
                static_cast<unsigned>(adapter_desc.AdapterLuid.HighPart), adapter_desc.AdapterLuid.LowPart,
                static_cast<unsigned>(requirements.adapterLuid.HighPart), requirements.adapterLuid.LowPart);
            mark_openxr_failed(); return false;
        }

        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        binding.device = device;
        XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
        session_info.next = &binding;
        session_info.systemId = system;
        result = xrCreateSession(instance, &session_info, &session);
        if (XR_FAILED(result)) return fail("xrCreateSession", result);
        XrReferenceSpaceCreateInfo space_info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        space_info.poseInReferenceSpace.orientation.w = 1.0f;
        result = xrCreateReferenceSpace(session, &space_info, &space);
        if (XR_FAILED(result)) return fail("xrCreateReferenceSpace", result);
        features::InputConfig input_config{};
        input_config.enabled = g_feature_input_enabled;
        input_config.optical_hand_tracking = g_feature_optical_hands_enabled;
        input_config.haptics =
            GetPrivateProfileIntW(L"Features", L"Haptics", 1, g_ini_path.c_str()) == 1;
        input_config.haptic_strength = static_cast<float>(
            GetPrivateProfileIntW(L"Features", L"HapticStrengthPercent", 65,
                g_ini_path.c_str())) / 100.0f;
        input_config.stick_deadzone = static_cast<float>(
            GetPrivateProfileIntW(L"Features", L"StickDeadzonePercent", 30, g_ini_path.c_str())) / 100.0f;
        input_config.horizontal_turn_speed = static_cast<float>(
            GetPrivateProfileIntW(L"Features", L"HorizontalTurnSpeed", 36, g_ini_path.c_str()));
        input_config.vertical_turn_speed = static_cast<float>(
            GetPrivateProfileIntW(L"Features", L"VerticalTurnSpeed", 28, g_ini_path.c_str()));
        input_config.vertical_turn =
            GetPrivateProfileIntW(L"Features", L"VerticalStickLook", 1, g_ini_path.c_str()) == 1;
        // Quest 3 keeps the established layout. SteamVR/Index gets a safe
        // default that avoids the Steam system/Esc binding and avoids using
        // thumbstick-click for sprint; both profiles can be edited in the
        // [Bindings.Quest] and [Bindings.SteamVR] sections of the INI.
        features::ControllerBindings steamvr_defaults{};
        steamvr_defaults.menu = features::BindingInput::left_trackpad_click;
        steamvr_defaults.sprint = features::BindingInput::left_grip;
        input_config.quest_bindings = load_controller_bindings(L"Bindings.Quest",
            features::ControllerBindings{});
        input_config.steamvr_bindings = load_controller_bindings(L"Bindings.SteamVR",
            steamvr_defaults);
        if (!g_input.initialize(instance, session, space, input_config,
                hand_tracking_supported && g_feature_optical_hands_enabled,
                openxr_11_enabled, generic_controller_supported))
            return fail("feature_input_initialize", XR_ERROR_INITIALIZATION_FAILED);

        uint32_t view_count = 0;
        result = xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, nullptr);
        if (XR_FAILED(result) || view_count != 2) return fail("xrEnumerateViewConfigurationViews.count", result);
        std::array<XrViewConfigurationView,2> view_info{{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        result = xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &view_count, view_info.data());
        if (XR_FAILED(result)) return fail("xrEnumerateViewConfigurationViews", result);

        uint32_t format_count = 0;
        result = xrEnumerateSwapchainFormats(session, 0, &format_count, nullptr);
        if (XR_FAILED(result) || format_count == 0) return fail("xrEnumerateSwapchainFormats.count", result);
        std::vector<int64_t> formats(format_count);
        result = xrEnumerateSwapchainFormats(session, format_count, &format_count, formats.data());
        if (XR_FAILED(result)) return fail("xrEnumerateSwapchainFormats", result);
        for (uint32_t i = 0; i != format_count; ++i)
            log_line("XR_SWAPCHAIN_FORMAT index=%u format=%lld", i, static_cast<long long>(formats[i]));

        // The engine's final target is R8G8B8A8_UNORM but contains display/sRGB-
        // encoded final pixels (matching the desktop presentation). Declaring the
        // eye images as linear UNORM made Meta apply an additional output transfer,
        // producing the bright, low-contrast headset image. Use the compatible
        // SRGB member of the same DXGI format family so the compositor preserves
        // the engine's final transfer function.
        if (source.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
        {
            log_line("FAIL stage=unexpected_source_color_format format=%u", static_cast<unsigned>(source.Format));
            mark_openxr_failed();
            return false;
        }
        const int64_t required_format = static_cast<int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        bool format_supported = false;
        for (int64_t f : formats) if (f == required_format) format_supported = true;
        if (!format_supported) { log_line("FAIL stage=srgb_swapchain_format_not_supported format=%lld", static_cast<long long>(required_format)); mark_openxr_failed(); return false; }

        for (uint32_t i = 0; i != 2; ++i)
        {
            XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            create.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            create.format = required_format;
            create.sampleCount = 1;
            create.width = view_info[i].recommendedImageRectWidth;
            create.height = view_info[i].recommendedImageRectHeight;
            create.faceCount = 1;
            create.arraySize = 1;
            create.mipCount = 1;
            result = xrCreateSwapchain(session, &create, &eyes[i].handle);
            if (XR_FAILED(result)) return fail("xrCreateSwapchain", result);
            eyes[i].width = create.width; eyes[i].height = create.height; eyes[i].format = required_format;
            uint32_t image_count = 0;
            result = xrEnumerateSwapchainImages(eyes[i].handle, 0, &image_count, nullptr);
            if (XR_FAILED(result) || image_count == 0) return fail("xrEnumerateSwapchainImages.count", result);
            eyes[i].images.resize(image_count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            result = xrEnumerateSwapchainImages(eyes[i].handle, image_count, &image_count,
                reinterpret_cast<XrSwapchainImageBaseHeader *>(eyes[i].images.data()));
            if (XR_FAILED(result)) return fail("xrEnumerateSwapchainImages", result);
            if (g_feature_hands_enabled || g_feature_startup_menu_enabled)
            {
                eyes[i].render_targets.resize(image_count, nullptr);
                for (uint32_t image = 0; image < image_count; ++image)
                {
                    D3D11_RENDER_TARGET_VIEW_DESC target_desc{};
                    target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                    target_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                    target_desc.Texture2D.MipSlice = 0;
                    const HRESULT target_hr = device->CreateRenderTargetView(
                        eyes[i].images[image].texture, &target_desc, &eyes[i].render_targets[image]);
                    if (FAILED(target_hr) || !eyes[i].render_targets[image])
                    {
                        log_line("FAIL stage=eye_hand_render_target eye=%u image=%u hr=%08x",
                            i, image, static_cast<unsigned>(target_hr));
                        return false;
                    }
                }
            }
        }
		if (g_feature_hands_enabled && !scrapvr::hands::initialize(device, log_line))
        {
            scrapvr::hands::shutdown();
            g_feature_hands_enabled = false;
            log_line("VR_FEATURE_HANDS_DISABLED reason=renderer_initialization_failed visual_stereo_continues=1");
        }
        std::wstring startup_menu_asset = g_ini_path;
        const size_t startup_menu_separator = startup_menu_asset.find_last_of(L"\\/");
        startup_menu_asset.resize(startup_menu_separator == std::wstring::npos ? 0 : startup_menu_separator + 1);
        startup_menu_asset += L"ScrapMechanicVR-StartupMenu.png";
        if (g_feature_startup_menu_enabled &&
            !g_startup_menu.initialize(device, startup_menu_asset.c_str()))
        {
            g_startup_menu.shutdown();
            g_feature_startup_menu_enabled = false;
            log_line("VR_FEATURE_STARTUP_MENU_DISABLED reason=renderer_initialization_failed visual_stereo_continues=1");
        }
        if (!initialize_mirror(device))
        {
            log_line("FAIL stage=desktop_mirror_initialize");
            mark_openxr_failed();
            return false;
        }
        desktop_width = source.Width; desktop_height = source.Height; source_format = source.Format;
        initialized = true;
        g_openxr_retry_after_ms.store(0, std::memory_order_release);
        g_desktop_fallback_logged.store(false, std::memory_order_release);
        g_openxr_unavailable_logged.store(false, std::memory_order_release);
        log_line("XR_INITIALIZED desktop_source=%ux%u source_format=%u swapchain_format=%lld color_encoding=sRGB runtime_views=%ux%u,%ux%u", source.Width, source.Height,
            static_cast<unsigned>(source.Format), static_cast<long long>(required_format), view_info[0].recommendedImageRectWidth, view_info[0].recommendedImageRectHeight,
            view_info[1].recommendedImageRectWidth, view_info[1].recommendedImageRectHeight);
        return true;
    }

    void poll_events()
    {
        if (instance == XR_NULL_HANDLE) return;
        for (;;)
        {
            XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
            XrResult result = xrPollEvent(instance, &event);
            if (result == XR_EVENT_UNAVAILABLE) break;
            if (XR_FAILED(result)) { fail("xrPollEvent", result); break; }
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            {
                const auto &changed = *reinterpret_cast<const XrEventDataSessionStateChanged *>(&event);
                state = changed.state;
                g_input.on_session_state(state);
                if (state == XR_SESSION_STATE_FOCUSED) last_focused_ms = GetTickCount64();
                log_line("XR_SESSION_STATE state=%d", static_cast<int>(state));
                if (state == XR_SESSION_STATE_READY && !running)
                {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    result = xrBeginSession(session, &begin);
                    if (XR_SUCCEEDED(result))
                    {
                        running = true; anchor_valid = false;
                        camera_mode_known = false; standing_camera.reset();
                        g_startup_menu.reset_world_anchor();
                        g_startup_menu.reset_state();
                        log_line("XR_SESSION_RUNNING");
                        if (g_explicit_vr_launch.complete())
                            log_line("VR_LAUNCH_HANDOFF completed=1 xr_session_running=1");
                    }
                    else fail("xrBeginSession", result);
                }
                else if (state == XR_SESSION_STATE_STOPPING && running)
                {
                    // No frame may remain begun when the runtime transitions to
                    // STOPPING. Close a deferred UI frame before ending session.
                    abandon_pending_frame();
                    result = xrEndSession(session);
                    running = false; anchor_valid = false;
                    g_vr_target_tracking_locked.store(false, std::memory_order_release);
                    camera_mode_known = false; standing_camera.reset();
                    g_startup_menu.reset_world_anchor();
                    g_startup_menu.reset_state();
                    restore_render_size_override();
                    restore_viewmodel_pass_patch();
                    if (XR_FAILED(result)) fail("xrEndSession", result);
                }
                else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING)
                {
                    running = false;
                    g_vr_target_tracking_locked.store(false, std::memory_order_release);
                    g_startup_menu.reset_world_anchor();
                    g_startup_menu.reset_state();
                    restore_render_size_override();
                    restore_viewmodel_pass_patch();
                    mark_openxr_failed(false); log_line("FAIL stage=session_exit_or_loss state=%d", static_cast<int>(state));
                }
            }
        }
        if (state != XR_SESSION_STATE_FOCUSED)
        {
            // Keep VR authoritative only through short runtime focus hand-offs.
            // This prevents a trigger edge from becoming a desktop-crosshair shot,
            // while restoring ordinary desktop controls after the headset is off.
            const uint64_t now = GetTickCount64();
            const bool transition_authority = running && last_focused_ms != 0 &&
                now >= last_focused_ms && now - last_focused_ms <= 1500;
            deactivate_hand_bridge(transition_authority);
        }
    }

    bool render_stereo(RenderSetupFn original, void *renderer, float scalar, const float *game_world_to_view,
                       const float *game_projection, void *settings, ID3D11DeviceContext *context, ID3D11Texture2D *source)
    {
        if (!running || g_failed.load())
        {
            restore_render_size_override();
            restore_viewmodel_pass_patch();
            return false;
        }
        context = bound_context(context, "render_stereo");
        if (!context) return fail("bound_d3d11_context_missing", XR_ERROR_GRAPHICS_DEVICE_INVALID);
        g_vr_target_tracking_locked.store(true, std::memory_order_release);

        const bool player_seated = scrapvr::tools::is_player_seated();
        if (!camera_mode_known || camera_mode_seated != player_seated)
        {
            camera_mode_known = true;
            camera_mode_seated = player_seated;
            standing_camera.reset();
            log_line("VR_CAMERA_PATH seated=%u base_transform=%s",
                player_seated ? 1u : 0u,
                player_seated ? "original_game_camera" :
                    "roll_neutral_pitch_preserved_height_locked");
        }
        float upright_world_to_view[16]{};
        const float *vr_world_to_view = game_world_to_view;
        if (player_seated)
        {
            if (!world_to_view_matrix(game_world_to_view))
                return fail("seated_world_to_view", XR_ERROR_VALIDATION_FAILURE);
        }
        else
        {
            if (!build_upright_world_to_view(
                    game_world_to_view, standing_camera, upright_world_to_view))
                return fail("upright_world_to_view", XR_ERROR_VALIDATION_FAILURE);
            vr_world_to_view = upright_world_to_view;
        }
        D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
        if (desc.Format != source_format || desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1)
        {
            log_line("FAIL stage=target_changed actual=%ux%u/%u", desc.Width, desc.Height, static_cast<unsigned>(desc.Format));
            mark_openxr_failed(); return false;
        }

        XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frame_state{XR_TYPE_FRAME_STATE};
        XrResult result = xrWaitFrame(session, &wait_info, &frame_state);
        if (XR_FAILED(result)) return fail("xrWaitFrame", result);
        XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
        result = xrBeginFrame(session, &begin_info);
        if (XR_FAILED(result)) return fail("xrBeginFrame", result);
        bool acquired[2]{};
        auto abort_frame = [&](const char *stage, XrResult cause) -> bool
        {
            restore_render_size_override();
            restore_viewmodel_pass_patch();
            for (uint32_t i = 0; i != 2; ++i)
            {
                if (!acquired[i]) continue;
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                const XrResult release_result = xrReleaseSwapchainImage(eyes[i].handle, &release);
                if (XR_FAILED(release_result)) log_line("FAIL stage=abort_release eye=%u xr=%d", i, static_cast<int>(release_result));
                acquired[i] = false;
            }
            XrFrameEndInfo empty{XR_TYPE_FRAME_END_INFO};
            empty.displayTime = frame_state.predictedDisplayTime;
            empty.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            const XrResult end_result = xrEndFrame(session, &empty);
            if (XR_FAILED(end_result)) log_line("FAIL stage=abort_xrEndFrame xr=%d", static_cast<int>(end_result));
            return fail(stage, cause);
        };

        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = frame_state.predictedDisplayTime;
        locate.space = space;
        XrViewState view_state{XR_TYPE_VIEW_STATE};
        std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};
        uint32_t view_count = 0;
        result = xrLocateViews(session, &locate, &view_state, 2, &view_count, views.data());
        if (XR_FAILED(result) || view_count != 2 ||
            (view_state.viewStateFlags & (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT)) !=
            (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT))
        {
            return abort_frame("xrLocateViews", result);
        }

        XrPosef head_pose = views[0].pose;
        head_pose.position = {
            (views[0].pose.position.x + views[1].pose.position.x) * 0.5f,
            (views[0].pose.position.y + views[1].pose.position.y) * 0.5f,
            (views[0].pose.position.z + views[1].pose.position.z) * 0.5f
        };
        if (g_feature_startup_menu_enabled)
        {
            g_startup_menu.update_visibility(g_input.game_ui_open_intent());
            XrPosef menu_head=head_pose;
            menu_head.orientation=yaw_only(head_pose.orientation);
            g_startup_menu.set_world_anchor(menu_head);
        }
        g_input.set_startup_menu(g_feature_startup_menu_enabled && g_startup_menu.visible(),
            g_feature_startup_menu_enabled && g_startup_menu.pointer_active());
        scrapvr::tools::set_render_suppressed(
            g_feature_startup_menu_enabled && g_startup_menu.visible());
        if (!g_input.sync(frame_state.predictedDisplayTime, head_pose))
            log_line("VR_FEATURE_INPUT_FRAME_INCONCLUSIVE visual_stereo_continues=1");
        if (g_input.consume_recenter_request())
        {
            anchor_valid = false;
            eye_math_logged = false;
            g_startup_menu.reset_world_anchor();
            log_line("VR_RECENTER_APPLIED camera_anchor_reset=1 render_path_unchanged=1");
        }
        for (uint32_t hand = 0; hand < 2; ++hand)
        {
            const auto &tracked = g_input.hand(hand);
            scrapvr::hands::set_pose(hand, tracked.pose,
                g_feature_hands_enabled && tracked.active, tracked.optical);
            scrapvr::hands::set_finger_articulation(
                hand, tracked.finger_curls.data(),
                reinterpret_cast<const float (*)[3]>(tracked.finger_bends.data()),
                tracked.precise_fingers);
            scrapvr::hands::set_interaction(hand, tracked.interaction);
            scrapvr::hands::set_firing(hand, tracked.firing);
        }
        if (!frame_state.shouldRender)
        {
            restore_render_size_override();
            restore_viewmodel_pass_patch();
            XrFrameEndInfo empty{XR_TYPE_FRAME_END_INFO}; empty.displayTime = frame_state.predictedDisplayTime; empty.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            result = xrEndFrame(session, &empty);
            if (XR_FAILED(result)) return fail("xrEndFrame.no_render", result);
            log_line("XR_FRAME_SKIPPED shouldRender=0 xrEndFrame=0");
            return false;
        }

        if (!install_viewmodel_pass_patch())
            return abort_frame("viewmodel_pass_patch", XR_ERROR_RUNTIME_FAILURE);

        if (source_width == 0 || source_height == 0)
        {
            if (!choose_vr_source_size(views, eyes, source_width, source_height))
                return abort_frame("recommended_resolution_mapping", XR_ERROR_VALIDATION_FAILURE);
            log_line("VR_RENDER_RESOLUTION desktop=%ux%u engine_offscreen=%ux%u submitted=%ux%u,%ux%u policy=runtime_recommended_exact guard_band=0 source_alignment=%u hidden_culling_margin=%u",
                desktop_width, desktop_height, source_width, source_height,
                eyes[0].width, eyes[0].height, eyes[1].width, eyes[1].height,
                kVrRenderTargetAlignment, kVrCullingMarginPixels);
        }

        if (!anchor_valid)
        {
            std::memcpy(anchor_game.m, vr_world_to_view, sizeof(anchor_game.m));
            anchor_head.orientation = yaw_only(views[0].pose.orientation);
            anchor_head.position = {
                (views[0].pose.position.x + views[1].pose.position.x) * 0.5f,
                (views[0].pose.position.y + views[1].pose.position.y) * 0.5f,
                (views[0].pose.position.z + views[1].pose.position.z) * 0.5f
            };
            anchor_valid = true;
            if (g_feature_startup_menu_enabled) g_startup_menu.set_world_anchor(anchor_head);
            log_line("CAMERA_ANCHOR view_translation=%.4f,%.4f,%.4f reference_yaw=(%.6f,%.6f,%.6f,%.6f)",
                anchor_game.m[12], anchor_game.m[13], anchor_game.m[14], anchor_head.orientation.x,
                anchor_head.orientation.y, anchor_head.orientation.z, anchor_head.orientation.w);
        }
        const float world_heading = world_heading_from_view(
            vr_world_to_view, head_pose, anchor_head);
        if (state == XR_SESSION_STATE_FOCUSED)
            publish_hand_bridge(anchor_head, vr_world_to_view);
        if (g_feature_startup_menu_enabled)
        {
            g_startup_menu.update_pointer(
                g_input.pointer_pose(1), g_input.pointer_pose_active(1));
            g_startup_menu.update_interaction(
                g_input.ui_select_down(), g_input.ui_scroll_axis());
            switch (g_startup_menu.consume_haptic_event())
            {
            case features::UiHapticEvent::hover:
                g_input.pulse_haptic(1,0.07f,10,65.0f);
                break;
            case features::UiHapticEvent::click:
                g_input.pulse_haptic(1,0.14f,20,85.0f);
                break;
            default:
                break;
            }
        }

        uint32_t indices[2]{};
        for (uint32_t i = 0; i != 2; ++i)
        {
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            result = xrAcquireSwapchainImage(eyes[i].handle, &acquire, &indices[i]);
            if (XR_FAILED(result)) return abort_frame("xrAcquireSwapchainImage", result);
            acquired[i] = true;
            XrSwapchainImageWaitInfo image_wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            image_wait.timeout = XR_INFINITE_DURATION;
            result = xrWaitSwapchainImage(eyes[i].handle, &image_wait);
            if (XR_FAILED(result)) return abort_frame("xrWaitSwapchainImage", result);
        }

        // Keep the engine's VR render-size fields stable for the whole active
        // OpenXR session. Restoring desktop dimensions after every stereo frame
        // forced FrameRenderTargets::createOrResize to rebuild the complete target
        // set on the next frame. The saved desktop values are restored whenever VR
        // stops, while the game configuration and swap-chain resolution are never
        // changed.
        if (!apply_render_size_override(renderer, source_width, source_height))
            return abort_frame("vr_render_size_override", XR_ERROR_VALIDATION_FAILURE);

        std::array<XrCompositionLayerProjectionView,2> projection_views{{
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}
        }};
        bool rendered[2]{};
        LARGE_INTEGER performance_frequency{};
        LARGE_INTEGER frame_render_begin{};
        QueryPerformanceFrequency(&performance_frequency);
        QueryPerformanceCounter(&frame_render_begin);
        double eye_render_ms[2]{};
        mirror_ready = false;
        for (uint32_t i = 0; i != 2; ++i)
        {
            float tracking_view[16]{};
            float eye_world_to_view[16]{};
            float eye_projection[16]{};
            EyeRenderMapping mapping{};
            if (!build_tracking_view(anchor_head, views[i].pose, tracking_view) ||
                !build_eye_projection(views[i].fov, game_projection, source_width, source_height,
                    eyes[i].width, eyes[i].height, eye_projection, mapping))
                return abort_frame("eye_camera_build", XR_ERROR_VALIDATION_FAILURE);
            if (mapping.width != static_cast<int32_t>(eyes[i].width) ||
                mapping.height != static_cast<int32_t>(eyes[i].height))
            {
                log_line("FAIL stage=eye_extent_changed eye=%u actual=%dx%d required=%ux%u", i,
                    mapping.width, mapping.height, eyes[i].width, eyes[i].height);
                return abort_frame("eye_extent_changed", XR_ERROR_VALIDATION_FAILURE);
            }
            multiply_column_major(tracking_view, vr_world_to_view, eye_world_to_view);
            g_active_eye = static_cast<int>(i);
            g_eye_camera_build_index = 0;
            // Advance renderer-owned temporal state once per OpenXR frame. The
            // second real eye render consumes the already-updated state without
            // advancing clouds, foliage, or shadow caches a second time.
            const float eye_scalar = i == 0 ? scalar : 0.0f;
            LARGE_INTEGER eye_begin{};
            QueryPerformanceCounter(&eye_begin);
            original(renderer, eye_scalar, eye_world_to_view, eye_projection, settings);
            g_active_eye = -1;
            ID3D11Texture2D *eye_source = refresh_present_frame_target(
                renderer, source_width, source_height, source_format);
            if (!eye_source)
                return abort_frame("present_target_reacquire", XR_ERROR_RUNTIME_FAILURE);
            context->CopySubresourceRegion(eyes[i].images[indices[i]].texture, 0, 0, 0, 0,
                eye_source, 0, &mapping.source_box);
            LARGE_INTEGER eye_end{};
            QueryPerformanceCounter(&eye_end);
            if (performance_frequency.QuadPart > 0)
                eye_render_ms[i] = static_cast<double>(eye_end.QuadPart - eye_begin.QuadPart) * 1000.0 /
                    static_cast<double>(performance_frequency.QuadPart);
            rendered[i] = true;

            projection_views[i].pose = views[i].pose;
            projection_views[i].fov = views[i].fov;
            projection_views[i].subImage.swapchain = eyes[i].handle;
            projection_views[i].subImage.imageRect = {{0,0},{mapping.width,mapping.height}};
            projection_views[i].subImage.imageArrayIndex = 0;

            if (!eye_math_logged)
            {
                log_line("EYE_CAMERA eye=%u pose_pos=%.6f,%.6f,%.6f pose_q=%.6f,%.6f,%.6f,%.6f runtime_fov=%.6f,%.6f,%.6f,%.6f submitted_fov=runtime_exact guard_band=0 hidden_culling_margin=%u view_t=%.6f,%.6f,%.6f projection=%.6f,%.6f,%.6f,%.6f depth=%.6f,%.6f,%.6f,%.6f crop=%u,%u,%u,%u extent=%d,%d temporal_scalar=%.6f",
                    i, views[i].pose.position.x, views[i].pose.position.y, views[i].pose.position.z,
                    views[i].pose.orientation.x, views[i].pose.orientation.y, views[i].pose.orientation.z, views[i].pose.orientation.w,
                    views[i].fov.angleLeft, views[i].fov.angleRight, views[i].fov.angleUp, views[i].fov.angleDown,
                    kVrCullingMarginPixels,
                    eye_world_to_view[12], eye_world_to_view[13], eye_world_to_view[14],
                    eye_projection[0], eye_projection[5], eye_projection[8], eye_projection[9],
                    eye_projection[10], eye_projection[11], eye_projection[14], eye_projection[15],
                    mapping.source_box.left, mapping.source_box.top, mapping.source_box.right,
                    mapping.source_box.bottom, mapping.width, mapping.height, eye_scalar);
            }
        }
        // Keep every feature overlay outside the two engine scene calls. The
        // startup menu is a deterministic world-space asset, not a captured
        // renderer surface, so it can be composited immediately with no deferred
        // Present hook and no third scene render. Hands render afterwards and
        // therefore remain visually in front while pointing.
        bool startup_menu_composited = false;
        if (g_feature_startup_menu_enabled && g_startup_menu.visible())
        {
            ID3D11RenderTargetView *menu_targets[2]{};
            uint32_t menu_widths[2]{eyes[0].width,eyes[1].width};
            uint32_t menu_heights[2]{eyes[0].height,eyes[1].height};
            for (uint32_t eye=0; eye<2; ++eye)
                if (indices[eye]<eyes[eye].render_targets.size())
                    menu_targets[eye]=eyes[eye].render_targets[indices[eye]];
            startup_menu_composited=g_startup_menu.render(context,views.data(),menu_targets,
                menu_widths,menu_heights);
        }
        // Keep every feature overlay outside the two engine scene calls. In
        // particular, no hand/tool D3D state can leak from the left overlay into
        // the right engine render (clouds, shadows and post passes stay symmetric).
        if (g_feature_hands_enabled &&
            (g_input.hand(0).active || g_input.hand(1).active))
        {
            const uint64_t target_sample_ms =
                g_right_action_target_ms.load(std::memory_order_acquire);
            const uint64_t target_now_ms = GetTickCount64();
            const float target_distance =
                g_right_action_target_distance.load(std::memory_order_acquire);
            const bool target_active = target_sample_ms != 0 &&
                target_now_ms >= target_sample_ms &&
                target_now_ms - target_sample_ms <= 250 &&
                std::isfinite(target_distance) && target_distance >= 0.05f;
            XrVector3f interaction_offset{};
            scrapvr::tools::InteractionLaserKind active_interaction_kind =
                scrapvr::tools::InteractionLaserKind::none;
            const bool interaction_tool_active = scrapvr::tools::get_interaction_laser_offset(
                interaction_offset, nullptr, &active_interaction_kind);
            const uint64_t interaction_sample_ms =
                g_interaction_laser_target_ms.load(std::memory_order_acquire);
            const float interaction_distance =
                g_interaction_laser_target_distance.load(std::memory_order_acquire);
            const uint32_t interaction_sample_kind =
                g_interaction_laser_target_kind.load(std::memory_order_acquire);
            const bool interaction_target_active = interaction_tool_active &&
                interaction_sample_kind == static_cast<uint32_t>(active_interaction_kind) &&
                interaction_sample_ms != 0 && target_now_ms >= interaction_sample_ms &&
                target_now_ms - interaction_sample_ms <= 250 &&
                std::isfinite(interaction_distance) && interaction_distance >= 0.05f;
            for (uint32_t i = 0; i < 2; ++i)
            {
                if (indices[i] >= eyes[i].render_targets.size() ||
                    !scrapvr::hands::render(context, eyes[i].render_targets[indices[i]],
                        eyes[i].width, eyes[i].height, views[i],
                        g_input.pointer_pose(1), g_input.pointer_pose_active(1),
                        target_distance, target_active, interaction_distance,
                        interaction_target_active, world_heading))
                {
                    if (!g_hands_render_failure_logged.exchange(true))
                        log_line("VR_FEATURE_HANDS_RENDER_FAILED eye=%u visual_stereo_continues=1", i);
                }
            }
        }
        eye_math_logged = true;
        frame_pending = true;
        pending_display_time = frame_state.predictedDisplayTime;
        pending_startup_menu_composited = startup_menu_composited;
        pending_views = views;
        pending_projection_views = projection_views;
        pending_render_begin = frame_render_begin;
        pending_frequency = performance_frequency;
        for (uint32_t i = 0; i < 2; ++i)
        {
            pending_indices[i] = indices[i];
            pending_acquired[i] = acquired[i];
            acquired[i] = false;
            pending_rendered[i] = rendered[i];
            pending_eye_ms[i] = eye_render_ms[i];
        }

        if (g_feature_startup_menu_enabled && g_startup_menu.visible() &&
            g_startup_menu.native_capture_due())
        {
            // Refresh the cached native UI only when it changes. The panel itself
            // remains a 72 Hz stereo overlay between captures, avoiding a complete
            // Quest-to-desktop target rebuild on every menu frame.
            restore_render_size_override();
            if (!startup_desktop_ui_logged)
            {
                startup_desktop_ui_logged=true;
                log_line("VR_STARTUP_MENU_DESKTOP_UI_PHASE desktop=%ux%u vr_source=%ux%u deferred_to_present=1",
                    desktop_width,desktop_height,source_width,source_height);
            }
            return true;
        }
        return finish_pending_frame(context, false);
    }
};

OpenXrState g_xr;

bool config_enabled()
{
    return GetPrivateProfileIntW(L"VR", L"Enabled", 0, g_ini_path.c_str()) == 1;
}

template <typename T>
bool read_pointer_field(const void *base, uintptr_t offset, T **value)
{
    const auto *field = reinterpret_cast<T *const *>(reinterpret_cast<uintptr_t>(base) + offset);
    if (!readable(field, sizeof(*field))) return false;
    *value = *field;
    return *value != nullptr;
}

bool capture_native_renderer(void *manager)
{
    if (g_device.load(std::memory_order_acquire) && g_context.load(std::memory_order_acquire) &&
        g_backbuffer_identity.load(std::memory_order_acquire)) return true;

    void *frame_renderer = nullptr;
    void *low_renderer = nullptr;
    IUnknown *device_field = nullptr;
    IUnknown *context_field = nullptr;
    IUnknown *backbuffer_field = nullptr;
    if (!read_pointer_field(manager, kFrameRendererOffset, &frame_renderer) ||
        !read_pointer_field(frame_renderer, kLowRendererOffset, &low_renderer) ||
        !read_pointer_field(low_renderer, kDeviceOffset, &device_field) ||
        !read_pointer_field(low_renderer, kContextOffset, &context_field) ||
        !read_pointer_field(low_renderer, kBackbufferOffset, &backbuffer_field)) return false;

    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    ID3D11Texture2D *backbuffer = nullptr;
    IUnknown *backbuffer_identity = nullptr;
    HRESULT device_hr = device_field->QueryInterface(IID_PPV_ARGS(&device));
    HRESULT context_hr = context_field->QueryInterface(IID_PPV_ARGS(&context));
    HRESULT texture_hr = backbuffer_field->QueryInterface(IID_PPV_ARGS(&backbuffer));
    HRESULT identity_hr = backbuffer ? backbuffer->QueryInterface(IID_PPV_ARGS(&backbuffer_identity)) : E_NOINTERFACE;
    if (FAILED(device_hr) || FAILED(context_hr) || FAILED(texture_hr) || FAILED(identity_hr) ||
        !device || !context || !backbuffer || !backbuffer_identity)
    {
        if (backbuffer_identity) backbuffer_identity->Release();
        if (backbuffer) backbuffer->Release();
        if (context) context->Release();
        if (device) device->Release();
        return false;
    }

    ID3D11Device *context_device = nullptr;
    context->GetDevice(&context_device);
    const bool same_device = context_device == device;
    if (context_device) context_device->Release();
    D3D11_TEXTURE2D_DESC desc{};
    backbuffer->GetDesc(&desc);
    if (!same_device || desc.Width == 0 || desc.Height == 0 || desc.ArraySize != 1 || desc.SampleDesc.Count != 1)
    {
        backbuffer_identity->Release();
        backbuffer->Release();
        context->Release();
        device->Release();
        return false;
    }

    const bool keep_pinned_game_device = g_game_device_pinned.load(std::memory_order_acquire) &&
        g_device.load(std::memory_order_acquire) && g_context.load(std::memory_order_acquire);
    if (keep_pinned_game_device)
    {
        context->Release();
        device->Release();
    }
    else
    {
        if (ID3D11Device *old = g_device.exchange(device, std::memory_order_acq_rel)) old->Release();
        if (ID3D11DeviceContext *old = g_context.exchange(context, std::memory_order_acq_rel)) old->Release();
    }
    if (IUnknown *old = g_backbuffer_identity.exchange(backbuffer_identity, std::memory_order_acq_rel)) old->Release();
    g_backbuffer_handle.store(reinterpret_cast<uint64_t>(backbuffer_field), std::memory_order_release);
    log_line("NATIVE_RENDERER_READY renderer=%p renderer_device=%p renderer_context=%p selected_device=%p selected_context=%p source=%s backbuffer=%p size=%ux%u format=%u",
        low_renderer, device, context, g_device.load(std::memory_order_acquire),
        g_context.load(std::memory_order_acquire), keep_pinned_game_device ? "game_swapchain" : "renderer_fallback",
        backbuffer_field, desc.Width, desc.Height, static_cast<unsigned>(desc.Format));
    backbuffer->Release();
    return true;
}

void diagnose_main_frame_target(void *manager, ID3D11Texture2D *target)
{
    if (!manager || !target || g_target_wrapper_diagnostic_logged.load(std::memory_order_acquire)) return;
    IUnknown *target_identity = nullptr;
    if (FAILED(target->QueryInterface(IID_PPV_ARGS(&target_identity))) || !target_identity) return;

    const uintptr_t frame_targets = reinterpret_cast<uintptr_t>(manager) + kFrameRenderTargetsOffset;
    const auto *main_slot = reinterpret_cast<void *const *>(frame_targets + kMainFrameTargetOffset);
    if (!readable(main_slot, sizeof(*main_slot)) || !*main_slot)
        log_line("MAIN_FRAME_WRAPPER_UNAVAILABLE frame_targets=%p slot=%p readable=%u wrapper=%p",
            reinterpret_cast<void *>(frame_targets), main_slot,
            readable(main_slot, sizeof(*main_slot)) ? 1u : 0u,
            readable(main_slot, sizeof(*main_slot)) ? *main_slot : nullptr);

    bool found = false;
    for (uintptr_t offset = 0xc8; offset <= 0x1f0; offset += sizeof(void *))
    {
        const auto *slot = reinterpret_cast<void *const *>(frame_targets + offset);
        if (!readable(slot, sizeof(*slot)) || !*slot) continue;
        const uintptr_t wrapper = reinterpret_cast<uintptr_t>(*slot);
        if (!readable(reinterpret_cast<const void *>(wrapper), 0x10)) continue;
        uintptr_t low = 0;
        std::memcpy(&low, reinterpret_cast<const void *>(wrapper + 8), sizeof(low));
        uint32_t width = 0, height = 0;
        if (low && readable(reinterpret_cast<const void *>(low), 0xa4))
        {
            uint16_t width16 = 0, height16 = 0;
            std::memcpy(&width16, reinterpret_cast<const void *>(low + 0xa0), sizeof(width16));
            std::memcpy(&height16, reinterpret_cast<const void *>(low + 0xa2), sizeof(height16));
            width = width16; height = height16;
        }

        auto scan_block = [&](uintptr_t block, size_t bytes, const char *kind) {
            if (!block || !readable(reinterpret_cast<const void *>(block), bytes)) return;
            for (size_t at = 0; at + sizeof(uintptr_t) <= bytes; at += sizeof(uintptr_t))
            {
                uintptr_t value = 0;
                std::memcpy(&value, reinterpret_cast<const void *>(block + at), sizeof(value));
                if (value == reinterpret_cast<uintptr_t>(target) ||
                    value == reinterpret_cast<uintptr_t>(target_identity))
                {
                    log_line("TARGET_WRAPPER_MATCH frame_offset=%llx wrapper=%p low=%p width=%u height=%u block=%s block_offset=%llx target=%p canonical=%p",
                        static_cast<unsigned long long>(offset), reinterpret_cast<void *>(wrapper),
                        reinterpret_cast<void *>(low), width, height, kind,
                        static_cast<unsigned long long>(at), target, target_identity);
                    found = true;
                }
            }
        };
        scan_block(wrapper, 0x100, "wrapper");
        scan_block(low, 0x180, "low");
        if (low && readable(reinterpret_cast<const void *>(low), 0x98))
        {
            for (size_t at = 0; at < 0x98; at += sizeof(uintptr_t))
            {
                uintptr_t child = 0;
                std::memcpy(&child, reinterpret_cast<const void *>(low + at), sizeof(child));
                if (child > 0x10000 && child != wrapper && child != low)
                    scan_block(child, 0x80, "low_child");
            }
        }
        if (offset == kMainFrameTargetOffset)
            log_line("MAIN_FRAME_WRAPPER frame_offset=%llx wrapper=%p low=%p width=%u height=%u target=%p canonical=%p",
                static_cast<unsigned long long>(offset), reinterpret_cast<void *>(wrapper),
                reinterpret_cast<void *>(low), width, height, target, target_identity);
    }
    log_line("TARGET_WRAPPER_DIAGNOSTIC result=%s frame_targets=%p", found ? "matched" : "unmatched",
        reinterpret_cast<void *>(frame_targets));
    g_target_wrapper_diagnostic_logged.store(true, std::memory_order_release);
    target_identity->Release();
}

ID3D11Texture2D *acquire_present_frame_target(void *manager, uint32_t expected_width,
                                               uint32_t expected_height, DXGI_FORMAT expected_format)
{
    if (!manager) return nullptr;
    const uintptr_t slot_address = reinterpret_cast<uintptr_t>(manager) +
        kFrameRenderTargetsOffset + kPresentFrameTargetOffset;
    void *wrapper = nullptr;
    if (!readable(reinterpret_cast<const void *>(slot_address), sizeof(wrapper))) return nullptr;
    std::memcpy(&wrapper, reinterpret_cast<const void *>(slot_address), sizeof(wrapper));
    if (!wrapper || !readable(reinterpret_cast<const uint8_t *>(wrapper) + kTextureWrapperLowOffset, sizeof(void *)))
        return nullptr;
    void *low = nullptr;
    std::memcpy(&low, reinterpret_cast<const uint8_t *>(wrapper) + kTextureWrapperLowOffset, sizeof(low));
    if (!low || !readable(reinterpret_cast<const uint8_t *>(low) + kTextureLowNativeOffset, sizeof(void *)))
        return nullptr;
    IUnknown *native = nullptr;
    std::memcpy(&native, reinterpret_cast<const uint8_t *>(low) + kTextureLowNativeOffset, sizeof(native));
    if (!native) return nullptr;
    ID3D11Texture2D *texture = nullptr;
    const HRESULT hr = native->QueryInterface(IID_PPV_ARGS(&texture));
    if (FAILED(hr) || !texture) return nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.Width != expected_width || desc.Height != expected_height ||
        desc.Format != expected_format || desc.ArraySize != 1 || desc.MipLevels != 1 ||
        desc.SampleDesc.Count != 1)
    {
        log_line("FAIL stage=present_target_reacquire_desc actual=%ux%u/%u expected=%ux%u/%u",
            desc.Width, desc.Height, static_cast<unsigned>(desc.Format), expected_width,
            expected_height, static_cast<unsigned>(expected_format));
        texture->Release();
        return nullptr;
    }
    return texture;
}

ID3D11Texture2D *refresh_present_frame_target(void *manager, uint32_t width, uint32_t height,
                                               DXGI_FORMAT format)
{
    ID3D11Texture2D *candidate = acquire_present_frame_target(manager, width, height, format);
    if (!candidate) return nullptr;
    ID3D11Texture2D *current = g_final_target.load(std::memory_order_acquire);
    if (current == candidate)
    {
        candidate->Release();
        return current;
    }
    ID3D11Texture2D *old = g_final_target.exchange(candidate, std::memory_order_acq_rel);
    if (old) old->Release();
    const uint64_t reacquires = g_vr_target_reacquires.fetch_add(1, std::memory_order_relaxed) + 1;
    if (reacquires <= 4 || (reacquires % 600) == 0)
        log_line("VR_TARGET_REACQUIRED count=%llu pointer=%p size=%ux%u format=%u",
            static_cast<unsigned long long>(reacquires), candidate, width, height,
            static_cast<unsigned>(format));
    return candidate;
}

// Some runtimes (notably VDXR during a cold launch) create the game swapchain
// before the renderer's present-target wrapper is populated. In that window the
// old path silently stayed desktop-only forever. The pinned game backbuffer is
// a valid render source and is already identity-checked by capture_native_renderer;
// use it only as an initialization fallback until the normal target appears.
ID3D11Texture2D *ensure_backbuffer_target()
{
    if (g_final_target.load(std::memory_order_acquire)) return
        g_final_target.load(std::memory_order_acquire);
    IUnknown *identity = g_backbuffer_identity.load(std::memory_order_acquire);
    if (!identity) return nullptr;
    ID3D11Texture2D *candidate = nullptr;
    if (FAILED(identity->QueryInterface(IID_PPV_ARGS(&candidate))) || !candidate) return nullptr;
    ID3D11Texture2D *old = g_final_target.exchange(candidate, std::memory_order_acq_rel);
    if (old) old->Release();
    log_line("VR_TARGET_FALLBACK source=pinned_game_backbuffer pointer=%p", candidate);
    return candidate;
}

bool run_highres_pc_probe(RenderSetupFn original, void *renderer, float scalar,
                          const float *world_to_view, const float *projection, void *settings)
{
    constexpr uint32_t probe_width = 2565;
    constexpr uint32_t probe_height = 2711;
    auto *base = static_cast<uint8_t *>(renderer);
    if (!base || !readable(base + kReportedWidthOffset, sizeof(float) * 2) ||
        !readable(base + kRenderWidthOffset, sizeof(uint32_t) * 2) ||
        !readable(base + kRenderScaleOffset, sizeof(float))) return false;
    uint32_t old_width = 0, old_height = 0;
    float old_scale = 1.0f, old_reported_width = 0.0f, old_reported_height = 0.0f;
    std::memcpy(&old_reported_width, base + kReportedWidthOffset, sizeof(float));
    std::memcpy(&old_reported_height, base + kReportedHeightOffset, sizeof(float));
    std::memcpy(&old_width, base + kRenderWidthOffset, sizeof(uint32_t));
    std::memcpy(&old_height, base + kRenderHeightOffset, sizeof(uint32_t));
    std::memcpy(&old_scale, base + kRenderScaleOffset, sizeof(float));
    constexpr float one = 1.0f;
    std::memcpy(base + kRenderWidthOffset, &probe_width, sizeof(probe_width));
    std::memcpy(base + kRenderHeightOffset, &probe_height, sizeof(probe_height));
    std::memcpy(base + kRenderScaleOffset, &one, sizeof(one));
    log_line("PC_HIGHRES_PROBE_BEGIN desktop=%ux%u scale=%.6f probe=%ux%u", old_width,
        old_height, old_scale, probe_width, probe_height);
    original(renderer, scalar, world_to_view, projection, settings);
    std::memcpy(base + kReportedWidthOffset, &old_reported_width, sizeof(old_reported_width));
    std::memcpy(base + kReportedHeightOffset, &old_reported_height, sizeof(old_reported_height));
    std::memcpy(base + kRenderWidthOffset, &old_width, sizeof(old_width));
    std::memcpy(base + kRenderHeightOffset, &old_height, sizeof(old_height));
    std::memcpy(base + kRenderScaleOffset, &old_scale, sizeof(old_scale));
    ID3D11Texture2D *target = refresh_present_frame_target(
        renderer, probe_width, probe_height, DXGI_FORMAT_R8G8B8A8_UNORM);
    log_line("PC_HIGHRES_PROBE_END target=%p restored=%ux%u scale=%.6f result=%s", target,
        old_width, old_height, old_scale, target ? "success" : "failure");
    return target != nullptr;
}

void __fastcall hk_render_setup(void *renderer, float scalar, const float *world_to_view, const float *projection, void *settings)
{
    auto original = reinterpret_cast<RenderSetupFn>(g_render_original.load(std::memory_order_acquire));
    if (!original) __fastfail(FAST_FAIL_INVALID_ARG);
    g_hook_reached.store(true, std::memory_order_release);
    g_last_render_manager.store(renderer, std::memory_order_release);
    const uint64_t frame = g_engine_frames.fetch_add(1) + 1;
    if (frame == 1) log_line("GATE hook_reached=1 rva=%llx", static_cast<unsigned long long>(kRenderSetupRva));
    capture_native_renderer(renderer);

    const bool view_valid = world_to_view_matrix(world_to_view);
    const bool projection_valid = perspective_matrix(projection);
    if (!g_camera_abi_logged.exchange(true, std::memory_order_acq_rel))
    {
        log_line("PC_CAMERA_ABI scalar=%.9f view_world_to_camera=%u projection_perspective=%u view=%p projection=%p settings=%p",
            scalar, view_valid ? 1u : 0u, projection_valid ? 1u : 0u, world_to_view, projection, settings);
        if (finite_matrix(world_to_view)) log_matrix("PC_WORLD_TO_VIEW", world_to_view);
        if (finite_matrix(projection)) log_matrix("PC_PROJECTION", projection);
    }

    ID3D11Texture2D *diagnostic_target = g_final_target.load(std::memory_order_acquire);
    if (diagnostic_target) diagnose_main_frame_target(renderer, diagnostic_target);

    if (!g_enabled.load(std::memory_order_acquire))
    {
        restore_viewmodel_pass_patch();
        if (g_highres_pc_probe_requested.load(std::memory_order_acquire) && diagnostic_target &&
            !g_highres_pc_probe_done.exchange(true, std::memory_order_acq_rel))
        {
            if (!run_highres_pc_probe(original, renderer, scalar, world_to_view, projection, settings))
                log_line("FAIL stage=pc_highres_probe");
            return;
        }
        original(renderer, scalar, world_to_view, projection, settings);
        return;
    }
    if (g_failed.load(std::memory_order_acquire))
    {
        const uint64_t now = GetTickCount64();
        const uint64_t retry_after = g_openxr_retry_after_ms.load(std::memory_order_acquire);
        if (retry_after != 0 && now >= retry_after)
        {
            // Retry transient renderer/runtime failures at a low rate while every
            // intervening frame continues through the original desktop renderer.
            // A genuinely absent HMD uses retry_after=0 and stays in quiet desktop
            // mode until the next launch, avoiding periodic runtime stalls.
            std::lock_guard retry_lock(g_xr_mutex);
            g_failed.store(false, std::memory_order_release);
            g_openxr_retry_after_ms.store(0, std::memory_order_release);
            g_xr.destroy();
        }
        if (g_failed.load(std::memory_order_acquire))
        {
            g_xr.restore_render_size_override();
            restore_viewmodel_pass_patch();
            if (!g_desktop_fallback_logged.exchange(true, std::memory_order_acq_rel))
                log_line("XR_DESKTOP_FALLBACK active=1 original_renderer=1 auto_retry=%u retry_delay_ms=%llu",
                    retry_after != 0 ? 1u : 0u,
                    static_cast<unsigned long long>(retry_after != 0 ? kOpenXrRetryDelayMs : 0));
            original(renderer, scalar, world_to_view, projection, settings);
            return;
        }
    }
    if (!view_valid || !projection_valid)
    {
        log_line("FAIL stage=live_camera_abi view_valid=%u projection_valid=%u", view_valid ? 1u : 0u, projection_valid ? 1u : 0u);
        mark_openxr_failed();
        restore_viewmodel_pass_patch();
        original(renderer, scalar, world_to_view, projection, settings);
        return;
    }

    ID3D11Device *device = g_device.load(std::memory_order_acquire);
    ID3D11DeviceContext *context = g_context.load(std::memory_order_acquire);
    ID3D11Texture2D *target = g_final_target.load(std::memory_order_acquire);
    if (!target) target = ensure_backbuffer_target();
    if (!device || !context || !target)
    {
        if (g_xr.initialized || g_xr.running)
        {
            log_line("FAIL stage=native_renderer_resource_lost device=%p context=%p target=%p", device, context, target);
            mark_openxr_failed();
            g_xr.restore_render_size_override();
            restore_viewmodel_pass_patch();
            original(renderer, scalar, world_to_view, projection, settings);
            return;
        }
        restore_viewmodel_pass_patch();
        original(renderer, scalar, world_to_view, projection, settings);
        return;
    }

    std::lock_guard lock(g_xr_mutex);
    if (!g_xr.initialized)
    {
        D3D11_TEXTURE2D_DESC desc{}; target->GetDesc(&desc);
        if (!g_xr.initialize(device, desc))
        {
            // Initialization may already own an instance, session, swapchains, or
            // input action spaces. Release the partial transaction before leaving
            // the hook so a later process/session can never hit XR_LIMIT_REACHED.
            g_xr.destroy();
            restore_viewmodel_pass_patch();
            original(renderer, scalar, world_to_view, projection, settings);
            return;
        }
    }
    // Close any prior frame before processing a possible STOPPING transition.
    if (g_xr.frame_pending)
    {
        if (!g_xr.finish_pending_frame(context))
        {
            if (g_failed.load(std::memory_order_acquire))
            {
                g_xr.restore_render_size_override();
                restore_viewmodel_pass_patch();
                original(renderer, scalar, world_to_view, projection, settings);
            }
            return;
        }
    }
    g_xr.poll_events();
    if (!g_xr.running)
    {
        g_xr.restore_render_size_override();
        restore_viewmodel_pass_patch();
        original(renderer, scalar, world_to_view, projection, settings);
        return;
    }
    if (!g_xr.render_stereo(original, renderer, scalar, world_to_view, projection, settings, context, target))
    {
        if (g_failed.load(std::memory_order_acquire))
        {
            g_xr.restore_render_size_override();
            restore_viewmodel_pass_patch();
            original(renderer, scalar, world_to_view, projection, settings);
        }
        else log_line("XR_FRAME_NOT_RENDERED");
        return;
    }
    g_stereo_frames.fetch_add(1);
}

void on_init_device(reshade::api::device *device)
{
    if (!device || device->get_api() != reshade::api::device_api::d3d11) return;
    const uint64_t sequence = g_observed_d3d11_devices.fetch_add(1, std::memory_order_relaxed) + 1;
    reshade::api::device *selected = g_game_api_device.load(std::memory_order_acquire);
    if (sequence <= 4 || selected == device)
        log_line("D3D11_DEVICE_OBSERVED sequence=%llu api_device=%p native=%p role=%s action=no_global_replacement",
            static_cast<unsigned long long>(sequence), device,
            reinterpret_cast<void *>(static_cast<uintptr_t>(device->get_native())),
            selected == device ? "game" : (selected ? "non_game" : "unclassified"));
}

void on_destroy_device(reshade::api::device *device)
{
    if (!device || device != g_game_api_device.load(std::memory_order_acquire))
    {
        if (device && device->get_api() == reshade::api::device_api::d3d11)
            log_line("D3D11_DEVICE_DESTROY_IGNORED api_device=%p role=non_game", device);
        return;
    }
    std::lock_guard lock(g_xr_mutex);
    g_xr.destroy();
    release_atomic(g_final_target);
    release_atomic(g_backbuffer_identity);
    release_atomic(g_context);
    release_atomic(g_device);
    g_game_api_swapchain.store(nullptr, std::memory_order_release);
    g_game_api_device.store(nullptr, std::memory_order_release);
    g_game_device_pinned.store(false, std::memory_order_release);
    log_line("D3D11_GAME_DEVICE_RELEASED api_device=%p", device);
}

bool on_copy_texture(reshade::api::command_list *command_list, reshade::api::resource source, uint32_t source_subresource,
                     const reshade::api::subresource_box *source_box, reshade::api::resource dest, uint32_t dest_subresource,
                     const reshade::api::subresource_box *dest_box, reshade::api::filter_mode)
{
    reshade::api::device *selected_device = g_game_api_device.load(std::memory_order_acquire);
    if (selected_device && (!command_list || command_list->get_device() != selected_device)) return false;
    // During an active OpenXR session the engine also copies toward the desktop
    // present chain. That resource is not the persistent VR eye source. Tracking
    // it here made g_final_target oscillate between desktop and VR extents during
    // native-menu captures, forcing redundant COM work and visible eye flashes.
    // Tracking is unlocked again whenever OpenXR stops, so PC mode is unchanged.
    if (g_active_eye >= 0 || g_vr_target_tracking_locked.load(std::memory_order_acquire))
    {
        if (!g_vr_target_tracking_suppressed_logged.exchange(true, std::memory_order_acq_rel))
            log_line("VR_DESKTOP_TARGET_TRACKING_SUPPRESSED scope=active_openxr_session eye=%d",
                g_active_eye);
        return false;
    }
    if (dest.handle == 0 || source.handle == 0 || source_subresource != 0 || dest_subresource != 0) return false;
    auto *dest_resource = reinterpret_cast<ID3D11Resource *>(static_cast<uintptr_t>(dest.handle));
    IUnknown *dest_identity = nullptr;
    if (!dest_resource || FAILED(dest_resource->QueryInterface(IID_PPV_ARGS(&dest_identity))) || !dest_identity) return false;
    IUnknown *expected_identity = g_backbuffer_identity.load(std::memory_order_acquire);
    const bool is_backbuffer = expected_identity && dest_identity == expected_identity;
    dest_identity->Release();
    if (!is_backbuffer) return false;
    auto *candidate_resource = reinterpret_cast<ID3D11Resource *>(static_cast<uintptr_t>(source.handle));
    ID3D11Texture2D *candidate = nullptr;
    if (!candidate_resource || FAILED(candidate_resource->QueryInterface(IID_PPV_ARGS(&candidate))) || !candidate) return false;
    D3D11_TEXTURE2D_DESC desc{}; candidate->GetDesc(&desc);
    const bool full_source = !source_box || (source_box->left == 0 && source_box->top == 0 && source_box->front == 0 &&
        source_box->right == desc.Width && source_box->bottom == desc.Height && source_box->back == 1);
    const bool full_dest = !dest_box || (dest_box->left == 0 && dest_box->top == 0 && dest_box->front == 0 &&
        dest_box->right == desc.Width && dest_box->bottom == desc.Height && dest_box->back == 1);
    if (!full_source || !full_dest || desc.ArraySize != 1 || desc.MipLevels != 1 || desc.SampleDesc.Count != 1)
    {
        candidate->Release(); return false;
    }
    ID3D11Texture2D *current = g_final_target.load(std::memory_order_acquire);
    if (current == candidate)
    {
        candidate->Release(); return false;
    }
    ID3D11Texture2D *old = g_final_target.exchange(candidate, std::memory_order_acq_rel);
    if (old) old->Release();
    g_target_found.store(true, std::memory_order_release);
    log_line("GATE target_found=1 pointer=%p size=%ux%u format=%u", candidate, desc.Width, desc.Height, static_cast<unsigned>(desc.Format));
    return false;
}

void on_present(reshade::api::command_queue *queue, reshade::api::swapchain *swapchain,
                const reshade::api::rect *, const reshade::api::rect *, uint32_t,
                const reshade::api::rect *)
{
    if (!g_enabled.load(std::memory_order_acquire) || !swapchain) return;
    if (swapchain != g_game_api_swapchain.load(std::memory_order_acquire)) return;
    reshade::api::device *selected_device = g_game_api_device.load(std::memory_order_acquire);
    if (selected_device && queue && queue->get_device() != selected_device) return;
    auto *native_swapchain = reinterpret_cast<IDXGISwapChain *>(
        static_cast<uintptr_t>(swapchain->get_native()));
    ID3D11Device *device = g_device.load(std::memory_order_acquire);
    ID3D11DeviceContext *context = g_context.load(std::memory_order_acquire);
    if (!native_swapchain || !device || !context) return;

    std::lock_guard lock(g_xr_mutex);
    g_xr.set_game_swapchain(native_swapchain);
    if (!g_xr.initialized || !g_xr.running) return;
    const bool completed_pending=g_xr.frame_pending;
    if (completed_pending && !g_xr.finish_pending_frame(context, true)) return;
    if (completed_pending) return;
    if (!g_xr.mirror_ready) return;
    if (!g_xr.mirror_left_eye(device, context, native_swapchain, true))
    {
        const uint64_t failures = g_mirror_present_failures.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!g_mirror_present_failure_logged.exchange(true, std::memory_order_acq_rel))
            log_line("DESKTOP_LEFT_EYE_MIRROR_FAIL first=%llu",
                static_cast<unsigned long long>(failures));
    }
}

void on_destroy_swapchain(reshade::api::swapchain *swapchain, bool resize)
{
    if (!swapchain) return;
    if (swapchain != g_game_api_swapchain.load(std::memory_order_acquire)) return;
    std::lock_guard lock(g_xr_mutex);
    if (g_xr.frame_pending) g_xr.abandon_pending_frame();
    g_startup_menu.reset_state();
    // IDXGISwapChain::ResizeBuffers requires every direct and indirect reference
    // to the old backbuffers to be released. The PC left-eye mirror owns an RTV
    // and canonical IUnknown for the current buffer, so release those at ReShade's
    // pre-resize/reset boundary. OpenXR eye swapchains are independent and remain
    // alive at their exact runtime-recommended extent.
    g_xr.release_mirror_backbuffer();
    g_xr.mirror_ready = false;
    g_xr.set_game_swapchain(nullptr);
    // The renderer bootstrap also retains the desktop backbuffer's canonical
    // IUnknown solely for resource-identity matching. It is another DXGI buffer
    // reference and must be dropped before ResizeBuffers. The next render hook
    // reacquires the new buffer identity through the validated renderer fields.
    release_atomic(g_backbuffer_identity);
    g_backbuffer_handle.store(0, std::memory_order_release);
    if (!resize) g_game_api_swapchain.store(nullptr, std::memory_order_release);
    log_line("DESKTOP_SWAPCHAIN_RESET_BEGIN resize=%u mirror_backbuffer_released=1 renderer_backbuffer_identity_released=1",
        resize ? 1u : 0u);
}

void on_init_swapchain(reshade::api::swapchain *swapchain, bool resize)
{
    if (!swapchain) return;
    auto *native_swapchain = reinterpret_cast<IDXGISwapChain *>(
        static_cast<uintptr_t>(swapchain->get_native()));
    DXGI_SWAP_CHAIN_DESC desc{};
    const HRESULT hr = native_swapchain ? native_swapchain->GetDesc(&desc) : E_POINTER;
    reshade::api::swapchain *selected_swapchain = g_game_api_swapchain.load(std::memory_order_acquire);
    if (selected_swapchain && selected_swapchain != swapchain) return;
    bool claimed_swapchain = false;
    if (!selected_swapchain)
    {
        if (FAILED(hr) || desc.BufferDesc.Width == 0 || desc.BufferDesc.Height == 0 ||
            desc.BufferDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
            !is_current_process_swapchain(swapchain, desc)) return;
        reshade::api::swapchain *expected = nullptr;
        if (g_game_api_swapchain.compare_exchange_strong(expected, swapchain,
                std::memory_order_acq_rel, std::memory_order_acquire)) claimed_swapchain = true;
        else if (expected != swapchain) return;
    }
    if (!pin_game_device(swapchain->get_device(), resize ? "game_swapchain_resize" : "game_swapchain_create"))
    {
        if (claimed_swapchain)
        {
            reshade::api::swapchain *expected = swapchain;
            g_game_api_swapchain.compare_exchange_strong(expected, nullptr,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }
        log_line("FAIL stage=pin_game_d3d11_device resize=%u", resize ? 1u : 0u);
        mark_openxr_failed();
        return;
    }
    std::lock_guard lock(g_xr_mutex);
    g_xr.set_game_swapchain(native_swapchain);
    if (SUCCEEDED(hr) && desc.BufferDesc.Width != 0 && desc.BufferDesc.Height != 0)
    {
        if (resize)
        {
            g_xr.pending_desktop_width = desc.BufferDesc.Width;
            g_xr.pending_desktop_height = desc.BufferDesc.Height;
        }
        g_xr.mirror_logged = false;
        log_line("DESKTOP_SWAPCHAIN_READY resize=%u size=%ux%u windowed=%u format=%u",
            resize ? 1u : 0u, desc.BufferDesc.Width, desc.BufferDesc.Height,
            desc.Windowed ? 1u : 0u, static_cast<unsigned>(desc.BufferDesc.Format));
    }
    else
    {
        log_line("FAIL stage=desktop_swapchain_desc resize=%u hr=%08x",
            resize ? 1u : 0u, static_cast<unsigned>(hr));
        mark_openxr_failed();
    }
}

bool validate_build()
{
    g_game = GetModuleHandleW(L"ScrapMechanic.exe");
    if (!g_game) return false;
    g_game_base = reinterpret_cast<uintptr_t>(g_game);
    auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(g_game_base);
    if (!readable(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(g_game_base + dos->e_lfanew);
    if (!readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.SizeOfImage != kGameImageSize || nt->FileHeader.TimeDateStamp != kGameTimestamp) return false;
    const void *entry = reinterpret_cast<const void *>(g_game_base + kRenderSetupRva);
    const void *camera_entry = reinterpret_cast<const void *>(g_game_base + kCameraBuildRva);
    const void *raycast_entry = reinterpret_cast<const void *>(g_game_base + kRaycastRva);
    const void *player_raycast_call = reinterpret_cast<const void *>(
        g_game_base + kPlayerToolRaycastReturnRva - sizeof(kPlayerToolRaycastCall));
    const void *raycast_fraction_store = reinterpret_cast<const void *>(
        g_game_base + kRaycastResultFractionStoreRva);
    const void *viewmodel_branch = reinterpret_cast<const void *>(g_game_base + kViewmodelSkipBranchRva);
    return readable(entry, sizeof(kRenderPrefix)) && std::memcmp(entry, kRenderPrefix, sizeof(kRenderPrefix)) == 0 &&
        readable(camera_entry, sizeof(kCameraBuildPrefix)) &&
        std::memcmp(camera_entry, kCameraBuildPrefix, sizeof(kCameraBuildPrefix)) == 0 &&
        readable(raycast_entry, sizeof(kRaycastPrefix)) &&
        std::memcmp(raycast_entry, kRaycastPrefix, sizeof(kRaycastPrefix)) == 0 &&
        readable(player_raycast_call, sizeof(kPlayerToolRaycastCall)) &&
        std::memcmp(player_raycast_call, kPlayerToolRaycastCall,
            sizeof(kPlayerToolRaycastCall)) == 0 &&
        readable(raycast_fraction_store, sizeof(kRaycastResultFractionStore)) &&
        std::memcmp(raycast_fraction_store, kRaycastResultFractionStore,
            sizeof(kRaycastResultFractionStore)) == 0 &&
        readable(viewmodel_branch, sizeof(kViewmodelConditionalBranch)) &&
        std::memcmp(viewmodel_branch, kViewmodelConditionalBranch, sizeof(kViewmodelConditionalBranch)) == 0;
}

bool install_hook()
{
    if (MH_Initialize() != MH_OK) return false;
    void *render_target = reinterpret_cast<void *>(g_game_base + kRenderSetupRva);
    void *camera_target = reinterpret_cast<void *>(g_game_base + kCameraBuildRva);
    void *raycast_target = reinterpret_cast<void *>(g_game_base + kRaycastRva);
    void *render_original = nullptr;
    void *camera_original = nullptr;
    void *raycast_original = nullptr;
    void *lua_pcall_original = nullptr;
    void *lua_call_original = nullptr;
    void *lua_getfield_original = nullptr;
    void *lua_newstate_original = nullptr;
    void *lua_loadbufferx_original = nullptr;
    if (MH_CreateHook(render_target, reinterpret_cast<void *>(hk_render_setup), &render_original) != MH_OK || !render_original)
    {
        MH_Uninitialize();
        return false;
    }
    if (MH_CreateHook(camera_target, reinterpret_cast<void *>(hk_camera_build), &camera_original) != MH_OK || !camera_original)
    {
        MH_RemoveHook(render_target);
        MH_Uninitialize();
        return false;
    }
    if (MH_CreateHook(raycast_target, reinterpret_cast<void *>(hk_raycast), &raycast_original) != MH_OK ||
        !raycast_original)
    {
        MH_RemoveHook(camera_target);
        MH_RemoveHook(render_target);
        MH_Uninitialize();
        return false;
    }
    if (resolve_lua_projectile_api())
    {
        const bool pcall_ready = MH_CreateHook(g_lua_pcall_target, reinterpret_cast<void *>(hk_lua_pcall),
                &lua_pcall_original) == MH_OK && lua_pcall_original;
        if (pcall_ready)
        {
            g_lua_pcall_original.store(lua_pcall_original, std::memory_order_release);
        }
        const bool call_ready = MH_CreateHook(g_lua_call_target, reinterpret_cast<void *>(hk_lua_call),
                &lua_call_original) == MH_OK && lua_call_original;
        if (call_ready)
        {
            g_lua_call_original.store(lua_call_original, std::memory_order_release);
        }
        const bool getfield_ready = MH_CreateHook(g_lua_getfield_target,
                reinterpret_cast<void *>(hk_lua_getfield), &lua_getfield_original) == MH_OK &&
            lua_getfield_original;
        if (getfield_ready)
        {
            g_lua_getfield_original.store(lua_getfield_original, std::memory_order_release);
        }
        const bool newstate_ready = MH_CreateHook(g_lua_newstate_target,
                reinterpret_cast<void *>(hk_lua_newstate), &lua_newstate_original) == MH_OK &&
            lua_newstate_original;
        if (newstate_ready)
        {
            g_lua_newstate_original.store(lua_newstate_original, std::memory_order_release);
        }
        const bool loadbufferx_ready = MH_CreateHook(g_lua_loadbufferx_target,
                reinterpret_cast<void *>(hk_lua_loadbufferx), &lua_loadbufferx_original) == MH_OK &&
            lua_loadbufferx_original;
        if (loadbufferx_ready)
        {
            g_lua_loadbufferx_original.store(lua_loadbufferx_original, std::memory_order_release);
        }
        if (pcall_ready && call_ready && getfield_ready && newstate_ready && loadbufferx_ready)
        {
            log_line("VR_LUA_PROJECTILE_HOOK_READY targets=lua51!lua_pcall,lua_call,lua_getfield,luaL_newstate,luaL_loadbufferx");
        }
        else
        {
            log_line("FAIL stage=lua_projectile_hook_create fallback=json_cache");
        }
    }
    else
    {
        log_line("FAIL stage=lua_projectile_api_resolve fallback=json_cache");
    }
    g_render_original.store(render_original, std::memory_order_release);
    g_camera_build_original.store(camera_original, std::memory_order_release);
    g_raycast_original.store(raycast_original, std::memory_order_release);
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        MH_RemoveHook(MH_ALL_HOOKS);
        g_lua_pcall_original.store(nullptr, std::memory_order_release);
        g_lua_call_original.store(nullptr, std::memory_order_release);
        g_lua_getfield_original.store(nullptr, std::memory_order_release);
        g_lua_newstate_original.store(nullptr, std::memory_order_release);
        g_lua_loadbufferx_original.store(nullptr, std::memory_order_release);
        g_camera_build_original.store(nullptr, std::memory_order_release);
        g_raycast_original.store(nullptr, std::memory_order_release);
        g_render_original.store(nullptr, std::memory_order_release);
        MH_Uninitialize();
        return false;
    }
    return true;
}

void initialize_paths()
{
    wchar_t module_path[MAX_PATH]{};
    GetModuleFileNameW(g_module, module_path, MAX_PATH);
    std::wstring directory(module_path);
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash != std::wstring::npos) directory.resize(slash + 1);
    g_ini_path = directory + L"ScrapMechanicVR.ini";
    const std::wstring log_path = directory + L"ScrapMechanicVR-v1.log";
    g_log = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    consume_vr_launch_request();
    g_enabled.store(config_enabled(), std::memory_order_release);
    g_highres_pc_probe_requested.store(
        GetPrivateProfileIntW(L"Diagnostic", L"HighResolutionProbe", 0, g_ini_path.c_str()) == 1,
        std::memory_order_release);
    g_feature_input_enabled =
        GetPrivateProfileIntW(L"Features", L"Input", 1, g_ini_path.c_str()) == 1;
    g_feature_optical_hands_enabled =
        GetPrivateProfileIntW(L"Features", L"OpticalHandTracking", 1, g_ini_path.c_str()) == 1;
    g_feature_hands_enabled =
        GetPrivateProfileIntW(L"Features", L"Hands", 1, g_ini_path.c_str()) == 1;
    g_feature_startup_menu_enabled =
        GetPrivateProfileIntW(L"Features", L"StartupMenuUI", 1, g_ini_path.c_str()) == 1;
}
} // namespace smvr

extern "C" __declspec(dllexport) const char *NAME = "Scrap Mechanic Native OpenXR VR v1";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Exact-build two-render OpenXR stereo visual test candidate";

extern "C" __declspec(dllexport) BOOL AddonInit(HMODULE addon_module, HMODULE reshade_module)
{
    smvr::g_module = addon_module;
    smvr::initialize_paths();
    smvr::reset_hand_bridge(true);
    smvr::deactivate_hand_bridge();
    smvr::remove_world_state_bridge();
    smvr::log_line("SMVR_V1_START enabled=%u game_build=24529696 exe_sha256=5D663BA2...A5B4F5", smvr::g_enabled.load() ? 1u : 0u);
    if (!smvr::validate_build()) { smvr::log_line("FAIL stage=validate_build"); return FALSE; }
    if (!reshade::register_addon(addon_module, reshade_module))
    {
        smvr::log_line("FAIL stage=register_addon");
        smvr::restore_viewmodel_pass_patch();
        return FALSE;
    }
    reshade::register_event<reshade::addon_event::init_device>(smvr::on_init_device);
    reshade::register_event<reshade::addon_event::destroy_device>(smvr::on_destroy_device);
    reshade::register_event<reshade::addon_event::copy_texture_region>(smvr::on_copy_texture);
    reshade::register_event<reshade::addon_event::init_swapchain>(smvr::on_init_swapchain);
    reshade::register_event<reshade::addon_event::destroy_swapchain>(smvr::on_destroy_swapchain);
    reshade::register_event<reshade::addon_event::present>(smvr::on_present);
    if (!smvr::install_hook())
    {
        smvr::log_line("FAIL stage=install_hook");
        reshade::unregister_event<reshade::addon_event::present>(smvr::on_present);
        reshade::unregister_event<reshade::addon_event::destroy_swapchain>(smvr::on_destroy_swapchain);
        reshade::unregister_event<reshade::addon_event::init_swapchain>(smvr::on_init_swapchain);
        reshade::unregister_event<reshade::addon_event::copy_texture_region>(smvr::on_copy_texture);
        reshade::unregister_event<reshade::addon_event::destroy_device>(smvr::on_destroy_device);
        reshade::unregister_event<reshade::addon_event::init_device>(smvr::on_init_device);
        reshade::unregister_addon(addon_module, reshade_module);
        smvr::restore_viewmodel_pass_patch();
        return FALSE;
    }
    smvr::log_line("HOOKS_INSTALLED render_rva=%llx camera_rva=%llx raycast_rva=%llx tool_caller_rva=%llx",
        static_cast<unsigned long long>(smvr::kRenderSetupRva),
        static_cast<unsigned long long>(smvr::kCameraBuildRva),
        static_cast<unsigned long long>(smvr::kRaycastRva),
        static_cast<unsigned long long>(smvr::kPlayerToolRaycastReturnRva));
    return TRUE;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module)
{
    reshade::unregister_event<reshade::addon_event::present>(smvr::on_present);
    reshade::unregister_event<reshade::addon_event::destroy_swapchain>(smvr::on_destroy_swapchain);
    reshade::unregister_event<reshade::addon_event::init_swapchain>(smvr::on_init_swapchain);
    reshade::unregister_event<reshade::addon_event::copy_texture_region>(smvr::on_copy_texture);
    reshade::unregister_event<reshade::addon_event::destroy_device>(smvr::on_destroy_device);
    reshade::unregister_event<reshade::addon_event::init_device>(smvr::on_init_device);
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);
    smvr::g_lua_pcall_original.store(nullptr, std::memory_order_release);
    smvr::g_lua_call_original.store(nullptr, std::memory_order_release);
    smvr::g_lua_getfield_original.store(nullptr, std::memory_order_release);
    smvr::g_lua_newstate_original.store(nullptr, std::memory_order_release);
    smvr::g_lua_loadbufferx_original.store(nullptr, std::memory_order_release);
    smvr::g_camera_build_original.store(nullptr, std::memory_order_release);
    smvr::g_raycast_original.store(nullptr, std::memory_order_release);
    smvr::g_render_original.store(nullptr, std::memory_order_release);
    MH_Uninitialize();
    if (!smvr::restore_viewmodel_pass_patch()) smvr::g_failed.store(true, std::memory_order_release);
    {
        std::lock_guard lock(smvr::g_xr_mutex);
        smvr::g_xr.destroy();
    }
    smvr::release_atomic(smvr::g_final_target);
    smvr::release_atomic(smvr::g_backbuffer_identity);
    smvr::release_atomic(smvr::g_context);
    smvr::release_atomic(smvr::g_device);
    smvr::g_game_api_swapchain.store(nullptr, std::memory_order_release);
    smvr::g_game_api_device.store(nullptr, std::memory_order_release);
    smvr::g_game_device_pinned.store(false, std::memory_order_release);
    smvr::log_line("SMVR_V1_STOP engine_frames=%llu stereo_frames=%llu xr_success=%llu camera_builds=%llu viewmodel_camera_matches=%llu viewmodel_pass_patched=%u mirror_present_failures=%llu failed=%u",
        static_cast<unsigned long long>(smvr::g_engine_frames.load()),
        static_cast<unsigned long long>(smvr::g_stereo_frames.load()),
        static_cast<unsigned long long>(smvr::g_openxr_success_frames.load()),
        static_cast<unsigned long long>(smvr::g_camera_build_calls.load()),
        static_cast<unsigned long long>(smvr::g_viewmodel_camera_hides.load()),
        smvr::g_viewmodel_pass_patched.load() ? 1u : 0u,
        static_cast<unsigned long long>(smvr::g_mirror_present_failures.load()),
        smvr::g_failed.load() ? 1u : 0u);
    if (smvr::g_log != INVALID_HANDLE_VALUE) { CloseHandle(smvr::g_log); smvr::g_log = INVALID_HANDLE_VALUE; }
    reshade::unregister_addon(addon_module ? addon_module : smvr::g_module, reshade_module);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { smvr::g_module = module; DisableThreadLibraryCalls(module); }
    return TRUE;
}
