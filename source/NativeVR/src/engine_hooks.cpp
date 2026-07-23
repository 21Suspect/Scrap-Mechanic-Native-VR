#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "engine_hooks.hpp"
#include "vr_runtime.hpp"
#include "vr_tools.hpp"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace engine_hooks
{
	namespace
	{
		constexpr uintptr_t render_rva = 0x006cee70;
		constexpr uintptr_t camera_rva = 0x007dcbb0;
		constexpr uintptr_t inner_render_rva = 0x006d2b00;
		constexpr uintptr_t reset_renderer_rva = 0x006d8b30;
		constexpr uintptr_t resize_renderer_rva = 0x006d7f70;
		constexpr uintptr_t raycast_rva = 0x0042b290;
		constexpr uintptr_t player_tool_raycast_return_rva = 0x0040e10b;
		constexpr uint32_t expected_timestamp = 0x69a5b8f7;
		constexpr uint32_t expected_image_size = 0x013e9000;
		constexpr size_t render_patch_size = 21;
		constexpr size_t camera_patch_size = 15;
		constexpr size_t raycast_patch_size = 15;

		const uint8_t expected_render_bytes[render_patch_size] = {
			0x40, 0x53, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x48, 0x83,
			0xec, 0x50, 0x4c, 0x8b, 0xb4, 0x24, 0xa0, 0x00, 0x00, 0x00
		};
		const uint8_t expected_camera_bytes[camera_patch_size] = {
			0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec,
			0xa0, 0x00, 0x00, 0x00
		};
		const uint8_t expected_raycast_bytes[raycast_patch_size] = {
			0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48,
			0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20
		};
		const uint8_t expected_inner_render_bytes[16] = {
			0x48, 0x8b, 0xc4, 0x55, 0x53, 0x56, 0x57, 0x41,
			0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48
		};
		const uint8_t expected_reset_renderer_bytes[16] = {
			0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
			0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57
		};
		const uint8_t expected_resize_renderer_bytes[16] = {
			0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57,
			0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
		};

		using RenderFunction = void (*)(void *, float, void *, void *, void *);
		using CameraFunction = void (*)(void *, const float *, const float *, float, float);
		using ResetRendererFunction = void (*)(void *);
		using ResizeRendererFunction = void (*)(void *, int, int);
		using RaycastFunction = uint8_t (*)(
			void *, void *, const float *, const float *, void *, void *);

		struct Detour
		{
			uint8_t *target = nullptr;
			void *trampoline = nullptr;
			size_t patch_size = 0;
			uint8_t original[32] = {};
		};

		LogFunction g_log = nullptr;
		Detour g_render_detour;
		Detour g_camera_detour;
		Detour g_raycast_detour;
		RenderFunction g_original_render = nullptr;
		RenderFunction g_inner_render = nullptr;
		ResetRendererFunction g_reset_renderer = nullptr;
		ResizeRendererFunction g_resize_renderer = nullptr;
		CameraFunction g_original_camera = nullptr;
		RaycastFunction g_original_raycast = nullptr;
		uint8_t *g_module_base = nullptr;
		volatile LONG64 g_render_calls = 0;
		volatile LONG64 g_camera_calls = 0;
		bool g_installed = false;
		bool g_first_frame_logged = false;
		bool g_stereo_render_logged = false;
		bool g_desktop_render_logged = false;
		bool g_desktop_capture_failure_logged = false;
		bool g_copy_failure_logged = false;
		bool g_resolution_switch_logged = false;
		bool g_neutral_deferred_pass_logged = false;
		thread_local int g_active_eye = -1;
		thread_local bool g_viewmodel_draw_active = false;
		bool g_viewmodel_stereo_logged = false;
		bool g_viewmodel_suppression_logged = false;
		bool g_tracking_reference_valid = false;
		XrPosef g_tracking_reference = {};
		ULONGLONG g_last_stereo_frame_ms = 0;
		ULONGLONG g_last_hand_physics_publish_ms = 0;
		uint64_t g_hand_physics_sequence = 0;
		bool g_hand_physics_publish_logged = false;
		bool g_right_hand_world_active = false;
		XrVector3f g_right_hand_world = {};
		XrVector3f g_right_hand_forward = {};
		XrVector3f g_right_hand_up = {};
		ULONGLONG g_right_hand_world_ms = 0;
		bool g_tool_raycast_logged = false;

		XrQuaternionf quaternion_conjugate(const XrQuaternionf &q)
		{
			return { -q.x, -q.y, -q.z, q.w };
		}

		XrQuaternionf quaternion_multiply(const XrQuaternionf &a, const XrQuaternionf &b)
		{
			return {
				a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
				a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
				a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
			};
		}

		XrQuaternionf yaw_only(const XrQuaternionf &orientation)
		{
			const float sin_yaw = 2.0f *
				(orientation.w * orientation.y + orientation.x * orientation.z);
			const float cos_yaw = 1.0f - 2.0f *
				(orientation.y * orientation.y + orientation.z * orientation.z);
			const float half_yaw = std::atan2(sin_yaw, cos_yaw) * 0.5f;
			return { 0.0f, std::sin(half_yaw), 0.0f, std::cos(half_yaw) };
		}

		XrVector3f rotate_vector(const XrQuaternionf &q, const XrVector3f &v)
		{
			const XrQuaternionf value = { v.x, v.y, v.z, 0.0f };
			const XrQuaternionf rotated = quaternion_multiply(
				quaternion_multiply(q, value),
				quaternion_conjugate(q));
			return { rotated.x, rotated.y, rotated.z };
		}

		bool normalize_vector(XrVector3f &value)
		{
			const float length_squared =
				value.x * value.x + value.y * value.y + value.z * value.z;
			if (!std::isfinite(length_squared) || length_squared < 0.0001f)
				return false;
			const float inverse_length = 1.0f / std::sqrt(length_squared);
			value.x *= inverse_length;
			value.y *= inverse_length;
			value.z *= inverse_length;
			return true;
		}

		void quaternion_matrix(const XrQuaternionf &q, float matrix[16])
		{
			const float xx = q.x * q.x;
			const float yy = q.y * q.y;
			const float zz = q.z * q.z;
			const float xy = q.x * q.y;
			const float xz = q.x * q.z;
			const float yz = q.y * q.z;
			const float xw = q.x * q.w;
			const float yw = q.y * q.w;
			const float zw = q.z * q.w;
			std::memset(matrix, 0, sizeof(float) * 16);
			matrix[0] = 1.0f - 2.0f * (yy + zz);
			matrix[1] = 2.0f * (xy + zw);
			matrix[2] = 2.0f * (xz - yw);
			matrix[4] = 2.0f * (xy - zw);
			matrix[5] = 1.0f - 2.0f * (xx + zz);
			matrix[6] = 2.0f * (yz + xw);
			matrix[8] = 2.0f * (xz + yw);
			matrix[9] = 2.0f * (yz - xw);
			matrix[10] = 1.0f - 2.0f * (xx + yy);
			matrix[15] = 1.0f;
		}

		void multiply_column_major(const float a[16], const float b[16], float result[16])
		{
			for (int column = 0; column < 4; ++column)
				for (int row = 0; row < 4; ++row)
				{
					result[column * 4 + row] = 0.0f;
					for (int k = 0; k < 4; ++k)
						result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
				}
		}

		bool build_eye_view_transform(const XrView &eye, const XrView &other, float transform[16])
		{
			if (!g_tracking_reference_valid)
			{
				g_tracking_reference.position = {
					(eye.pose.position.x + other.pose.position.x) * 0.5f,
					(eye.pose.position.y + other.pose.position.y) * 0.5f,
					(eye.pose.position.z + other.pose.position.z) * 0.5f
				};
				// Calibrate horizontal facing only. Capturing headset pitch or roll here
				// tilts the entire virtual ground and is a major comfort hazard.
				g_tracking_reference.orientation = yaw_only(eye.pose.orientation);
				g_tracking_reference_valid = true;
				if (g_log != nullptr)
					g_log("VIEW RECENTERED: origin=(%.3f,%.3f,%.3f), yaw-only floor alignment active",
						g_tracking_reference.position.x,
						g_tracking_reference.position.y,
						g_tracking_reference.position.z);
			}

			const XrQuaternionf inverse_reference = quaternion_conjugate(g_tracking_reference.orientation);
			const XrQuaternionf relative_orientation = quaternion_multiply(
				inverse_reference,
				eye.pose.orientation);
			const XrQuaternionf view_orientation = quaternion_conjugate(relative_orientation);
			const XrVector3f position_delta = {
				eye.pose.position.x - g_tracking_reference.position.x,
				eye.pose.position.y - g_tracking_reference.position.y,
				eye.pose.position.z - g_tracking_reference.position.z
			};
			const XrVector3f relative_position = rotate_vector(inverse_reference, position_delta);
			const XrVector3f inverse_position = {
				-relative_position.x,
				-relative_position.y,
				-relative_position.z
			};
			const XrVector3f view_position = rotate_vector(view_orientation, inverse_position);
			quaternion_matrix(view_orientation, transform);
			transform[12] = view_position.x;
			transform[13] = view_position.y;
			transform[14] = view_position.z;
			return true;
		}

		XrVector3f view_point_to_world(const float world_to_view[16], const XrVector3f &point)
		{
			const XrVector3f translated = {
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

		void publish_hand_physics(const float *game_world_to_view)
		{
			if (!g_tracking_reference_valid || game_world_to_view == nullptr)
				return;
			const ULONGLONG now = GetTickCount64();
			if (now - g_last_hand_physics_publish_ms < 20)
				return;
			XrPosef poses[2] = {};
			bool optical[2] = {};
			bool interaction[2] = {};
			bool active[2] = {
				scrapvr::get_tracked_hand_pose(0, poses[0], optical[0], interaction[0]),
				scrapvr::get_tracked_hand_pose(1, poses[1], optical[1], interaction[1])
			};
			if (!active[0] && !active[1])
				return;
			XrVector3f world[2] = {};
			XrVector3f forward[2] = {};
			XrVector3f up[2] = {};
			const XrQuaternionf inverse_reference = quaternion_conjugate(g_tracking_reference.orientation);
			for (uint32_t hand = 0; hand < 2; ++hand)
			{
				if (!active[hand]) continue;
				const XrVector3f delta = {
					poses[hand].position.x - g_tracking_reference.position.x,
					poses[hand].position.y - g_tracking_reference.position.y,
					poses[hand].position.z - g_tracking_reference.position.z
				};
				world[hand] = view_point_to_world(game_world_to_view, rotate_vector(inverse_reference, delta));
				const XrQuaternionf relative_orientation = quaternion_multiply(
					inverse_reference, poses[hand].orientation);
				forward[hand] = view_vector_to_world(game_world_to_view,
					rotate_vector(relative_orientation, { 0.0f, 0.0f, -1.0f }));
				up[hand] = view_vector_to_world(game_world_to_view,
					rotate_vector(relative_orientation, { 0.0f, 1.0f, 0.0f }));
			}
			g_right_hand_world_active = active[1];
			if (active[1])
			{
				g_right_hand_world = world[1];
				g_right_hand_forward = forward[1];
				g_right_hand_up = up[1];
				g_right_hand_world_ms = now;
			}
			uint64_t hammer_swing_sequence = 0;
			XrVector3f hammer_swing_tracking = {};
			XrVector3f hammer_swing_world = {};
			if (scrapvr::get_optical_hammer_swing(
					hammer_swing_sequence, hammer_swing_tracking))
			{
				hammer_swing_world = view_vector_to_world(
					game_world_to_view,
					rotate_vector(inverse_reference, hammer_swing_tracking));
				if (!normalize_vector(hammer_swing_world))
					hammer_swing_sequence = 0;
			}
			wchar_t module_path[MAX_PATH] = {};
			HMODULE module = GetModuleHandleW(L"scrap_native_vr.addon64");
			if (module == nullptr || GetModuleFileNameW(module, module_path, MAX_PATH) == 0)
				return;
			wchar_t *slash = wcsrchr(module_path, L'\\');
			if (slash == nullptr) return;
			*slash = L'\0';
			wchar_t directory[MAX_PATH] = {};
			swprintf(directory, MAX_PATH, L"%s\\..\\Data\\NativeVR", module_path);
			CreateDirectoryW(directory, nullptr);
			wchar_t path[MAX_PATH] = {}, temporary[MAX_PATH] = {};
			swprintf(path, MAX_PATH, L"%s\\hand_physics.json", directory);
			swprintf(temporary, MAX_PATH, L"%s\\hand_physics.tmp", directory);
			char json[1024] = {};
			const int length = std::snprintf(json, sizeof(json),
				"{\"sequence\":%llu,\"opticalGunTrigger\":%s,\"hammerSwingSequence\":%llu,\"hammerSwingDirection\":{\"x\":%.5f,\"y\":%.5f,\"z\":%.5f},\"left\":{\"active\":%s,\"interact\":%s,\"x\":%.5f,\"y\":%.5f,\"z\":%.5f,\"fx\":%.5f,\"fy\":%.5f,\"fz\":%.5f,\"ux\":%.5f,\"uy\":%.5f,\"uz\":%.5f},"
				"\"right\":{\"active\":%s,\"interact\":%s,\"x\":%.5f,\"y\":%.5f,\"z\":%.5f,\"fx\":%.5f,\"fy\":%.5f,\"fz\":%.5f,\"ux\":%.5f,\"uy\":%.5f,\"uz\":%.5f}}",
				static_cast<unsigned long long>(++g_hand_physics_sequence),
				scrapvr::get_optical_gun_trigger() ? "true" : "false",
				static_cast<unsigned long long>(hammer_swing_sequence),
				hammer_swing_world.x, hammer_swing_world.y, hammer_swing_world.z,
				active[0] ? "true" : "false", interaction[0] ? "true" : "false",
				world[0].x, world[0].y, world[0].z,
				forward[0].x, forward[0].y, forward[0].z, up[0].x, up[0].y, up[0].z,
				active[1] ? "true" : "false", interaction[1] ? "true" : "false",
				world[1].x, world[1].y, world[1].z,
				forward[1].x, forward[1].y, forward[1].z, up[1].x, up[1].y, up[1].z);
			HANDLE file = CreateFileW(temporary, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return;
			DWORD written = 0;
			const bool success = length > 0 && WriteFile(file, json, static_cast<DWORD>(length), &written, nullptr) != FALSE && written == static_cast<DWORD>(length);
			CloseHandle(file);
			if (success)
			{
				if (MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				{
					g_last_hand_physics_publish_ms = now;
					if (!g_hand_physics_publish_logged && g_log != nullptr)
					{
						g_hand_physics_publish_logged = true;
						g_log("VR HAND BRIDGE ACTIVE: bounded world-space poses and deliberate interaction state published");
					}
				}
			}
		}

		bool build_eye_camera_matrices(
			uint32_t eye_index,
			const float *game_world_to_view,
			const float *game_projection,
			float eye_world_to_view[16],
			float eye_projection[16])
		{
			if (game_world_to_view == nullptr || game_projection == nullptr)
				return false;

			XrView eye = { XR_TYPE_VIEW };
			XrView other = { XR_TYPE_VIEW };
			if (!scrapvr::get_eye_view(eye_index, eye) ||
				!scrapvr::get_eye_view(1u - eye_index, other))
				return false;

			float tracking_view[16] = {};
			if (!build_eye_view_transform(eye, other, tracking_view))
				return false;
			multiply_column_major(tracking_view, game_world_to_view, eye_world_to_view);
			if (eye_index == 0)
				publish_hand_physics(game_world_to_view);

			const float tan_left = std::tan(eye.fov.angleLeft);
			const float tan_right = std::tan(eye.fov.angleRight);
			const float tan_down = std::tan(eye.fov.angleDown);
			const float tan_up = std::tan(eye.fov.angleUp);
			const float tan_width = tan_right - tan_left;
			const float tan_height = tan_up - tan_down;
			if (std::fabs(tan_width) < 0.001f || std::fabs(tan_height) < 0.001f)
				return false;

			std::memset(eye_projection, 0, sizeof(float) * 16);
			eye_projection[0] = 2.0f / tan_width;
			eye_projection[5] = 2.0f / tan_height;
			eye_projection[8] = (tan_right + tan_left) / tan_width;
			eye_projection[9] = (tan_up + tan_down) / tan_height;
			// Preserve Scrap Mechanic's build-specific reversed-Z depth mapping.
			eye_projection[10] = game_projection[10];
			eye_projection[11] = game_projection[11];
			eye_projection[14] = game_projection[14];
			eye_projection[15] = game_projection[15];
			return true;
		}

		void encode_absolute_jump(uint8_t *destination, const void *target)
		{
			destination[0] = 0xff;
			destination[1] = 0x25;
			std::memset(destination + 2, 0, 4);
			const uint64_t address = reinterpret_cast<uint64_t>(target);
			std::memcpy(destination + 6, &address, sizeof(address));
		}

		bool create_detour(
			Detour &detour,
			uint8_t *target,
			const uint8_t *expected,
			size_t patch_size,
			void *hook)
		{
			if (patch_size < 14 || patch_size > sizeof(detour.original) ||
				std::memcmp(target, expected, patch_size) != 0)
				return false;

			detour.target = target;
			detour.patch_size = patch_size;
			std::memcpy(detour.original, target, patch_size);
			detour.trampoline = VirtualAlloc(
				nullptr,
				patch_size + 14,
				MEM_RESERVE | MEM_COMMIT,
				PAGE_EXECUTE_READWRITE);
			if (detour.trampoline == nullptr)
				return false;

			auto *trampoline = static_cast<uint8_t *>(detour.trampoline);
			std::memcpy(trampoline, detour.original, patch_size);
			encode_absolute_jump(trampoline + patch_size, target + patch_size);

			DWORD old_protection = 0;
			if (!VirtualProtect(target, patch_size, PAGE_EXECUTE_READWRITE, &old_protection))
				return false;
			encode_absolute_jump(target, hook);
			if (patch_size > 14)
				std::memset(target + 14, 0x90, patch_size - 14);
			FlushInstructionCache(GetCurrentProcess(), target, patch_size);
			DWORD ignored = 0;
			VirtualProtect(target, patch_size, old_protection, &ignored);
			return true;
		}

		void remove_detour(Detour &detour)
		{
			if (detour.target != nullptr && detour.patch_size != 0)
			{
				DWORD old_protection = 0;
				if (VirtualProtect(detour.target, detour.patch_size, PAGE_EXECUTE_READWRITE, &old_protection))
				{
					std::memcpy(detour.target, detour.original, detour.patch_size);
					FlushInstructionCache(GetCurrentProcess(), detour.target, detour.patch_size);
					DWORD ignored = 0;
					VirtualProtect(detour.target, detour.patch_size, old_protection, &ignored);
				}
			}
			if (detour.trampoline != nullptr)
				VirtualFree(detour.trampoline, 0, MEM_RELEASE);
			detour = Detour();
		}

		uint8_t hooked_raycast(
			void *world,
			void *unused,
			const float *origin,
			const float *delta,
			void *ignore,
			void *result)
		{
			const void *return_address = __builtin_return_address(0);
			// Generic hand-tip origin for native player interactions. The calibrated
			// laser origin replaces it when connect, paint, or weld is selected.
			XrVector3f local_offset = { 0.0f, -0.035f, -0.120f };
			const ULONGLONG now = GetTickCount64();
			if (g_module_base != nullptr &&
				return_address == g_module_base + player_tool_raycast_return_rva &&
				g_right_hand_world_active && now - g_right_hand_world_ms <= 250 &&
				origin != nullptr && delta != nullptr)
			{
				scrapvr::tools::get_interaction_laser_offset(local_offset);
				XrVector3f forward = g_right_hand_forward;
				XrVector3f up = g_right_hand_up;
				if (normalize_vector(forward))
				{
					const float up_projection =
						up.x * forward.x + up.y * forward.y + up.z * forward.z;
					up.x -= forward.x * up_projection;
					up.y -= forward.y * up_projection;
					up.z -= forward.z * up_projection;
					XrVector3f right = {
						forward.y * up.z - forward.z * up.y,
						forward.z * up.x - forward.x * up.z,
						forward.x * up.y - forward.y * up.x
					};
					if (normalize_vector(up) && normalize_vector(right))
					{
						const float range_squared =
							delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
						if (std::isfinite(range_squared) && range_squared >= 0.01f &&
							range_squared <= 400.0f)
						{
							// The native laser uses OpenXR hand-local coordinates. Local +Z is
							// opposite the published forward vector, hence the -forward*z term.
							const float redirected_origin[3] = {
								g_right_hand_world.x + right.x * local_offset.x + up.x * local_offset.y - forward.x * local_offset.z,
								g_right_hand_world.y + right.y * local_offset.x + up.y * local_offset.y - forward.y * local_offset.z,
								g_right_hand_world.z + right.z * local_offset.x + up.z * local_offset.y - forward.z * local_offset.z
							};
							const float range = std::sqrt(range_squared);
							const float redirected_delta[3] = {
								forward.x * range, forward.y * range, forward.z * range
							};
							if (!g_tool_raycast_logged && g_log != nullptr)
							{
								g_tool_raycast_logged = true;
								g_log("VR ACTION RAY ACTIVE: native player hit-tests follow the right hand; connect/paint/weld use their calibrated laser origins");
							}
							return g_original_raycast(
								world, unused, redirected_origin, redirected_delta, ignore, result);
						}
					}
				}
			}
			return g_original_raycast(world, unused, origin, delta, ignore, result);
		}

		void hooked_render(void *manager, float delta, void *arg3, void *arg4, void *settings)
		{
			InterlockedIncrement64(&g_render_calls);
			if (!scrapvr::begin_engine_frame())
			{
				g_original_render(manager, delta, arg3, arg4, settings);
				return;
			}
			if (scrapvr::consume_recenter_request())
			{
				g_tracking_reference_valid = false;
				if (g_log != nullptr)
					g_log("RECENTER APPLIED: next tracked view establishes a level yaw-only origin");
			}
			const ULONGLONG now = GetTickCount64();
			if (g_last_stereo_frame_ms == 0 || now - g_last_stereo_frame_ms > 1000)
				g_tracking_reference_valid = false;
			g_last_stereo_frame_ms = now;

			bool copied = true;
			uint32_t eye_width = 0;
			uint32_t eye_height = 0;
			const bool eye_size_ready =
				scrapvr::get_eye_render_size(0, eye_width, eye_height) &&
				eye_width > 0 && eye_height > 0;
			const int desktop_width = *reinterpret_cast<int *>(static_cast<uint8_t *>(manager) + 0x0c);
			const int desktop_height = *reinterpret_cast<int *>(static_cast<uint8_t *>(manager) + 0x10);
			if (!eye_size_ready)
			{
				g_original_render(manager, delta, arg3, arg4, settings);
				scrapvr::end_engine_frame();
				return;
			}

			// Scrap Mechanic consumes terrain decals and SSAO on only the first complete
			// renderer invocation of a frame. Consume that asymmetric contribution in a
			// tiny discarded pass before either submitted eye so both eyes take the same
			// neutral path. The desktop mirror later reuses a completed eye instead of
			// invoking another full-resolution scene render.
			constexpr int neutral_pass_size = 64;
			g_active_eye = -1;
			g_reset_renderer(manager);
			g_resize_renderer(manager, neutral_pass_size, neutral_pass_size);
			*reinterpret_cast<int *>(static_cast<uint8_t *>(manager) + 0x0c) = neutral_pass_size;
			*reinterpret_cast<int *>(static_cast<uint8_t *>(manager) + 0x10) = neutral_pass_size;
			g_original_render(manager, delta, arg3, arg4, settings);
			if (!g_neutral_deferred_pass_logged && g_log != nullptr)
			{
				g_neutral_deferred_pass_logged = true;
				g_log("SYMMETRIC NEUTRAL DEFERRED PATH ACTIVE: one-per-frame decals and SSAO are consumed in a discarded 64x64 pass before both eyes");
			}

			g_reset_renderer(manager);
			g_resize_renderer(manager, static_cast<int>(eye_width), static_cast<int>(eye_height));
			*reinterpret_cast<int *>(static_cast<uint8_t *>(manager) + 0x0c) = static_cast<int>(eye_width);
			*reinterpret_cast<int *>(static_cast<uint8_t *>(manager) + 0x10) = static_cast<int>(eye_height);
			for (int pass = 0; pass < 2; ++pass)
			{
				const int eye = pass;
				g_active_eye = eye;
				g_viewmodel_draw_active = false;
				float eye_world_to_view[16] = {};
				float eye_projection[16] = {};
				const bool camera_ready = build_eye_camera_matrices(
					static_cast<uint32_t>(eye),
					static_cast<const float *>(arg3),
					static_cast<const float *>(arg4),
					eye_world_to_view,
					eye_projection);
				const bool high_resolution_ready =
					eye_size_ready && scrapvr::begin_eye_render(static_cast<uint32_t>(eye));
				// The discarded neutral pass is the only render that advances temporal
				// state. Both eyes render the already-updated scene with zero delta so
				// render-driven effects (notably foliage wind) do not run three times per
				// displayed VR frame. Gameplay and physics simulation run outside this hook.
				g_original_render(
					manager,
					0.0f,
					camera_ready ? eye_world_to_view : arg3,
					camera_ready ? eye_projection : arg4,
					settings);
				scrapvr::render_tracked_hands(static_cast<uint32_t>(eye));
				scrapvr::end_eye_render();
				copied = high_resolution_ready &&
					scrapvr::copy_game_frame_to_eye(static_cast<uint32_t>(eye)) && copied;
			}
			g_active_eye = -1;
			g_viewmodel_draw_active = false;
			g_reset_renderer(manager);
			g_resize_renderer(manager, desktop_width, desktop_height);
			*reinterpret_cast<int *>(static_cast<uint8_t *>(manager) + 0x0c) = desktop_width;
			*reinterpret_cast<int *>(static_cast<uint8_t *>(manager) + 0x10) = desktop_height;
			if (!g_resolution_switch_logged && g_log != nullptr)
			{
				g_resolution_switch_logged = true;
				g_log("DESKTOP MIRROR SIZE RESTORED: renderer bookkeeping returned to the PC window dimensions after both eyes");
			}

			// Present the already-completed left eye on the monitor. This removes the third
			// full-resolution scene render while VR is focused. If shader creation, texture
			// compatibility, or mirroring fails, retain the proven native desktop fallback.
			const bool desktop_mirrored = scrapvr::mirror_eye_to_desktop(0);
			if (!desktop_mirrored)
				g_inner_render(manager, 0.0f, arg3, arg4, settings);
			// Preserve the completed world-only desktop image. Scrap Mechanic draws modal
			// GUI after this renderer returns; Present then isolates that difference into
			// an OpenXR quad and submits it together with both still-acquired eye images.
			scrapvr::capture_desktop_frame();
			scrapvr::clear_desktop_for_ui_capture();
			if (!g_desktop_render_logged)
			{
				g_desktop_render_logged = true;
				if (g_log != nullptr)
					g_log(desktop_mirrored
						? "NATIVE DESKTOP MIRROR ACTIVE: monitor reuses the completed left-eye frame"
						: "NATIVE DESKTOP FALLBACK ACTIVE: PC scene render retained because eye mirroring was unavailable");
			}
			if (!g_stereo_render_logged)
			{
				g_stereo_render_logged = true;
				if (g_log != nullptr)
					g_log("ENGINE NATIVE STEREO ACTIVE: early per-eye cameras drive culling, shadows, decals, and rendering");
			}
			if (!copied && !g_copy_failure_logged)
			{
				g_copy_failure_logged = true;
				if (g_log != nullptr)
					g_log("Native stereo copy rejected: game and OpenXR backbuffer formats or sample counts are incompatible");
			}
		}

		void hooked_camera(
			void *state,
			const float *projection,
			const float *world_to_view,
			float near_plane,
			float far_plane)
		{
			InterlockedIncrement64(&g_camera_calls);
			const bool perspective =
				projection != nullptr && world_to_view != nullptr &&
				std::fabs(projection[11] + 1.0f) < 0.05f &&
				std::fabs(projection[15]) < 0.05f;
			const bool camera_relative =
				world_to_view != nullptr &&
				std::fabs(world_to_view[0] - 1.0f) < 0.001f &&
				std::fabs(world_to_view[5] - 1.0f) < 0.001f &&
				std::fabs(world_to_view[10] - 1.0f) < 0.001f &&
				std::fabs(world_to_view[15] - 1.0f) < 0.001f &&
				std::fabs(world_to_view[1]) < 0.001f &&
				std::fabs(world_to_view[2]) < 0.001f &&
				std::fabs(world_to_view[4]) < 0.001f &&
				std::fabs(world_to_view[6]) < 0.001f &&
				std::fabs(world_to_view[8]) < 0.001f &&
				std::fabs(world_to_view[9]) < 0.001f &&
				std::fabs(world_to_view[12]) < 0.001f &&
				std::fabs(world_to_view[13]) < 0.001f &&
				std::fabs(world_to_view[14]) < 0.001f;
			g_viewmodel_draw_active = g_active_eye >= 0 && perspective && camera_relative;
			if (g_active_eye >= 0 && perspective && camera_relative)
			{
				float eye_world_to_view[16] = {};
				float eye_projection[16] = {};
				if (build_eye_camera_matrices(
						static_cast<uint32_t>(g_active_eye),
						world_to_view,
						projection,
						eye_world_to_view,
						eye_projection))
				{
					g_original_camera(
						state,
						eye_projection,
						eye_world_to_view,
						near_plane,
						far_plane);
					if (!g_viewmodel_stereo_logged && g_log != nullptr)
					{
						g_viewmodel_stereo_logged = true;
						g_log("VR VIEWMODEL IDENTIFIED: camera-relative first-person layer isolated from world, decals, shadows, and desktop rendering");
					}
					return;
				}
			}
			// The main camera is injected at RenderStateManager::render so all derived
			// passes agree. Do not rotate shadow, reflection, or decal cameras here.
			g_original_camera(state, projection, world_to_view, near_plane, far_plane);
		}
	}

	bool install(LogFunction log)
	{
		if (g_installed)
			return true;
		g_log = log;

		auto *module = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
		if (module == nullptr)
			return false;
		const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(module);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			return false;
		const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(module + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE ||
			nt->FileHeader.TimeDateStamp != expected_timestamp ||
			nt->OptionalHeader.SizeOfImage != expected_image_size)
		{
			if (g_log != nullptr)
				g_log(
					"Engine hook rejected: build guard mismatch timestamp=0x%x imageSize=0x%x",
					nt->FileHeader.TimeDateStamp,
					nt->OptionalHeader.SizeOfImage);
			return false;
		}
		if (std::memcmp(module + inner_render_rva, expected_inner_render_bytes, sizeof(expected_inner_render_bytes)) != 0 ||
			std::memcmp(module + reset_renderer_rva, expected_reset_renderer_bytes, sizeof(expected_reset_renderer_bytes)) != 0 ||
			std::memcmp(module + resize_renderer_rva, expected_resize_renderer_bytes, sizeof(expected_resize_renderer_bytes)) != 0)
		{
			if (g_log != nullptr)
				g_log("Engine high-resolution helpers rejected: build-specific prologue mismatch");
			return false;
		}
		g_inner_render = reinterpret_cast<RenderFunction>(module + inner_render_rva);
		g_reset_renderer = reinterpret_cast<ResetRendererFunction>(module + reset_renderer_rva);
		g_resize_renderer = reinterpret_cast<ResizeRendererFunction>(module + resize_renderer_rva);
		g_module_base = module;

		if (!create_detour(
				g_camera_detour,
				module + camera_rva,
				expected_camera_bytes,
				camera_patch_size,
				reinterpret_cast<void *>(hooked_camera)))
		{
			if (g_log != nullptr)
				g_log("Engine camera hook rejected: prologue bytes did not match build 22163681");
			return false;
		}
		g_original_camera = reinterpret_cast<CameraFunction>(g_camera_detour.trampoline);

		if (!create_detour(
				g_render_detour,
				module + render_rva,
				expected_render_bytes,
				render_patch_size,
				reinterpret_cast<void *>(hooked_render)))
		{
			remove_detour(g_camera_detour);
			g_original_camera = nullptr;
			if (g_log != nullptr)
				g_log("Engine render hook rejected: prologue bytes did not match build 22163681");
			return false;
		}
		g_original_render = reinterpret_cast<RenderFunction>(g_render_detour.trampoline);

		if (!create_detour(
				g_raycast_detour,
				module + raycast_rva,
				expected_raycast_bytes,
				raycast_patch_size,
				reinterpret_cast<void *>(hooked_raycast)))
		{
			remove_detour(g_render_detour);
			remove_detour(g_camera_detour);
			g_original_render = nullptr;
			g_original_camera = nullptr;
			g_module_base = nullptr;
			if (g_log != nullptr)
				g_log("Engine tool-ray hook rejected: prologue bytes did not match build 22163681");
			return false;
		}
		g_original_raycast = reinterpret_cast<RaycastFunction>(g_raycast_detour.trampoline);
		g_installed = true;
		if (g_log != nullptr)
			g_log("ENGINE HOOK READY: guarded stereo renderer and native VR tool-ray bridge installed; derived camera uploads remain pass-through");
		return true;
	}

	void on_present()
	{
		if (!g_installed)
			return;
		const LONG64 render_calls = InterlockedExchange64(&g_render_calls, 0);
		const LONG64 camera_calls = InterlockedExchange64(&g_camera_calls, 0);
		if (!g_first_frame_logged && render_calls > 0 && camera_calls > 0)
		{
			g_first_frame_logged = true;
			if (g_log != nullptr)
				g_log(
					"ENGINE HOOK VERIFIED: renderCalls=%lld cameraBuilds=%lld in one frame",
					static_cast<long long>(render_calls),
					static_cast<long long>(camera_calls));
		}
	}

	void uninstall()
	{
		if (!g_installed)
			return;
		remove_detour(g_raycast_detour);
		remove_detour(g_render_detour);
		remove_detour(g_camera_detour);
		g_original_render = nullptr;
		g_inner_render = nullptr;
		g_reset_renderer = nullptr;
		g_resize_renderer = nullptr;
		g_original_camera = nullptr;
		g_original_raycast = nullptr;
		g_module_base = nullptr;
		g_right_hand_world_active = false;
		g_installed = false;
		if (g_log != nullptr)
			g_log("Engine hooks removed and original prologues restored");
		g_log = nullptr;
	}

	bool is_installed()
	{
		return g_installed;
	}

	bool suppress_viewmodel_draw()
	{
		if (g_viewmodel_draw_active && !g_viewmodel_suppression_logged)
		{
			g_viewmodel_suppression_logged = true;
			if (g_log != nullptr)
				g_log("VR DUPLICATE VIEWMODEL SUPPRESSED: original first-person arms and head-locked tool removed from stereo eyes");
		}
		return g_viewmodel_draw_active;
	}
}
