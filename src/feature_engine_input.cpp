#include "feature_engine_input.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace smvr
{
void log_line(const char *format, ...);
}

namespace smvr::features
{
namespace
{
constexpr uint32_t kExpectedTimestamp = 0x6A7060DD;
constexpr uint32_t kExpectedImageSize = 0x01C1D000;

// Exact RVAs ported by instruction-level comparison between the archived
// 0.7.4.778 executable used by the working GitHub mod and Steam build 24529696.
constexpr uintptr_t kInputManagerPointerRva = 0x01A62708;
constexpr uintptr_t kMouseDownRva = 0x0061A520;
constexpr uintptr_t kMouseUpRva = 0x0061A600;
constexpr uintptr_t kMouseMoveRva = 0x0061A6F0;
constexpr uintptr_t kQueueEventRva = 0x0061A420;
constexpr size_t kEventQueueOffset = 0x1E0;

using MouseButtonFunction = void (*)(void *, int, int);
using MouseMoveFunction = void (*)(void *, int, int, int, int, int);
using QueueEventFunction = void (*)(void *, const void *);

// Binary layout copied by kQueueEventRva. The +0x08 region is an empty MSVC
// std::wstring in small-string mode (capacity seven); the payload begins at
// +0x28. This is used only for the wheel event, for which the game exposes no
// separate wrapper in this build.
struct alignas(8) NativeInputEvent
{
    uint32_t type = 0;
    uint32_t reserved = 0;
    uint16_t inline_text[8]{};
    uint64_t text_size = 0;
    uint64_t text_capacity = 7;
    int32_t data[6]{};
};
static_assert(sizeof(NativeInputEvent) == 0x40);
static_assert(offsetof(NativeInputEvent, inline_text) == 0x08);
static_assert(offsetof(NativeInputEvent, data) == 0x28);

bool readable_address(const void *address, size_t length)
{
    if (!address || length == 0) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    const auto begin = reinterpret_cast<uintptr_t>(address);
    const auto end = begin + length;
    const auto region_end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    return end >= begin && end <= region_end;
}

bool executable_address(const void *address, size_t length)
{
    if (!readable_address(address, length)) return false;
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(address, &info, sizeof(info));
    const DWORD protection = info.Protect & 0xffu;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

template <size_t N>
bool bytes_equal(const uint8_t *base, uintptr_t rva, size_t offset,
                 const std::array<uint8_t, N> &expected)
{
    const uint8_t *address = base + rva + offset;
    return executable_address(address, expected.size()) &&
        std::memcmp(address, expected.data(), expected.size()) == 0;
}
} // namespace

EngineInputQueue &EngineInputQueue::instance()
{
    static EngineInputQueue queue;
    return queue;
}

bool EngineInputQueue::validate_build_layout()
{
    if (validation_attempted_) return layout_valid_;
    validation_attempted_ = true;

    const auto *base = reinterpret_cast<const uint8_t *>(GetModuleHandleW(nullptr));
    if (!readable_address(base, sizeof(IMAGE_DOS_HEADER))) return false;
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    if (!readable_address(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.TimeDateStamp != kExpectedTimestamp ||
        nt->OptionalHeader.SizeOfImage != kExpectedImageSize)
        return false;

    // Prologues plus event-construction sites prove both the entry points and
    // their semantics. Relative call/cookie displacements are intentionally not
    // included, since their targets are independently checked below.
    const bool down_ok =
        bytes_equal(base, kMouseDownRva, 0x00,
            std::array<uint8_t, 6>{0x40,0x53,0x48,0x83,0xEC,0x70}) &&
        bytes_equal(base, kMouseDownRva, 0x4B,
            std::array<uint8_t, 21>{0xC7,0x44,0x24,0x20,0x01,0x00,0x00,0x00,
                0x89,0x5C,0x24,0x48,0x89,0x54,0x24,0x4C,0x44,0x89,0x44,0x24,0x50}) &&
        bytes_equal(base, kMouseDownRva, 0x60,
            std::array<uint8_t, 7>{0x48,0x81,0xC1,0xE0,0x01,0x00,0x00});
    const bool up_ok =
        bytes_equal(base, kMouseUpRva, 0x00,
            std::array<uint8_t, 6>{0x40,0x53,0x48,0x83,0xEC,0x70}) &&
        bytes_equal(base, kMouseUpRva, 0x4B,
            std::array<uint8_t, 25>{0xC7,0x44,0x24,0x20,0x01,0x00,0x00,0x00,
                0xC7,0x44,0x24,0x48,0x01,0x00,0x00,0x00,0x89,0x54,0x24,0x4C,
                0x44,0x89,0x44,0x24,0x50}) &&
        bytes_equal(base, kMouseUpRva, 0x64,
            std::array<uint8_t, 7>{0x48,0x81,0xC1,0xE0,0x01,0x00,0x00});
    const bool move_ok =
        bytes_equal(base, kMouseMoveRva, 0x00,
            std::array<uint8_t, 8>{0x4C,0x8B,0xDC,0x53,0x48,0x83,0xEC,0x70}) &&
        bytes_equal(base, kMouseMoveRva, 0x58,
            std::array<uint8_t, 8>{0xC7,0x44,0x24,0x20,0x03,0x00,0x00,0x00}) &&
        bytes_equal(base, kMouseMoveRva, 0x7B,
            std::array<uint8_t, 7>{0x48,0x81,0xC1,0xE0,0x01,0x00,0x00});
    const bool queue_ok = bytes_equal(base, kQueueEventRva, 0x00,
        std::array<uint8_t, 20>{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,
            0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20});
    const bool pointer_ok = readable_address(base + kInputManagerPointerRva, sizeof(void *));
    layout_valid_ = down_ok && up_ok && move_ok && queue_ok && pointer_ok;
    if (!layout_valid_ && !invalid_logged_)
    {
        invalid_logged_ = true;
        log_line("VR_UI_ENGINE_INPUT_REJECTED build_layout_mismatch down=%u up=%u move=%u queue=%u pointer=%u win32_fallback=0",
            down_ok?1u:0u,up_ok?1u:0u,move_ok?1u:0u,queue_ok?1u:0u,pointer_ok?1u:0u);
    }
    return layout_valid_;
}

void *EngineInputQueue::input_manager()
{
    if (!validate_build_layout()) return nullptr;
    auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    auto **storage = reinterpret_cast<void **>(base + kInputManagerPointerRva);
    void *manager = *storage;
    if (!readable_address(manager, kEventQueueOffset + 0x28))
    {
        if (!waiting_logged_)
        {
            waiting_logged_ = true;
            log_line("VR_UI_ENGINE_INPUT_WAITING reason=input_manager_not_initialized win32_fallback=0");
        }
        return nullptr;
    }
    waiting_logged_ = false;
    return manager;
}

bool EngineInputQueue::available()
{
    return input_manager() != nullptr;
}

bool EngineInputQueue::queue_mouse_move(int delta_x, int delta_y, int client_x, int client_y)
{
    void *manager = input_manager();
    if (!manager) return false;
    auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    reinterpret_cast<MouseMoveFunction>(base + kMouseMoveRva)(
        manager, delta_x, delta_y, client_x, client_y, 0);
    if (!active_logged_)
    {
        active_logged_ = true;
        log_line("VR_UI_ENGINE_INPUT_ACTIVE route=private_input_event_queue move_rva=%08llx down_rva=%08llx up_rva=%08llx manager_rva=%08llx win32_mouse_simulation=0",
            static_cast<unsigned long long>(kMouseMoveRva),
            static_cast<unsigned long long>(kMouseDownRva),
            static_cast<unsigned long long>(kMouseUpRva),
            static_cast<unsigned long long>(kInputManagerPointerRva));
    }
    return true;
}

bool EngineInputQueue::queue_mouse_delta(int delta_x, int delta_y,
                                         int client_width, int client_height)
{
    void *manager = input_manager();
    if (!manager || client_width <= 0 || client_height <= 0) return false;
    const auto *bytes = reinterpret_cast<const uint8_t *>(manager);
    int current_x = 0, current_y = 0;
    std::memcpy(&current_x, bytes + 0x30, sizeof(current_x));
    std::memcpy(&current_y, bytes + 0x34, sizeof(current_y));
    const int client_x = std::clamp(current_x + delta_x, 0, client_width - 1);
    const int client_y = std::clamp(current_y + delta_y, 0, client_height - 1);
    return queue_mouse_move(delta_x, delta_y, client_x, client_y);
}

bool EngineInputQueue::queue_mouse_button(uint32_t button, bool down)
{
    void *manager = input_manager();
    if (!manager || button > 4) return false;
    auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    const uintptr_t rva = down ? kMouseDownRva : kMouseUpRva;
    reinterpret_cast<MouseButtonFunction>(base + rva)(
        manager, static_cast<int>(button), 0);
    return true;
}

bool EngineInputQueue::queue_mouse_wheel(int delta)
{
    void *manager = input_manager();
    if (!manager || delta == 0) return false;
    NativeInputEvent event{};
    event.type = 2;
    event.data[0] = delta;
    event.data[1] = 0;
    auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    reinterpret_cast<QueueEventFunction>(base + kQueueEventRva)(
        reinterpret_cast<uint8_t *>(manager) + kEventQueueOffset, &event);
    return true;
}
} // namespace smvr::features
