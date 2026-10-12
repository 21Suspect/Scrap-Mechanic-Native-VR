# Unpublished material lighting correction

The previous overlay bound its constants only to VS b0, despite the pixel
shaders reading eye position and material lighting from PS b0. Bind the correct
buffer independently for hands and tools and preserve/release the original PS
buffer through the enclosing graphics-state guard.

The last player-state packet also reported daylight zero. Creative/Survival now
mirror the cycle actually supplied to setOutdoorLighting at 4 Hz. Player code
accepts only a recent packet, falls back to the getter, logs probe failures, and
casts cover probes from the tool origin. No engine render settings are changed.

Validation: native build, seven HLSL entry points, Lua syntax and mocked
noon/night/cover/stale-packet regression checks. Headset validation still needed.

Limitations: the directional light is an estimated world-space direction;
cover is a coarse physics probe, not engine shadow maps. Colored creation
headlights and scene reflections are not yet integrated. These require an
actual local-light/engine rendering path, not another brightness adjustment.
