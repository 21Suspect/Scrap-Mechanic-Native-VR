# Scrap Mechanic VR 1.4.10

- Updated for Scrap Mechanic 1.0.6.889 (Steam build 25442087).
- Improved VR hand/tool material lighting, normal detail and contrast; fixed missing pixel-shader lighting bindings.
- Added day/night lighting input from Creative and Survival, plus approximate cover shading.
- Refreshed the compiled game-data cache to address incorrect tool icons after updating the game.
- Refresh controller poses immediately before drawing hands and held items, without extra smoothing delay. Headset feel may vary; feedback welcome.

Close Scrap Mechanic before installing. The first launch may rebuild cached data.

Known limitations: cover shading is approximate, not projected engine shadows. Colored creation headlights do not yet illuminate VR hands/tools. Optical-hand tracking is unchanged by the late controller-pose refresh.
