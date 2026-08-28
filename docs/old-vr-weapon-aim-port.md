# Legacy weapon aiming and the Chapter 2 port

This note records the behavior verified from the local checkout of the old GitHub implementation. It is architecture evidence, not a claim that the new path works in a headset.

## Old implementation

`Survival/Scripts/game/SurvivalPlayer.lua` polls `Data/NativeVR/hand_physics.json`, requires a changing sequence, and builds an orthonormal world-space basis from the tracked right hand:

- `forward` is the controller aim direction.
- `up` is projected perpendicular to `forward` and normalized.
- `right = forward:cross(up)`.
- A weapon muzzle is `handPosition + right*x + up*y - forward*z`.

The legacy rifle, shotgun, and gatling scripts then use that result at their firing functions. A VR shot:

- uses the tracked muzzle instead of `calculateFirePosition()`;
- uses the tracked muzzle for the fake third- and first-person origins;
- skips the desktop third-person camera aim assist;
- uses tracked `forward` instead of the camera direction;
- skips the desktop 0.955-degree sight calibration;
- applies the weapon's normal spread; and
- passes the resulting origin and direction directly to `sm.projectile.projectileAttack` and the replicated shot-effect path.

The legacy controller-local muzzle offsets are rifle `{-0.198,-0.035,-0.466}`, shotgun `{-0.199,-0.035,-0.503}`, and gatling `{-0.198,-0.035,-0.509}`.

## Why the first Chapter 2 port did not work

The first port placed `getGunAim` on the player-owned `Chapter2VR` table. Runtime diagnostics showed the native hand JSON advancing and the active gun changing, but produced no weapon-aim or redirected-shot records. Scrap Mechanic 1.0 executes equipped tools on separate Logic Tasks, so a player-owned Lua bridge is not a reliable dependency for the weapon callback.

## Chapter 2 local candidates

Every patched Chapter 2 gun now explicitly loads `Chapter2VRWeaponAim.lua`. That module independently reads the advancing native bridge in the weapon execution context, reconstructs the same old basis/muzzle equation, and fails closed to normal PC aiming when the bridge is absent, stale, invalid, or belongs to an earlier process. The six current weapons pass fixed UUIDs to the module and replace their projectile origin/direction at the actual firing call.

The 0.2.4 headset test proved that merely reading at muzzle-effect/fire callbacks was insufficient: the module loaded, but no weapon accepted an advancing pose and every projectile remained on the desktop-camera path. Candidate 0.2.5 polls the bridge from every equipped-weapon update, matching the legacy player's continuous polling. A trigger callback therefore consumes an already-primed, recent pose instead of trying to establish freshness on the first shot.

The trace markers are deliberately separate:

- `SCRAPVR_WEAPON_MODULE_LOADED`: the tool script loaded the module.
- `SCRAPVR_WEAPON_AIM_ACTIVE`: an advancing tracked pose was accepted for that weapon.
- `SCRAPVR_GUNSHOT`: the logged muzzle/direction were passed down the VR firing branch.

Only a headset test can confirm that the projectile visually leaves the rendered barrel.
