#include "vr_runtime.hpp"
#include "vr_hands.hpp"
#include "vr_tools.hpp"

#include <cmath>
#include <cstring>

namespace scrapvr
{
	namespace
	{
		constexpr uint32_t eye_count = 2;
		constexpr uint32_t max_swapchain_images = 8;
		constexpr uint32_t ui_width = 1600;
		constexpr uint32_t ui_height = 900;
		constexpr float ui_distance = 0.55f;
		constexpr float ui_world_width = 0.92f;
		constexpr float ui_world_height = ui_world_width * static_cast<float>(ui_height) /
			static_cast<float>(ui_width);
		// Full runtime recommendation misses Quest Link's 72 Hz deadline in this
		// three-view engine integration. Seventy-five percent per axis provides enough
		// headroom to avoid the runtime locking the app to half-rate reprojection.
		constexpr float eye_render_scale = 0.75f;

		struct EyeSwapchain
		{
			XrSwapchain handle = XR_NULL_HANDLE;
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t image_count = 0;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
			XrSwapchainImageD3D11KHR images[max_swapchain_images] = {};
			ID3D11RenderTargetView *render_targets[max_swapchain_images] = {};
			ID3D11ShaderResourceView *shader_resources[max_swapchain_images] = {};
		};

		struct UiSwapchain
		{
			XrSwapchain handle = XR_NULL_HANDLE;
			uint32_t image_count = 0;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
			XrSwapchainImageD3D11KHR images[max_swapchain_images] = {};
			ID3D11RenderTargetView *render_targets[max_swapchain_images] = {};
		};

		struct UiMatrix
		{
			float m[16] = {};
		};

		struct UiConstants
		{
			UiMatrix transform;
			float pointer[4] = {};
			float laser_start[4] = {};
			float laser_end[4] = {};
		};

