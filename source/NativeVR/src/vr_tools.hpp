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
	enum class ContextAction { none, rotate_placement, paint_palette };

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
	bool get_interaction_laser_offset(XrVector3f &offset, XrVector3f *local_direction = nullptr);
	// Returns the calibrated hand-local projectile/nozzle origin for the active
	// gun, thrown item, or extinguisher together with its equipped Chapter 2 UUID.
	// Lua uses this value for spawning; no visible debug ray is required.
	bool get_gun_muzzle_offset(XrVector3f &offset, XrVector3f &local_direction, const char *&item_uuid);
	bool is_hammer_active();
	HapticProfile active_haptic_profile();
	// Mirrors Scrap Mechanic's contextual Q action for right-controller B.
	// B still sends the normal use/interact action independently, so seats remain
	// enterable while a placeable item is equipped.
	ContextAction active_context_action();
	bool is_player_seated();
	bool is_player_first_person();
	void set_render_suppressed(bool suppressed);
	void shutdown();
}
