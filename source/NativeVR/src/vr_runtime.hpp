#pragma once

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace scrapvr
{
	using LogFunction = void (*)(const char *format, ...);

	bool initialize_session(
		ID3D11Device *device,
		XrInstance instance,
		XrSystemId system_id,
		PFN_xrGetInstanceProcAddr get_instance_proc_addr,
		LogFunction log,
		bool hand_tracking_enabled);
	void set_game_swapchain(IDXGISwapChain *swapchain);
	bool begin_engine_frame();
	bool capture_desktop_frame();
	bool clear_desktop_for_ui_capture();
	bool restore_desktop_frame();
	bool get_eye_view(uint32_t eye_index, XrView &view);
	bool get_tracked_hand_pose(uint32_t hand_index, XrPosef &pose, bool &optical, bool &interaction);
	bool get_optical_gun_trigger();
	bool get_optical_hammer_swing(uint64_t &sequence, XrVector3f &direction);
	bool get_eye_render_size(uint32_t eye_index, uint32_t &width, uint32_t &height);
	bool begin_eye_render(uint32_t eye_index);
	bool render_tracked_hands(uint32_t eye_index);
	void end_eye_render();
	bool redirect_eye_render_target(
		ID3D11DeviceContext *context,
		uint32_t count,
		ID3D11RenderTargetView *const *render_targets,
		ID3D11DepthStencilView *depth_stencil);
	bool copy_game_frame_to_eye(uint32_t eye_index);
	bool mirror_eye_to_desktop(uint32_t eye_index);
	void end_engine_frame();
	void on_present();
	bool consume_recenter_request();
	void shutdown();
	bool is_initialized();
}
