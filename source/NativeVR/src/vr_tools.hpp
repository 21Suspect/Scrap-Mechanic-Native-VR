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

#include <array>
#include <cstddef>

namespace scrapvr::tools
{
	using LogFunction = void (*)(const char *format, ...);
	enum class HapticProfile { none, hammer, tool, gun };
	enum class ContextAction { none, rotate_placement, paint_palette };
	enum class InteractionLaserKind : uint32_t { none = 0, surface = 1, connection = 2 };
	// Compass markers are exchanged in the same low-rate packet as the wrist
	// vitals.  Keep the collection bounded: the stock compass can contain many
	// transient icons during raids, but a small deterministic cap avoids making
	// the native bitmap bridge grow without limit.
	inline constexpr size_t kMaxWristHudWaypoints = 24;
	struct WristHudWaypoint
	{
		float angle = 0.0f;      // world bearing, radians; north is +Y
		float distance = 0.0f;   // horizontal metres from the player
		uint32_t kind = 1;       // 1 waypoint, 2 enemy, 3 event, 4 lost item
	};
	// Player vitals exchanged by the low-rate Lua player-state bridge.  The
	// native wrist HUD deliberately consumes this packet instead of scraping the
	// desktop status panel, so it remains visible and readable in stereo VR.
	struct WristHudState
	{
		bool active = false;
		bool conscious = true;
		float health = 0.0f;
		float max_health = 100.0f;
		// Breath is optional because Creative and custom game modes may not
		// publish SurvivalPlayer stats. A zero max value means no oxygen data.
		float breath = 0.0f;
		float max_breath = 0.0f;
		uint32_t time_minutes = 0;
		std::array<WristHudWaypoint, kMaxWristHudWaypoints> waypoints{};
		uint32_t waypoint_count = 0;
	};

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
		bool right_firing,
		const XrPosef &right_aim_pose,
		bool right_aim_active,
		float right_target_distance,
		bool right_target_active,
		float interaction_target_distance,
		bool interaction_target_active);
	// Returns the calibrated hand-local pointer origin only for the three native
	// tools whose real engine hit-test must follow the visible VR laser.
	bool get_interaction_laser_offset(
		XrVector3f &offset,
		XrVector3f *local_direction = nullptr,
		InteractionLaserKind *kind = nullptr);
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
	WristHudState wrist_hud_state();
	void set_render_suppressed(bool suppressed);
	void shutdown();
}
