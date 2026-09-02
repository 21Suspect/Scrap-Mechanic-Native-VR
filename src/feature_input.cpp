#include "feature_input.hpp"
#include "feature_engine_input.hpp"
#include "vr_tools.hpp"

#include <algorithm>
#include <cmath>

namespace smvr
{
void log_line(const char *format, ...);
}

namespace smvr::features
{
namespace
{
constexpr uint32_t kHandCount = 2;
constexpr uint64_t kInventoryHoldMilliseconds = 420;
constexpr float kTurnReferenceRateHz = 72.0f;

BOOL CALLBACK find_game_window(HWND window, LPARAM parameter)
{
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == GetCurrentProcessId() && IsWindowVisible(window) &&
        GetWindow(window, GW_OWNER) == nullptr)
    {
        *reinterpret_cast<HWND *>(parameter) = window;
        return FALSE;
    }
    return TRUE;
}

HWND game_window()
{
    HWND window = nullptr;
    EnumWindows(find_game_window, reinterpret_cast<LPARAM>(&window));
    return window;
}

void focus_game_window()
{
    if (HWND window = game_window(); window != nullptr)
        SetForegroundWindow(window);
}

bool send_key(WORD key, bool down, bool &state)
{
    if (down == state) return true;
    if (!EngineInputQueue::instance().queue_key(key,down)) return false;
    state = down;
    return true;
}

bool send_mouse_button(uint32_t button, bool down, bool &state)
{
    if (down == state) return true;
    if (!EngineInputQueue::instance().queue_mouse_button(button,down)) return false;
    state = down;
    return true;
}

void send_mouse_wheel(LONG delta)
{
    EngineInputQueue::instance().queue_mouse_wheel(delta);
}

XrVector3f rotate_vector(const XrQuaternionf &q, const XrVector3f &v)
{
    const XrVector3f u{q.x, q.y, q.z};
    const float uv = u.x * v.x + u.y * v.y + u.z * v.z;
    const float uu = u.x * u.x + u.y * u.y + u.z * u.z;
    const XrVector3f cross{
        u.y * v.z - u.z * v.y,
        u.z * v.x - u.x * v.z,
        u.x * v.y - u.y * v.x
    };
    return {
        2.0f * uv * u.x + (q.w * q.w - uu) * v.x + 2.0f * q.w * cross.x,
        2.0f * uv * u.y + (q.w * q.w - uu) * v.y + 2.0f * q.w * cross.y,
        2.0f * uv * u.z + (q.w * q.w - uu) * v.z + 2.0f * q.w * cross.z
    };
}

bool horizontal_forward(const XrPosef &head, XrVector3f &forward)
{
    forward = rotate_vector(head.orientation, {0.0f, 0.0f, -1.0f});
    forward.y = 0.0f;
    const float length_squared = forward.x * forward.x + forward.z * forward.z;
    if (!std::isfinite(length_squared) || length_squared < 0.0001f) return false;
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    forward.x *= inverse_length;
    forward.z *= inverse_length;
    return true;
}

float distance(const XrVector3f &a, const XrVector3f &b)
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

bool bend_angle(const XrHandJointLocationEXT *joints, XrHandJointEXT a,
                XrHandJointEXT b, XrHandJointEXT c, float &angle)
{
    constexpr XrSpaceLocationFlags valid = XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if ((joints[a].locationFlags & valid) == 0 || (joints[b].locationFlags & valid) == 0 ||
        (joints[c].locationFlags & valid) == 0) return false;
    XrVector3f first{
        joints[b].pose.position.x - joints[a].pose.position.x,
        joints[b].pose.position.y - joints[a].pose.position.y,
        joints[b].pose.position.z - joints[a].pose.position.z
    };
    XrVector3f second{
        joints[c].pose.position.x - joints[b].pose.position.x,
        joints[c].pose.position.y - joints[b].pose.position.y,
        joints[c].pose.position.z - joints[b].pose.position.z
    };
    const float first_length = std::sqrt(first.x * first.x + first.y * first.y + first.z * first.z);
    const float second_length = std::sqrt(second.x * second.x + second.y * second.y + second.z * second.z);
    if (first_length < 0.001f || second_length < 0.001f) return false;
    const float cosine = std::clamp(
        (first.x * second.x + first.y * second.y + first.z * second.z) /
            (first_length * second_length), -1.0f, 1.0f);
    angle = std::acos(cosine);
    return std::isfinite(angle);
}
} // namespace

bool InputBridge::initialize(XrInstance instance, XrSession session, XrSpace base_space,
                             const InputConfig &config, bool hand_tracking_extension_enabled,
                             bool openxr_11_enabled, bool generic_controller_enabled)
{
    shutdown();
    config_ = config;
    if (!config_.enabled)
    {
        log_line("VR_FEATURE_INPUT disabled_by_config=1");
        return true;
    }
    instance_ = instance;
    session_ = session;
    base_space_ = base_space;
    openxr_11_enabled_ = openxr_11_enabled;
    generic_controller_enabled_ = generic_controller_enabled;
    if (instance_ == XR_NULL_HANDLE || session_ == XR_NULL_HANDLE || base_space_ == XR_NULL_HANDLE)
        return false;
    if (!create_actions())
    {
        log_line("FAIL stage=feature_input_create_actions");
        shutdown();
        return false;
    }
    create_optical_trackers(hand_tracking_extension_enabled && config_.optical_hand_tracking);
    initialized_ = true;
    log_line("VR_FEATURE_INPUT_READY controllers=oculus_touch,meta_touch_1_1,valve_index,khr_generic poses=1 locomotion=1 turn=1 buttons=1 recenter=1 haptics=%u strength=%.2f optical_hands=%u openxr_1_1=%u generic_controller=%u",
        config_.haptics ? 1u : 0u, config_.haptic_strength,
        optical_trackers_[0] != XR_NULL_HANDLE && optical_trackers_[1] != XR_NULL_HANDLE ? 1u : 0u,
        openxr_11_enabled_ ? 1u : 0u, generic_controller_enabled_ ? 1u : 0u);
    return true;
}

