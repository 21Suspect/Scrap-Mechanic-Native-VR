#pragma once

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <windows.h>
#include <d3d11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace scrapvr::tools
{
	using LogFunction = void (*)(const char *format, ...);
	enum class HapticProfile { none, hammer, tool, gun };

	bool initialize(ID3D11Device *device, LogFunction log);
	bool render(
		ID3D11DeviceContext *context,
		ID3D11RenderTargetView *target,
		ID3D11DepthStencilView *depth,
		uint32_t width,
		uint32_t height,
		const XrView &eye,
		const XrPosef &right_hand_pose,
		bool right_hand_active,
		bool right_firing);
	// Returns the calibrated hand-local pointer origin only for the three native
	// tools whose real engine hit-test must follow the visible VR laser.
	bool get_interaction_laser_offset(XrVector3f &offset);
	bool is_hammer_active();
	HapticProfile active_haptic_profile();
	bool is_player_seated();
	bool is_player_first_person();
	void set_render_suppressed(bool suppressed);
	void shutdown();
}
