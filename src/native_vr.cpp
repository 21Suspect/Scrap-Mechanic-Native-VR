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

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

using RenderSetupFn = void (__fastcall *)(void *, float, const float *, const float *, void *);
using CameraBuildFn = void (__fastcall *)(void *, const float *, const float *, float, float, float);

HMODULE g_module = nullptr;
HMODULE g_game = nullptr;
uintptr_t g_game_base = 0;
std::atomic<void *> g_render_original{nullptr};
std::atomic<void *> g_camera_build_original{nullptr};
std::atomic<ID3D11Device *> g_device{nullptr};
std::atomic<ID3D11DeviceContext *> g_context{nullptr};
std::atomic<ID3D11Texture2D *> g_final_target{nullptr};
std::atomic<uint64_t> g_backbuffer_handle{0};
std::atomic<IUnknown *> g_backbuffer_identity{nullptr};
std::atomic<bool> g_native_device_from_reshade{false};
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_hook_reached{false};
std::atomic<bool> g_target_found{false};
std::atomic<bool> g_failed{false};
std::atomic<uint64_t> g_engine_frames{0};
std::atomic<uint64_t> g_stereo_frames{0};
std::atomic<uint64_t> g_openxr_success_frames{0};
std::atomic<bool> g_camera_abi_logged{false};
std::atomic<uint64_t> g_camera_build_calls{0};
std::atomic<uint64_t> g_vr_camera_diagnostic_calls{0};
std::atomic<uint64_t> g_viewmodel_camera_hides{0};
std::atomic<bool> g_viewmodel_camera_hide_logged{false};
std::atomic<bool> g_viewmodel_pass_patched{false};
std::atomic<bool> g_vr_target_tracking_suppressed_logged{false};
std::atomic<uint64_t> g_vr_target_reacquires{0};
std::atomic<uint64_t> g_mirror_present_failures{0};
std::atomic<bool> g_mirror_present_failure_logged{false};
std::atomic<void *> g_last_render_manager{nullptr};
std::atomic<bool> g_target_wrapper_diagnostic_logged{false};
std::atomic<bool> g_highres_pc_probe_requested{false};
std::atomic<bool> g_highres_pc_probe_done{false};
thread_local int g_active_eye = -1;
thread_local uint32_t g_eye_camera_build_index = 0;
std::mutex g_log_mutex;
std::mutex g_xr_mutex;
HANDLE g_log = INVALID_HANDLE_VALUE;
std::wstring g_ini_path;

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

bool build_eye_projection(const XrFovf &fov, const float *game_projection,
                          uint32_t source_width, uint32_t source_height,
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
        source_width < 16 || source_height < 16) return false;

    // Scrap Mechanic 1.0's cloud reconstruction and cascade-shadow setup assume
    // a centered projection. Render the smallest centered frustum containing the
    // runtime eye FOV, then crop the exact asymmetric tangent interval for OpenXR.
    // This preserves the Quest projection after composition without exposing the
    // derived engine passes to non-zero projection center terms.
    const float symmetric_x = (std::max)(-tan_left, tan_right);
    const float symmetric_y = (std::max)(-tan_down, tan_up);
    if (symmetric_x < 0.001f || symmetric_y < 0.001f) return false;
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
    const float ndc_right = tan_right / symmetric_x;
    const float ndc_down = tan_down / symmetric_y;
    const float ndc_up = tan_up / symmetric_y;
    const auto clamp_u32 = [](long value, uint32_t maximum) -> uint32_t {
        if (value < 0) return 0;
        if (static_cast<uint64_t>(value) > maximum) return maximum;
        return static_cast<uint32_t>(value);
    };
    uint32_t left = clamp_u32(std::lround((ndc_left + 1.0f) * 0.5f * static_cast<float>(source_width)), source_width - 1);
    uint32_t right = clamp_u32(std::lround((ndc_right + 1.0f) * 0.5f * static_cast<float>(source_width)), source_width);
    uint32_t top = clamp_u32(std::lround((1.0f - ndc_up) * 0.5f * static_cast<float>(source_height)), source_height - 1);
    uint32_t bottom = clamp_u32(std::lround((1.0f - ndc_down) * 0.5f * static_cast<float>(source_height)), source_height);
    if (right <= left + 8 || bottom <= top + 8) return false;
    mapping.source_box = {left, top, 0, right, bottom, 1};
    mapping.width = static_cast<int32_t>(right - left);
    mapping.height = static_cast<int32_t>(bottom - top);
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
    return eye_crop_width(views[0].fov, width) == static_cast<int32_t>(eyes[0].width) &&
        eye_crop_width(views[1].fov, width) == static_cast<int32_t>(eyes[1].width) &&
        eye_crop_height(views[0].fov, height) == static_cast<int32_t>(eyes[0].height) &&
        eye_crop_height(views[1].fov, height) == static_cast<int32_t>(eyes[1].height);
}