bool InputBridge::create_actions()
{
    auto path = [&](const char *name, XrPath &target) -> bool {
        const XrResult result = xrStringToPath(instance_, name, &target);
        if (XR_FAILED(result)) log_line("FAIL stage=xrStringToPath path=%s xr=%d", name, static_cast<int>(result));
        return XR_SUCCEEDED(result);
    };
    if (!path("/user/hand/left", hand_paths_[0]) || !path("/user/hand/right", hand_paths_[1]))
        return false;

    XrActionSetCreateInfo set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy_s(set_info.actionSetName, "gameplay");
    strcpy_s(set_info.localizedActionSetName, "Scrap Mechanic VR Gameplay");
    XrResult result = xrCreateActionSet(instance_, &set_info, &action_set_);
    if (XR_FAILED(result)) return false;

    auto create = [&](XrActionType type, const char *name, const char *localized,
                      uint32_t path_count, const XrPath *paths, XrAction &action) -> bool {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        info.actionType = type;
        strcpy_s(info.actionName, name);
        strcpy_s(info.localizedActionName, localized);
        info.countSubactionPaths = path_count;
        info.subactionPaths = paths;
        const XrResult create_result = xrCreateAction(action_set_, &info, &action);
        if (XR_FAILED(create_result))
            log_line("FAIL stage=xrCreateAction action=%s xr=%d", name, static_cast<int>(create_result));
        return XR_SUCCEEDED(create_result);
    };
    if (!create(XR_ACTION_TYPE_POSE_INPUT, "hand_grip_pose", "Hand Grip Pose", 2, hand_paths_, grip_pose_action_) ||
        !create(XR_ACTION_TYPE_POSE_INPUT, "hand_aim_pose", "Hand Aim Pose", 2, hand_paths_, aim_pose_action_) ||
        !create(XR_ACTION_TYPE_FLOAT_INPUT, "trigger_value", "Index Trigger", 2, hand_paths_, trigger_action_) ||
        !create(XR_ACTION_TYPE_VECTOR2F_INPUT, "move_turn", "Movement and Turn", 2, hand_paths_, thumbstick_action_) ||
        !create(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze_value", "Grip Squeeze", 2, hand_paths_, squeeze_action_) ||
        !create(XR_ACTION_TYPE_BOOLEAN_INPUT, "primary_button", "Primary Button", 2, hand_paths_, primary_button_action_) ||
        !create(XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary_button", "Secondary Button", 2, hand_paths_, secondary_button_action_) ||
        !create(XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click", "Thumbstick Click", 2, hand_paths_, stick_click_action_) ||
        !create(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_button", "Menu Button", 1, &hand_paths_[0], menu_button_action_) ||
        !create(XR_ACTION_TYPE_FLOAT_INPUT, "index_menu_force", "Index Trackpad Menu Press", 1,
            &hand_paths_[0], index_menu_force_action_) ||
        !create(XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic_output", "Subtle Haptic Feedback", 2,
            hand_paths_, haptic_action_))
        return false;

    XrPath common[16]{};
    if (!path("/interaction_profiles/oculus/touch_controller", touch_profile_path_) ||
        !path("/interaction_profiles/valve/index_controller", index_profile_path_) ||
        !path("/user/hand/left/input/grip/pose", common[0]) ||
        !path("/user/hand/right/input/grip/pose", common[1]) ||
        !path("/user/hand/left/input/aim/pose", common[2]) ||
        !path("/user/hand/right/input/aim/pose", common[3]) ||
        !path("/user/hand/left/input/trigger/value", common[4]) ||
        !path("/user/hand/right/input/trigger/value", common[5]) ||
        !path("/user/hand/left/input/thumbstick", common[6]) ||
        !path("/user/hand/right/input/thumbstick", common[7]) ||
        !path("/user/hand/left/input/squeeze/value", common[8]) ||
        !path("/user/hand/right/input/squeeze/value", common[9]) ||
        !path("/user/hand/left/input/thumbstick/click", common[10]) ||
        !path("/user/hand/right/input/thumbstick/click", common[11]) ||
        !path("/user/hand/left/output/haptic", common[12]) ||
        !path("/user/hand/right/output/haptic", common[13]) ||
        !path("/user/hand/left/input/a/click", common[14]) ||
        !path("/user/hand/left/input/b/click", common[15])) return false;

    if (openxr_11_enabled_)
    {
        constexpr const char *meta_profiles[5]{
            "/interaction_profiles/meta/touch_plus_controller",
            "/interaction_profiles/meta/touch_pro_controller",
            "/interaction_profiles/meta/touch_controller_quest_2",
            "/interaction_profiles/meta/touch_controller_quest_1_rift_s",
            "/interaction_profiles/meta/touch_controller_rift_cv1"
        };
        for (size_t i = 0; i < std::size(meta_profiles); ++i)
            if (!path(meta_profiles[i], meta_touch_profile_paths_[i])) return false;
    }
    if (generic_controller_enabled_ &&
        !path("/interaction_profiles/khr/generic_controller", generic_profile_path_))
        return false;

    XrPath touch[5]{};
    if (!path("/user/hand/left/input/x/click", touch[0]) ||
        !path("/user/hand/right/input/a/click", touch[1]) ||
        !path("/user/hand/left/input/y/click", touch[2]) ||
        !path("/user/hand/right/input/b/click", touch[3]) ||
        !path("/user/hand/left/input/menu/click", touch[4])) return false;

    XrPath index[4]{};
    if (!path("/user/hand/right/input/a/click", index[0]) ||
        !path("/user/hand/right/input/b/click", index[1]) ||
        !path("/user/hand/left/input/system/click", index[2]) ||
        !path("/user/hand/left/input/trackpad/force", index[3])) return false;

    XrPath generic[4]{};
    if (generic_controller_enabled_ &&
        (!path("/user/hand/left/input/primary/click", generic[0]) ||
         !path("/user/hand/right/input/primary/click", generic[1]) ||
         !path("/user/hand/left/input/secondary/click", generic[2]) ||
         !path("/user/hand/right/input/secondary/click", generic[3])))
        return false;

    const XrActionSuggestedBinding touch_bindings[19]{
        {grip_pose_action_, common[0]}, {grip_pose_action_, common[1]},
        {aim_pose_action_, common[2]}, {aim_pose_action_, common[3]},
        {trigger_action_, common[4]}, {trigger_action_, common[5]},
        {thumbstick_action_, common[6]}, {thumbstick_action_, common[7]},
        {squeeze_action_, common[8]}, {squeeze_action_, common[9]},
        {primary_button_action_, touch[0]}, {primary_button_action_, touch[1]},
        {secondary_button_action_, touch[2]}, {secondary_button_action_, touch[3]},
        {stick_click_action_, common[10]}, {stick_click_action_, common[11]},
        {menu_button_action_, touch[4]},
        {haptic_action_, common[12]}, {haptic_action_, common[13]}
    };
    const XrActionSuggestedBinding index_bindings[20]{
        {grip_pose_action_, common[0]}, {grip_pose_action_, common[1]},
        {aim_pose_action_, common[2]}, {aim_pose_action_, common[3]},
        {trigger_action_, common[4]}, {trigger_action_, common[5]},
        {thumbstick_action_, common[6]}, {thumbstick_action_, common[7]},
        {squeeze_action_, common[8]}, {squeeze_action_, common[9]},
        {primary_button_action_, common[14]}, {primary_button_action_, index[0]},
        {secondary_button_action_, common[15]}, {secondary_button_action_, index[1]},
        {stick_click_action_, common[10]}, {stick_click_action_, common[11]},
        {menu_button_action_, index[2]}, {index_menu_force_action_, index[3]},
        {haptic_action_, common[12]}, {haptic_action_, common[13]}
    };
    const XrActionSuggestedBinding generic_bindings[18]{
        {grip_pose_action_, common[0]}, {grip_pose_action_, common[1]},
        {aim_pose_action_, common[2]}, {aim_pose_action_, common[3]},
        {trigger_action_, common[4]}, {trigger_action_, common[5]},
        {thumbstick_action_, common[6]}, {thumbstick_action_, common[7]},
        {squeeze_action_, common[8]}, {squeeze_action_, common[9]},
        {primary_button_action_, generic[0]}, {primary_button_action_, generic[1]},
        {secondary_button_action_, generic[2]}, {secondary_button_action_, generic[3]},
        {stick_click_action_, common[10]}, {stick_click_action_, common[11]},
        {haptic_action_, common[12]}, {haptic_action_, common[13]}
    };
    auto suggest = [&](const char *name, XrPath profile, const XrActionSuggestedBinding *bindings,
                       uint32_t count) -> bool {
        XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggested.interactionProfile = profile;
        suggested.countSuggestedBindings = count;
        suggested.suggestedBindings = bindings;
        const XrResult suggest_result = xrSuggestInteractionProfileBindings(instance_, &suggested);
        if (XR_FAILED(suggest_result))
        {
            log_line("VR_INPUT_PROFILE_UNAVAILABLE profile=%s xr=%d continuing_with_other_profiles=1",
                name, static_cast<int>(suggest_result));
            return false;
        }
        log_line("VR_INPUT_PROFILE_BINDINGS profile=%s bindings=%u", name, count);
        return true;
    };
    const bool touch_available = suggest("oculus_touch", touch_profile_path_, touch_bindings,
        static_cast<uint32_t>(std::size(touch_bindings)));
    const bool index_available = suggest("valve_index", index_profile_path_, index_bindings,
        static_cast<uint32_t>(std::size(index_bindings)));
    bool meta_available = false;
    if (openxr_11_enabled_)
    {
        constexpr const char *meta_names[5]{
            "meta_touch_plus", "meta_touch_pro", "meta_touch_quest_2",
            "meta_touch_quest_1_rift_s", "meta_touch_rift_cv1"
        };
        for (size_t i = 0; i < std::size(meta_names); ++i)
            meta_available = suggest(meta_names[i], meta_touch_profile_paths_[i], touch_bindings,
                static_cast<uint32_t>(std::size(touch_bindings))) || meta_available;
    }
    const bool generic_available = generic_controller_enabled_ &&
        suggest("khr_generic", generic_profile_path_, generic_bindings,
            static_cast<uint32_t>(std::size(generic_bindings)));
    if (!touch_available && !index_available && !meta_available && !generic_available) return false;

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &action_set_;
    result = xrAttachSessionActionSets(session_, &attach);
    if (XR_FAILED(result)) return false;

    for (uint32_t hand = 0; hand < kHandCount; ++hand)
    {
        XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        info.action = grip_pose_action_;
        info.subactionPath = hand_paths_[hand];
        info.poseInActionSpace.orientation.w = 1.0f;
        result = xrCreateActionSpace(session_, &info, &hand_spaces_[hand]);
        if (XR_FAILED(result)) return false;
        info.action = aim_pose_action_;
        result = xrCreateActionSpace(session_, &info, &aim_spaces_[hand]);
        if (XR_FAILED(result)) return false;
    }
    return true;
}

const char *InputBridge::interaction_profile_name(XrPath profile) const
{
    if (profile == touch_profile_path_) return "oculus_touch";
    if (profile == index_profile_path_) return "valve_index";
    constexpr const char *meta_names[5]{
        "meta_touch_plus", "meta_touch_pro", "meta_touch_quest_2",
        "meta_touch_quest_1_rift_s", "meta_touch_rift_cv1"
    };
    for (size_t i = 0; i < meta_touch_profile_paths_.size(); ++i)
        if (profile != XR_NULL_PATH && profile == meta_touch_profile_paths_[i]) return meta_names[i];
    if (profile != XR_NULL_PATH && profile == generic_profile_path_) return "khr_generic";
    if (profile == XR_NULL_PATH) return "none";
    return "other";
}

void InputBridge::update_interaction_profiles()
{
    for (uint32_t hand = 0; hand < kHandCount; ++hand)
    {
        XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
        const XrResult result = xrGetCurrentInteractionProfile(session_, hand_paths_[hand], &state);
        if (XR_FAILED(result) || state.interactionProfile == active_profile_paths_[hand]) continue;
        active_profile_paths_[hand] = state.interactionProfile;

        char profile_path[XR_MAX_PATH_LENGTH]{};
        uint32_t written = 0;
        if (state.interactionProfile != XR_NULL_PATH &&
            XR_FAILED(xrPathToString(instance_, state.interactionProfile,
                static_cast<uint32_t>(std::size(profile_path)), &written, profile_path)))
            strcpy_s(profile_path, "<unavailable>");
        else if (state.interactionProfile == XR_NULL_PATH)
            strcpy_s(profile_path, "<none>");
        log_line("VR_CONTROLLER_PROFILE hand=%s profile=%s path=%s",
            hand == 0 ? "left" : "right",
            interaction_profile_name(state.interactionProfile), profile_path);
        input_active_logged_ = false;
    }
}

void InputBridge::create_optical_trackers(bool extension_enabled)
{
    if (!extension_enabled)
    {
        log_line("VR_FEATURE_OPTICAL_HANDS available=0 enabled=0");
        return;
    }
    PFN_xrVoidFunction create = nullptr, destroy = nullptr, locate = nullptr;
    const XrResult create_result = xrGetInstanceProcAddr(instance_, "xrCreateHandTrackerEXT", &create);
    const XrResult destroy_result = xrGetInstanceProcAddr(instance_, "xrDestroyHandTrackerEXT", &destroy);
    const XrResult locate_result = xrGetInstanceProcAddr(instance_, "xrLocateHandJointsEXT", &locate);
    if (XR_FAILED(create_result) || XR_FAILED(destroy_result) || XR_FAILED(locate_result) ||
        !create || !destroy || !locate)
    {
        log_line("VR_FEATURE_OPTICAL_HANDS available=1 functions=0 create_xr=%d destroy_xr=%d locate_xr=%d",
            static_cast<int>(create_result), static_cast<int>(destroy_result), static_cast<int>(locate_result));
        return;
    }
    create_hand_tracker_ = reinterpret_cast<PFN_xrCreateHandTrackerEXT>(create);
    destroy_hand_tracker_ = reinterpret_cast<PFN_xrDestroyHandTrackerEXT>(destroy);
    locate_hand_joints_ = reinterpret_cast<PFN_xrLocateHandJointsEXT>(locate);
    for (uint32_t hand = 0; hand < kHandCount; ++hand)
    {
        XrHandTrackerCreateInfoEXT info{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        info.hand = hand == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
        info.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        const XrResult result = create_hand_tracker_(session_, &info, &optical_trackers_[hand]);
        if (XR_FAILED(result))
        {
            log_line("VR_FEATURE_OPTICAL_HANDS tracker=%u xr=%d", hand, static_cast<int>(result));
            optical_trackers_[hand] = XR_NULL_HANDLE;
        }
    }
}

void InputBridge::on_session_state(XrSessionState state)
{
    session_state_ = state;
    if (!config_.enabled) return;
    input_rearm_required_ = true;
    if (state == XR_SESSION_STATE_FOCUSED)
    {
        focus_game_window();
        locomotion_reference_valid_ = false;
    }
    else
    {
        release_injected_input();
        for (HandState &hand_state : hands_) hand_state.active = false;
    }
}

void InputBridge::set_startup_menu(bool visible, bool pointer_active)
{
    startup_menu_visible_ = visible;
    startup_menu_pointer_active_ = visible && pointer_active;
}

bool InputBridge::game_ui_open_intent() const
{
    return game_ui_open_intent_until_ms_ != 0 &&
        GetTickCount64() < game_ui_open_intent_until_ms_;
}

bool InputBridge::pulse_haptic(uint32_t hand, float amplitude, uint32_t duration_ms,
                               float frequency)
{
    if (!initialized_ || !config_.haptics || hand >= kHandCount || hands_[hand].optical ||
        session_ == XR_NULL_HANDLE || haptic_action_ == XR_NULL_HANDLE ||
        session_state_ != XR_SESSION_STATE_FOCUSED)
        return false;
    const uint64_t now=GetTickCount64();
    if (now-last_haptic_ms_[hand] < 12) return false;

    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action=haptic_action_;
    info.subactionPath=hand_paths_[hand];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude=std::clamp(amplitude*std::clamp(config_.haptic_strength,0.0f,1.0f),
        0.0f,0.35f);
    vibration.duration=static_cast<XrDuration>(std::clamp(duration_ms,8u,45u))*1000000;
    vibration.frequency=frequency;
    const XrResult result=xrApplyHapticFeedback(session_,&info,
        reinterpret_cast<const XrHapticBaseHeader *>(&vibration));
    if (XR_FAILED(result))
    {
        if (!haptic_failure_logged_)
        {
            haptic_failure_logged_=true;
            log_line("VR_HAPTIC_UNAVAILABLE hand=%u xr=%d",hand,static_cast<int>(result));
        }
        return false;
    }
    last_haptic_ms_[hand]=now;
    if (!haptic_ready_logged_)
    {
        haptic_ready_logged_=true;
        log_line("VR_HAPTIC_ACTIVE output=openxr amplitude_cap=0.35 duration_cap_ms=45 strength=%.2f",
            config_.haptic_strength);
    }
    return true;
}

bool InputBridge::get_boolean(XrAction action, uint32_t hand, bool &value)
{
    value = false;
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = hand_paths_[hand];
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    const XrResult result = xrGetActionStateBoolean(session_, &info, &state);
    if (XR_FAILED(result)) return false;
    value = state.isActive == XR_TRUE && state.currentState == XR_TRUE;
    return true;
}

bool InputBridge::get_float(XrAction action, uint32_t hand, float &value, bool *active)
{
    value = 0.0f;
    if (active) *active = false;
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = hand_paths_[hand];
    XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
    const XrResult result = xrGetActionStateFloat(session_, &info, &state);
    if (XR_FAILED(result)) return false;
    if (active) *active = state.isActive == XR_TRUE;
    if (state.isActive == XR_TRUE) value = state.currentState;
    return true;
}

bool InputBridge::get_vector(XrAction action, uint32_t hand, XrVector2f &value)
{
    value = {};
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = hand_paths_[hand];
    XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
    const XrResult result = xrGetActionStateVector2f(session_, &info, &state);
    if (XR_FAILED(result)) return false;
    if (state.isActive == XR_TRUE) value = state.currentState;
    return true;
}

bool InputBridge::update_controller_pose(uint32_t hand, XrTime display_time)
{
    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = grip_pose_action_;
    get.subactionPath = hand_paths_[hand];
    XrActionStatePose pose_state{XR_TYPE_ACTION_STATE_POSE};
    if (XR_FAILED(xrGetActionStatePose(session_, &get, &pose_state)) || pose_state.isActive != XR_TRUE)
        return false;
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    const XrResult result = xrLocateSpace(hand_spaces_[hand], base_space_, display_time, &location);
    constexpr XrSpaceLocationFlags required =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if (XR_FAILED(result) || (location.locationFlags & required) != required) return false;
    hands_[hand].pose = location.pose;
    hands_[hand].active = true;
    hands_[hand].optical = false;
    if (!hand_pose_logged_[hand])
    {
        hand_pose_logged_[hand] = true;
        log_line("VR_CONTROLLER_POSE hand=%s profile=%s position=%.4f,%.4f,%.4f orientation=%.5f,%.5f,%.5f,%.5f",
            hand == 0 ? "left" : "right", interaction_profile_name(active_profile_paths_[hand]),
            location.pose.position.x, location.pose.position.y, location.pose.position.z,
            location.pose.orientation.x, location.pose.orientation.y, location.pose.orientation.z,
            location.pose.orientation.w);
    }
    return true;
}

bool InputBridge::update_controller_aim_pose(uint32_t hand, XrTime display_time)
{
    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = aim_pose_action_;
    get.subactionPath = hand_paths_[hand];
    XrActionStatePose pose_state{XR_TYPE_ACTION_STATE_POSE};
    if (XR_FAILED(xrGetActionStatePose(session_, &get, &pose_state)) || pose_state.isActive != XR_TRUE)
        return false;
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    const XrResult result = xrLocateSpace(aim_spaces_[hand], base_space_, display_time, &location);
    constexpr XrSpaceLocationFlags required =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if (XR_FAILED(result) || (location.locationFlags & required) != required) return false;
    pointer_poses_[hand] = location.pose;
    if (!aim_pose_logged_[hand])
    {
        aim_pose_logged_[hand] = true;
        log_line("VR_CONTROLLER_AIM_POSE hand=%s profile=%s source=openxr_aim_pose laser_axis=minus_z",
            hand == 0 ? "left" : "right", interaction_profile_name(active_profile_paths_[hand]));
    }
    return true;
}

void InputBridge::update_optical_hands(XrTime display_time, const bool controller_pose_active[2])
{
    if (!locate_hand_joints_) return;
    const XrHandJointEXT chains[5][5]{
        {XR_HAND_JOINT_THUMB_METACARPAL_EXT, XR_HAND_JOINT_THUMB_PROXIMAL_EXT,
         XR_HAND_JOINT_THUMB_DISTAL_EXT, XR_HAND_JOINT_THUMB_TIP_EXT, XR_HAND_JOINT_THUMB_TIP_EXT},
        {XR_HAND_JOINT_INDEX_METACARPAL_EXT, XR_HAND_JOINT_INDEX_PROXIMAL_EXT,
         XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, XR_HAND_JOINT_INDEX_DISTAL_EXT, XR_HAND_JOINT_INDEX_TIP_EXT},
        {XR_HAND_JOINT_MIDDLE_METACARPAL_EXT, XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,
         XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, XR_HAND_JOINT_MIDDLE_DISTAL_EXT, XR_HAND_JOINT_MIDDLE_TIP_EXT},
        {XR_HAND_JOINT_RING_METACARPAL_EXT, XR_HAND_JOINT_RING_PROXIMAL_EXT,
         XR_HAND_JOINT_RING_INTERMEDIATE_EXT, XR_HAND_JOINT_RING_DISTAL_EXT, XR_HAND_JOINT_RING_TIP_EXT},
        {XR_HAND_JOINT_LITTLE_METACARPAL_EXT, XR_HAND_JOINT_LITTLE_PROXIMAL_EXT,
         XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, XR_HAND_JOINT_LITTLE_DISTAL_EXT, XR_HAND_JOINT_LITTLE_TIP_EXT}
    };
    for (uint32_t hand = 0; hand < kHandCount; ++hand)
    {
        if (optical_trackers_[hand] == XR_NULL_HANDLE) continue;
        std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joints{};
        XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
        locations.jointCount = static_cast<uint32_t>(joints.size());
        locations.jointLocations = joints.data();
        XrHandJointsLocateInfoEXT locate{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
        locate.baseSpace = base_space_;
        locate.time = display_time;
        if (XR_FAILED(locate_hand_joints_(optical_trackers_[hand], &locate, &locations)) ||
            locations.isActive != XR_TRUE) continue;

        constexpr XrSpaceLocationFlags pose_valid =
            XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        constexpr XrSpaceLocationFlags position_valid = XR_SPACE_LOCATION_POSITION_VALID_BIT;
        const auto &palm = joints[XR_HAND_JOINT_PALM_EXT];
        const bool optical_active = !controller_pose_active[hand] &&
            (palm.locationFlags & pose_valid) == pose_valid;
        if (!optical_active) continue;
        hands_[hand].pose = palm.pose;
        hands_[hand].active = true;
        hands_[hand].optical = true;
        const bool previous_precise = hands_[hand].precise_fingers;
        hands_[hand].precise_fingers = true;
        const auto &thumb_tip = joints[XR_HAND_JOINT_THUMB_TIP_EXT];
        const auto &index_tip = joints[XR_HAND_JOINT_INDEX_TIP_EXT];
        if ((thumb_tip.locationFlags & position_valid) != 0 &&
            (index_tip.locationFlags & position_valid) != 0)
        {
            const float pinch_distance = distance(thumb_tip.pose.position, index_tip.pose.position);
            optical_pinch_down_[hand] = optical_pinch_down_[hand]
                ? pinch_distance < 0.040f : pinch_distance < 0.025f;
        }
        else optical_pinch_down_[hand] = false;

        for (uint32_t finger = 0; finger < 5; ++finger)
        {
            float bends[3]{};
            const bool valid[3] = {
                bend_angle(joints.data(), chains[finger][0], chains[finger][1],
                    chains[finger][2], bends[0]),
                bend_angle(joints.data(), chains[finger][1], chains[finger][2],
                    chains[finger][3], bends[1]),
                finger != 0 && bend_angle(joints.data(), chains[finger][2], chains[finger][3],
                    chains[finger][4], bends[2])
            };
            if (finger == 0)
            {
                // The joint angle is useful for thumb opposition, but Quest's
                // fully open thumb still retains a small anatomical bend. Expand
                // the open end of the range without losing the pinch/close end.
                if (valid[0] && valid[1])
                {
                    const float curl = std::clamp((bends[0] + bends[1]) / 2.2f, 0.0f, 1.0f);
                    hands_[hand].finger_curls[0] = std::clamp(
                        ((1.0f - curl) - 0.17f) / 0.83f, 0.0f, 1.0f);
                }
                continue;
            }

            // A small straight-hand dead zone removes the residual bend that
            // Quest joint positions report even when the user fully opens a
            // finger. Preserve each valid joint independently instead of
            // reducing the whole finger to one open/closed value. An invalid
            // transient joint retains its last filtered value to prevent pops.
            constexpr float open_dead_zone[3] = {0.22f, 0.18f, 0.14f};
            constexpr float maximum_bend[3] = {1.15f, 1.25f, 0.98f};
            float total_bend = 0.0f;
            for (uint32_t joint = 0; joint < 3; ++joint)
            {
                float &filtered = hands_[hand].finger_bends[finger][joint];
                if (valid[joint])
                {
                    const float target = std::clamp(
                        bends[joint] - open_dead_zone[joint], 0.0f, maximum_bend[joint]);
                    filtered = previous_precise ? filtered + (target - filtered) * 0.55f : target;
                }
                total_bend += filtered;
            }
            hands_[hand].finger_curls[finger] = std::clamp(total_bend / 3.6f, 0.0f, 1.0f);
        }
        hands_[hand].interaction = optical_pinch_down_[hand];
        hands_[hand].firing = hand == 1 && optical_pinch_down_[hand];
        if (!optical_hand_logged_[hand])
        {
            optical_hand_logged_[hand] = true;
            log_line("VR_OPTICAL_HAND_TRACKED hand=%s fallback_when_controller_inactive=1 pinch_hysteresis=1 articulation=per_joint_12dof open_range=v2",
                hand == 0 ? "left" : "right");
        }
    }
}

bool InputBridge::sync(XrTime display_time, const XrPosef &head_pose)
{
    if (!config_.enabled || !initialized_) return true;
    if (session_state_ != XR_SESSION_STATE_FOCUSED)
    {
        release_injected_input();
        return true;
    }
    XrActiveActionSet active{action_set_, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    const XrResult result = xrSyncActions(session_, &sync);
    if (XR_FAILED(result))
    {
        release_injected_input();
        if (!sync_failure_logged_)
        {
            sync_failure_logged_ = true;
            log_line("VR_FEATURE_INPUT_SYNC_FAILED xr=%d", static_cast<int>(result));
        }
        return false;
    }
    sync_failure_logged_ = false;
    update_interaction_profiles();
    for (HandState &hand_state : hands_)
    {
        hand_state.active = false;
        hand_state.optical = false;
        hand_state.interaction = false;
        hand_state.firing = false;
    }
    bool controller_pose_active[2]{};
    bool controller_aim_active[2]{};
    for (uint32_t hand = 0; hand < kHandCount; ++hand)
    {
        controller_pose_active[hand] = update_controller_pose(hand, display_time);
        controller_aim_active[hand] = update_controller_aim_pose(hand, display_time);
    }
    update_optical_hands(display_time, controller_pose_active);
    for (uint32_t hand = 0; hand < kHandCount; ++hand)
    {
        if (hands_[hand].optical)
        {
            pointer_poses_[hand] = hands_[hand].pose;
            pointer_pose_active_[hand] = hands_[hand].active;
        }
        else if (controller_aim_active[hand])
        {
            pointer_pose_active_[hand] = true;
        }
        else
        {
            // Runtime profiles without an aim binding retain the old grip-pose
            // fallback; supported Touch and Index controllers use the dedicated
            // OpenXR aim pose above.
            pointer_poses_[hand] = hands_[hand].pose;
            pointer_pose_active_[hand] = hands_[hand].active;
        }
    }
    update_game_input(head_pose);
    return true;
}

void InputBridge::update_game_input(const XrPosef &head_pose)
{
    XrVector2f move{}, turn{};
    bool a = false, b = false, x = false, y = false, left_click = false, right_click = false;
    bool menu = false;
    float trigger[2]{}, squeeze[2]{}, index_menu_force = 0.0f;
    bool trigger_active[2]{};
    bool index_menu_force_active = false;
    if (!get_vector(thumbstick_action_, 0, move) || !get_vector(thumbstick_action_, 1, turn) ||
        !get_boolean(primary_button_action_, 1, a) || !get_boolean(secondary_button_action_, 1, b) ||
        !get_boolean(primary_button_action_, 0, x) || !get_boolean(secondary_button_action_, 0, y) ||
        !get_boolean(stick_click_action_, 0, left_click) || !get_boolean(stick_click_action_, 1, right_click) ||
        !get_boolean(menu_button_action_, 0, menu) ||
        !get_float(index_menu_force_action_, 0, index_menu_force, &index_menu_force_active) ||
        !get_float(trigger_action_, 0, trigger[0], &trigger_active[0]) ||
        !get_float(trigger_action_, 1, trigger[1], &trigger_active[1]) ||
        !get_float(squeeze_action_, 0, squeeze[0]) || !get_float(squeeze_action_, 1, squeeze[1]))
    {
        release_injected_input();
        return;
    }
    // SteamVR commonly reserves the Index system button for its dashboard.
    // Keep that standard binding, but also make a deliberate left trackpad
    // press a reliable application-level Menu input.
    menu = menu || (index_menu_force_active && index_menu_force > 0.55f);

    for (uint32_t hand = 0; hand < kHandCount; ++hand)
    {
        hands_[hand].trigger = trigger[hand];
        hands_[hand].squeeze = squeeze[hand];
        if (!hands_[hand].optical)
        {
            hands_[hand].finger_curls = {0.0f, trigger[hand], squeeze[hand], squeeze[hand], squeeze[hand]};
            hands_[hand].precise_fingers = false;
            hands_[hand].interaction = trigger[hand] > 0.55f;
            hands_[hand].firing = hand == 1 && trigger[hand] > 0.55f;
        }
    }

    const bool recenter_down = left_click && right_click;
    const uint64_t now = GetTickCount64();
    if (input_rearm_required_)
    {
        const bool neutral = !a && !b && !x && !y && !left_click && !right_click && !menu &&
            trigger[0] < 0.20f && trigger[1] < 0.20f && !optical_pinch_down_[0] &&
            !optical_pinch_down_[1] && std::fabs(move.x) < 0.20f && std::fabs(move.y) < 0.20f &&
            std::fabs(turn.x) < 0.20f && std::fabs(turn.y) < 0.20f;
        release_injected_input();
        if (!neutral) return;
        input_rearm_required_ = false;
        log_line("VR_INPUT_REARMED controls_neutral=1 session_focused=1");
    }
    const bool menu_pressed=menu && !menu_was_down_;
    if (!startup_menu_visible_ && menu_pressed)
    {
        game_ui_open_intent_until_ms_=now+1200;
        pulse_haptic(0,0.10f,18,75.0f);
    }
    if (recenter_down)
    {
        if (recenter_hold_start_ms_ == 0) recenter_hold_start_ms_ = now;
        if (!recenter_latched_ && now - recenter_hold_start_ms_ >= 1000)
        {
            recenter_latched_ = true;
            recenter_requested_ = true;
            locomotion_reference_valid_ = false;
            pulse_haptic(0,0.16f,32,85.0f);
            pulse_haptic(1,0.16f,32,85.0f);
            log_line("VR_RECENTER_REQUESTED source=both_thumbsticks hold_ms=%llu",
                static_cast<unsigned long long>(now - recenter_hold_start_ms_));
        }
    }
    else
    {
        recenter_hold_start_ms_ = 0;
        recenter_latched_ = false;
    }

    for (uint32_t hand = 0; hand < kHandCount; ++hand)
    {
        if (hands_[hand].optical) continue;
        if (trigger_active[hand])
        {
            controller_trigger_last_active_[hand] = now;
            const bool desired = controller_trigger_down_[hand] ? trigger[hand] > 0.30f : trigger[hand] > 0.62f;
            if (desired == controller_trigger_down_[hand])
            {
                controller_trigger_candidate_down_[hand] = desired;
                controller_trigger_candidate_since_[hand] = now;
            }
            else if (desired != controller_trigger_candidate_down_[hand])
            {
                controller_trigger_candidate_down_[hand] = desired;
                controller_trigger_candidate_since_[hand] = now;
            }
            else if (now - controller_trigger_candidate_since_[hand] >= (desired ? 18u : 75u))
            {
                controller_trigger_down_[hand] = desired;
                controller_trigger_candidate_since_[hand] = now;
            }
        }
        else if (controller_trigger_down_[hand] && controller_trigger_last_active_[hand] != 0 &&
                 now - controller_trigger_last_active_[hand] >= 250)
        {
            controller_trigger_down_[hand] = false;
            controller_trigger_candidate_down_[hand] = false;
            controller_trigger_candidate_since_[hand] = now;
        }
    }
    const bool right_trigger = hands_[1].optical ? optical_pinch_down_[1] : trigger[1] > 0.55f;
    const bool left_trigger = hands_[0].optical ? optical_pinch_down_[0] : trigger[0] > 0.55f;
    const bool right_trigger_pressed=right_trigger && !right_primary_was_down_;
    const bool left_trigger_pressed=left_trigger && !left_primary_was_down_;
    const bool right_grip = squeeze[1] > 0.55f;
    const bool either_grip = right_grip || squeeze[0] > 0.55f;
    const bool quick_transfer = startup_menu_visible_ && a && either_grip;
    right_use_down_ = !startup_menu_visible_ && b;
    if (startup_menu_visible_)
    {
        // While the dedicated startup panel is present, controller input belongs
        // to that panel. Do not let a UI click also move the player, turn the
        // camera, fire a tool, or open an unrelated gameplay screen.
        send_key('W', false, key_forward_);
        send_key('S', false, key_backward_);
        send_key('A', false, key_left_);
        send_key('D', false, key_right_);
        // Scrap Mechanic already implements quick inventory transfer as
        // Shift+click. Keep the modifier inside the same private input queue as
        // the panel click, so Grip+A works without synthesizing desktop input.
        send_key(VK_SHIFT, quick_transfer, key_sprint_);
        send_key(VK_CONTROL, false, key_crouch_);
        send_key(VK_SPACE, false, key_jump_);
        send_key('E', false, key_use_);
        send_key('Q', false, key_context_);
        send_key('V', false, key_camera_);
        send_key('X', false, key_zoom_in_);
        send_key('C', false, key_zoom_out_);
        send_key('I', false, key_inventory_);
        send_key(VK_UP, false, key_lift_up_);
        send_key(VK_DOWN, false, key_lift_down_);
        send_key(VK_ESCAPE, menu, key_menu_);
        // UI selection must react to the current controller trigger sample. Waiting
        // for the gameplay-oriented debouncer made controller clicks dependent
        // on several subsequent action-sync frames, while optical pinch used
        // its live state. Preserve the same private game-input queue downstream;
        // this does not synthesize any Windows mouse input.
        const bool controller_ui_trigger = !hands_[1].optical && trigger_active[1] && right_trigger;
        ui_select_down_ = controller_ui_trigger || optical_pinch_down_[1] || a;
        if (quick_transfer && !quick_transfer_was_down_)
        {
            pulse_haptic(1,0.08f,15,78.0f);
            log_line("VR_QUICK_TRANSFER source=grip+A route=engine_shift_click");
        }
        if (controller_ui_trigger != controller_ui_trigger_was_down_)
        {
            controller_ui_trigger_was_down_ = controller_ui_trigger;
            log_line("VR_UI_CONTROLLER_TRIGGER state=%u value=%.3f active=%u route=private_input_event_queue win32_mouse_simulation=0",
                controller_ui_trigger ? 1u : 0u, trigger[1], trigger_active[1] ? 1u : 0u);
        }
        ui_scroll_axis_ = turn.y;
        // The menu renderer queues exact client coordinates through Scrap
        // Mechanic's input manager. Ensure no gameplay mouse button remains held.
        send_mouse_button(0,false,mouse_attack_);
        send_mouse_button(1,false,mouse_secondary_);
        hands_[0].interaction = hands_[0].firing = false;
        hands_[1].interaction = hands_[1].firing = false;
        x_was_down_ = y_was_down_ = xy_chord_latched_ = false;
        y_inventory_latched_ = false;
        y_hold_start_ms_ = 0;
        last_turn_update_ms_ = now;
        turn_residual_x_ = turn_residual_y_ = 0.0f;
        right_primary_was_down_=right_trigger;
        left_primary_was_down_=left_trigger;
        menu_was_down_=menu;
        b_was_down_=b;
        quick_transfer_was_down_=quick_transfer;
        lift_axis_was_active_=false;
        return;
    }
    ui_select_down_ = false;
    controller_ui_trigger_was_down_ = false;
    ui_scroll_axis_ = 0.0f;
    XrVector3f current_forward{};
    XrVector2f stable_move = move;
    if (horizontal_forward(head_pose, current_forward))
    {
        if (!locomotion_reference_valid_)
        {
            locomotion_reference_forward_ = current_forward;
            locomotion_reference_valid_ = true;
        }
        const XrVector3f reference_right{-locomotion_reference_forward_.z, 0.0f,
                                         locomotion_reference_forward_.x};
        const XrVector3f current_right{-current_forward.z, 0.0f, current_forward.x};
        const XrVector3f desired{
            current_right.x * move.x + current_forward.x * move.y, 0.0f,
            current_right.z * move.x + current_forward.z * move.y
        };
        stable_move = {
            desired.x * reference_right.x + desired.z * reference_right.z,
            desired.x * locomotion_reference_forward_.x + desired.z * locomotion_reference_forward_.z
        };
    }

    const float deadzone = std::clamp(config_.stick_deadzone, 0.05f, 0.95f);
    const bool player_seated = scrapvr::tools::is_player_seated();
    const bool player_first_person = scrapvr::tools::is_player_first_person();
    const bool lift_axis_active = !player_seated && right_grip &&
        std::fabs(turn.y) > deadzone && std::fabs(turn.y) >= std::fabs(turn.x);
    send_key('W', stable_move.y > deadzone, key_forward_);
    send_key('S', stable_move.y < -deadzone, key_backward_);
    send_key('A', stable_move.x < -deadzone, key_left_);
    send_key('D', stable_move.x > deadzone, key_right_);
    send_key(VK_SHIFT, left_click && !recenter_down, key_sprint_);
    send_key(VK_CONTROL, right_click && !recenter_down, key_crouch_);
    send_key(VK_SPACE, a, key_jump_);
    send_key('E', b, key_use_);
    const scrapvr::tools::ContextAction context_action = scrapvr::tools::active_context_action();
    const bool contextual_b = b && context_action != scrapvr::tools::ContextAction::none;
    send_key('Q', contextual_b, key_context_);
    send_key(VK_UP, lift_axis_active && turn.y > 0.0f, key_lift_up_);
    send_key(VK_DOWN, lift_axis_active && turn.y < 0.0f, key_lift_down_);
    send_key(VK_ESCAPE, menu, key_menu_);
    if (lift_axis_active && !lift_axis_was_active_)
    {
        pulse_haptic(1,0.07f,14,72.0f);
        log_line("VR_LIFT_CONTROL source=right_grip+right_stick direction=%s",
            turn.y > 0.0f ? "raise" : "lower");
    }
    lift_axis_was_active_ = lift_axis_active;
    quick_transfer_was_down_ = false;
    if (b && !b_was_down_)
    {
        pulse_haptic(1,0.08f,14,70.0f);
        if (context_action == scrapvr::tools::ContextAction::rotate_placement)
            log_line("VR_CONTEXT_ACTION source=right_B action=next_rotation use_interact=preserved");
        else if (context_action == scrapvr::tools::ContextAction::paint_palette)
            log_line("VR_CONTEXT_ACTION source=right_B action=paint_palette use_interact=preserved");
    }

    if (right_trigger_pressed)
    {
        switch (scrapvr::tools::active_haptic_profile())
        {
        case scrapvr::tools::HapticProfile::gun:
            pulse_haptic(1,0.18f,24,105.0f);
            break;
        case scrapvr::tools::HapticProfile::hammer:
            pulse_haptic(1,0.13f,20,75.0f);
            break;
        case scrapvr::tools::HapticProfile::tool:
            pulse_haptic(1,0.10f,17,80.0f);
            break;
        default:
            pulse_haptic(1,0.07f,12,70.0f);
            break;
        }
    }
    if (left_trigger_pressed)
        pulse_haptic(0,0.07f,12,70.0f);

    // The old mod's seated path is intentionally edge based: hold V only until
    // Lua reports first-person, so entering a seat cannot repeatedly cycle the
    // game's camera modes. X/Y become the game's X/C vehicle zoom bindings.
    send_key('V', player_seated && !player_first_person, key_camera_);
    send_key('X', player_seated && x, key_zoom_in_);
    send_key('C', player_seated && y, key_zoom_out_);
    if (!player_seated)
    {
        if (y && !y_was_down_)
            y_hold_start_ms_ = now;
        else if (!y)
            y_hold_start_ms_ = 0;

        const bool inventory_chord_pressed = x && y && !xy_chord_latched_ &&
            !y_inventory_latched_;
        const bool inventory_hold_pressed = y && !x && !y_inventory_latched_ &&
            !xy_chord_latched_ && y_hold_start_ms_ != 0 &&
            now - y_hold_start_ms_ >= kInventoryHoldMilliseconds;
        if (inventory_chord_pressed || inventory_hold_pressed)
        {
            game_ui_open_intent_until_ms_=now+1200;
            pulse_haptic(0,0.10f,18,75.0f);
            log_line("VR_INVENTORY_REQUEST source=%s key=I pulse=1",
                inventory_hold_pressed ? "left_Y_hold" : "left_X+Y");
        }
        if (x && y) xy_chord_latched_ = true;
        if (inventory_hold_pressed) y_inventory_latched_ = true;
        // A one-update key pulse is sufficient for Scrap Mechanic's toggle and
        // cannot leave the inventory action held across a menu/focus transition.
        send_key('I', inventory_chord_pressed || inventory_hold_pressed, key_inventory_);
        if (!x && x_was_down_ && !xy_chord_latched_) send_mouse_wheel(WHEEL_DELTA);
        if (!y && y_was_down_ && !xy_chord_latched_ && !y_inventory_latched_)
            send_mouse_wheel(-WHEEL_DELTA);
    }
    else
    {
        xy_chord_latched_ = false;
        y_inventory_latched_ = false;
        y_hold_start_ms_ = 0;
        send_key('I', false, key_inventory_);
    }
    x_was_down_ = x;
    y_was_down_ = y;
    if (!x && !y)
    {
        xy_chord_latched_ = false;
        y_inventory_latched_ = false;
    }

    right_primary_was_down_=right_trigger;
    left_primary_was_down_=left_trigger;
    menu_was_down_=menu;
    b_was_down_=b;

    send_mouse_button(0,right_trigger,mouse_attack_);
    send_mouse_button(1,left_trigger,mouse_secondary_);

    const uint64_t elapsed_turn_ms = last_turn_update_ms_ != 0 && now >= last_turn_update_ms_
        ? now - last_turn_update_ms_ : 0;
    last_turn_update_ms_ = now;
    const float turn_seconds = elapsed_turn_ms == 0
        ? (1.0f / 72.0f)
        : std::clamp(static_cast<float>(elapsed_turn_ms) / 1000.0f, 0.0f, 0.050f);
    auto shaped_stick = [deadzone](float value) {
        const float magnitude = std::fabs(value);
        if (magnitude <= deadzone) return 0.0f;
        const float normalized = (magnitude - deadzone) / (1.0f - deadzone);
        return std::copysign(normalized * normalized, value);
    };
    float horizontal = shaped_stick(turn.x);
    float vertical = shaped_stick(turn.y);
    if (lift_axis_active) vertical = 0.0f;
    if (std::fabs(horizontal) >= std::fabs(vertical)) vertical = 0.0f;
    else horizontal = 0.0f;
    // The public turn-speed values were calibrated as mouse pixels per update
    // at Quest's 72 Hz reference rate. Time-normalize that proven scale instead
    // of interpreting it as pixels per second (which made yaw roughly 72x too
    // weak to move Scrap Mechanic's camera at ordinary sensitivity settings).
    const float reference_frames = turn_seconds * kTurnReferenceRateHz;
    turn_residual_x_ += horizontal * config_.horizontal_turn_speed * reference_frames;
    turn_residual_y_ += -vertical * config_.vertical_turn_speed * reference_frames;
    const int delta_x = static_cast<int>(std::trunc(turn_residual_x_));
    const int delta_y = static_cast<int>(std::trunc(turn_residual_y_));
    turn_residual_x_ -= static_cast<float>(delta_x);
    turn_residual_y_ -= static_cast<float>(delta_y);
    if (delta_x != 0 || delta_y != 0)
    {
        if (HWND window=game_window(); window)
        {
            RECT client{};
            if (GetClientRect(window,&client))
                EngineInputQueue::instance().queue_mouse_delta(delta_x,delta_y,
                    std::max(1L,client.right-client.left),
                    std::max(1L,client.bottom-client.top));
        }
    }

    if (!input_active_logged_)
    {
        input_active_logged_ = true;
        log_line("VR_INPUT_ACTIVE mapping=openxr_controller left_profile=%s right_profile=%s locomotion=hmd_relative turn=right_stick_smooth_time_normalized triggers=mouse buttons=right_A_jump,right_B_use+context_Q,left_X_Y_hotbar_or_seated_zoom,left_Y_hold_or_X+Y_inventory,right_grip+stick_lift,grip+A_quick_transfer menu=dedicated_or_index_trackpad recenter=dual_stick_1s route=private_input_event_queue",
            interaction_profile_name(active_profile_paths_[0]),
            interaction_profile_name(active_profile_paths_[1]));
    }
}

void InputBridge::release_injected_input()
{
    send_key('W', false, key_forward_);
    send_key('S', false, key_backward_);
    send_key('A', false, key_left_);
    send_key('D', false, key_right_);
    send_key(VK_SHIFT, false, key_sprint_);
    send_key(VK_CONTROL, false, key_crouch_);
    send_key(VK_SPACE, false, key_jump_);
    send_key('E', false, key_use_);
    send_key('Q', false, key_context_);
    send_key('V', false, key_camera_);
    send_key('X', false, key_zoom_in_);
    send_key('C', false, key_zoom_out_);
    send_key('I', false, key_inventory_);
    send_key(VK_UP, false, key_lift_up_);
    send_key(VK_DOWN, false, key_lift_down_);
    send_key(VK_ESCAPE, false, key_menu_);
    send_mouse_button(0,false,mouse_attack_);
    send_mouse_button(1,false,mouse_secondary_);
    ui_select_down_ = false;
    controller_ui_trigger_was_down_ = false;
    right_use_down_ = false;
    ui_scroll_axis_ = 0.0f;
    x_was_down_ = y_was_down_ = xy_chord_latched_ = false;
    y_inventory_latched_ = false;
    y_hold_start_ms_ = 0;
    last_turn_update_ms_ = 0;
    turn_residual_x_ = turn_residual_y_ = 0.0f;
    right_primary_was_down_=left_primary_was_down_=menu_was_down_=b_was_down_=false;
    quick_transfer_was_down_=false;
    lift_axis_was_active_=false;
}

bool InputBridge::consume_recenter_request()
{
    const bool requested = recenter_requested_;
    recenter_requested_ = false;
    return requested;
}

void InputBridge::reset_runtime_state()
{
    session_state_ = XR_SESSION_STATE_UNKNOWN;
    locomotion_reference_valid_ = false;
    recenter_hold_start_ms_ = 0;
    recenter_latched_ = false;
    recenter_requested_ = false;
    input_active_logged_ = false;
    sync_failure_logged_ = false;
    active_profile_paths_[0] = active_profile_paths_[1] = XR_NULL_PATH;
    hand_pose_logged_[0] = hand_pose_logged_[1] = false;
    aim_pose_logged_[0] = aim_pose_logged_[1] = false;
    pointer_pose_active_[0] = pointer_pose_active_[1] = false;
    optical_hand_logged_[0] = optical_hand_logged_[1] = false;
    optical_pinch_down_[0] = optical_pinch_down_[1] = false;
    controller_trigger_down_[0] = controller_trigger_down_[1] = false;
    controller_trigger_candidate_down_[0] = controller_trigger_candidate_down_[1] = false;
    controller_trigger_candidate_since_[0] = controller_trigger_candidate_since_[1] = 0;
    controller_trigger_last_active_[0] = controller_trigger_last_active_[1] = 0;
    startup_menu_visible_ = false;
    startup_menu_pointer_active_ = false;
    ui_select_down_ = false;
    controller_ui_trigger_was_down_ = false;
    right_use_down_ = false;
    ui_scroll_axis_ = 0.0f;
    startup_menu_scroll_last_ms_ = 0;
    game_ui_open_intent_until_ms_=0;
    input_rearm_required_=true;
    last_haptic_ms_[0]=last_haptic_ms_[1]=0;
    right_primary_was_down_=left_primary_was_down_=menu_was_down_=b_was_down_=false;
    haptic_ready_logged_=haptic_failure_logged_=false;
    for (HandState &hand_state : hands_) hand_state = {};
}

void InputBridge::shutdown()
{
    release_injected_input();
    for (XrSpace &hand_space : hand_spaces_)
    {
        if (hand_space != XR_NULL_HANDLE) xrDestroySpace(hand_space);
        hand_space = XR_NULL_HANDLE;
    }
    for (XrSpace &aim_space : aim_spaces_)
    {
        if (aim_space != XR_NULL_HANDLE) xrDestroySpace(aim_space);
        aim_space = XR_NULL_HANDLE;
    }
    if (destroy_hand_tracker_)
    {
        for (XrHandTrackerEXT &tracker : optical_trackers_)
        {
            if (tracker != XR_NULL_HANDLE) destroy_hand_tracker_(tracker);
            tracker = XR_NULL_HANDLE;
        }
    }
    if (session_ != XR_NULL_HANDLE && haptic_action_ != XR_NULL_HANDLE)
    {
        for (uint32_t hand=0; hand<kHandCount; ++hand)
        {
            XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
            info.action=haptic_action_;
            info.subactionPath=hand_paths_[hand];
            xrStopHapticFeedback(session_,&info);
        }
    }
    if (action_set_ != XR_NULL_HANDLE) xrDestroyActionSet(action_set_);
    action_set_ = XR_NULL_HANDLE;
    grip_pose_action_ = aim_pose_action_ = trigger_action_ = thumbstick_action_ = squeeze_action_ = XR_NULL_HANDLE;
    primary_button_action_ = secondary_button_action_ = stick_click_action_ = menu_button_action_ =
        index_menu_force_action_ = haptic_action_ = XR_NULL_HANDLE;
    hand_paths_[0] = hand_paths_[1] = XR_NULL_PATH;
    touch_profile_path_ = index_profile_path_ = generic_profile_path_ = XR_NULL_PATH;
    meta_touch_profile_paths_.fill(XR_NULL_PATH);
    openxr_11_enabled_ = false;
    generic_controller_enabled_ = false;
    create_hand_tracker_ = nullptr;
    destroy_hand_tracker_ = nullptr;
    locate_hand_joints_ = nullptr;
    initialized_ = false;
    instance_ = XR_NULL_HANDLE;
    session_ = XR_NULL_HANDLE;
    base_space_ = XR_NULL_HANDLE;
    reset_runtime_state();
}
} // namespace smvr::features
