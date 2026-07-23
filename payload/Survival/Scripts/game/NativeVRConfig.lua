-- VR-only gameplay bridge settings for Scrap Mechanic build 22163681.
-- These values bound client tracking data before server-side interactions.
if g_nativeVrConfig == nil then
	g_nativeVrConfig = {
		vrHands = {
			enabled = true,
			contactRadius = 0.20,
			maximumReach = 2.5,
			maximumHandSpeed = 6.0,
			maximumBodyMass = 2000.0,
			maximumImpulse = 120.0,
			impulseScale = 0.12,
			maximumShapesPerHand = 16,
			interactionEnabled = true,
			interactionRadius = 0.22,
			touchReleasePadding = 0.06,
			touchReleaseHysteresis = 0.08,
			touchReleaseDebounceTicks = 12,
			interactionCooldownTicks = 10,
			renderSelectedTool = true,
			toolForwardOffset = 0.045,
			toolUpOffset = -0.035,
			toolRotationOffset = 1.57079632679,
			toolMirrorAim = true,
			toolRollCorrection = 1.57079632679,
			toolPointerEnabled = true,
			toolPointerRange = 20.0,
			toolPointerThickness = 0.008
		}
	}
end