ID3D11Texture2D *refresh_present_frame_target(void *manager, uint32_t width, uint32_t height,
                                               DXGI_FORMAT format);

struct OpenXrState
{
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false;
    bool initialized = false;
    bool anchor_valid = false;
    bool eye_math_logged = false;
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
            g_failed.store(true, std::memory_order_release);
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
            log_line("VR_RENDER_SIZE_RESTORED desktop=%ux%u scale=%.6f",
                original_render_width, original_render_height, original_render_scale);
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
            log_line("VR_RENDER_SIZE_PERSISTENT desktop=%ux%u vr=%ux%u desktop_setting_unchanged=1",
                original_render_width, original_render_height, width, height);
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

    bool mirror_left_eye(ID3D11Device *device, ID3D11DeviceContext *context, IDXGISwapChain *swapchain)
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
        return true;
    }

    void destroy()
    {
        restore_render_size_override();
        restore_viewmodel_pass_patch();
        release_mirror();
        mirror_failed = false;
        if (running && session != XR_NULL_HANDLE) xrEndSession(session);
        running = false;
        for (auto &eye : eyes) { if (eye.handle != XR_NULL_HANDLE) xrDestroySwapchain(eye.handle); eye = {}; }
        if (space != XR_NULL_HANDLE) xrDestroySpace(space);
        if (session != XR_NULL_HANDLE) xrDestroySession(session);
        if (instance != XR_NULL_HANDLE) xrDestroyInstance(instance);
        instance = XR_NULL_HANDLE; system = XR_NULL_SYSTEM_ID; session = XR_NULL_HANDLE; space = XR_NULL_HANDLE;
        initialized = false; anchor_valid = false; eye_math_logged = false; state = XR_SESSION_STATE_UNKNOWN;
        source_width = source_height = desktop_width = desktop_height = 0;
        pending_desktop_width = pending_desktop_height = 0;
    }

    bool fail(const char *stage, XrResult result)
    {
        log_line("FAIL stage=%s xr=%d", stage, static_cast<int>(result));
        g_failed.store(true, std::memory_order_release);
        return false;
    }

    bool initialize(ID3D11Device *device, const D3D11_TEXTURE2D_DESC &source)
    {
        const char *extensions[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };
        XrInstanceCreateInfo instance_info{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(instance_info.applicationInfo.applicationName, "Scrap Mechanic Native VR v1");
        instance_info.applicationInfo.applicationVersion = 1;
        strcpy_s(instance_info.applicationInfo.engineName, "Scrap Mechanic 1.0");
        instance_info.applicationInfo.engineVersion = 876;
        instance_info.applicationInfo.apiVersion = XR_MAKE_VERSION(1,0,34);
        instance_info.enabledExtensionCount = 1;
        instance_info.enabledExtensionNames = extensions;
        XrResult result = xrCreateInstance(&instance_info, &instance);
        if (XR_FAILED(result)) return fail("xrCreateInstance", result);

        XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
        result = xrGetInstanceProperties(instance, &properties);
        if (XR_FAILED(result)) return fail("xrGetInstanceProperties", result);
        log_line("XR_RUNTIME name=%s version=%u.%u.%u", properties.runtimeName,
            XR_VERSION_MAJOR(properties.runtimeVersion), XR_VERSION_MINOR(properties.runtimeVersion), XR_VERSION_PATCH(properties.runtimeVersion));

        XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
        system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        result = xrGetSystem(instance, &system_info, &system);
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
            log_line("FAIL stage=adapter_luid_match hr=%08x", static_cast<unsigned>(hr));
            g_failed.store(true); return false;
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
            g_failed.store(true, std::memory_order_release);
            return false;
        }
        const int64_t required_format = static_cast<int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        bool format_supported = false;
        for (int64_t f : formats) if (f == required_format) format_supported = true;
        if (!format_supported) { log_line("FAIL stage=srgb_swapchain_format_not_supported format=%lld", static_cast<long long>(required_format)); g_failed.store(true); return false; }

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
        }
        if (!initialize_mirror(device))
        {
            log_line("FAIL stage=desktop_mirror_initialize");
            g_failed.store(true, std::memory_order_release);
            return false;
        }
        desktop_width = source.Width; desktop_height = source.Height; source_format = source.Format;
        initialized = true;
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
                log_line("XR_SESSION_STATE state=%d", static_cast<int>(state));
                if (state == XR_SESSION_STATE_READY && !running)
                {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    result = xrBeginSession(session, &begin);
                    if (XR_SUCCEEDED(result)) { running = true; anchor_valid = false; log_line("XR_SESSION_RUNNING"); }
                    else fail("xrBeginSession", result);
                }
                else if (state == XR_SESSION_STATE_STOPPING && running)
                {
                    result = xrEndSession(session);
                    running = false; anchor_valid = false;
                    restore_render_size_override();
                    restore_viewmodel_pass_patch();
                    if (XR_FAILED(result)) fail("xrEndSession", result);
                }
                else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING)
                {
                    running = false;
                    restore_render_size_override();
                    restore_viewmodel_pass_patch();
                    g_failed.store(true); log_line("FAIL stage=session_exit_or_loss state=%d", static_cast<int>(state));
                }
            }
        }
    }

    bool render_stereo(RenderSetupFn original, void *renderer, float scalar, const float *game_world_to_view,
                       const float *game_projection, void *settings, ID3D11DeviceContext *context, ID3D11Texture2D *source)
    {
        poll_events();
        if (!running || g_failed.load())
        {
            restore_render_size_override();
            restore_viewmodel_pass_patch();
            return false;
        }
        D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
        if (desc.Format != source_format || desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1)
        {
            log_line("FAIL stage=target_changed actual=%ux%u/%u", desc.Width, desc.Height, static_cast<unsigned>(desc.Format));
            g_failed.store(true); return false;
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
            log_line("VR_RENDER_RESOLUTION desktop=%ux%u engine_offscreen=%ux%u submitted=%ux%u,%ux%u policy=runtime_recommended_exact guard_band=0",
                desktop_width, desktop_height, source_width, source_height,
                eyes[0].width, eyes[0].height, eyes[1].width, eyes[1].height);
        }

        if (!anchor_valid)
        {
            std::memcpy(anchor_game.m, game_world_to_view, sizeof(anchor_game.m));
            anchor_head.orientation = yaw_only(views[0].pose.orientation);
            anchor_head.position = {
                (views[0].pose.position.x + views[1].pose.position.x) * 0.5f,
                (views[0].pose.position.y + views[1].pose.position.y) * 0.5f,
                (views[0].pose.position.z + views[1].pose.position.z) * 0.5f
            };
            anchor_valid = true;
            log_line("CAMERA_ANCHOR view_translation=%.4f,%.4f,%.4f reference_yaw=(%.6f,%.6f,%.6f,%.6f)",
                anchor_game.m[12], anchor_game.m[13], anchor_game.m[14], anchor_head.orientation.x,
                anchor_head.orientation.y, anchor_head.orientation.z, anchor_head.orientation.w);
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
                    eye_projection, mapping))
                return abort_frame("eye_camera_build", XR_ERROR_VALIDATION_FAILURE);
            if (mapping.width != static_cast<int32_t>(eyes[i].width) ||
                mapping.height != static_cast<int32_t>(eyes[i].height))
            {
                log_line("FAIL stage=eye_extent_changed eye=%u actual=%dx%d required=%ux%u", i,
                    mapping.width, mapping.height, eyes[i].width, eyes[i].height);
                return abort_frame("eye_extent_changed", XR_ERROR_VALIDATION_FAILURE);
            }
            multiply_column_major(tracking_view, game_world_to_view, eye_world_to_view);
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
            if (i == 0 && mirror_texture)
            {
                context->CopySubresourceRegion(mirror_texture, 0, 0, 0, 0,
                    eye_source, 0, &mapping.source_box);
                mirror_ready = true;
            }
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
                log_line("EYE_CAMERA eye=%u pose_pos=%.6f,%.6f,%.6f pose_q=%.6f,%.6f,%.6f,%.6f runtime_fov=%.6f,%.6f,%.6f,%.6f submitted_fov=runtime_exact guard_band=0 view_t=%.6f,%.6f,%.6f projection=%.6f,%.6f,%.6f,%.6f depth=%.6f,%.6f,%.6f,%.6f crop=%u,%u,%u,%u extent=%d,%d temporal_scalar=%.6f",
                    i, views[i].pose.position.x, views[i].pose.position.y, views[i].pose.position.z,
                    views[i].pose.orientation.x, views[i].pose.orientation.y, views[i].pose.orientation.z, views[i].pose.orientation.w,
                    views[i].fov.angleLeft, views[i].fov.angleRight, views[i].fov.angleUp, views[i].fov.angleDown,
                    eye_world_to_view[12], eye_world_to_view[13], eye_world_to_view[14],
                    eye_projection[0], eye_projection[5], eye_projection[8], eye_projection[9],
                    eye_projection[10], eye_projection[11], eye_projection[14], eye_projection[15],
                    mapping.source_box.left, mapping.source_box.top, mapping.source_box.right,
                    mapping.source_box.bottom, mapping.width, mapping.height, eye_scalar);
            }
        }
        eye_math_logged = true;
        context->Flush();
        XrResult release_failure = XR_SUCCESS;
        for (uint32_t i = 0; i != 2; ++i)
        {
            if (acquired[i])
            {
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                result = xrReleaseSwapchainImage(eyes[i].handle, &release);
                if (XR_FAILED(result) && XR_SUCCEEDED(release_failure)) release_failure = result;
                acquired[i] = false;
            }
        }
        if (XR_FAILED(release_failure)) return abort_frame("xrReleaseSwapchainImage", release_failure);
        if (!rendered[0] || !rendered[1]) return fail("both_eyes_not_rendered", XR_ERROR_RUNTIME_FAILURE);

        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = space;
        layer.viewCount = 2;
        layer.views = projection_views.data();
        const XrCompositionLayerBaseHeader *layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer)};
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = frame_state.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = 1;
        end.layers = layers;
        result = xrEndFrame(session, &end);
        if (XR_FAILED(result)) return fail("xrEndFrame", result);
        LARGE_INTEGER frame_render_end{};
        QueryPerformanceCounter(&frame_render_end);
        const double render_ms = performance_frequency.QuadPart > 0
            ? static_cast<double>(frame_render_end.QuadPart - frame_render_begin.QuadPart) * 1000.0 /
                static_cast<double>(performance_frequency.QuadPart)
            : 0.0;
        const uint64_t completed = g_openxr_success_frames.fetch_add(1) + 1;
        if (completed <= 10 || (completed % 120) == 0)
            log_line("OPENXR_FRAME_SUCCESS frame=%llu left_rendered=1 right_rendered=1 xrEndFrame=0 render_ms=%.3f eye_ms=%.3f,%.3f",
                static_cast<unsigned long long>(completed), render_ms, eye_render_ms[0], eye_render_ms[1]);
        return true;
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

    const bool keep_reshade_native = g_native_device_from_reshade.load(std::memory_order_acquire) &&
        g_device.load(std::memory_order_acquire) && g_context.load(std::memory_order_acquire);
    if (keep_reshade_native)
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
    log_line("NATIVE_RENDERER_READY renderer=%p device=%p context=%p backbuffer=%p size=%ux%u format=%u",
        low_renderer, device, context, backbuffer_field, desc.Width, desc.Height, static_cast<unsigned>(desc.Format));
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
    if (g_failed.load(std::memory_order_acquire)) return;
    if (!view_valid || !projection_valid)
    {
        log_line("FAIL stage=live_camera_abi view_valid=%u projection_valid=%u", view_valid ? 1u : 0u, projection_valid ? 1u : 0u);
        g_failed.store(true, std::memory_order_release);
        return;
    }

    ID3D11Device *device = g_device.load(std::memory_order_acquire);
    ID3D11DeviceContext *context = g_context.load(std::memory_order_acquire);
    ID3D11Texture2D *target = g_final_target.load(std::memory_order_acquire);
    if (!device || !context || !target)
    {
        if (g_xr.initialized || g_xr.running)
        {
            log_line("FAIL stage=native_renderer_resource_lost device=%p context=%p target=%p", device, context, target);
            g_failed.store(true, std::memory_order_release);
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
        if (!g_failed.load()) log_line("XR_FRAME_NOT_RENDERED");
        return;
    }
    g_stereo_frames.fetch_add(1);
}

