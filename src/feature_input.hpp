#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <openxr/openxr.h>

#include <array>
#include <cstdint>

namespace smvr::features
{
struct InputConfig
{
    bool enabled = true;
    bool optical_hand_tracking = true;
    bool haptics = true;
    float haptic_strength = 0.65f;
    float stick_deadzone = 0.30f;
    float horizontal_turn_speed = 36.0f;
    float vertical_turn_speed = 28.0f;
};

struct HandState
{
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    std::array<float, 5> finger_curls{};
    // Optical hand tracking supplies independent MCP/PIP/DIP bend angles for
    // every finger. Thumb rendering intentionally keeps its confirmed scalar
    // behavior, while index through little use these three tracked joints.
    std::array<std::array<float, 3>, 5> finger_bends{};
    float trigger = 0.0f;
    float squeeze = 0.0f;
    bool active = false;
    bool optical = false;
    bool interaction = false;
    bool firing = false;
    bool precise_fingers = false;
};

class InputBridge
{
public:
    bool initialize(XrInstance instance, XrSession session, XrSpace base_space,
                    const InputConfig &config, bool hand_tracking_extension_enabled);
    void on_session_state(XrSessionState state);
    void set_startup_menu(bool visible, bool pointer_active);
    bool sync(XrTime display_time, const XrPosef &head_pose);
    bool pulse_haptic(uint32_t hand, float amplitude, uint32_t duration_ms,
                      float frequency = XR_FREQUENCY_UNSPECIFIED);
    void release_injected_input();
    bool consume_recenter_request();
    void shutdown();

    bool initialized() const { return initialized_; }
    const HandState &hand(uint32_t index) const { return hands_[index < 2 ? index : 0]; }
    const XrPosef &pointer_pose(uint32_t index) const { return pointer_poses_[index < 2 ? index : 0]; }
    bool pointer_pose_active(uint32_t index) const { return pointer_pose_active_[index < 2 ? index : 0]; }
    bool ui_select_down() const { return ui_select_down_; }
    float ui_scroll_axis() const { return ui_scroll_axis_; }
    bool game_ui_open_intent() const;

private:
    bool create_actions();
    void create_optical_trackers(bool extension_enabled);
    bool update_controller_pose(uint32_t hand, XrTime display_time);
    bool update_controller_aim_pose(uint32_t hand, XrTime display_time);
    void update_optical_hands(XrTime display_time, const bool controller_pose_active[2]);
    void update_game_input(const XrPosef &head_pose);
    bool get_boolean(XrAction action, uint32_t hand, bool &value);
    bool get_float(XrAction action, uint32_t hand, float &value, bool *active = nullptr);
    bool get_vector(XrAction action, uint32_t hand, XrVector2f &value);
    void reset_runtime_state();

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace base_space_ = XR_NULL_HANDLE;
    XrSessionState session_state_ = XR_SESSION_STATE_UNKNOWN;
    InputConfig config_{};
    bool initialized_ = false;

    XrActionSet action_set_ = XR_NULL_HANDLE;
    XrAction grip_pose_action_ = XR_NULL_HANDLE;
    XrAction aim_pose_action_ = XR_NULL_HANDLE;
    XrAction trigger_action_ = XR_NULL_HANDLE;
    XrAction thumbstick_action_ = XR_NULL_HANDLE;
    XrAction squeeze_action_ = XR_NULL_HANDLE;
    XrAction primary_button_action_ = XR_NULL_HANDLE;
    XrAction secondary_button_action_ = XR_NULL_HANDLE;
    XrAction stick_click_action_ = XR_NULL_HANDLE;
    XrAction menu_button_action_ = XR_NULL_HANDLE;
    XrAction haptic_action_ = XR_NULL_HANDLE;
    XrPath hand_paths_[2]{XR_NULL_PATH, XR_NULL_PATH};
    XrSpace hand_spaces_[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};
    XrSpace aim_spaces_[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};

    PFN_xrCreateHandTrackerEXT create_hand_tracker_ = nullptr;
    PFN_xrDestroyHandTrackerEXT destroy_hand_tracker_ = nullptr;
    PFN_xrLocateHandJointsEXT locate_hand_joints_ = nullptr;
    XrHandTrackerEXT optical_trackers_[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};
    HandState hands_[2]{};
    XrPosef pointer_poses_[2]{};
    bool pointer_pose_active_[2]{};
    bool optical_pinch_down_[2]{};
    bool controller_trigger_down_[2]{};
    bool controller_trigger_candidate_down_[2]{};
    uint64_t controller_trigger_candidate_since_[2]{};
    uint64_t controller_trigger_last_active_[2]{};

    bool key_forward_ = false;
    bool key_backward_ = false;
    bool key_left_ = false;
    bool key_right_ = false;
    bool key_sprint_ = false;
    bool key_crouch_ = false;
    bool key_jump_ = false;
    bool key_use_ = false;
    bool key_camera_ = false;
    bool key_zoom_in_ = false;
    bool key_zoom_out_ = false;
    bool key_inventory_ = false;
    bool key_menu_ = false;
    bool mouse_attack_ = false;
    bool mouse_secondary_ = false;
    bool startup_menu_visible_ = false;
    bool startup_menu_pointer_active_ = false;
    bool ui_select_down_ = false;
    float ui_scroll_axis_ = 0.0f;
    uint64_t startup_menu_scroll_last_ms_ = 0;
    bool x_was_down_ = false;
    bool y_was_down_ = false;
    bool xy_chord_latched_ = false;
    bool locomotion_reference_valid_ = false;
    XrVector3f locomotion_reference_forward_{0.0f, 0.0f, -1.0f};
    uint64_t recenter_hold_start_ms_ = 0;
    bool recenter_latched_ = false;
    bool recenter_requested_ = false;
    bool input_active_logged_ = false;
    bool input_suspended_logged_ = false;
    bool sync_failure_logged_ = false;
    bool controller_ui_trigger_was_down_ = false;
    bool right_primary_was_down_ = false;
    bool left_primary_was_down_ = false;
    bool menu_was_down_ = false;
    bool b_was_down_ = false;
    uint64_t game_ui_open_intent_until_ms_ = 0;
    uint64_t last_haptic_ms_[2]{};
    bool haptic_ready_logged_ = false;
    bool haptic_failure_logged_ = false;
    bool hand_pose_logged_[2]{};
    bool aim_pose_logged_[2]{};
    bool optical_hand_logged_[2]{};
};
} // namespace smvr::features
