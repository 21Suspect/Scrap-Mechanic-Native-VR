#pragma once

#include <cstdint>

namespace smvr::features
{
// Build-locked access to Scrap Mechanic 1.0.5.876's private input-event queue.
// There is deliberately no Win32 keyboard or mouse fallback: an unavailable or
// mismatched engine route fails closed instead of controlling the desktop.
class EngineInputQueue
{
public:
    static EngineInputQueue &instance();

    bool queue_mouse_move(int delta_x, int delta_y, int client_x, int client_y);
    bool queue_mouse_delta(int delta_x, int delta_y, int client_width, int client_height);
    bool queue_key(uint32_t virtual_key, bool down);
    bool queue_mouse_button(uint32_t button, bool down);
    bool queue_mouse_wheel(int delta);
    bool available();

private:
    bool validate_build_layout();
    void *input_manager();

    bool validation_attempted_ = false;
    bool layout_valid_ = false;
    bool active_logged_ = false;
    bool waiting_logged_ = false;
    bool invalid_logged_ = false;
    uint32_t keyboard_modifiers_ = 0;
};
} // namespace smvr::features