void on_init_device(reshade::api::device *device)
{
    if (!device || device->get_api() != reshade::api::device_api::d3d11) return;
    auto *native = reinterpret_cast<ID3D11Device *>(static_cast<uintptr_t>(device->get_native()));
    if (!native) return;
    native->AddRef();
    ID3D11DeviceContext *context = nullptr;
    native->GetImmediateContext(&context);
    if (!context)
    {
        native->Release();
        return;
    }
    if (ID3D11Device *old = g_device.exchange(native, std::memory_order_acq_rel)) old->Release();
    if (ID3D11DeviceContext *old = g_context.exchange(context, std::memory_order_acq_rel)) old->Release();
    g_native_device_from_reshade.store(true, std::memory_order_release);
    log_line("D3D11_NATIVE_DEVICE_READY device=%p context=%p", native, context);
}

void on_destroy_device(reshade::api::device *)
{
    std::lock_guard lock(g_xr_mutex);
    g_xr.destroy();
    release_atomic(g_final_target);
    release_atomic(g_backbuffer_identity);
    release_atomic(g_context);
    release_atomic(g_device);
    g_native_device_from_reshade.store(false, std::memory_order_release);
}

bool on_copy_texture(reshade::api::command_list *, reshade::api::resource source, uint32_t source_subresource,
                     const reshade::api::subresource_box *source_box, reshade::api::resource dest, uint32_t dest_subresource,
                     const reshade::api::subresource_box *dest_box, reshade::api::filter_mode)
{
    // During either real eye render the engine copies its completed scene toward
    // the desktop-present chain. That desktop resource is not the VR eye source.
    // Tracking it here caused g_final_target to oscillate between 1920x1080 and
    // the persistent VR target every eye and forced redundant COM work/logging.
    // PC mode remains unchanged because g_active_eye is -1 outside VR renders.
    if (g_active_eye >= 0)
    {
        if (!g_vr_target_tracking_suppressed_logged.exchange(true, std::memory_order_acq_rel))
            log_line("VR_DESKTOP_TARGET_TRACKING_SUPPRESSED eye=%d", g_active_eye);
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

void on_present(reshade::api::command_queue *, reshade::api::swapchain *swapchain,
                const reshade::api::rect *, const reshade::api::rect *, uint32_t,
                const reshade::api::rect *)
{
    if (!g_enabled.load(std::memory_order_acquire) || !swapchain) return;
    auto *native_swapchain = reinterpret_cast<IDXGISwapChain *>(
        static_cast<uintptr_t>(swapchain->get_native()));
    ID3D11Device *device = g_device.load(std::memory_order_acquire);
    ID3D11DeviceContext *context = g_context.load(std::memory_order_acquire);
    if (!native_swapchain || !device || !context) return;

    std::lock_guard lock(g_xr_mutex);
    if (!g_xr.initialized || !g_xr.running || !g_xr.mirror_ready) return;
    if (!g_xr.mirror_left_eye(device, context, native_swapchain))
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
    std::lock_guard lock(g_xr_mutex);
    // IDXGISwapChain::ResizeBuffers requires every direct and indirect reference
    // to the old backbuffers to be released. The PC left-eye mirror owns an RTV
    // and canonical IUnknown for the current buffer, so release those at ReShade's
    // pre-resize/reset boundary. OpenXR eye swapchains are independent and remain
    // alive at their exact runtime-recommended extent.
    g_xr.release_mirror_backbuffer();
    g_xr.mirror_ready = false;
    // The renderer bootstrap also retains the desktop backbuffer's canonical
    // IUnknown solely for resource-identity matching. It is another DXGI buffer
    // reference and must be dropped before ResizeBuffers. The next render hook
    // reacquires the new buffer identity through the validated renderer fields.
    release_atomic(g_backbuffer_identity);
    g_backbuffer_handle.store(0, std::memory_order_release);
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
    std::lock_guard lock(g_xr_mutex);
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
        g_failed.store(true, std::memory_order_release);
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
    const void *viewmodel_branch = reinterpret_cast<const void *>(g_game_base + kViewmodelSkipBranchRva);
    return readable(entry, sizeof(kRenderPrefix)) && std::memcmp(entry, kRenderPrefix, sizeof(kRenderPrefix)) == 0 &&
        readable(camera_entry, sizeof(kCameraBuildPrefix)) &&
        std::memcmp(camera_entry, kCameraBuildPrefix, sizeof(kCameraBuildPrefix)) == 0 &&
        readable(viewmodel_branch, sizeof(kViewmodelConditionalBranch)) &&
        std::memcmp(viewmodel_branch, kViewmodelConditionalBranch, sizeof(kViewmodelConditionalBranch)) == 0;
}

bool install_hook()
{
    if (MH_Initialize() != MH_OK) return false;
    void *render_target = reinterpret_cast<void *>(g_game_base + kRenderSetupRva);
    void *camera_target = reinterpret_cast<void *>(g_game_base + kCameraBuildRva);
    void *render_original = nullptr;
    void *camera_original = nullptr;
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
    g_render_original.store(render_original, std::memory_order_release);
    g_camera_build_original.store(camera_original, std::memory_order_release);
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        MH_RemoveHook(camera_target);
        MH_RemoveHook(render_target);
        g_camera_build_original.store(nullptr, std::memory_order_release);
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
    g_enabled.store(config_enabled(), std::memory_order_release);
    g_highres_pc_probe_requested.store(
        GetPrivateProfileIntW(L"Diagnostic", L"HighResolutionProbe", 0, g_ini_path.c_str()) == 1,
        std::memory_order_release);
}
} // namespace smvr

extern "C" __declspec(dllexport) const char *NAME = "Scrap Mechanic Native OpenXR VR v1";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Exact-build two-render OpenXR stereo visual test candidate";

extern "C" __declspec(dllexport) BOOL AddonInit(HMODULE addon_module, HMODULE reshade_module)
{
    smvr::g_module = addon_module;
    smvr::initialize_paths();
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
    smvr::log_line("HOOK_INSTALLED rva=%llx", static_cast<unsigned long long>(smvr::kRenderSetupRva));
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
    smvr::g_camera_build_original.store(nullptr, std::memory_order_release);
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