		LogFunction g_log = nullptr;
		PFN_xrGetInstanceProcAddr g_get_proc = nullptr;
		XrInstance g_instance = XR_NULL_HANDLE;
		XrSystemId g_system_id = XR_NULL_SYSTEM_ID;
		XrSession g_session = XR_NULL_HANDLE;
		XrSpace g_local_space = XR_NULL_HANDLE;
		XrReferenceSpaceType g_reference_space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
		XrSessionState g_session_state = XR_SESSION_STATE_UNKNOWN;
		bool g_session_running = false;
		bool g_initialized = false;
		ULONGLONG g_last_init_attempt = 0;
		ID3D11Device *g_device = nullptr;
		ID3D11DeviceContext *g_context = nullptr;
		IDXGISwapChain *g_game_swapchain = nullptr;
		ID3D11Texture2D *g_desktop_frame = nullptr;
		ID3D11ShaderResourceView *g_desktop_frame_view = nullptr;
		ID3D11Texture2D *g_present_frame = nullptr;
		ID3D11ShaderResourceView *g_present_frame_view = nullptr;
		EyeSwapchain g_eyes[eye_count];
		UiSwapchain g_ui;
		XrView g_views[eye_count] = {};
		uint32_t g_acquired_images[eye_count] = {};
		bool g_eye_acquired[eye_count] = { false, false };
		bool g_eye_direct_rendered[eye_count] = { false, false };
		thread_local int g_redirect_eye = -1;
		bool g_frame_active = false;
		XrTime g_frame_display_time = 0;
		bool g_game_copy_logged = false;
		bool g_desktop_capture_logged = false;
		bool g_direct_eye_render_logged = false;
		bool g_direct_eye_render_failure_logged = false;
		ID3D11VertexShader *g_mirror_vertex_shader = nullptr;
		ID3D11PixelShader *g_mirror_pixel_shader = nullptr;
		ID3D11SamplerState *g_mirror_sampler = nullptr;
		ID3D11Buffer *g_mirror_constants = nullptr;
		ID3D11RasterizerState *g_mirror_rasterizer = nullptr;
		bool g_mirror_renderer_failed = false;
		bool g_mirror_logged = false;
		ID3D11VertexShader *g_ui_vertex_shader = nullptr;
		ID3D11VertexShader *g_ui_quad_vertex_shader = nullptr;
		ID3D11VertexShader *g_ui_laser_vertex_shader = nullptr;
		ID3D11PixelShader *g_ui_pixel_shader = nullptr;
		ID3D11PixelShader *g_ui_laser_pixel_shader = nullptr;
		ID3D11SamplerState *g_ui_sampler = nullptr;
		ID3D11Buffer *g_ui_constants = nullptr;
		ID3D11RasterizerState *g_ui_rasterizer = nullptr;
		ID3D11BlendState *g_ui_blend = nullptr;
		bool g_ui_renderer_failed = false;
		bool g_ui_visible = false;
		bool g_ui_was_visible = false;
		bool g_ui_layer_ready = false;
		bool g_ui_logged = false;
		bool g_ui_capture_logged = false;
		XrPosef g_ui_pose = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } };
		float g_ui_pointer_u = 0.5f;
		float g_ui_pointer_v = 0.5f;
		bool g_ui_pointer_active = false;
		XrVector3f g_ui_pointer_origin = {};
		XrVector3f g_ui_pointer_world = {};
		bool g_ui_drag_active = false;
		using EngineMouseButtonFunction = void (__fastcall *)(void *, int, int);
		using EngineMouseMoveFunction = void (__fastcall *)(void *, int, int, int, int, int);
		constexpr uintptr_t engine_input_manager_pointer_rva = 0x01267760;
		constexpr uintptr_t engine_mouse_down_rva = 0x0053c460;
		constexpr uintptr_t engine_mouse_up_rva = 0x0053c540;
		constexpr uintptr_t engine_mouse_move_rva = 0x0053c630;
		int g_ui_pointer_client_x = 0;
		int g_ui_pointer_client_y = 0;
		bool g_ui_pointer_client_initialized = false;
		bool g_ui_engine_button_down = false;
		bool g_ui_engine_input_logged = false;
		bool g_ui_engine_input_unavailable_logged = false;
		bool g_x_was_down = false;
		bool g_y_was_down = false;
		bool g_xy_chord_latched = false;
		ULONGLONG g_ui_scroll_last = 0;

		XrActionSet g_action_set = XR_NULL_HANDLE;
		XrAction g_grip_pose_action = XR_NULL_HANDLE;
		XrAction g_trigger_action = XR_NULL_HANDLE;
		XrAction g_thumbstick_action = XR_NULL_HANDLE;
		XrAction g_squeeze_action = XR_NULL_HANDLE;
		XrAction g_primary_button_action = XR_NULL_HANDLE;
		XrAction g_secondary_button_action = XR_NULL_HANDLE;
		XrAction g_stick_click_action = XR_NULL_HANDLE;
		XrAction g_menu_button_action = XR_NULL_HANDLE;
		XrPath g_hand_paths[eye_count] = { XR_NULL_PATH, XR_NULL_PATH };
		XrSpace g_hand_spaces[eye_count] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
		bool g_hand_pose_logged[eye_count] = { false, false };
		bool g_frame_submitted_logged = false;
		bool g_head_pose_logged = false;
		bool g_hand_tracking_enabled = false;
		XrHandTrackerEXT g_optical_hand_trackers[eye_count] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
		bool g_optical_hand_active[eye_count] = { false, false };
		bool g_optical_hand_logged[eye_count] = { false, false };
		volatile LONG g_recenter_request = 0;
		ULONGLONG g_recenter_hold_start = 0;
		bool g_recenter_latched = false;

		PFN_xrGetD3D11GraphicsRequirementsKHR p_xrGetD3D11GraphicsRequirementsKHR = nullptr;
		PFN_xrCreateSession p_xrCreateSession = nullptr;
		PFN_xrDestroySession p_xrDestroySession = nullptr;
		PFN_xrCreateReferenceSpace p_xrCreateReferenceSpace = nullptr;
		PFN_xrEnumerateReferenceSpaces p_xrEnumerateReferenceSpaces = nullptr;
		PFN_xrDestroySpace p_xrDestroySpace = nullptr;
		PFN_xrEnumerateViewConfigurationViews p_xrEnumerateViewConfigurationViews = nullptr;
		PFN_xrEnumerateSwapchainFormats p_xrEnumerateSwapchainFormats = nullptr;
		PFN_xrCreateSwapchain p_xrCreateSwapchain = nullptr;
		PFN_xrDestroySwapchain p_xrDestroySwapchain = nullptr;
		PFN_xrEnumerateSwapchainImages p_xrEnumerateSwapchainImages = nullptr;
		PFN_xrAcquireSwapchainImage p_xrAcquireSwapchainImage = nullptr;
		PFN_xrWaitSwapchainImage p_xrWaitSwapchainImage = nullptr;
		PFN_xrReleaseSwapchainImage p_xrReleaseSwapchainImage = nullptr;
		PFN_xrPollEvent p_xrPollEvent = nullptr;
		PFN_xrBeginSession p_xrBeginSession = nullptr;
		PFN_xrEndSession p_xrEndSession = nullptr;
		PFN_xrWaitFrame p_xrWaitFrame = nullptr;
		PFN_xrBeginFrame p_xrBeginFrame = nullptr;
		PFN_xrEndFrame p_xrEndFrame = nullptr;
		PFN_xrLocateViews p_xrLocateViews = nullptr;
		PFN_xrStringToPath p_xrStringToPath = nullptr;
		PFN_xrCreateActionSet p_xrCreateActionSet = nullptr;
		PFN_xrDestroyActionSet p_xrDestroyActionSet = nullptr;
		PFN_xrCreateAction p_xrCreateAction = nullptr;
		PFN_xrSuggestInteractionProfileBindings p_xrSuggestInteractionProfileBindings = nullptr;
		PFN_xrAttachSessionActionSets p_xrAttachSessionActionSets = nullptr;
		PFN_xrCreateActionSpace p_xrCreateActionSpace = nullptr;
		PFN_xrSyncActions p_xrSyncActions = nullptr;
		PFN_xrGetActionStatePose p_xrGetActionStatePose = nullptr;
		PFN_xrGetActionStateFloat p_xrGetActionStateFloat = nullptr;
		PFN_xrGetActionStateVector2f p_xrGetActionStateVector2f = nullptr;
		PFN_xrGetActionStateBoolean p_xrGetActionStateBoolean = nullptr;
		PFN_xrLocateSpace p_xrLocateSpace = nullptr;
		PFN_xrCreateHandTrackerEXT p_xrCreateHandTrackerEXT = nullptr;
		PFN_xrDestroyHandTrackerEXT p_xrDestroyHandTrackerEXT = nullptr;
		PFN_xrLocateHandJointsEXT p_xrLocateHandJointsEXT = nullptr;

		bool g_key_forward = false;
		bool g_key_backward = false;
		bool g_key_left = false;
		bool g_key_right = false;
		bool g_key_sprint = false;
		bool g_key_jump = false;
		bool g_key_use = false;
		bool g_key_inventory = false;
		bool g_key_menu = false;
		bool g_key_crawl = false;
		bool g_key_zoom_in = false;
		bool g_key_zoom_out = false;
		bool g_key_camera = false;
		bool g_locomotion_reference_valid = false;
		XrVector3f g_locomotion_reference_forward = { 0.0f, 0.0f, -1.0f };
		bool g_locomotion_basis_logged = false;
		bool g_mouse_attack = false;
		bool g_mouse_create = false;
		bool g_optical_gun_trigger = false;
		bool g_optical_pinch_down[eye_count] = { false, false };
		bool g_controller_trigger_down[eye_count] = { false, false };
		bool g_controller_trigger_candidate_down[eye_count] = { false, false };
		ULONGLONG g_controller_trigger_candidate_since[eye_count] = { 0, 0 };
		ULONGLONG g_controller_trigger_last_active[eye_count] = { 0, 0 };
		XrVector3f g_optical_right_palm_previous = {};
		XrTime g_optical_right_palm_time = 0;
		bool g_optical_hammer_swing_armed = true;
		XrTime g_last_optical_hammer_swing_time = 0;
		uint32_t g_optical_hammer_click_frames = 0;
		uint64_t g_optical_hammer_swing_sequence = 0;
		XrVector3f g_optical_hammer_swing_direction = { 0.0f, 0.0f, -1.0f };
		bool g_optical_pinch_logged = false;
		bool g_optical_hammer_swing_logged = false;
		bool g_touch_input_logged = false;
		bool g_left_stick_observed = false;
		bool g_a_button_observed = false;
		bool g_y_button_observed = false;
		bool g_game_focus_logged = false;
		bool g_input_suspended_logged = false;

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

		HWND get_game_window()
		{
			HWND window = nullptr;
			EnumWindows(find_game_window, reinterpret_cast<LPARAM>(&window));
			return window;
		}

		bool game_has_foreground()
		{
			DWORD process_id = 0;
			GetWindowThreadProcessId(GetForegroundWindow(), &process_id);
			return process_id == GetCurrentProcessId();
		}

		void *engine_input_manager()
		{
			auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
			if (base == nullptr)
				return nullptr;
			return *reinterpret_cast<void **>(base + engine_input_manager_pointer_rva);
		}

		bool queue_engine_mouse_move(int delta_x, int delta_y, int client_x, int client_y)
		{
			void *manager = engine_input_manager();
			if (manager == nullptr)
			{
				if (!g_ui_engine_input_unavailable_logged && g_log != nullptr)
				{
					g_ui_engine_input_unavailable_logged = true;
					g_log("VR UI ENGINE INPUT WAITING: Scrap Mechanic input manager is not initialized yet");
				}
				return false;
			}
			auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
			auto function = reinterpret_cast<EngineMouseMoveFunction>(
				base + engine_mouse_move_rva);
			function(manager, delta_x, delta_y, client_x, client_y, 0);
			g_ui_engine_input_unavailable_logged = false;
			if (!g_ui_engine_input_logged && g_log != nullptr)
			{
				g_ui_engine_input_logged = true;
				g_log("VR UI ENGINE INPUT ACTIVE: hand pointer and buttons are queued directly into Scrap Mechanic's private input event buffer");
			}
			return true;
		}

		bool queue_engine_mouse_button(bool down)
		{
			void *manager = engine_input_manager();
			if (manager == nullptr)
				return false;
			auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
			auto function = reinterpret_cast<EngineMouseButtonFunction>(base +
				(down ? engine_mouse_down_rva : engine_mouse_up_rva));
			function(manager, 0, 0);
			return true;
		}

		void release_ui_engine_mouse_button()
		{
			if (!g_ui_engine_button_down)
				return;
			if (queue_engine_mouse_button(false))
				g_ui_engine_button_down = false;
		}

		void focus_game_window()
		{
			HWND window = get_game_window();
			if (window == nullptr)
				return;
			ShowWindow(window, SW_RESTORE);
			const bool focused = SetForegroundWindow(window) != FALSE;
			if (focused && !g_game_focus_logged)
			{
				g_game_focus_logged = true;
				g_log("VR INPUT FOCUS: Scrap Mechanic foregrounded; controller injection is process-scoped");
			}
		}

		void set_key(WORD key, bool down, bool &state)
		{
			if (down == state)
				return;
			if (!game_has_foreground() && down)
			{
				state = false;
				return;
			}
			INPUT input = {};
			input.type = INPUT_KEYBOARD;
			input.ki.wVk = 0;
			input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(key, MAPVK_VK_TO_VSC));
			input.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
			SendInput(1, &input, sizeof(input));
			state = down;
		}

		void set_mouse_button(DWORD down_flag, DWORD up_flag, bool down, bool &state)
		{
			if (down == state)
				return;
			if (!game_has_foreground() && down)
			{
				state = false;
				return;
			}
			INPUT input = {};
			input.type = INPUT_MOUSE;
			input.mi.dwFlags = down ? down_flag : up_flag;
			SendInput(1, &input, sizeof(input));
			state = down;
		}

		void set_primary_mouse(bool down, bool modal_ui)
		{
			if (modal_ui)
			{
				// Never leave a globally injected gameplay click held when the menu opens.
				set_mouse_button(
					MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, false, g_mouse_attack);
				if (down == g_ui_engine_button_down)
					return;
				if (queue_engine_mouse_button(down))
					g_ui_engine_button_down = down;
				return;
			}
			release_ui_engine_mouse_button();
			set_mouse_button(
				MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, down, g_mouse_attack);
		}

		void send_mouse_wheel(LONG delta)
		{
			if (!game_has_foreground())
				return;
			INPUT input = {};
			input.type = INPUT_MOUSE;
			input.mi.dwFlags = MOUSEEVENTF_WHEEL;
			input.mi.mouseData = static_cast<DWORD>(delta);
			SendInput(1, &input, sizeof(input));
		}

		XrVector3f rotate_vector(const XrQuaternionf &q, const XrVector3f &v)
		{
			const XrVector3f u = { q.x, q.y, q.z };
			const float uv = u.x * v.x + u.y * v.y + u.z * v.z;
			const float uu = u.x * u.x + u.y * u.y + u.z * u.z;
			const XrVector3f cross = {
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

		bool horizontal_hmd_forward(XrVector3f &forward)
		{
			forward = rotate_vector(g_views[0].pose.orientation, { 0.0f, 0.0f, -1.0f });
			// OpenXR is Y-up. Locomotion deliberately discards headset pitch and
			// roll, then normalizes a freshly projected direction every frame.
			forward.y = 0.0f;
			const float length_squared = forward.x * forward.x + forward.z * forward.z;
			if (!std::isfinite(length_squared) || length_squared < 0.0001f)
				return false;
			const float inverse_length = 1.0f / std::sqrt(length_squared);
			forward.x *= inverse_length;
			forward.z *= inverse_length;
			return true;
		}

		XrVector2f hmd_relative_movement(const XrVector2f &stick)
		{
			XrVector3f current_forward = {};
			if (!horizontal_hmd_forward(current_forward))
				return stick;
			if (!g_locomotion_reference_valid)
			{
				g_locomotion_reference_forward = current_forward;
				g_locomotion_reference_valid = true;
				if (!g_locomotion_basis_logged && g_log != nullptr)
				{
					g_locomotion_basis_logged = true;
					g_log("VR LOCOMOTION BASIS: current HMD forward is projected onto the horizontal plane and normalized every frame; no pitch, roll, or accumulated rotation");
				}
			}

			const XrVector3f reference_right = {
				-g_locomotion_reference_forward.z,
				0.0f,
				g_locomotion_reference_forward.x
			};
			const XrVector3f current_right = {
				-current_forward.z,
				0.0f,
				current_forward.x
			};
			const XrVector3f desired = {
				current_right.x * stick.x + current_forward.x * stick.y,
				0.0f,
				current_right.z * stick.x + current_forward.z * stick.y
			};
			return {
				desired.x * reference_right.x + desired.z * reference_right.z,
				desired.x * g_locomotion_reference_forward.x +
					desired.z * g_locomotion_reference_forward.z
			};
		}

		float dot_vector(const XrVector3f &a, const XrVector3f &b)
		{
			return a.x * b.x + a.y * b.y + a.z * b.z;
		}

		UiMatrix ui_identity()
		{
			UiMatrix result = {};
			result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0f;
			return result;
		}

		UiMatrix ui_multiply(const UiMatrix &a, const UiMatrix &b)
		{
			UiMatrix result = {};
			for (int column = 0; column < 4; ++column)
				for (int row = 0; row < 4; ++row)
					for (int k = 0; k < 4; ++k)
						result.m[column * 4 + row] +=
							a.m[k * 4 + row] * b.m[column * 4 + k];
			return result;
		}

		UiMatrix ui_pose_matrix(const XrPosef &pose)
		{
			const float x = pose.orientation.x;
			const float y = pose.orientation.y;
			const float z = pose.orientation.z;
			const float w = pose.orientation.w;
			UiMatrix result = ui_identity();
			result.m[0] = 1 - 2 * (y * y + z * z);
			result.m[4] = 2 * (x * y - z * w);
			result.m[8] = 2 * (x * z + y * w);
			result.m[1] = 2 * (x * y + z * w);
			result.m[5] = 1 - 2 * (x * x + z * z);
			result.m[9] = 2 * (y * z - x * w);
			result.m[2] = 2 * (x * z - y * w);
			result.m[6] = 2 * (y * z + x * w);
			result.m[10] = 1 - 2 * (x * x + y * y);
			result.m[12] = pose.position.x;
			result.m[13] = pose.position.y;
			result.m[14] = pose.position.z;
			return result;
		}

		UiMatrix ui_inverse_pose(const XrPosef &pose)
		{
			XrPosef inverse = {};
			inverse.orientation = {
				-pose.orientation.x, -pose.orientation.y,
				-pose.orientation.z, pose.orientation.w
			};
			inverse.position = rotate_vector(inverse.orientation, {
				-pose.position.x, -pose.position.y, -pose.position.z
			});
			return ui_pose_matrix(inverse);
		}

		UiMatrix ui_projection(const XrFovf &fov)
		{
			constexpr float near_z = 0.025f;
			constexpr float far_z = 100.0f;
			const float left = std::tan(fov.angleLeft);
			const float right = std::tan(fov.angleRight);
			const float down = std::tan(fov.angleDown);
			const float up = std::tan(fov.angleUp);
			UiMatrix result = {};
			result.m[0] = 2.0f / (right - left);
			result.m[5] = 2.0f / (up - down);
			result.m[8] = (right + left) / (right - left);
			result.m[9] = (up + down) / (up - down);
			result.m[10] = -far_z / (far_z - near_z);
			result.m[11] = -1.0f;
			result.m[14] = -(far_z * near_z) / (far_z - near_z);
			return result;
		}

		UiMatrix ui_panel_model()
		{
			UiMatrix scale = ui_identity();
			scale.m[0] = ui_world_width;
			scale.m[5] = ui_world_height;
			return ui_multiply(ui_pose_matrix(g_ui_pose), scale);
		}

		void update_ui_pointer_from_hand()
		{
			g_ui_pointer_active = false;
			if (!g_ui_visible || !game_has_foreground())
				return;
			XrPosef hand = {};
			bool optical = false;
			bool interaction = false;
			if (!hands::get_pose(1, hand, optical, interaction))
				return;
			const XrVector3f ray_direction = rotate_vector(hand.orientation, { 0.0f, 0.0f, -1.0f });
			const XrVector3f panel_normal = rotate_vector(g_ui_pose.orientation, { 0.0f, 0.0f, 1.0f });
			const float denominator = dot_vector(ray_direction, panel_normal);
			if (std::fabs(denominator) < 0.0001f)
				return;
			const XrVector3f to_panel = {
				g_ui_pose.position.x - hand.position.x,
				g_ui_pose.position.y - hand.position.y,
				g_ui_pose.position.z - hand.position.z
			};
			const float distance = dot_vector(to_panel, panel_normal) / denominator;
			if (distance <= 0.02f || distance > 4.0f)
				return;
			const XrVector3f world_hit = {
				hand.position.x + ray_direction.x * distance,
				hand.position.y + ray_direction.y * distance,
				hand.position.z + ray_direction.z * distance
			};
			const XrVector3f panel_hit = {
				world_hit.x - g_ui_pose.position.x,
				world_hit.y - g_ui_pose.position.y,
				world_hit.z - g_ui_pose.position.z
			};
			const XrQuaternionf inverse = {
				-g_ui_pose.orientation.x, -g_ui_pose.orientation.y,
				-g_ui_pose.orientation.z, g_ui_pose.orientation.w
			};
			const XrVector3f local = rotate_vector(inverse, panel_hit);
			const float u = local.x / ui_world_width + 0.5f;
			const float v = 0.5f - local.y / ui_world_height;
			if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
				return;

			HWND window = get_game_window();
			RECT client = {};
			if (window == nullptr || !GetClientRect(window, &client))
				return;
			const int width = client.right - client.left;
			const int height = client.bottom - client.top;
			if (width <= 0 || height <= 0)
				return;
			const int client_x = static_cast<int>(u * static_cast<float>(width - 1));
			const int client_y = static_cast<int>(v * static_cast<float>(height - 1));
			const int previous_x = g_ui_pointer_client_x;
			const int previous_y = g_ui_pointer_client_y;
			const bool first_pointer_sample = !g_ui_pointer_client_initialized;
			g_ui_pointer_client_x = client_x;
			g_ui_pointer_client_y = client_y;
			g_ui_pointer_client_initialized = true;
			int delta_x = first_pointer_sample ? 1 : client_x - previous_x;
			int delta_y = first_pointer_sample ? 0 : client_y - previous_y;
			if (delta_x == 0 && delta_y == 0 && first_pointer_sample)
				delta_x = 1;
			if (first_pointer_sample || delta_x != 0 || delta_y != 0)
				queue_engine_mouse_move(delta_x, delta_y, client_x, client_y);
			g_ui_pointer_u = u;
			g_ui_pointer_v = v;
			g_ui_pointer_origin = hand.position;
			g_ui_pointer_world = world_hit;
			g_ui_pointer_active = true;
		}

		void release_injected_input()
		{
			set_key('W', false, g_key_forward);
			set_key('S', false, g_key_backward);
			set_key('A', false, g_key_left);
			set_key('D', false, g_key_right);
			set_key(VK_SHIFT, false, g_key_sprint);
			set_key(VK_SPACE, false, g_key_jump);
			set_key('E', false, g_key_use);
			set_key('I', false, g_key_inventory);
			set_key(VK_ESCAPE, false, g_key_menu);
			set_key(VK_CONTROL, false, g_key_crawl);
			set_key('X', false, g_key_zoom_in);
			set_key('C', false, g_key_zoom_out);
			set_key('V', false, g_key_camera);
			set_primary_mouse(false, false);
			set_mouse_button(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, false, g_mouse_create);
			g_x_was_down = false;
			g_y_was_down = false;
			g_xy_chord_latched = false;
			g_ui_drag_active = false;
		}

		template <typename T>
		bool resolve(const char *name, T &target)
		{
			PFN_xrVoidFunction function = nullptr;
			const XrResult result = g_get_proc(g_instance, name, &function);
			target = reinterpret_cast<T>(function);
			if (XR_FAILED(result) || target == nullptr)
			{
				g_log("OpenXR session resolve failed: %s result=%d", name, static_cast<int>(result));
				return false;
			}
			return true;
		}

		bool resolve_session_functions()
		{
			return
				resolve("xrGetD3D11GraphicsRequirementsKHR", p_xrGetD3D11GraphicsRequirementsKHR) &&
				resolve("xrCreateSession", p_xrCreateSession) &&
				resolve("xrDestroySession", p_xrDestroySession) &&
				resolve("xrCreateReferenceSpace", p_xrCreateReferenceSpace) &&
				resolve("xrEnumerateReferenceSpaces", p_xrEnumerateReferenceSpaces) &&
				resolve("xrDestroySpace", p_xrDestroySpace) &&
				resolve("xrEnumerateViewConfigurationViews", p_xrEnumerateViewConfigurationViews) &&
				resolve("xrEnumerateSwapchainFormats", p_xrEnumerateSwapchainFormats) &&
				resolve("xrCreateSwapchain", p_xrCreateSwapchain) &&
				resolve("xrDestroySwapchain", p_xrDestroySwapchain) &&
				resolve("xrEnumerateSwapchainImages", p_xrEnumerateSwapchainImages) &&
				resolve("xrAcquireSwapchainImage", p_xrAcquireSwapchainImage) &&
				resolve("xrWaitSwapchainImage", p_xrWaitSwapchainImage) &&
				resolve("xrReleaseSwapchainImage", p_xrReleaseSwapchainImage) &&
				resolve("xrPollEvent", p_xrPollEvent) &&
				resolve("xrBeginSession", p_xrBeginSession) &&
				resolve("xrEndSession", p_xrEndSession) &&
				resolve("xrWaitFrame", p_xrWaitFrame) &&
				resolve("xrBeginFrame", p_xrBeginFrame) &&
				resolve("xrEndFrame", p_xrEndFrame) &&
				resolve("xrLocateViews", p_xrLocateViews) &&
				resolve("xrStringToPath", p_xrStringToPath) &&
				resolve("xrCreateActionSet", p_xrCreateActionSet) &&
				resolve("xrDestroyActionSet", p_xrDestroyActionSet) &&
				resolve("xrCreateAction", p_xrCreateAction) &&
				resolve("xrSuggestInteractionProfileBindings", p_xrSuggestInteractionProfileBindings) &&
				resolve("xrAttachSessionActionSets", p_xrAttachSessionActionSets) &&
				resolve("xrCreateActionSpace", p_xrCreateActionSpace) &&
				resolve("xrSyncActions", p_xrSyncActions) &&
				resolve("xrGetActionStatePose", p_xrGetActionStatePose) &&
				resolve("xrGetActionStateFloat", p_xrGetActionStateFloat) &&
				resolve("xrGetActionStateVector2f", p_xrGetActionStateVector2f) &&
				resolve("xrGetActionStateBoolean", p_xrGetActionStateBoolean) &&
				resolve("xrLocateSpace", p_xrLocateSpace);
		}

		bool resolve_hand_tracking_functions()
		{
			if (!g_hand_tracking_enabled)
				return false;
			PFN_xrVoidFunction create = nullptr;
			PFN_xrVoidFunction destroy = nullptr;
			PFN_xrVoidFunction locate = nullptr;
			if (XR_FAILED(g_get_proc(g_instance, "xrCreateHandTrackerEXT", &create)) ||
				XR_FAILED(g_get_proc(g_instance, "xrDestroyHandTrackerEXT", &destroy)) ||
				XR_FAILED(g_get_proc(g_instance, "xrLocateHandJointsEXT", &locate)) ||
				create == nullptr || destroy == nullptr || locate == nullptr)
			{
				g_log("XR_EXT_hand_tracking functions unavailable; using Quest Touch only");
				g_hand_tracking_enabled = false;
				return false;
			}
			p_xrCreateHandTrackerEXT = reinterpret_cast<PFN_xrCreateHandTrackerEXT>(create);
			p_xrDestroyHandTrackerEXT = reinterpret_cast<PFN_xrDestroyHandTrackerEXT>(destroy);
			p_xrLocateHandJointsEXT = reinterpret_cast<PFN_xrLocateHandJointsEXT>(locate);
			return true;
		}

		void release_mirror_renderer()
		{
			if (g_mirror_rasterizer != nullptr) g_mirror_rasterizer->Release();
			if (g_mirror_constants != nullptr) g_mirror_constants->Release();
			if (g_mirror_sampler != nullptr) g_mirror_sampler->Release();
			if (g_mirror_pixel_shader != nullptr) g_mirror_pixel_shader->Release();
			if (g_mirror_vertex_shader != nullptr) g_mirror_vertex_shader->Release();
			g_mirror_rasterizer = nullptr;
			g_mirror_constants = nullptr;
			g_mirror_sampler = nullptr;
			g_mirror_pixel_shader = nullptr;
			g_mirror_vertex_shader = nullptr;
			g_mirror_renderer_failed = false;
			g_mirror_logged = false;
		}

		void release_ui_renderer()
		{
			if (g_ui_blend != nullptr) g_ui_blend->Release();
			if (g_ui_rasterizer != nullptr) g_ui_rasterizer->Release();
			if (g_ui_constants != nullptr) g_ui_constants->Release();
			if (g_ui_sampler != nullptr) g_ui_sampler->Release();
			if (g_ui_pixel_shader != nullptr) g_ui_pixel_shader->Release();
			if (g_ui_laser_pixel_shader != nullptr) g_ui_laser_pixel_shader->Release();
			if (g_ui_vertex_shader != nullptr) g_ui_vertex_shader->Release();
			if (g_ui_quad_vertex_shader != nullptr) g_ui_quad_vertex_shader->Release();
			if (g_ui_laser_vertex_shader != nullptr) g_ui_laser_vertex_shader->Release();
			g_ui_blend = nullptr;
			g_ui_rasterizer = nullptr;
			g_ui_constants = nullptr;
			g_ui_sampler = nullptr;
			g_ui_pixel_shader = nullptr;
			g_ui_laser_pixel_shader = nullptr;
			g_ui_vertex_shader = nullptr;
			g_ui_quad_vertex_shader = nullptr;
			g_ui_laser_vertex_shader = nullptr;
			g_ui_renderer_failed = false;
		}

		void release_ui_swapchain()
		{
			for (uint32_t i = 0; i < g_ui.image_count; ++i)
			{
				if (g_ui.render_targets[i] != nullptr)
					g_ui.render_targets[i]->Release();
				g_ui.render_targets[i] = nullptr;
			}
			if (g_ui.handle != XR_NULL_HANDLE && p_xrDestroySwapchain != nullptr)
				p_xrDestroySwapchain(g_ui.handle);
			g_ui = UiSwapchain();
		}

		void release_ui_capture()
		{
			if (g_desktop_frame_view != nullptr) g_desktop_frame_view->Release();
			if (g_desktop_frame != nullptr) g_desktop_frame->Release();
			if (g_present_frame_view != nullptr) g_present_frame_view->Release();
			if (g_present_frame != nullptr) g_present_frame->Release();
			g_desktop_frame_view = nullptr;
			g_desktop_frame = nullptr;
			g_present_frame_view = nullptr;
			g_present_frame = nullptr;
		}

		bool initialize_mirror_renderer()
		{
			if (g_mirror_vertex_shader != nullptr && g_mirror_pixel_shader != nullptr &&
				g_mirror_sampler != nullptr && g_mirror_constants != nullptr &&
				g_mirror_rasterizer != nullptr)
				return true;
			if (g_mirror_renderer_failed || g_device == nullptr)
				return false;

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
			using Compile = HRESULT (WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
			auto compile = compiler != nullptr
				? reinterpret_cast<Compile>(GetProcAddress(compiler, "D3DCompile"))
				: nullptr;
			ID3DBlob *vertex_blob = nullptr;
			ID3DBlob *pixel_blob = nullptr;
			ID3DBlob *errors = nullptr;
			if (compile == nullptr || FAILED(compile(
				shader, std::strlen(shader), "vr_desktop_mirror", nullptr, nullptr,
				"vs_main", "vs_5_0", 0, 0, &vertex_blob, &errors)))
			{
				if (g_log != nullptr)
					g_log("VR DESKTOP MIRROR: vertex shader compilation failed%s%s",
						errors != nullptr ? ": " : "",
						errors != nullptr ? static_cast<const char *>(errors->GetBufferPointer()) : "");
				if (errors != nullptr) errors->Release();
				if (compiler != nullptr) FreeLibrary(compiler);
				g_mirror_renderer_failed = true;
				return false;
			}
			if (errors != nullptr) errors->Release();
			errors = nullptr;
			if (FAILED(compile(
				shader, std::strlen(shader), "vr_desktop_mirror", nullptr, nullptr,
				"ps_main", "ps_5_0", 0, 0, &pixel_blob, &errors)))
			{
				if (g_log != nullptr)
					g_log("VR DESKTOP MIRROR: pixel shader compilation failed%s%s",
						errors != nullptr ? ": " : "",
						errors != nullptr ? static_cast<const char *>(errors->GetBufferPointer()) : "");
				if (errors != nullptr) errors->Release();
				vertex_blob->Release();
				if (compiler != nullptr) FreeLibrary(compiler);
				g_mirror_renderer_failed = true;
				return false;
			}
			if (compiler != nullptr) FreeLibrary(compiler);

			bool created = SUCCEEDED(g_device->CreateVertexShader(
				vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(), nullptr,
				&g_mirror_vertex_shader)) &&
				SUCCEEDED(g_device->CreatePixelShader(
					pixel_blob->GetBufferPointer(), pixel_blob->GetBufferSize(), nullptr,
					&g_mirror_pixel_shader));
			vertex_blob->Release();
			pixel_blob->Release();
			if (errors != nullptr) errors->Release();

			D3D11_SAMPLER_DESC sampler = {};
			sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.MaxLOD = D3D11_FLOAT32_MAX;
			D3D11_BUFFER_DESC constants = {};
			constants.ByteWidth = 16;
			constants.Usage = D3D11_USAGE_DEFAULT;
			constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			D3D11_RASTERIZER_DESC rasterizer = {};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_NONE;
			rasterizer.DepthClipEnable = TRUE;
			created = created &&
				SUCCEEDED(g_device->CreateSamplerState(&sampler, &g_mirror_sampler)) &&
				SUCCEEDED(g_device->CreateBuffer(&constants, nullptr, &g_mirror_constants)) &&
				SUCCEEDED(g_device->CreateRasterizerState(&rasterizer, &g_mirror_rasterizer));
			if (!created)
			{
				release_mirror_renderer();
				g_mirror_renderer_failed = true;
				if (g_log != nullptr)
					g_log("VR DESKTOP MIRROR: D3D11 resource creation failed; retaining desktop-render fallback");
				return false;
			}
			return true;
		}

		bool initialize_ui_renderer()
		{
			if (g_ui_vertex_shader != nullptr && g_ui_quad_vertex_shader != nullptr &&
				g_ui_laser_vertex_shader != nullptr && g_ui_pixel_shader != nullptr &&
				g_ui_laser_pixel_shader != nullptr && g_ui_sampler != nullptr &&
				g_ui_constants != nullptr && g_ui_rasterizer != nullptr && g_ui_blend != nullptr)
				return true;
			if (g_ui_renderer_failed || g_device == nullptr)
				return false;

			const char *shader = R"(
				cbuffer UiConstants : register(b0) {
					float4x4 transform;
					float4 pointerData;
					float4 laserStart;
					float4 laserEnd;
				};
				struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
				VSOut vs_fullscreen(uint id : SV_VertexID) {
					float2 uv = float2((id << 1) & 2, id & 2);
					VSOut output;
					output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
					output.uv = uv;
					return output;
				}
				VSOut vs_quad(uint id : SV_VertexID) {
					float2 corners[6] = {
						float2(0,0), float2(1,0), float2(0,1),
						float2(0,1), float2(1,0), float2(1,1)
					};
					VSOut output;
					output.uv = corners[id];
					float4 local = float4(output.uv.x - 0.5, 0.5 - output.uv.y, 0.0, 1.0);
					output.position = mul(transform, local);
					return output;
				}
				VSOut vs_laser(uint id : SV_VertexID) {
					VSOut output;
					output.uv = 0.0;
					float3 world = id == 0 ? laserStart.xyz : laserEnd.xyz;
					output.position = mul(transform, float4(world, 1.0));
					return output;
				}
				Texture2D uiTexture : register(t0);
				SamplerState uiSampler : register(s0);
				float4 ps_main(VSOut input) : SV_TARGET {
					float2 uv = input.uv;
					float4 ui = uiTexture.SampleLevel(uiSampler, uv, 0.0);
					bool vrPanel = pointerData.w > 0.5;
					if (vrPanel && (uv.y < 0.040 || uv.y > 0.875)) ui = 0.0;
					float intensity = max(ui.r, max(ui.g, ui.b));
					if (vrPanel && intensity < 0.004 && ui.a < 0.85) ui = 0.0;
					float alpha = saturate(ui.a);
					float3 color = alpha > 0.001 ? saturate(ui.rgb / alpha) : ui.rgb;
					if (vrPanel && pointerData.z > 0.5) {
						float2 delta = uv - pointerData.xy;
						delta.x *= 1600.0 / 900.0;
						float radius = length(delta);
						if (radius < 0.016)
							return radius < 0.010 ? float4(1.0, 0.76, 0.04, 1.0) : float4(0.08, 0.08, 0.08, 1.0);
					}
					return float4(color, alpha);
				}
				float4 ps_laser(VSOut input) : SV_TARGET {
					return float4(1.0, 0.76, 0.04, 1.0);
				}
			)";
			HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
			using Compile = HRESULT (WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
			auto compile = compiler != nullptr
				? reinterpret_cast<Compile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
			ID3DBlob *fullscreen_blob = nullptr;
			ID3DBlob *quad_blob = nullptr;
			ID3DBlob *laser_vertex_blob = nullptr;
			ID3DBlob *pixel_blob = nullptr;
			ID3DBlob *laser_pixel_blob = nullptr;
			ID3DBlob *errors = nullptr;
			auto compile_shader = [&](const char *entry, const char *profile, ID3DBlob **blob)
			{
				if (errors != nullptr) { errors->Release(); errors = nullptr; }
				return compile != nullptr && SUCCEEDED(compile(
					shader, std::strlen(shader), "vr_modal_ui", nullptr, nullptr,
					entry, profile, 0, 0, blob, &errors));
			};
			if (!compile_shader("vs_fullscreen", "vs_5_0", &fullscreen_blob) ||
				!compile_shader("vs_quad", "vs_5_0", &quad_blob) ||
				!compile_shader("vs_laser", "vs_5_0", &laser_vertex_blob) ||
				!compile_shader("ps_main", "ps_5_0", &pixel_blob) ||
				!compile_shader("ps_laser", "ps_5_0", &laser_pixel_blob))
			{
				if (g_log != nullptr)
					g_log("VR UI-ONLY COMPOSITOR: shader compilation failed%s%s",
						errors != nullptr ? ": " : "",
						errors != nullptr ? static_cast<const char *>(errors->GetBufferPointer()) : "");
				if (errors != nullptr) errors->Release();
				if (fullscreen_blob != nullptr) fullscreen_blob->Release();
				if (quad_blob != nullptr) quad_blob->Release();
				if (laser_vertex_blob != nullptr) laser_vertex_blob->Release();
				if (pixel_blob != nullptr) pixel_blob->Release();
				if (laser_pixel_blob != nullptr) laser_pixel_blob->Release();
				if (compiler != nullptr) FreeLibrary(compiler);
				g_ui_renderer_failed = true;
				return false;
			}
			if (compiler != nullptr) FreeLibrary(compiler);
			if (errors != nullptr) errors->Release();

			bool created = SUCCEEDED(g_device->CreateVertexShader(
				fullscreen_blob->GetBufferPointer(), fullscreen_blob->GetBufferSize(), nullptr,
				&g_ui_vertex_shader)) &&
				SUCCEEDED(g_device->CreateVertexShader(
					quad_blob->GetBufferPointer(), quad_blob->GetBufferSize(), nullptr,
					&g_ui_quad_vertex_shader)) &&
				SUCCEEDED(g_device->CreateVertexShader(
					laser_vertex_blob->GetBufferPointer(), laser_vertex_blob->GetBufferSize(), nullptr,
					&g_ui_laser_vertex_shader)) &&
				SUCCEEDED(g_device->CreatePixelShader(
					pixel_blob->GetBufferPointer(), pixel_blob->GetBufferSize(), nullptr,
					&g_ui_pixel_shader)) &&
				SUCCEEDED(g_device->CreatePixelShader(
					laser_pixel_blob->GetBufferPointer(), laser_pixel_blob->GetBufferSize(), nullptr,
					&g_ui_laser_pixel_shader));
			fullscreen_blob->Release();
			quad_blob->Release();
			laser_vertex_blob->Release();
			pixel_blob->Release();
			laser_pixel_blob->Release();

			D3D11_SAMPLER_DESC sampler = {};
			sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.MaxLOD = D3D11_FLOAT32_MAX;
			D3D11_BUFFER_DESC constants = {};
			constants.ByteWidth = sizeof(UiConstants);
			constants.Usage = D3D11_USAGE_DEFAULT;
			constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			D3D11_RASTERIZER_DESC rasterizer = {};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_NONE;
			rasterizer.DepthClipEnable = TRUE;
			D3D11_BLEND_DESC blend = {};
			blend.RenderTarget[0].BlendEnable = TRUE;
			blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			created = created &&
				SUCCEEDED(g_device->CreateSamplerState(&sampler, &g_ui_sampler)) &&
				SUCCEEDED(g_device->CreateBuffer(&constants, nullptr, &g_ui_constants)) &&
				SUCCEEDED(g_device->CreateRasterizerState(&rasterizer, &g_ui_rasterizer)) &&
				SUCCEEDED(g_device->CreateBlendState(&blend, &g_ui_blend));
			if (!created)
			{
				release_ui_renderer();
				g_ui_renderer_failed = true;
				if (g_log != nullptr) g_log("VR UI-ONLY COMPOSITOR: D3D11 resource creation failed");
				return false;
			}
			return true;
		}

		void release_eye(EyeSwapchain &eye)
		{
			for (uint32_t i = 0; i < eye.image_count; ++i)
			{
				if (eye.shader_resources[i] != nullptr)
					eye.shader_resources[i]->Release();
				eye.shader_resources[i] = nullptr;
				if (eye.render_targets[i] != nullptr)
				eye.render_targets[i]->Release();
				eye.render_targets[i] = nullptr;
			}
			if (eye.handle != XR_NULL_HANDLE && p_xrDestroySwapchain != nullptr)
				p_xrDestroySwapchain(eye.handle);
			eye = EyeSwapchain();
		}

		void cleanup_session_objects()
		{
			release_injected_input();
			hands::shutdown();
			g_session_running = false;
			g_frame_active = false;
			g_frame_display_time = 0;
			for (uint32_t i = 0; i < eye_count; ++i)
				g_eye_acquired[i] = false;
			for (uint32_t i = 0; i < eye_count; ++i)
				release_eye(g_eyes[i]);
			release_ui_swapchain();

			for (uint32_t i = 0; i < eye_count; ++i)
			{
				if (g_optical_hand_trackers[i] != XR_NULL_HANDLE && p_xrDestroyHandTrackerEXT != nullptr)
					p_xrDestroyHandTrackerEXT(g_optical_hand_trackers[i]);
				g_optical_hand_trackers[i] = XR_NULL_HANDLE;
				if (g_hand_spaces[i] != XR_NULL_HANDLE && p_xrDestroySpace != nullptr)
					p_xrDestroySpace(g_hand_spaces[i]);
				g_hand_spaces[i] = XR_NULL_HANDLE;
			}
			if (g_local_space != XR_NULL_HANDLE && p_xrDestroySpace != nullptr)
				p_xrDestroySpace(g_local_space);
			g_local_space = XR_NULL_HANDLE;

			if (g_session != XR_NULL_HANDLE && p_xrDestroySession != nullptr)
				p_xrDestroySession(g_session);
			g_session = XR_NULL_HANDLE;

			if (g_action_set != XR_NULL_HANDLE && p_xrDestroyActionSet != nullptr)
				p_xrDestroyActionSet(g_action_set);
			g_action_set = XR_NULL_HANDLE;
			g_grip_pose_action = XR_NULL_HANDLE;
			g_trigger_action = XR_NULL_HANDLE;
			g_thumbstick_action = XR_NULL_HANDLE;
			g_squeeze_action = XR_NULL_HANDLE;
			g_primary_button_action = XR_NULL_HANDLE;
			g_secondary_button_action = XR_NULL_HANDLE;
			g_stick_click_action = XR_NULL_HANDLE;
			g_menu_button_action = XR_NULL_HANDLE;
			g_hand_tracking_enabled = false;
			p_xrCreateHandTrackerEXT = nullptr;
			p_xrDestroyHandTrackerEXT = nullptr;
			p_xrLocateHandJointsEXT = nullptr;
			InterlockedExchange(&g_recenter_request, 0);
			g_recenter_hold_start = 0;
			g_recenter_latched = false;
			g_locomotion_reference_valid = false;
			g_locomotion_basis_logged = false;
			g_left_stick_observed = false;
			g_a_button_observed = false;
			g_y_button_observed = false;
			g_game_focus_logged = false;
			g_input_suspended_logged = false;
			g_optical_gun_trigger = false;
			g_optical_pinch_down[0] = g_optical_pinch_down[1] = false;
			g_controller_trigger_down[0] = g_controller_trigger_down[1] = false;
			g_controller_trigger_candidate_down[0] = g_controller_trigger_candidate_down[1] = false;
			g_controller_trigger_candidate_since[0] = g_controller_trigger_candidate_since[1] = 0;
			g_controller_trigger_last_active[0] = g_controller_trigger_last_active[1] = 0;
			g_optical_right_palm_previous = {};
			g_optical_right_palm_time = 0;
			g_optical_hammer_swing_armed = true;
			g_last_optical_hammer_swing_time = 0;
			g_optical_hammer_click_frames = 0;
			g_optical_hammer_swing_sequence = 0;
			g_optical_hammer_swing_direction = { 0.0f, 0.0f, -1.0f };
			g_optical_pinch_logged = false;
			g_optical_hammer_swing_logged = false;

			release_ui_capture();
			release_mirror_renderer();
			release_ui_renderer();
			if (g_context != nullptr)
				g_context->Release();
			g_context = nullptr;
			if (g_device != nullptr)
				g_device->Release();
			g_device = nullptr;
			if (g_game_swapchain != nullptr)
				g_game_swapchain->Release();
			g_game_swapchain = nullptr;
			g_redirect_eye = -1;
			g_eye_direct_rendered[0] = false;
			g_eye_direct_rendered[1] = false;
			g_ui_visible = false;
			g_ui_was_visible = false;
			g_ui_layer_ready = false;
			g_ui_pointer_active = false;
			g_ui_logged = false;
			g_ui_capture_logged = false;
			g_ui_scroll_last = 0;
			g_ui_drag_active = false;
			g_ui_pointer_client_initialized = false;
			g_ui_engine_button_down = false;
			g_ui_engine_input_logged = false;
			g_ui_engine_input_unavailable_logged = false;
			g_initialized = false;
		}

		bool create_reference_space()
		{
			uint32_t count = 0;
			XrReferenceSpaceType supported[8] = {};
			bool stage_supported = false;
			if (XR_SUCCEEDED(p_xrEnumerateReferenceSpaces(g_session, 0, &count, nullptr)) &&
				count > 0 && count <= 8 &&
				XR_SUCCEEDED(p_xrEnumerateReferenceSpaces(g_session, count, &count, supported)))
				for (uint32_t i = 0; i < count; ++i)
					stage_supported = stage_supported || supported[i] == XR_REFERENCE_SPACE_TYPE_STAGE;
			XrReferenceSpaceCreateInfo info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
			info.referenceSpaceType =
				stage_supported ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
			info.poseInReferenceSpace.orientation.w = 1.0f;
			const XrResult result = p_xrCreateReferenceSpace(g_session, &info, &g_local_space);
			if (XR_FAILED(result))
			{
				g_log("xrCreateReferenceSpace(LOCAL) failed: result=%d", static_cast<int>(result));
				return false;
			}
			g_reference_space_type = info.referenceSpaceType;
			g_log("Tracking reference space: %s (floor-stable=%u)",
				g_reference_space_type == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE" : "LOCAL",
				g_reference_space_type == XR_REFERENCE_SPACE_TYPE_STAGE ? 1u : 0u);
			return true;
		}

		void create_optical_hand_trackers()
		{
			if (!resolve_hand_tracking_functions())
				return;
			for (uint32_t i = 0; i < eye_count; ++i)
			{
				XrHandTrackerCreateInfoEXT info = { XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
				info.hand = i == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
				info.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
				const XrResult result =
					p_xrCreateHandTrackerEXT(g_session, &info, &g_optical_hand_trackers[i]);
				if (XR_FAILED(result))
				{
					g_log("xrCreateHandTrackerEXT(%s) failed: result=%d",
						i == 0 ? "left" : "right", static_cast<int>(result));
					g_optical_hand_trackers[i] = XR_NULL_HANDLE;
				}
			}
			if (g_optical_hand_trackers[0] != XR_NULL_HANDLE ||
				g_optical_hand_trackers[1] != XR_NULL_HANDLE)
				g_log("OPTICAL HAND TRACKING READY: 26 joints per hand; gestures are visual-only");
		}

		bool choose_swapchain_format(int64_t &selected)
		{
			uint32_t count = 0;
			XrResult result = p_xrEnumerateSwapchainFormats(g_session, 0, &count, nullptr);
			if (XR_FAILED(result) || count == 0 || count > 64)
			{
				g_log("xrEnumerateSwapchainFormats(count) failed: result=%d count=%u", static_cast<int>(result), count);
				return false;
			}

			int64_t formats[64] = {};
			result = p_xrEnumerateSwapchainFormats(g_session, count, &count, formats);
			if (XR_FAILED(result))
				return false;

			const int64_t preferred[] = {
				DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
				DXGI_FORMAT_R8G8B8A8_UNORM,
				DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
				DXGI_FORMAT_B8G8R8A8_UNORM
			};
			for (const int64_t candidate : preferred)
			{
				for (uint32_t i = 0; i < count; ++i)
				if (formats[i] == candidate)
				{
					selected = candidate;
					g_log("OpenXR swapchain format selected: DXGI=%lld", static_cast<long long>(selected));
					return true;
				}
			}
			g_log("No supported RGBA8 OpenXR swapchain format was found");
			return false;
		}

		bool create_eye_swapchains()
		{
			uint32_t view_count = 0;
			XrResult result = p_xrEnumerateViewConfigurationViews(
				g_instance,
				g_system_id,
				XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
				0,
				&view_count,
				nullptr);
			if (XR_FAILED(result) || view_count != eye_count)
				return false;

			XrViewConfigurationView config_views[eye_count] = {
				{ XR_TYPE_VIEW_CONFIGURATION_VIEW },
				{ XR_TYPE_VIEW_CONFIGURATION_VIEW }
			};
			result = p_xrEnumerateViewConfigurationViews(
				g_instance,
				g_system_id,
				XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
				eye_count,
				&view_count,
				config_views);
			if (XR_FAILED(result))
				return false;

			int64_t format = 0;
			if (!choose_swapchain_format(format))
				return false;

			for (uint32_t eye_index = 0; eye_index < eye_count; ++eye_index)
			{
				EyeSwapchain &eye = g_eyes[eye_index];
				const uint32_t recommended_width = config_views[eye_index].recommendedImageRectWidth;
				const uint32_t recommended_height = config_views[eye_index].recommendedImageRectHeight;
				eye.width = (static_cast<uint32_t>(recommended_width * eye_render_scale + 0.5f)) & ~1u;
				eye.height = (static_cast<uint32_t>(recommended_height * eye_render_scale + 0.5f)) & ~1u;
				eye.format = static_cast<DXGI_FORMAT>(format);
				g_log("Eye render target: eye=%u recommended=%ux%u scale=%.2f target=%ux%u",
					eye_index, recommended_width, recommended_height, eye_render_scale, eye.width, eye.height);

				XrSwapchainCreateInfo info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
				info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
				info.format = format;
				info.sampleCount = 1;
				info.width = eye.width;
				info.height = eye.height;
				info.faceCount = 1;
				info.arraySize = 1;
				info.mipCount = 1;
				result = p_xrCreateSwapchain(g_session, &info, &eye.handle);
				if (XR_FAILED(result))
				{
					g_log("xrCreateSwapchain eye=%u failed: result=%d", eye_index, static_cast<int>(result));
					return false;
				}

				result = p_xrEnumerateSwapchainImages(eye.handle, 0, &eye.image_count, nullptr);
				if (XR_FAILED(result) || eye.image_count == 0 || eye.image_count > max_swapchain_images)
				{
					g_log("OpenXR swapchain image count invalid: eye=%u result=%d count=%u", eye_index, static_cast<int>(result), eye.image_count);
					return false;
				}
				for (uint32_t i = 0; i < eye.image_count; ++i)
					eye.images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
				result = p_xrEnumerateSwapchainImages(
					eye.handle,
					eye.image_count,
					&eye.image_count,
					reinterpret_cast<XrSwapchainImageBaseHeader *>(eye.images));
				if (XR_FAILED(result))
					return false;

				for (uint32_t i = 0; i < eye.image_count; ++i)
				{
					D3D11_TEXTURE2D_DESC texture_desc = {};
					eye.images[i].texture->GetDesc(&texture_desc);
					if (i == 0)
						g_log("Swapchain texture: eye=%u size=%ux%u format=%u array=%u samples=%u bind=0x%x misc=0x%x",
							eye_index,
							texture_desc.Width,
							texture_desc.Height,
							static_cast<unsigned int>(texture_desc.Format),
							texture_desc.ArraySize,
							texture_desc.SampleDesc.Count,
							static_cast<unsigned int>(texture_desc.BindFlags),
							static_cast<unsigned int>(texture_desc.MiscFlags));

					D3D11_RENDER_TARGET_VIEW_DESC view_desc = {};
					// Scrap Mechanic's final backbuffer is already display-gamma encoded. Rendering
					// it through an sRGB RTV would encode it a second time and wash out the image.
					// The OpenXR swapchain remains declared sRGB for compositor interpretation;
					// use its typeless texture's linear-compatible RTV for bit-identical output.
					view_desc.Format = eye.format;
					if (eye.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
						view_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
					else if (eye.format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
						view_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
					if (texture_desc.ArraySize > 1)
					{
						view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
						view_desc.Texture2DArray.MipSlice = 0;
						view_desc.Texture2DArray.FirstArraySlice = 0;
						view_desc.Texture2DArray.ArraySize = 1;
					}
					else
					{
						view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
						view_desc.Texture2D.MipSlice = 0;
					}
					const HRESULT hr = g_device->CreateRenderTargetView(eye.images[i].texture, &view_desc, &eye.render_targets[i]);
					if (FAILED(hr))
					{
						g_log("CreateRenderTargetView failed: eye=%u image=%u hr=0x%08x", eye_index, i, static_cast<unsigned int>(hr));
						return false;
					}
					D3D11_SHADER_RESOURCE_VIEW_DESC shader_desc = {};
					shader_desc.Format = view_desc.Format;
					if (texture_desc.ArraySize > 1)
					{
						shader_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
						shader_desc.Texture2DArray.MostDetailedMip = 0;
						shader_desc.Texture2DArray.MipLevels = 1;
						shader_desc.Texture2DArray.FirstArraySlice = 0;
						shader_desc.Texture2DArray.ArraySize = 1;
					}
					else
					{
						shader_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
						shader_desc.Texture2D.MostDetailedMip = 0;
						shader_desc.Texture2D.MipLevels = 1;
					}
					const HRESULT shader_hr = g_device->CreateShaderResourceView(
						eye.images[i].texture, &shader_desc, &eye.shader_resources[i]);
					if (FAILED(shader_hr))
					{
						g_log("CreateShaderResourceView failed: eye=%u image=%u hr=0x%08x",
							eye_index, i, static_cast<unsigned int>(shader_hr));
						return false;
					}
				}
				g_log("Eye swapchain created: eye=%u size=%ux%u images=%u RTV_DXGI=%u",
					eye_index, eye.width, eye.height, eye.image_count, static_cast<unsigned int>(
						eye.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM :
						eye.format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ? DXGI_FORMAT_B8G8R8A8_UNORM : eye.format));
			}
			return true;
		}

		bool create_ui_swapchain()
		{
			int64_t format = 0;
			if (!choose_swapchain_format(format))
				return false;
			g_ui.format = static_cast<DXGI_FORMAT>(format);
			XrSwapchainCreateInfo info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
			info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
			info.format = format;
			info.sampleCount = 1;
			info.width = ui_width;
			info.height = ui_height;
			info.faceCount = 1;
			info.arraySize = 1;
			info.mipCount = 1;
			XrResult result = p_xrCreateSwapchain(g_session, &info, &g_ui.handle);
			if (XR_FAILED(result))
			{
				g_log("xrCreateSwapchain floating UI failed: result=%d", static_cast<int>(result));
				return false;
			}
			result = p_xrEnumerateSwapchainImages(g_ui.handle, 0, &g_ui.image_count, nullptr);
			if (XR_FAILED(result) || g_ui.image_count == 0 || g_ui.image_count > max_swapchain_images)
				return false;
			for (uint32_t i = 0; i < g_ui.image_count; ++i)
				g_ui.images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
			result = p_xrEnumerateSwapchainImages(
				g_ui.handle, g_ui.image_count, &g_ui.image_count,
				reinterpret_cast<XrSwapchainImageBaseHeader *>(g_ui.images));
			if (XR_FAILED(result))
				return false;
			for (uint32_t i = 0; i < g_ui.image_count; ++i)
			{
				D3D11_TEXTURE2D_DESC texture_desc = {};
				g_ui.images[i].texture->GetDesc(&texture_desc);
				D3D11_RENDER_TARGET_VIEW_DESC view_desc = {};
				view_desc.Format = g_ui.format;
				if (g_ui.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
					view_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				else if (g_ui.format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
					view_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				view_desc.Texture2D.MipSlice = 0;
				if (FAILED(g_device->CreateRenderTargetView(
					g_ui.images[i].texture, &view_desc, &g_ui.render_targets[i])))
					return false;
			}
			g_log("Floating UI swapchain created: %ux%u images=%u width=%.2fm distance=%.2fm",
				ui_width, ui_height, g_ui.image_count, ui_world_width, ui_distance);
			return true;
		}

		bool path(const char *text, XrPath &value)
		{
			const XrResult result = p_xrStringToPath(g_instance, text, &value);
			if (XR_FAILED(result))
				g_log("xrStringToPath failed: %s result=%d", text, static_cast<int>(result));
			return XR_SUCCEEDED(result);
		}

		bool create_actions()
		{
			if (!path("/user/hand/left", g_hand_paths[0]) || !path("/user/hand/right", g_hand_paths[1]))
				return false;

			XrActionSetCreateInfo set_info = { XR_TYPE_ACTION_SET_CREATE_INFO };
			std::strcpy(set_info.actionSetName, "gameplay");
			std::strcpy(set_info.localizedActionSetName, "Scrap Mechanic VR Gameplay");
			set_info.priority = 0;
			XrResult result = p_xrCreateActionSet(g_instance, &set_info, &g_action_set);
			if (XR_FAILED(result))
			{
				g_log("xrCreateActionSet failed: result=%d", static_cast<int>(result));
				return false;
			}

			XrActionCreateInfo pose_info = { XR_TYPE_ACTION_CREATE_INFO };
			pose_info.actionType = XR_ACTION_TYPE_POSE_INPUT;
			std::strcpy(pose_info.actionName, "hand_grip_pose");
			std::strcpy(pose_info.localizedActionName, "Hand Grip Pose");
			pose_info.countSubactionPaths = eye_count;
			pose_info.subactionPaths = g_hand_paths;
			result = p_xrCreateAction(g_action_set, &pose_info, &g_grip_pose_action);
			if (XR_FAILED(result))
				return false;

			XrActionCreateInfo trigger_info = { XR_TYPE_ACTION_CREATE_INFO };
			trigger_info.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
			std::strcpy(trigger_info.actionName, "trigger_value");
			std::strcpy(trigger_info.localizedActionName, "Index Trigger");
			trigger_info.countSubactionPaths = eye_count;
			trigger_info.subactionPaths = g_hand_paths;
			result = p_xrCreateAction(g_action_set, &trigger_info, &g_trigger_action);
			if (XR_FAILED(result))
				return false;

			auto create_hand_action = [&](XrActionType type, const char *name, const char *localized, XrAction &action)
			{
				XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
				info.actionType = type;
				std::strcpy(info.actionName, name);
				std::strcpy(info.localizedActionName, localized);
				info.countSubactionPaths = eye_count;
				info.subactionPaths = g_hand_paths;
				return XR_SUCCEEDED(p_xrCreateAction(g_action_set, &info, &action));
			};
			if (!create_hand_action(XR_ACTION_TYPE_VECTOR2F_INPUT, "move_turn", "Movement and Turn", g_thumbstick_action) ||
				!create_hand_action(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze_value", "Grip Squeeze", g_squeeze_action) ||
				!create_hand_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "primary_button", "Primary Button", g_primary_button_action) ||
				!create_hand_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary_button", "Secondary Button", g_secondary_button_action) ||
				!create_hand_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click", "Thumbstick Click", g_stick_click_action))
				return false;
			XrActionCreateInfo menu_info = { XR_TYPE_ACTION_CREATE_INFO };
			menu_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			std::strcpy(menu_info.actionName, "menu_button");
			std::strcpy(menu_info.localizedActionName, "Menu Button");
			menu_info.countSubactionPaths = 1;
			menu_info.subactionPaths = &g_hand_paths[0];
			if (XR_FAILED(p_xrCreateAction(g_action_set, &menu_info, &g_menu_button_action)))
				return false;

			XrPath profile = XR_NULL_PATH;
			XrPath binding_paths[15] = {};
			if (!path("/interaction_profiles/oculus/touch_controller", profile) ||
				!path("/user/hand/left/input/grip/pose", binding_paths[0]) ||
				!path("/user/hand/right/input/grip/pose", binding_paths[1]) ||
				!path("/user/hand/left/input/trigger/value", binding_paths[2]) ||
				!path("/user/hand/right/input/trigger/value", binding_paths[3]) ||
				!path("/user/hand/left/input/thumbstick", binding_paths[4]) ||
				!path("/user/hand/right/input/thumbstick", binding_paths[5]) ||
				!path("/user/hand/left/input/squeeze/value", binding_paths[6]) ||
				!path("/user/hand/right/input/squeeze/value", binding_paths[7]) ||
				!path("/user/hand/left/input/x/click", binding_paths[8]) ||
				!path("/user/hand/right/input/a/click", binding_paths[9]) ||
				!path("/user/hand/left/input/y/click", binding_paths[10]) ||
				!path("/user/hand/right/input/b/click", binding_paths[11]) ||
				!path("/user/hand/left/input/thumbstick/click", binding_paths[12]) ||
				!path("/user/hand/right/input/thumbstick/click", binding_paths[13]) ||
				!path("/user/hand/left/input/menu/click", binding_paths[14]))
				return false;

			XrActionSuggestedBinding bindings[15] = {
				{ g_grip_pose_action, binding_paths[0] },
				{ g_grip_pose_action, binding_paths[1] },
				{ g_trigger_action, binding_paths[2] },
				{ g_trigger_action, binding_paths[3] },
				{ g_thumbstick_action, binding_paths[4] },
				{ g_thumbstick_action, binding_paths[5] },
				{ g_squeeze_action, binding_paths[6] },
				{ g_squeeze_action, binding_paths[7] },
				{ g_primary_button_action, binding_paths[8] },
				{ g_primary_button_action, binding_paths[9] },
				{ g_secondary_button_action, binding_paths[10] },
				{ g_secondary_button_action, binding_paths[11] },
				{ g_stick_click_action, binding_paths[12] },
				{ g_stick_click_action, binding_paths[13] },
				{ g_menu_button_action, binding_paths[14] }
			};
			XrInteractionProfileSuggestedBinding suggested = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
			suggested.interactionProfile = profile;
			suggested.countSuggestedBindings = 15;
			suggested.suggestedBindings = bindings;
			result = p_xrSuggestInteractionProfileBindings(g_instance, &suggested);
			if (XR_FAILED(result))
			{
				g_log("Touch binding suggestion failed: result=%d", static_cast<int>(result));
				return false;
			}

			XrSessionActionSetsAttachInfo attach = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
			attach.countActionSets = 1;
			attach.actionSets = &g_action_set;
			result = p_xrAttachSessionActionSets(g_session, &attach);
			if (XR_FAILED(result))
				return false;

			for (uint32_t i = 0; i < eye_count; ++i)
			{
				XrActionSpaceCreateInfo space_info = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
				space_info.action = g_grip_pose_action;
				space_info.subactionPath = g_hand_paths[i];
				space_info.poseInActionSpace.orientation.w = 1.0f;
				result = p_xrCreateActionSpace(g_session, &space_info, &g_hand_spaces[i]);
				if (XR_FAILED(result))
					return false;
			}
			g_log("Touch action set attached: poses, sticks, triggers, grips, face buttons, and menu");
			return true;
		}

		void poll_events()
		{
			for (;;)
			{
				XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };
				const XrResult result = p_xrPollEvent(g_instance, &event);
				if (result == XR_EVENT_UNAVAILABLE)
					break;
				if (XR_FAILED(result))
				break;

				if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
				{
					const auto &changed = *reinterpret_cast<const XrEventDataSessionStateChanged *>(&event);
					g_session_state = changed.state;
					g_log("OpenXR session state changed: %d", static_cast<int>(g_session_state));
					if (g_session_state != XR_SESSION_STATE_FOCUSED)
						release_injected_input();
					else
						focus_game_window();
					if (g_session_state == XR_SESSION_STATE_READY && !g_session_running)
					{
						XrSessionBeginInfo begin_info = { XR_TYPE_SESSION_BEGIN_INFO };
						begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
						const XrResult begin_result = p_xrBeginSession(g_session, &begin_info);
						if (XR_SUCCEEDED(begin_result))
						{
							g_session_running = true;
							g_locomotion_reference_valid = false;
							g_log("OpenXR session begun");
						}
						else
							g_log("xrBeginSession failed: result=%d", static_cast<int>(begin_result));
					}
					else if (g_session_state == XR_SESSION_STATE_STOPPING && g_session_running)
					{
						p_xrEndSession(g_session);
						g_session_running = false;
						g_log("OpenXR session ended");
					}
					else if (g_session_state == XR_SESSION_STATE_EXITING ||
						g_session_state == XR_SESSION_STATE_LOSS_PENDING)
						g_session_running = false;
				}
				else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
				{
					g_session_running = false;
					g_log("OpenXR instance loss pending");
				}
			}
		}

		void update_hands(XrTime display_time)
		{
			bool controller_pose_active[eye_count] = { false, false };
			for (uint32_t hand = 0; hand < eye_count; ++hand)
				hands::set_pose(hand, {}, false, false);
			XrActiveActionSet active = { g_action_set, XR_NULL_PATH };
			XrActionsSyncInfo sync = { XR_TYPE_ACTIONS_SYNC_INFO };
			sync.countActiveActionSets = 1;
			sync.activeActionSets = &active;
			if (XR_FAILED(p_xrSyncActions(g_session, &sync)))
				return;

			for (uint32_t i = 0; i < eye_count; ++i)
			{
				XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
				get_info.action = g_grip_pose_action;
				get_info.subactionPath = g_hand_paths[i];
				XrActionStatePose pose_state = { XR_TYPE_ACTION_STATE_POSE };
				if (XR_FAILED(p_xrGetActionStatePose(g_session, &get_info, &pose_state)) || !pose_state.isActive)
					continue;

				XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
				const XrResult result = p_xrLocateSpace(g_hand_spaces[i], g_local_space, display_time, &location);
				const XrSpaceLocationFlags required =
					XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
				if (XR_SUCCEEDED(result) && (location.locationFlags & required) == required)
				{
					hands::set_pose(i, location.pose, true, false);
					controller_pose_active[i] = true;
					if (!g_hand_pose_logged[i])
					{
						g_log("TRACKED TOUCH %s: position=(%.3f,%.3f,%.3f) orientation=(%.3f,%.3f,%.3f,%.3f)",
							i == 0 ? "LEFT" : "RIGHT",
							location.pose.position.x,
							location.pose.position.y,
							location.pose.position.z,
							location.pose.orientation.x,
							location.pose.orientation.y,
							location.pose.orientation.z,
							location.pose.orientation.w);
						g_hand_pose_logged[i] = true;
					}
				}
			}

			if (g_session_state != XR_SESSION_STATE_FOCUSED)
			{
				release_injected_input();
				return;
			}
			auto get_vector = [&](XrAction action, uint32_t hand)
			{
				XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
				info.action = action;
				info.subactionPath = g_hand_paths[hand];
				XrActionStateVector2f state = { XR_TYPE_ACTION_STATE_VECTOR2F };
				p_xrGetActionStateVector2f(g_session, &info, &state);
				return state.isActive ? state.currentState : XrVector2f{ 0.0f, 0.0f };
			};
			auto get_float = [&](XrAction action, uint32_t hand, bool *active = nullptr)
			{
				XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
				info.action = action;
				info.subactionPath = g_hand_paths[hand];
				XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };
				p_xrGetActionStateFloat(g_session, &info, &state);
				if (active != nullptr)
					*active = state.isActive == XR_TRUE;
				return state.isActive ? state.currentState : 0.0f;
			};
			auto get_boolean = [&](XrAction action, uint32_t hand)
			{
				XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
				info.action = action;
				info.subactionPath = g_hand_paths[hand];
				XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
				p_xrGetActionStateBoolean(g_session, &info, &state);
				return state.isActive && state.currentState == XR_TRUE;
			};

			float optical_curls[eye_count][5] = {};
			float right_palm_speed = 0.0f;
			XrVector3f right_palm_direction = {};
			bool right_palm_velocity_valid = false;
			for (uint32_t hand = 0; hand < eye_count; ++hand)
			{
				g_optical_hand_active[hand] = false;
				if (g_optical_hand_trackers[hand] == XR_NULL_HANDLE ||
					p_xrLocateHandJointsEXT == nullptr)
					continue;
				XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT] = {};
				XrHandJointLocationsEXT locations = { XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
				locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
				locations.jointLocations = joints;
				XrHandJointsLocateInfoEXT locate = { XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
				locate.baseSpace = g_local_space;
				locate.time = display_time;
				if (XR_FAILED(p_xrLocateHandJointsEXT(
						g_optical_hand_trackers[hand], &locate, &locations)) ||
					locations.isActive != XR_TRUE)
					continue;
				const XrSpaceLocationFlags valid = XR_SPACE_LOCATION_POSITION_VALID_BIT;
				const XrSpaceLocationFlags pose_valid =
					XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
				const XrHandJointLocationEXT &palm = joints[XR_HAND_JOINT_PALM_EXT];
				const bool palm_pose_valid = (palm.locationFlags & pose_valid) == pose_valid;
				// Touch grip space and optical palm space have different origins. Never
				// alternate between them while a controller pose is available, because an
				// attached tool will visibly jump and rotate between coordinate frames.
				if (!controller_pose_active[hand] && palm_pose_valid)
					hands::set_pose(hand, palm.pose, true, true);
				const bool optical_active = !controller_pose_active[hand] && palm_pose_valid;
				const XrHandJointLocationEXT &thumb_tip = joints[XR_HAND_JOINT_THUMB_TIP_EXT];
				const XrHandJointLocationEXT &index_tip = joints[XR_HAND_JOINT_INDEX_TIP_EXT];
				if (optical_active && (thumb_tip.locationFlags & valid) != 0 &&
					(index_tip.locationFlags & valid) != 0)
				{
					const float pinch_x = thumb_tip.pose.position.x - index_tip.pose.position.x;
					const float pinch_y = thumb_tip.pose.position.y - index_tip.pose.position.y;
					const float pinch_z = thumb_tip.pose.position.z - index_tip.pose.position.z;
					const float pinch_distance = std::sqrt(
						pinch_x * pinch_x + pinch_y * pinch_y + pinch_z * pinch_z);
					// Match Quest's familiar pinch interaction while adding enough release
					// hysteresis to survive fingertip tracking noise during a held action.
					g_optical_pinch_down[hand] = g_optical_pinch_down[hand]
						? pinch_distance < 0.040f
						: pinch_distance < 0.025f;
				}
				else
					g_optical_pinch_down[hand] = false;
				if (hand == 1 && optical_active)
				{
					if (g_optical_right_palm_time != 0 && display_time > g_optical_right_palm_time)
					{
						const XrTime elapsed = display_time - g_optical_right_palm_time;
						if (elapsed >= 5000000 && elapsed <= 100000000)
						{
							const float seconds = static_cast<float>(elapsed) * 0.000000001f;
							right_palm_direction = {
								palm.pose.position.x - g_optical_right_palm_previous.x,
								palm.pose.position.y - g_optical_right_palm_previous.y,
								palm.pose.position.z - g_optical_right_palm_previous.z
							};
							const float distance = std::sqrt(
								right_palm_direction.x * right_palm_direction.x +
								right_palm_direction.y * right_palm_direction.y +
								right_palm_direction.z * right_palm_direction.z);
							if (distance > 0.001f)
							{
								right_palm_speed = distance / seconds;
								right_palm_direction.x /= distance;
								right_palm_direction.y /= distance;
								right_palm_direction.z /= distance;
								right_palm_velocity_valid = true;
							}
						}
					}
					g_optical_right_palm_previous = palm.pose.position;
					g_optical_right_palm_time = display_time;
				}
				auto bend_angle = [&](XrHandJointEXT a, XrHandJointEXT b, XrHandJointEXT c)
				{
					if ((joints[a].locationFlags & valid) == 0 || (joints[b].locationFlags & valid) == 0 ||
						(joints[c].locationFlags & valid) == 0)
						return 0.0f;
					XrVector3f first = {
						joints[b].pose.position.x - joints[a].pose.position.x,
						joints[b].pose.position.y - joints[a].pose.position.y,
						joints[b].pose.position.z - joints[a].pose.position.z };
					XrVector3f second = {
						joints[c].pose.position.x - joints[b].pose.position.x,
						joints[c].pose.position.y - joints[b].pose.position.y,
						joints[c].pose.position.z - joints[b].pose.position.z };
					const float first_length = std::sqrt(first.x * first.x + first.y * first.y + first.z * first.z);
					const float second_length = std::sqrt(second.x * second.x + second.y * second.y + second.z * second.z);
					if (first_length < 0.001f || second_length < 0.001f) return 0.0f;
					float cosine = (first.x * second.x + first.y * second.y + first.z * second.z) /
						(first_length * second_length);
					if (cosine < -1.0f) cosine = -1.0f; else if (cosine > 1.0f) cosine = 1.0f;
					return std::acos(cosine);
				};
				const XrHandJointEXT chains[5][5] = {
					{ XR_HAND_JOINT_THUMB_METACARPAL_EXT, XR_HAND_JOINT_THUMB_PROXIMAL_EXT, XR_HAND_JOINT_THUMB_DISTAL_EXT, XR_HAND_JOINT_THUMB_TIP_EXT, XR_HAND_JOINT_THUMB_TIP_EXT },
					{ XR_HAND_JOINT_INDEX_METACARPAL_EXT, XR_HAND_JOINT_INDEX_PROXIMAL_EXT, XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, XR_HAND_JOINT_INDEX_DISTAL_EXT, XR_HAND_JOINT_INDEX_TIP_EXT },
					{ XR_HAND_JOINT_MIDDLE_METACARPAL_EXT, XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, XR_HAND_JOINT_MIDDLE_DISTAL_EXT, XR_HAND_JOINT_MIDDLE_TIP_EXT },
					{ XR_HAND_JOINT_RING_METACARPAL_EXT, XR_HAND_JOINT_RING_PROXIMAL_EXT, XR_HAND_JOINT_RING_INTERMEDIATE_EXT, XR_HAND_JOINT_RING_DISTAL_EXT, XR_HAND_JOINT_RING_TIP_EXT },
					{ XR_HAND_JOINT_LITTLE_METACARPAL_EXT, XR_HAND_JOINT_LITTLE_PROXIMAL_EXT, XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, XR_HAND_JOINT_LITTLE_DISTAL_EXT, XR_HAND_JOINT_LITTLE_TIP_EXT }
				};
				for (uint32_t finger = 0; finger < 5; ++finger)
				{
					float curl = bend_angle(chains[finger][0], chains[finger][1], chains[finger][2]) +
						bend_angle(chains[finger][1], chains[finger][2], chains[finger][3]);
					if (finger != 0)
						curl += bend_angle(chains[finger][2], chains[finger][3], chains[finger][4]);
					curl /= finger == 0 ? 2.2f : 3.6f;
					curl = curl > 1.0f ? 1.0f : (curl < 0.0f ? 0.0f : curl);
					// Quest's thumb-chain angle increases in the mechanic glove's open
					// direction, opposite to the four finger curl convention.
					optical_curls[hand][finger] = finger == 0 ? 1.0f - curl : curl;
				}
				g_optical_hand_active[hand] = optical_active;
				if (!g_optical_hand_logged[hand])
				{
					g_optical_hand_logged[hand] = true;
					g_log("OPTICAL HAND ACTIVE: %s joints tracked; pinch tool input and physical hammer swing enabled",
						hand == 0 ? "left" : "right");
				}
			}
			if (!g_optical_hand_active[1])
			{
				g_optical_pinch_down[1] = false;
				g_optical_right_palm_time = 0;
			}
			const bool hammer_active = scrapvr::tools::is_hammer_active();
			if (g_optical_hand_active[1] && hammer_active && right_palm_velocity_valid)
			{
				if (right_palm_speed < 0.55f)
					g_optical_hammer_swing_armed = true;
				const bool cooldown_ready = g_last_optical_hammer_swing_time == 0 ||
					display_time - g_last_optical_hammer_swing_time >= 450000000;
				if (g_optical_hammer_swing_armed && cooldown_ready &&
					right_palm_speed >= 1.55f)
				{
					g_optical_hammer_swing_armed = false;
					g_last_optical_hammer_swing_time = display_time;
					g_optical_hammer_click_frames = 2;
					g_optical_hammer_swing_direction = right_palm_direction;
					++g_optical_hammer_swing_sequence;
					if (!g_optical_hammer_swing_logged && g_log != nullptr)
					{
						g_optical_hammer_swing_logged = true;
						g_log("OPTICAL HAMMER SWING ACTIVE: 1.55 m/s threshold, 0.55 m/s re-arm, 450 ms cooldown");
					}
				}
			}
			else
				g_optical_hammer_swing_armed = true;
			const bool optical_pinch_primary =
				g_optical_hand_active[1] && !hammer_active && g_optical_pinch_down[1];
			const bool optical_hammer_primary =
				g_optical_hand_active[1] && hammer_active && g_optical_hammer_click_frames > 0;
			const bool optical_primary = optical_pinch_primary || optical_hammer_primary;
			g_optical_gun_trigger = optical_pinch_primary;
			if (optical_pinch_primary && !g_optical_pinch_logged && g_log != nullptr)
			{
				g_optical_pinch_logged = true;
				g_log("OPTICAL PINCH PRIMARY ACTIVE: right index-thumb tap/hold drives non-hammer tools");
			}
			bool controller_trigger_raw[eye_count] = { false, false };
			for (uint32_t hand = 0; hand < eye_count; ++hand)
			{
				bool trigger_active = false;
				const float trigger = get_float(g_trigger_action, hand, &trigger_active);
				controller_trigger_raw[hand] = trigger_active && trigger > 0.55f;
				const ULONGLONG trigger_now = GetTickCount64();
				// Quest Link can intermittently report an inactive action while a Touch
				// trigger remains held. Treat those samples as brief dropouts and require
				// a stable threshold transition before publishing a game button edge.
				// This guarantees one down/up pair per physical squeeze instead of an
				// autoclick stream when OpenXR input availability flickers.
				if (trigger_active)
				{
					g_controller_trigger_last_active[hand] = trigger_now;
					const bool desired = g_controller_trigger_down[hand]
						? trigger > 0.30f
						: trigger > 0.62f;
					if (desired == g_controller_trigger_down[hand])
					{
						g_controller_trigger_candidate_down[hand] = desired;
						g_controller_trigger_candidate_since[hand] = trigger_now;
					}
					else if (desired != g_controller_trigger_candidate_down[hand])
					{
						g_controller_trigger_candidate_down[hand] = desired;
						g_controller_trigger_candidate_since[hand] = trigger_now;
					}
					else
					{
						const ULONGLONG debounce_ms = desired ? 18 : 75;
						if (trigger_now - g_controller_trigger_candidate_since[hand] >= debounce_ms)
						{
							g_controller_trigger_down[hand] = desired;
							g_controller_trigger_candidate_since[hand] = trigger_now;
						}
					}
				}
				else if (g_controller_trigger_down[hand] &&
					g_controller_trigger_last_active[hand] != 0 &&
					trigger_now - g_controller_trigger_last_active[hand] >= 250)
				{
					// Do not leave a click held if a controller really disconnects.
					g_controller_trigger_down[hand] = false;
					g_controller_trigger_candidate_down[hand] = false;
					g_controller_trigger_candidate_since[hand] = trigger_now;
				}
				if (g_optical_hand_active[hand])
					hands::set_finger_curls(hand, optical_curls[hand]);
				else
				{
					const float grip = get_float(g_squeeze_action, hand);
					const float controller_curls[5] = { 0.18f, trigger, grip, grip, grip };
					hands::set_finger_curls(hand, controller_curls);
				}
				hands::set_interaction(hand,
					(!g_optical_hand_active[hand] && controller_trigger_raw[hand]) ||
					(g_optical_hand_active[hand] && !hammer_active && g_optical_pinch_down[hand]));
				hands::set_firing(hand,
					hand == 1 && ((!g_optical_hand_active[hand] && controller_trigger_raw[hand]) || optical_primary));
			}
			update_ui_pointer_from_hand();

			const XrVector2f move = get_vector(g_thumbstick_action, 0);
			const XrVector2f turn = get_vector(g_thumbstick_action, 1);
			const float deadzone = 0.30f;
			const bool a_button = get_boolean(g_primary_button_action, 1);
			const bool x_button = get_boolean(g_primary_button_action, 0);
			const bool y_button = get_boolean(g_secondary_button_action, 0);
			if (!g_left_stick_observed &&
				(std::fabs(move.x) > deadzone || std::fabs(move.y) > deadzone))
			{
				g_left_stick_observed = true;
				g_log("TOUCH ACTION VERIFIED: left stick=(%.2f,%.2f)", move.x, move.y);
			}
			if (!g_a_button_observed && a_button)
			{
				g_a_button_observed = true;
				g_log("TOUCH ACTION VERIFIED: right A button active");
			}
			if (!g_y_button_observed && y_button)
			{
				g_y_button_observed = true;
				g_log("TOUCH ACTION VERIFIED: left Y button active");
			}
			const bool left_stick_click = get_boolean(g_stick_click_action, 0);
			const bool right_stick_click = get_boolean(g_stick_click_action, 1);
			const bool controller_recenter = left_stick_click && right_stick_click;
			const bool recenter_down = controller_recenter;
			const bool input_target_ready = game_has_foreground();
			if (!input_target_ready)
			{
				release_injected_input();
				if (!g_input_suspended_logged)
				{
					g_input_suspended_logged = true;
					g_log("VR INPUT SUSPENDED: another desktop application has focus; no controller keys, clicks, or aim will be injected");
				}
			}
			else
				g_input_suspended_logged = false;
			const ULONGLONG now = GetTickCount64();
			if (recenter_down)
			{
				if (g_recenter_hold_start == 0)
					g_recenter_hold_start = now;
				if (!g_recenter_latched && now - g_recenter_hold_start >= 1000)
				{
					g_recenter_latched = true;
					g_locomotion_reference_valid = false;
					InterlockedExchange(&g_recenter_request, 1);
					g_log("RECENTER REQUESTED: both thumbsticks held; yaw, roll, pitch, and tracking origin will recalibrate");
				}
			}
			else
			{
				g_recenter_hold_start = 0;
				g_recenter_latched = false;
			}

			const bool modal_ui = g_ui_visible;
			const bool player_seated = scrapvr::tools::is_player_seated();
			const bool player_first_person = scrapvr::tools::is_player_first_person();
			scrapvr::tools::set_render_suppressed(modal_ui);
			// Hold the normal camera-toggle key only until Lua confirms the seated
			// view is first person. set_key emits one edge, so this cannot cycle.
			set_key('V', !modal_ui && player_seated && !player_first_person, g_key_camera);
			set_key('X', !modal_ui && player_seated && x_button, g_key_zoom_in);
			set_key('C', !modal_ui && player_seated && y_button, g_key_zoom_out);
			if (!modal_ui && !player_seated)
			{
				if (x_button && y_button)
					g_xy_chord_latched = true;
				if (!x_button && g_x_was_down && !g_xy_chord_latched)
					send_mouse_wheel(WHEEL_DELTA);
				if (!y_button && g_y_was_down && !g_xy_chord_latched)
					send_mouse_wheel(-WHEEL_DELTA);
				set_key('I', x_button && y_button, g_key_inventory);
			}
			else
			{
				g_xy_chord_latched = false;
				set_key('I', false, g_key_inventory);
			}
			g_x_was_down = x_button;
			g_y_was_down = y_button;
			if (!x_button && !y_button)
				g_xy_chord_latched = false;

			const XrVector2f stable_move = hmd_relative_movement(move);
			set_key('W', !modal_ui && stable_move.y > deadzone, g_key_forward);
			set_key('S', !modal_ui && stable_move.y < -deadzone, g_key_backward);
			set_key('A', !modal_ui && stable_move.x < -deadzone, g_key_left);
			set_key('D', !modal_ui && stable_move.x > deadzone, g_key_right);
			set_key(VK_SHIFT, !modal_ui && left_stick_click && !controller_recenter, g_key_sprint);
			set_key(VK_CONTROL, !modal_ui && right_stick_click && !controller_recenter, g_key_crawl);
			set_key(VK_SPACE, !modal_ui && a_button, g_key_jump);
			set_key('E',
				!modal_ui && get_boolean(g_secondary_button_action, 1),
				g_key_use);
			XrActionStateGetInfo menu_get = { XR_TYPE_ACTION_STATE_GET_INFO };
			menu_get.action = g_menu_button_action;
			menu_get.subactionPath = g_hand_paths[0];
			XrActionStateBoolean menu_state = { XR_TYPE_ACTION_STATE_BOOLEAN };
			p_xrGetActionStateBoolean(g_session, &menu_get, &menu_state);
			const bool menu_down = menu_state.isActive && menu_state.currentState == XR_TRUE;
			set_key(VK_ESCAPE, menu_down, g_key_menu);
			const bool menu_physical_click = g_controller_trigger_down[1] ||
				(g_optical_hand_active[1] && g_optical_pinch_down[1]) || a_button;
			if (!menu_physical_click)
				g_ui_drag_active = false;
			const bool menu_click = modal_ui && menu_physical_click &&
				(g_ui_pointer_active || g_ui_drag_active);
			if (menu_click)
				g_ui_drag_active = true;
			set_primary_mouse(
				modal_ui ? menu_click : (controller_trigger_raw[1] || optical_primary),
				modal_ui);
			set_mouse_button(
				MOUSEEVENTF_RIGHTDOWN,
				MOUSEEVENTF_RIGHTUP,
				!modal_ui && controller_trigger_raw[0],
				g_mouse_create);
			// Keep the pulse down for two OpenXR frames so the game observes one
			// start transition, then release it until the physical swing re-arms.
			if (g_optical_hammer_click_frames > 0)
				--g_optical_hammer_click_frames;

			if (input_target_ready && !modal_ui &&
				(std::fabs(turn.x) > deadzone || std::fabs(turn.y) > deadzone))
			{
				float horizontal_turn = std::fabs(turn.x) > deadzone ? turn.x : 0.0f;
				float vertical_turn = std::fabs(turn.y) > deadzone ? turn.y : 0.0f;
				// A predominantly horizontal stick command must produce pure yaw.
				// This prevents minor Y-axis noise from turning a level rotation into
				// a diagonal climb while preserving deliberate vertical look input.
				if (std::fabs(horizontal_turn) >= std::fabs(vertical_turn))
					vertical_turn = 0.0f;
				INPUT mouse = {};
				mouse.type = INPUT_MOUSE;
				mouse.mi.dx = static_cast<LONG>(
					horizontal_turn * std::fabs(horizontal_turn) * 36.0f);
				mouse.mi.dy = static_cast<LONG>(
					-vertical_turn * std::fabs(vertical_turn) * 28.0f);
				mouse.mi.dwFlags = MOUSEEVENTF_MOVE;
				SendInput(1, &mouse, sizeof(mouse));
			}
			else if (input_target_ready && modal_ui && std::fabs(turn.y) > 0.55f &&
				now - g_ui_scroll_last >= 120)
			{
				g_ui_scroll_last = now;
				send_mouse_wheel(turn.y > 0.0f ? WHEEL_DELTA : -WHEEL_DELTA);
			}
			if (!g_touch_input_logged)
			{
				g_touch_input_logged = true;
				g_log("TOUCH INPUT ACTIVE: X/Y=hotbar or seated X/C zoom, X+Y=inventory while standing, A/trigger/pinch=menu click-drag, right-stick Y=menu scroll, left menu=Escape");
			}
		}

		void release_acquired_eyes()
		{
			for (uint32_t i = 0; i < eye_count; ++i)
			{
				if (!g_eye_acquired[i])
					continue;
				XrSwapchainImageReleaseInfo release = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
				p_xrReleaseSwapchainImage(g_eyes[i].handle, &release);
				g_eye_acquired[i] = false;
			}
		}

		DXGI_FORMAT capture_texture_format(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
				return DXGI_FORMAT_R8G8B8A8_TYPELESS;
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
				return DXGI_FORMAT_B8G8R8A8_TYPELESS;
			default:
				return format;
			}
		}

		DXGI_FORMAT capture_view_format(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
				return DXGI_FORMAT_B8G8R8A8_UNORM;
			default:
				return format;
			}
		}

		bool capture_backbuffer(
			ID3D11Texture2D *&destination,
			ID3D11ShaderResourceView *&destination_view)
		{
			if (g_game_swapchain == nullptr || g_device == nullptr || g_context == nullptr)
				return false;
			ID3D11Texture2D *backbuffer = nullptr;
			if (FAILED(g_game_swapchain->GetBuffer(
				0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backbuffer))) ||
				backbuffer == nullptr)
				return false;
			D3D11_TEXTURE2D_DESC source_desc = {};
			backbuffer->GetDesc(&source_desc);
			if (source_desc.SampleDesc.Count != 1 || source_desc.ArraySize != 1)
			{
				backbuffer->Release();
				return false;
			}
			const DXGI_FORMAT texture_format = capture_texture_format(source_desc.Format);
			bool recreate = destination == nullptr || destination_view == nullptr;
			if (!recreate)
			{
				D3D11_TEXTURE2D_DESC saved_desc = {};
				destination->GetDesc(&saved_desc);
				recreate = saved_desc.Width != source_desc.Width ||
					saved_desc.Height != source_desc.Height || saved_desc.Format != texture_format;
			}
			if (recreate)
			{
				if (destination_view != nullptr) destination_view->Release();
				if (destination != nullptr) destination->Release();
				destination_view = nullptr;
				destination = nullptr;
				D3D11_TEXTURE2D_DESC saved_desc = source_desc;
				saved_desc.Format = texture_format;
				saved_desc.MipLevels = 1;
				saved_desc.Usage = D3D11_USAGE_DEFAULT;
				saved_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				saved_desc.CPUAccessFlags = 0;
				saved_desc.MiscFlags = 0;
				if (FAILED(g_device->CreateTexture2D(&saved_desc, nullptr, &destination)))
				{
					backbuffer->Release();
					return false;
				}
				D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
				view_desc.Format = capture_view_format(source_desc.Format);
				view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				view_desc.Texture2D.MostDetailedMip = 0;
				view_desc.Texture2D.MipLevels = 1;
				if (FAILED(g_device->CreateShaderResourceView(
					destination, &view_desc, &destination_view)))
				{
					destination->Release();
					destination = nullptr;
					backbuffer->Release();
					return false;
				}
			}
			g_context->CopyResource(destination, backbuffer);
			backbuffer->Release();
			return true;
		}

		bool modal_cursor_visible()
		{
			CURSORINFO cursor = { sizeof(CURSORINFO) };
			return game_has_foreground() && GetCursorInfo(&cursor) &&
				(cursor.flags & CURSOR_SHOWING) != 0;
		}

		void anchor_ui_panel()
		{
			g_ui_pose.orientation = g_views[0].pose.orientation;
			const XrVector3f head = {
				(g_views[0].pose.position.x + g_views[1].pose.position.x) * 0.5f,
				(g_views[0].pose.position.y + g_views[1].pose.position.y) * 0.5f,
				(g_views[0].pose.position.z + g_views[1].pose.position.z) * 0.5f
			};
			const XrVector3f offset = rotate_vector(
				g_ui_pose.orientation, { 0.0f, 0.0f, -ui_distance });
			g_ui_pose.position = {
				head.x + offset.x,
				head.y + offset.y,
				head.z + offset.z
			};
		}

		bool prepare_ui_layer()
		{
			g_ui_layer_ready = false;
			if (g_desktop_frame_view == nullptr || !capture_backbuffer(
				g_present_frame, g_present_frame_view))
			{
				restore_desktop_frame();
				return false;
			}
			if (!restore_desktop_frame() || !initialize_ui_renderer())
				return false;

			auto draw_ui = [&](ID3D11RenderTargetView *target, uint32_t width, uint32_t height,
				ID3D11VertexShader *vertex_shader, uint32_t vertex_count, const UiConstants &constants)
			{
				if (target == nullptr || width == 0 || height == 0)
					return;
				g_context->OMSetRenderTargets(1, &target, nullptr);
			const float blend_factor[4] = { 0, 0, 0, 0 };
			g_context->OMSetBlendState(g_ui_blend, blend_factor, 0xffffffff);
			g_context->OMSetDepthStencilState(nullptr, 0);
			D3D11_VIEWPORT viewport = {};
			viewport.Width = static_cast<float>(width);
			viewport.Height = static_cast<float>(height);
			viewport.MaxDepth = 1.0f;
			g_context->RSSetViewports(1, &viewport);
			g_context->RSSetState(g_ui_rasterizer);
			g_context->IASetInputLayout(nullptr);
			g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ID3D11Buffer *no_vertex_buffer = nullptr;
			const UINT zero = 0;
			g_context->IASetVertexBuffers(0, 1, &no_vertex_buffer, &zero, &zero);
			g_context->UpdateSubresource(g_ui_constants, 0, nullptr, &constants, 0, 0);
			g_context->VSSetShader(vertex_shader, nullptr, 0);
			g_context->VSSetConstantBuffers(0, 1, &g_ui_constants);
			g_context->PSSetShader(g_ui_pixel_shader, nullptr, 0);
			g_context->PSSetShaderResources(0, 1, &g_present_frame_view);
			g_context->PSSetSamplers(0, 1, &g_ui_sampler);
			g_context->Draw(vertex_count, 0);
			ID3D11ShaderResourceView *no_source = nullptr;
			g_context->PSSetShaderResources(0, 1, &no_source);
				g_context->OMSetRenderTargets(0, nullptr, nullptr);
			};
			auto draw_pointer_laser = [&](ID3D11RenderTargetView *target, uint32_t width,
				uint32_t height, uint32_t eye)
			{
				if (!g_ui_pointer_active || target == nullptr || width == 0 || height == 0)
					return;
				UiConstants constants = {};
				constants.transform = ui_multiply(
					ui_projection(g_views[eye].fov), ui_inverse_pose(g_views[eye].pose));
				constants.laser_start[0] = g_ui_pointer_origin.x;
				constants.laser_start[1] = g_ui_pointer_origin.y;
				constants.laser_start[2] = g_ui_pointer_origin.z;
				constants.laser_start[3] = 1.0f;
				constants.laser_end[0] = g_ui_pointer_world.x;
				constants.laser_end[1] = g_ui_pointer_world.y;
				constants.laser_end[2] = g_ui_pointer_world.z;
				constants.laser_end[3] = 1.0f;

				g_context->OMSetRenderTargets(1, &target, nullptr);
				const float blend_factor[4] = { 0, 0, 0, 0 };
				g_context->OMSetBlendState(g_ui_blend, blend_factor, 0xffffffff);
				g_context->OMSetDepthStencilState(nullptr, 0);
				D3D11_VIEWPORT viewport = {};
				viewport.Width = static_cast<float>(width);
				viewport.Height = static_cast<float>(height);
				viewport.MaxDepth = 1.0f;
				g_context->RSSetViewports(1, &viewport);
				g_context->RSSetState(g_ui_rasterizer);
				g_context->IASetInputLayout(nullptr);
				g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
				ID3D11Buffer *no_vertex_buffer = nullptr;
				const UINT zero = 0;
				g_context->IASetVertexBuffers(0, 1, &no_vertex_buffer, &zero, &zero);
				g_context->UpdateSubresource(g_ui_constants, 0, nullptr, &constants, 0, 0);
				g_context->VSSetShader(g_ui_laser_vertex_shader, nullptr, 0);
				g_context->VSSetConstantBuffers(0, 1, &g_ui_constants);
				g_context->PSSetShader(g_ui_laser_pixel_shader, nullptr, 0);
				g_context->Draw(2, 0);
				g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				g_context->OMSetRenderTargets(0, nullptr, nullptr);
			};

			// Rebuild the monitor exactly from its preserved eye mirror plus the true
			// transparent GUI-only frame. This keeps the desktop experience unchanged.
			ID3D11Texture2D *backbuffer = nullptr;
			if (SUCCEEDED(g_game_swapchain->GetBuffer(
				0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backbuffer))) &&
				backbuffer != nullptr)
			{
				D3D11_TEXTURE2D_DESC description = {};
				backbuffer->GetDesc(&description);
				ID3D11RenderTargetView *target = nullptr;
				if (SUCCEEDED(g_device->CreateRenderTargetView(backbuffer, nullptr, &target)))
				{
					UiConstants desktop_constants = {};
					desktop_constants.transform = ui_identity();
					draw_ui(target, description.Width, description.Height,
						g_ui_vertex_shader, 3, desktop_constants);
					target->Release();
				}
				backbuffer->Release();
			}

			g_ui_visible = modal_cursor_visible();
			scrapvr::tools::set_render_suppressed(g_ui_visible);
			if (!g_ui_visible)
			{
				// Closing or hiding the panel must release only the private UI
				// button. Releasing the global gameplay button here runs once per
				// Present and turns a held tool trigger into a frame-rate autoclicker.
				release_ui_engine_mouse_button();
				g_ui_was_visible = false;
				g_ui_pointer_active = false;
				g_ui_pointer_client_initialized = false;
				g_ui_drag_active = false;
				return false;
			}
			if (!g_ui_was_visible)
				anchor_ui_panel();
			g_ui_was_visible = true;

			const UiMatrix panel_model = ui_panel_model();
			for (uint32_t eye = 0; eye < eye_count; ++eye)
			{
				if (!g_eye_acquired[eye])
					continue;
				UiConstants vr_constants = {};
				vr_constants.transform = ui_multiply(
					ui_projection(g_views[eye].fov),
					ui_multiply(ui_inverse_pose(g_views[eye].pose), panel_model));
				vr_constants.pointer[0] = g_ui_pointer_u;
				vr_constants.pointer[1] = g_ui_pointer_v;
				vr_constants.pointer[2] = g_ui_pointer_active ? 1.0f : 0.0f;
				vr_constants.pointer[3] = 1.0f;
				ID3D11RenderTargetView *eye_target =
					g_eyes[eye].render_targets[g_acquired_images[eye]];
				draw_ui(eye_target, g_eyes[eye].width, g_eyes[eye].height,
					g_ui_quad_vertex_shader, 6, vr_constants);
				draw_pointer_laser(eye_target, g_eyes[eye].width, g_eyes[eye].height, eye);
				// The compositor panel is deliberately drawn before a second hands-only
				// pass so tracked gloves naturally remain in front of the panel and pointer.
				g_context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
				hands::render(g_context, eye_target, g_eyes[eye].width,
					g_eyes[eye].height, g_views[eye]);
			}
			g_context->Flush();
			if (!g_ui_logged)
			{
				g_ui_logged = true;
				g_log("VR UI-ONLY PANEL ACTIVE: transparent native GUI is rendered into both eyes at 0.55 m, permanent HUD bands are cropped, and tracked hands are redrawn in front");
			}
			return true;
		}

		void end_empty_frame()
		{
			XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
			end_info.displayTime = g_frame_display_time;
			end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
			end_info.layerCount = 0;
			end_info.layers = nullptr;
			p_xrEndFrame(g_session, &end_info);
			g_frame_active = false;
		}
	}

	bool initialize_session(
		ID3D11Device *device,
		XrInstance instance,
		XrSystemId system_id,
		PFN_xrGetInstanceProcAddr get_instance_proc_addr,
		LogFunction log,
		bool hand_tracking_enabled)
	{
		if (g_initialized)
			return true;
		if (device == nullptr || instance == XR_NULL_HANDLE || system_id == XR_NULL_SYSTEM_ID || log == nullptr)
			return false;
		if (GetTickCount64() - g_last_init_attempt < 5000)
			return false;
		g_last_init_attempt = GetTickCount64();

		g_log = log;
		g_get_proc = get_instance_proc_addr;
		g_instance = instance;
		g_system_id = system_id;
		g_hand_tracking_enabled = hand_tracking_enabled;
		if (!resolve_session_functions())
			return false;

		XrGraphicsRequirementsD3D11KHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
		XrResult result = p_xrGetD3D11GraphicsRequirementsKHR(g_instance, g_system_id, &requirements);
		if (XR_FAILED(result))
		{
			g_log("xrGetD3D11GraphicsRequirementsKHR failed: result=%d", static_cast<int>(result));
			return false;
		}
		g_log("D3D11 graphics requirement: adapterLuid=%08x:%08x minFeatureLevel=0x%x gameFeatureLevel=0x%x",
			static_cast<unsigned int>(requirements.adapterLuid.HighPart),
			static_cast<unsigned int>(requirements.adapterLuid.LowPart),
			static_cast<unsigned int>(requirements.minFeatureLevel),
			static_cast<unsigned int>(device->GetFeatureLevel()));

		g_device = device;
		g_device->AddRef();
		g_device->GetImmediateContext(&g_context);
		hands::initialize(g_device, g_log);

		XrGraphicsBindingD3D11KHR binding = { XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
		binding.device = g_device;
		XrSessionCreateInfo session_info = { XR_TYPE_SESSION_CREATE_INFO };
		session_info.next = &binding;
		session_info.systemId = g_system_id;
		result = p_xrCreateSession(g_instance, &session_info, &g_session);
		if (XR_FAILED(result))
		{
			g_log("xrCreateSession(D3D11) failed: result=%d", static_cast<int>(result));
			cleanup_session_objects();
			return false;
		}

		if (!create_reference_space() || !create_eye_swapchains() ||
			!initialize_ui_renderer() || !create_actions())
		{
			g_log("OpenXR session resource creation failed");
			cleanup_session_objects();
			return false;
		}
		create_optical_hand_trackers();

		g_initialized = true;
		g_log("MILESTONE 2 SESSION CREATED: D3D11 binding, stereo swapchains, UI-only in-eye compositor, frame timing, and Touch actions ready");
		return true;
	}

	void set_game_swapchain(IDXGISwapChain *swapchain)
	{
		if (swapchain == g_game_swapchain)
			return;
		if (swapchain != nullptr)
			swapchain->AddRef();
		if (g_game_swapchain != nullptr)
			g_game_swapchain->Release();
		g_game_swapchain = swapchain;
		release_ui_capture();
		g_game_copy_logged = false;
	}

	bool capture_desktop_frame()
	{
		return capture_backbuffer(g_desktop_frame, g_desktop_frame_view);
	}

	bool clear_desktop_for_ui_capture()
	{
		if (g_game_swapchain == nullptr || g_device == nullptr || g_context == nullptr)
			return false;
		ID3D11Texture2D *backbuffer = nullptr;
		if (FAILED(g_game_swapchain->GetBuffer(
			0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backbuffer))) ||
			backbuffer == nullptr)
			return false;
		ID3D11RenderTargetView *target = nullptr;
		const HRESULT result = g_device->CreateRenderTargetView(backbuffer, nullptr, &target);
		backbuffer->Release();
		if (FAILED(result) || target == nullptr)
			return false;
		const float transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		g_context->ClearRenderTargetView(target, transparent);
		target->Release();
		if (!g_ui_capture_logged && g_log != nullptr)
		{
			g_ui_capture_logged = true;
			g_log("UI-ONLY CAPTURE ACTIVE: monitor world is preserved, the backbuffer is cleared transparent, and only subsequent native GUI draws are captured");
		}
		return true;
	}

	bool restore_desktop_frame()
	{
		if (g_desktop_frame == nullptr || g_game_swapchain == nullptr || g_context == nullptr)
			return false;
		ID3D11Texture2D *backbuffer = nullptr;
		if (FAILED(g_game_swapchain->GetBuffer(
			0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backbuffer))) ||
			backbuffer == nullptr)
			return false;
		g_context->CopyResource(backbuffer, g_desktop_frame);
		backbuffer->Release();
		if (!g_desktop_capture_logged)
		{
			g_desktop_capture_logged = true;
			if (g_log != nullptr)
				g_log("NATIVE DESKTOP PRESERVED: untouched game frame captured before VR eyes and restored before Present");
		}
		return true;
	}

	bool begin_engine_frame()
	{
		if (!g_initialized || !g_session_running || g_game_swapchain == nullptr || g_frame_active)
			return false;

		XrFrameWaitInfo wait_info = { XR_TYPE_FRAME_WAIT_INFO };
		XrFrameState frame_state = { XR_TYPE_FRAME_STATE };
		XrResult result = p_xrWaitFrame(g_session, &wait_info, &frame_state);
		if (XR_FAILED(result))
			return false;

		XrFrameBeginInfo begin_info = { XR_TYPE_FRAME_BEGIN_INFO };
		result = p_xrBeginFrame(g_session, &begin_info);
		if (XR_FAILED(result))
			return false;

		g_frame_active = true;
		g_frame_display_time = frame_state.predictedDisplayTime;
		if (!frame_state.shouldRender)
		{
			end_empty_frame();
			return false;
		}

		XrViewLocateInfo locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
		locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		locate_info.displayTime = g_frame_display_time;
		locate_info.space = g_local_space;
		XrViewState view_state = { XR_TYPE_VIEW_STATE };
		uint32_t located_view_count = 0;
		for (uint32_t i = 0; i < eye_count; ++i)
			g_views[i] = { XR_TYPE_VIEW };
		result = p_xrLocateViews(
			g_session,
			&locate_info,
			&view_state,
			eye_count,
			&located_view_count,
			g_views);
		const XrViewStateFlags required =
			XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
		if (XR_FAILED(result) || located_view_count != eye_count || (view_state.viewStateFlags & required) != required)
		{
			end_empty_frame();
			return false;
		}

		if (!g_head_pose_logged)
		{
			const XrVector3f head = {
				(g_views[0].pose.position.x + g_views[1].pose.position.x) * 0.5f,
				(g_views[0].pose.position.y + g_views[1].pose.position.y) * 0.5f,
				(g_views[0].pose.position.z + g_views[1].pose.position.z) * 0.5f
			};
			const XrQuaternionf orientation = g_views[0].pose.orientation;
			g_log("TRACKED HEAD: position=(%.3f,%.3f,%.3f) orientation=(%.3f,%.3f,%.3f,%.3f)",
				head.x, head.y, head.z,
				orientation.x, orientation.y, orientation.z, orientation.w);
			g_head_pose_logged = true;
		}
		update_hands(g_frame_display_time);

		for (uint32_t i = 0; i < eye_count; ++i)
		{
			EyeSwapchain &eye = g_eyes[i];
			XrSwapchainImageAcquireInfo acquire = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
			result = p_xrAcquireSwapchainImage(eye.handle, &acquire, &g_acquired_images[i]);
			if (XR_FAILED(result))
			{
				release_acquired_eyes();
				end_empty_frame();
				return false;
			}
			g_eye_acquired[i] = true;
			XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			wait.timeout = XR_INFINITE_DURATION;
			result = p_xrWaitSwapchainImage(eye.handle, &wait);
			if (XR_FAILED(result))
			{
				release_acquired_eyes();
				end_empty_frame();
				return false;
			}
			const float color[4] = { 0.015f, 0.08f, 0.16f, 1.0f };
			g_context->ClearRenderTargetView(eye.render_targets[g_acquired_images[i]], color);
		}
		return true;
	}

	bool get_eye_view(uint32_t eye_index, XrView &view)
	{
		if (!g_frame_active || eye_index >= eye_count)
			return false;
		view = g_views[eye_index];
		return true;
	}

	bool get_tracked_hand_pose(uint32_t hand_index, XrPosef &pose, bool &optical, bool &interaction)
	{
		return g_frame_active && hands::get_pose(hand_index, pose, optical, interaction);
	}

	bool get_optical_gun_trigger()
	{
		return g_frame_active && g_optical_gun_trigger;
	}

	bool get_optical_hammer_swing(uint64_t &sequence, XrVector3f &direction)
	{
		sequence = g_optical_hammer_swing_sequence;
		direction = g_optical_hammer_swing_direction;
		return g_frame_active && sequence != 0;
	}

	bool get_eye_render_size(uint32_t eye_index, uint32_t &width, uint32_t &height)
	{
		if (!g_frame_active || eye_index >= eye_count || g_eyes[eye_index].handle == XR_NULL_HANDLE)
			return false;
		width = g_eyes[eye_index].width;
		height = g_eyes[eye_index].height;
		return width > 0 && height > 0;
	}

	bool begin_eye_render(uint32_t eye_index)
	{
		if (!g_frame_active || eye_index >= eye_count || !g_eye_acquired[eye_index] || g_context == nullptr)
			return false;
		g_redirect_eye = static_cast<int>(eye_index);
		g_eye_direct_rendered[eye_index] = false;
		ID3D11RenderTargetView *target = g_eyes[eye_index].render_targets[g_acquired_images[eye_index]];
		if (target == nullptr)
			return false;
		const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		g_context->ClearRenderTargetView(target, clear);
		return true;
	}

	bool render_tracked_hands(uint32_t eye_index)
	{
		if (!g_frame_active || eye_index >= eye_count || !g_eye_acquired[eye_index] ||
			g_context == nullptr)
			return false;
		ID3D11RenderTargetView *target = g_eyes[eye_index].render_targets[g_acquired_images[eye_index]];
		return hands::render(
			g_context,
			target,
			g_eyes[eye_index].width,
			g_eyes[eye_index].height,
			g_views[eye_index]);
	}

	void end_eye_render()
	{
		g_redirect_eye = -1;
	}

	bool redirect_eye_render_target(
		ID3D11DeviceContext *context,
		uint32_t count,
		ID3D11RenderTargetView *const *render_targets,
		ID3D11DepthStencilView *depth_stencil)
	{
		if (g_redirect_eye < 0 || g_redirect_eye >= static_cast<int>(eye_count) ||
			context == nullptr || count == 0 || count > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT ||
			render_targets == nullptr || depth_stencil != nullptr || g_game_swapchain == nullptr)
			return false;

		ID3D11Texture2D *backbuffer = nullptr;
		if (FAILED(g_game_swapchain->GetBuffer(
				0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backbuffer))) ||
			backbuffer == nullptr)
			return false;

		ID3D11RenderTargetView *replacement[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		bool replaced = false;
		for (uint32_t i = 0; i < count; ++i)
		{
			replacement[i] = render_targets[i];
			if (render_targets[i] == nullptr)
				continue;
			ID3D11Resource *resource = nullptr;
			render_targets[i]->GetResource(&resource);
			if (resource == backbuffer)
			{
				replacement[i] = g_eyes[g_redirect_eye].render_targets[g_acquired_images[g_redirect_eye]];
				replaced = replacement[i] != nullptr;
			}
			if (resource != nullptr)
				resource->Release();
		}
		backbuffer->Release();
		if (!replaced)
			return false;

		context->OMSetRenderTargets(count, replacement, nullptr);
		g_eye_direct_rendered[g_redirect_eye] = true;
		return true;
	}

	bool copy_game_frame_to_eye(uint32_t eye_index)
	{
		if (!g_frame_active || eye_index >= eye_count || !g_eye_acquired[eye_index] ||
			g_game_swapchain == nullptr || g_context == nullptr)
			return false;
		if (g_eye_direct_rendered[eye_index])
		{
			if (!g_direct_eye_render_logged)
			{
				g_direct_eye_render_logged = true;
				g_log("NATIVE RECOMMENDED EYE RENDER ACTIVE: engine output is bound directly to %ux%u OpenXR eye textures",
					g_eyes[eye_index].width, g_eyes[eye_index].height);
			}
			return true;
		}
		if (!g_direct_eye_render_failure_logged)
		{
			g_direct_eye_render_failure_logged = true;
			g_log("Native recommended-resolution target was not bound; refusing a low-resolution fallback copy");
		}
		return false;

		ID3D11Texture2D *source = nullptr;
		const HRESULT hr = g_game_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&source));
		if (FAILED(hr) || source == nullptr)
			return false;

		EyeSwapchain &eye = g_eyes[eye_index];
		ID3D11Texture2D *destination = eye.images[g_acquired_images[eye_index]].texture;
		D3D11_TEXTURE2D_DESC source_desc = {};
		D3D11_TEXTURE2D_DESC destination_desc = {};
		source->GetDesc(&source_desc);
		destination->GetDesc(&destination_desc);
		if (!g_game_copy_logged)
		{
			g_log("NATIVE STEREO COPY: game=%ux%u format=%u samples=%u eye=%ux%u format=%u samples=%u",
				source_desc.Width,
				source_desc.Height,
				static_cast<unsigned int>(source_desc.Format),
				source_desc.SampleDesc.Count,
				destination_desc.Width,
				destination_desc.Height,
				static_cast<unsigned int>(destination_desc.Format),
				destination_desc.SampleDesc.Count);
			g_game_copy_logged = true;
		}

		const bool rgba_compatible =
			(source_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
			 source_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
			 source_desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) &&
			(destination_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
			 destination_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
			 destination_desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS);
		if (!rgba_compatible || source_desc.SampleDesc.Count != destination_desc.SampleDesc.Count)
		{
			source->Release();
			return false;
		}

		const uint32_t copy_width = source_desc.Width < destination_desc.Width
			? source_desc.Width : destination_desc.Width;
		const uint32_t copy_height = source_desc.Height < destination_desc.Height
			? source_desc.Height : destination_desc.Height;
		const uint32_t source_x = (source_desc.Width - copy_width) / 2;
		const uint32_t source_y = (source_desc.Height - copy_height) / 2;
		const uint32_t destination_x = (destination_desc.Width - copy_width) / 2;
		const uint32_t destination_y = (destination_desc.Height - copy_height) / 2;
		D3D11_BOX source_box = {
			source_x,
			source_y,
			0,
			source_x + copy_width,
			source_y + copy_height,
			1
		};
		g_context->CopySubresourceRegion(
			destination,
			0,
			destination_x,
			destination_y,
			0,
			source,
			0,
			&source_box);
		source->Release();
		return true;
	}

	bool mirror_eye_to_desktop(uint32_t eye_index)
	{
		if (!g_frame_active || eye_index >= eye_count || !g_eye_acquired[eye_index] ||
			g_game_swapchain == nullptr || g_context == nullptr ||
			!g_eye_direct_rendered[eye_index] || !initialize_mirror_renderer())
			return false;

		EyeSwapchain &eye = g_eyes[eye_index];
		ID3D11ShaderResourceView *source = eye.shader_resources[g_acquired_images[eye_index]];
		if (source == nullptr)
			return false;
		ID3D11Texture2D *backbuffer = nullptr;
		if (FAILED(g_game_swapchain->GetBuffer(
			0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backbuffer))) ||
			backbuffer == nullptr)
			return false;
		D3D11_TEXTURE2D_DESC backbuffer_desc = {};
		backbuffer->GetDesc(&backbuffer_desc);
		if (backbuffer_desc.SampleDesc.Count != 1 || backbuffer_desc.Width == 0 ||
			backbuffer_desc.Height == 0)
		{
			backbuffer->Release();
			return false;
		}

		D3D11_RENDER_TARGET_VIEW_DESC target_desc = {};
		target_desc.Format = backbuffer_desc.Format;
		if (target_desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
			target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		else if (target_desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS)
			target_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		target_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		target_desc.Texture2D.MipSlice = 0;
		ID3D11RenderTargetView *target = nullptr;
		if (FAILED(g_device->CreateRenderTargetView(backbuffer, &target_desc, &target)) ||
			target == nullptr)
		{
			backbuffer->Release();
			return false;
		}

		const float source_aspect = static_cast<float>(eye.width) /
			static_cast<float>(eye.height);
		const float target_aspect = static_cast<float>(backbuffer_desc.Width) /
			static_cast<float>(backbuffer_desc.Height);
		float uv_transform[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
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
		g_context->UpdateSubresource(g_mirror_constants, 0, nullptr, uv_transform, 0, 0);
		g_context->OMSetRenderTargets(1, &target, nullptr);
		g_context->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
		g_context->OMSetDepthStencilState(nullptr, 0);
		D3D11_VIEWPORT viewport = {
			0.0f, 0.0f,
			static_cast<float>(backbuffer_desc.Width),
			static_cast<float>(backbuffer_desc.Height),
			0.0f, 1.0f
		};
		g_context->RSSetViewports(1, &viewport);
		g_context->RSSetState(g_mirror_rasterizer);
		g_context->IASetInputLayout(nullptr);
		g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ID3D11Buffer *no_vertex_buffer = nullptr;
		UINT zero = 0;
		g_context->IASetVertexBuffers(0, 1, &no_vertex_buffer, &zero, &zero);
		g_context->VSSetShader(g_mirror_vertex_shader, nullptr, 0);
		g_context->VSSetConstantBuffers(0, 1, &g_mirror_constants);
		g_context->PSSetShader(g_mirror_pixel_shader, nullptr, 0);
		g_context->PSSetShaderResources(0, 1, &source);
		g_context->PSSetSamplers(0, 1, &g_mirror_sampler);
		g_context->Draw(3, 0);
		ID3D11ShaderResourceView *no_source = nullptr;
		g_context->PSSetShaderResources(0, 1, &no_source);
		g_context->OMSetRenderTargets(0, nullptr, nullptr);
		target->Release();
		backbuffer->Release();

		if (!g_mirror_logged)
		{
			g_mirror_logged = true;
			if (g_log != nullptr)
				g_log("VR DESKTOP EYE MIRROR ACTIVE: left eye %ux%u aspect-cropped into PC backbuffer %ux%u without a third full scene render",
					eye.width, eye.height, backbuffer_desc.Width, backbuffer_desc.Height);
		}
		return true;
	}

	void end_engine_frame()
	{
		if (!g_frame_active)
			return;
		g_context->Flush();
		release_acquired_eyes();

		XrCompositionLayerProjectionView projection_views[eye_count] = {};
		for (uint32_t i = 0; i < eye_count; ++i)
		{
			projection_views[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
			projection_views[i].pose = g_views[i].pose;
			projection_views[i].fov = g_views[i].fov;
			projection_views[i].subImage.swapchain = g_eyes[i].handle;
			projection_views[i].subImage.imageRect.offset = { 0, 0 };
			projection_views[i].subImage.imageRect.extent = {
				static_cast<int32_t>(g_eyes[i].width),
				static_cast<int32_t>(g_eyes[i].height)
			};
		}
		XrCompositionLayerProjection projection_layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
		projection_layer.space = g_local_space;
		projection_layer.viewCount = eye_count;
		projection_layer.views = projection_views;
		const XrCompositionLayerBaseHeader *layers[1] = {
			reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projection_layer)
		};
		XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
		end_info.displayTime = g_frame_display_time;
		end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		end_info.layerCount = 1;
		end_info.layers = layers;
		const XrResult result = p_xrEndFrame(g_session, &end_info);
		g_frame_active = false;
		g_ui_layer_ready = false;
		if (XR_FAILED(result))
			g_log("xrEndFrame(native stereo) failed: result=%d", static_cast<int>(result));
		else if (!g_frame_submitted_logged)
		{
			g_frame_submitted_logged = true;
			g_log("MILESTONE 3 NATIVE STEREO SUBMITTED: two engine renders copied into Quest eye swapchains");
		}
	}

	void on_present()
	{
		if (!g_initialized)
			return;
		poll_events();
		if (g_frame_active)
		{
			prepare_ui_layer();
			end_engine_frame();
		}
	}

	bool consume_recenter_request()
	{
		return InterlockedExchange(&g_recenter_request, 0) != 0;
	}

	void shutdown()
	{
		if (g_log != nullptr && g_initialized)
			g_log("Shutting down OpenXR session resources");
		cleanup_session_objects();
		g_instance = XR_NULL_HANDLE;
		g_system_id = XR_NULL_SYSTEM_ID;
		g_get_proc = nullptr;
		g_log = nullptr;
	}

	bool is_initialized()
	{
		return g_initialized;
	}
}
