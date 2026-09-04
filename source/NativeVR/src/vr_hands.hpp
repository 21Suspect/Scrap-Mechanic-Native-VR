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

namespace scrapvr::hands
{
	using LogFunction = void (*)(const char *format, ...);

	bool initialize(ID3D11Device *device, LogFunction log);
	void set_pose(uint32_t hand, const XrPosef &pose, bool active, bool optical);
	void set_interaction(uint32_t hand, bool interaction);
	void set_firing(uint32_t hand, bool firing);
	bool get_pose(uint32_t hand, XrPosef &pose, bool &optical, bool &interaction);
	void set_finger_articulation(
		uint32_t hand, const float curls[5], const float bends[5][3], bool precise);
	bool render(
		ID3D11DeviceContext *context,
		ID3D11RenderTargetView *target,
		uint32_t width,
		uint32_t height,
		const XrView &eye,
		const XrPosef &right_aim_pose,
		bool right_aim_active,
		float right_target_distance,
		bool right_target_active,
		float interaction_target_distance,
		bool interaction_target_active,
		float world_heading);
	void shutdown();
}
