-- Chapter 2 / Scrap Mechanic 1.0 VR gameplay bridge limits.
-- Client tracking is validated again on the server before it can affect the world.
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
			interactionCooldownTicks = 10
		}
	}
end
