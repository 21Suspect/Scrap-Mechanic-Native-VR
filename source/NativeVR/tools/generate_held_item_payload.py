#!/usr/bin/env python3
"""Generate Chapter 2 held-item adapters from the installed stock Lua files.

The stock scripts stay authoritative for animations, inventory transactions and
networking.  This generator only adds four VR concerns: tracked trigger input,
tracked action rays/origins, adapter identification for the native renderer and
desktop-safe fallback behavior.
"""

from __future__ import annotations

import argparse
from pathlib import Path


TOOLS = {
    "Bucket.lua": ("Bucket", "bucket", "self, primaryState, secondaryState, forceBuildActive"),
    "Glowstick.lua": ("Glowstick", "glowstick", "self, primaryState, secondaryState, forceBuildActive"),
    "Cornade.lua": ("Cornade", "cornade", "self, primaryState, secondaryState, forceBuildActive"),
    "ClayTool.lua": ("ClayTool", "clay", "self, primaryState, secondaryState, forceBuildActive"),
    "ExtinguisherTool.lua": ("ExtinguisherTool", "extinguisher", "self, primaryState, secondaryState, forceBuildActive"),
    "Planter.lua": ("Planter", "planter", "self, primaryState, secondaryState"),
    "Fertilizer.lua": ("Fertilizer", "fertilizer", "self, primaryState, secondaryState"),
    "Eat.lua": ("Eat", "food", "self, primaryState, secondaryState, forceBuildActive"),
    "Feeder.lua": ("Feeder", "feeder", "self, primaryState, secondaryState"),
    "SoilBag.lua": ("SoilBag", "soilbag", "self, primaryState, secondaryState, forceBuildActive"),
    "KeyTool.lua": ("KeyTool", "key", "self, primaryState, secondaryState"),
    "ResourceTool.lua": ("ResourceTool", "resource", "self, primaryState, secondaryState"),
    "CarryTool.lua": ("CarryTool", "carry", "self, primaryState, secondaryState"),
    "LogBook.lua": ("LogBook", "logbook", "self, primaryState, secondaryState"),
}


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one stock match, found {count}")
    return text.replace(old, new, 1)


def replace_span(text: str, start: str, end: str, new: str, label: str) -> str:
    if text.count(start) != 1:
        raise RuntimeError(f"{label}: expected one start marker, found {text.count(start)}")
    begin = text.index(start)
    finish = text.find(end, begin)
    if finish < 0:
        raise RuntimeError(f"{label}: end marker not found")
    finish += len(end)
    return text[:begin] + new + text[finish:]


def inject_adapter(text: str, class_name: str, adapter: str, args: str) -> str:
    marker = f"function {class_name}.client_onEquippedUpdate( {args} )\n"
    injected = marker + (
        f'\tif Chapter2VR then\n'
        f'\t\tif Chapter2VR.markAdapter then Chapter2VR.markAdapter( "{adapter}" ) end\n'
        f'\t\tif Chapter2VR.primaryState then primaryState = Chapter2VR.primaryState( self, primaryState ) end\n'
        f'\tend\n'
    )
    return replace_once(text, marker, injected, f"{class_name} adapter hook")


def tracked_latest_raycast(owner: str = "self.tool:getOwner():getCharacter()", distance: str = "7.5") -> str:
    return (
        "local success, result\n"
        "\tif Chapter2VR and Chapter2VR.actionRaycast then\n"
        f"\t\tsuccess, result = Chapter2VR.actionRaycast( {distance}, {owner} )\n"
        "\telse\n"
        "\t\tsuccess, result = sm.localPlayer.getLatestRaycast()\n"
        "\tend"
    )


def patch_bucket(text: str) -> str:
    text = replace_once(
        text,
        "\t\tlocal rayStart = sm.localPlayer.getRaycastStart()\n"
        "\t\tlocal rayDir = sm.localPlayer.getDirection()\n"
        "\t\tlocal success, result = sm.physics.raycast( rayStart, rayStart + rayDir * 7.5, sm.localPlayer.getPlayer().character, bit.bor( sm.physics.filter.default, sm.physics.filter.areaTrigger ) )",
        "\t\tlocal rayStart = sm.localPlayer.getRaycastStart()\n"
        "\t\tlocal rayDir = sm.localPlayer.getDirection()\n"
        "\t\tlocal vrPose, vrActive = nil, false\n"
        "\t\tif Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end\n"
        "\t\tif vrActive then\n"
        "\t\t\trayStart = vrPose.position\n"
        "\t\t\trayDir = vrPose.direction:normalize()\n"
        "\t\tend\n"
        "\t\tlocal success, result = sm.physics.raycast( rayStart, rayStart + rayDir * 7.5, sm.localPlayer.getPlayer().character, bit.bor( sm.physics.filter.default, sm.physics.filter.areaTrigger ) )",
        "Bucket fill ray",
    )
    return replace_once(
        text,
        "\t\t\tlocal firstPerson = self.tool:isInFirstPersonView()\n"
        "\t\t\tlocal dir = sm.localPlayer.getDirection()\n"
        "\t\t\tlocal firePos = self:calculateFirePosition()\n"
        "\t\t\tlocal fakePos = self:calculateTpMuzzlePos()\n"
        "\t\t\t\n"
        "\t\t\tlocal forward = sm.vec3.new( 0, 0, 1 ):cross( sm.localPlayer.getRight() )\n"
        "\t\t\tlocal pitchScale = forward:dot( dir )\n"
        "\t\t\tdir = dir:rotate( math.rad( pitchScale * 18 ), sm.camera.getRight() )",
        "\t\t\tlocal firstPerson = self.tool:isInFirstPersonView()\n"
        "\t\t\tlocal vrPose, vrActive = nil, false\n"
        "\t\t\tif Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end\n"
        "\t\t\tlocal dir = vrActive and vrPose.direction:normalize() or sm.localPlayer.getDirection()\n"
        "\t\t\tlocal firePos = vrActive and vrPose.position or self:calculateFirePosition()\n"
        "\t\t\tlocal fakePos = vrActive and vrPose.position or self:calculateTpMuzzlePos()\n"
        "\t\t\tif not vrActive then\n"
        "\t\t\t\tlocal forward = sm.vec3.new( 0, 0, 1 ):cross( sm.localPlayer.getRight() )\n"
        "\t\t\t\tlocal pitchScale = forward:dot( dir )\n"
        "\t\t\t\tdir = dir:rotate( math.rad( pitchScale * 18 ), sm.camera.getRight() )\n"
        "\t\t\tend",
        "Bucket throw pose",
    )


def patch_glowstick(text: str) -> str:
    old = """\t\t\tif self.tool:getOwner().character then
\t\t\t\tlocal facingDir = sm.localPlayer.getDirection()

\t\t\t\t-- local dir = sm.localPlayer.getDirection()
\t\t\t\tlocal modifier = math.sqrt( 1 - ( facingDir.z * facingDir.z ) ) -- Pythagorean identity
\t\t\t\t-- local firePos = GetOwnerPosition( self.tool ) + sm.vec3.new( 0, 0, 0.5 )

\t\t\t\t\t



\t\t\t\t-- Scale down throw velocity when looking down
\t\t\t\tlocal maxVelocity = 25.0
\t\t\t\tlocal minVelocity = 15.0
\t\t\t\tlocal fireVelocity = minVelocity + ( maxVelocity - minVelocity ) * modifier

\t\t\t\tlocal dir = facingDir:rotate( math.rad( 6.943279 ) * modifier, sm.camera.getRight() ) -- 15 m sight calibration


\t\t\t\tlocal handPosition = self.tool:getTpBonePos("jnt_right_hand")
\t\t\t\t
\t\t\t\tlocal centeredFirePos = GetOwnerPosition(self.tool) + sm.vec3.new(0, 0, 0.5)
\t\t\t\tlocal right = facingDir:cross(sm.vec3.new(0,0,1))
\t\t\t\tlocal screenOffset = right * 0.4
\t\t\t\tlocal firePos = handPosition + facingDir * -0.5 + screenOffset
\t\t\t\tlocal rayCastDir = (centeredFirePos - firePos):normalize()
\t\t\t\tlocal firePosTest = firePos + rayCastDir * -0.3
\t\t\t\t
\t\t\t\tlocal hit = sm.physics.raycast(firePosTest, centeredFirePos, self.tool:getOwner().character)
\t\t\t\tlocal hitColor = sm.color.new(1, 0, 0)
\t\t\t\tif hit then
\t\t\t\t\thitColor = sm.color.new(0, 1, 0)
\t\t\t\tend
\t\t\t\tsm.debugDraw.addArrow("firePosAndCenterd", firePosTest, centeredFirePos, hitColor)
\t\t\t\tif hit then
\t\t\t\t\tfirePos = handPosition
\t\t\t\tend
\t\t\t\tif self.tool:isInFirstPersonView() then
\t\t\t\t\tfirePos = centeredFirePos
\t\t\t\tend

\t\t\t\tsm.projectile.projectileAttack( projectile_glowstick, 0, firePos, dir * fireVelocity, self.tool:getOwner(),handPosition )
\t\t\tend"""
    new = """\t\t\tif self.tool:getOwner().character then
\t\t\t\tlocal vrPose, vrActive = nil, false
\t\t\t\tif Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end
\t\t\t\tlocal facingDir = vrActive and vrPose.direction:normalize() or sm.localPlayer.getDirection()
\t\t\t\tlocal modifier = math.sqrt( math.max( 0, 1 - ( facingDir.z * facingDir.z ) ) )
\t\t\t\tlocal maxVelocity = 25.0
\t\t\t\tlocal minVelocity = 15.0
\t\t\t\tlocal fireVelocity = minVelocity + ( maxVelocity - minVelocity ) * modifier
\t\t\t\tlocal dir = facingDir
\t\t\t\tlocal handPosition = vrActive and vrPose.position or self.tool:getTpBonePos("jnt_right_hand")
\t\t\t\tlocal firePos = handPosition
\t\t\t\tif not vrActive then
\t\t\t\t\tdir = facingDir:rotate( math.rad( 6.943279 ) * modifier, sm.camera.getRight() )
\t\t\t\t\tlocal centeredFirePos = GetOwnerPosition(self.tool) + sm.vec3.new(0, 0, 0.5)
\t\t\t\t\tlocal right = facingDir:cross(sm.vec3.new(0,0,1))
\t\t\t\t\tlocal screenOffset = right * 0.4
\t\t\t\t\tfirePos = handPosition + facingDir * -0.5 + screenOffset
\t\t\t\t\tlocal rayCastDir = (centeredFirePos - firePos):normalize()
\t\t\t\t\tlocal firePosTest = firePos + rayCastDir * -0.3
\t\t\t\t\tlocal hit = sm.physics.raycast(firePosTest, centeredFirePos, self.tool:getOwner().character)
\t\t\t\t\tif hit then firePos = handPosition end
\t\t\t\t\tif self.tool:isInFirstPersonView() then firePos = centeredFirePos end
\t\t\t\tend
\t\t\t\tsm.projectile.projectileAttack( projectile_glowstick, 0, firePos, dir * fireVelocity, self.tool:getOwner(), handPosition )
\t\t\tend"""
    return replace_span(
        text,
        "\t\t\tif self.tool:getOwner().character then\n\t\t\t\tlocal facingDir = sm.localPlayer.getDirection()",
        "\t\t\t\tsm.projectile.projectileAttack( projectile_glowstick, 0, firePos, dir * fireVelocity, self.tool:getOwner(),handPosition )\n\t\t\tend",
        new,
        "Glowstick tracked throw",
    )


def patch_cornade(text: str) -> str:
    old = """\t\t\tif self.tool:getOwner().character then
\t\t\t\tlocal facingDir = sm.localPlayer.getDirection()
\t\t\t\tlocal modifier = math.sqrt( 1 - (facingDir.z * facingDir.z) ) -- Pythagorean identity

\t\t\t\tlocal handPosition = self.tool:getTpBonePos( "jnt_right_hand" )

\t\t\t\t-- Scale down throw velocity when looking down
\t\t\t\tlocal maxVelocity = 25.0
\t\t\t\tlocal minVelocity = 15.0
\t\t\t\tlocal fireVelocity = minVelocity + (maxVelocity - minVelocity) * modifier

\t\t\t\tlocal dir = facingDir:rotate( math.rad( 6.943279 ) * modifier, sm.camera.getRight() ) -- 15 m sight calibration

\t\t\t\tlocal centeredFirePos = GetOwnerPosition( self.tool ) + sm.vec3.new( 0, 0, 0.5 )
\t\t\t\tlocal right = facingDir:cross( sm.vec3.new( 0, 0, 1 ) )
\t\t\t\tlocal screenOffset = right * -0.2
\t\t\t\tlocal firePos = handPosition + facingDir * -0.5 + screenOffset
\t\t\t\tlocal rayCastDir = (centeredFirePos - firePos):normalize()
\t\t\t\tlocal firePosTest = firePos + rayCastDir * -0.3
\t\t\t\tlocal bFirePosObstructed, _ = sm.physics.raycast( firePosTest, centeredFirePos, self.tool:getOwner().character )
\t\t\t\tif bFirePosObstructed then
\t\t\t\t\tfirePos = centeredFirePos
\t\t\t\tend
\t\t\t\tif self.tool:isInFirstPersonView() then
\t\t\t\t\tfirePos = centeredFirePos
\t\t\t\tend

\t\t\t\tsm.debugDraw.addSphere( "Firepos", firePos, 0.1, sm.color.new( 1, 0, 0 ) )
\t\t\t\tlocal params = {
\t\t\t\t\tfakePosition = firePos,
\t\t\t\t\tposition = firePos,
\t\t\t\t\tfireVel = dir * fireVelocity,
\t\t\t\t\towner = self.tool:getOwner()
\t\t\t\t}
\t\t\t\tif self.tool:isLocal() then
\t\t\t\t\tsetFpAnimation( self.fpAnimations, "equip", 0.2 )
\t\t\t\tend
\t\t\t\tsetTpAnimation( self.tpAnimations, "equip", 5.0 )
\t\t\t\tself.network:sendToServer( "sv_n_spawn", params )
\t\t\tend"""
    new = """\t\t\tif self.tool:getOwner().character then
\t\t\t\tlocal vrPose, vrActive = nil, false
\t\t\t\tif Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end
\t\t\t\tlocal facingDir = vrActive and vrPose.direction:normalize() or sm.localPlayer.getDirection()
\t\t\t\tlocal modifier = math.sqrt( math.max( 0, 1 - (facingDir.z * facingDir.z) ) )
\t\t\t\tlocal handPosition = vrActive and vrPose.position or self.tool:getTpBonePos( "jnt_right_hand" )
\t\t\t\tlocal maxVelocity = 25.0
\t\t\t\tlocal minVelocity = 15.0
\t\t\t\tlocal fireVelocity = minVelocity + (maxVelocity - minVelocity) * modifier
\t\t\t\tlocal dir = facingDir
\t\t\t\tlocal firePos = handPosition
\t\t\t\tif not vrActive then
\t\t\t\t\tdir = facingDir:rotate( math.rad( 6.943279 ) * modifier, sm.camera.getRight() )
\t\t\t\t\tlocal centeredFirePos = GetOwnerPosition( self.tool ) + sm.vec3.new( 0, 0, 0.5 )
\t\t\t\t\tlocal right = facingDir:cross( sm.vec3.new( 0, 0, 1 ) )
\t\t\t\t\tlocal screenOffset = right * -0.2
\t\t\t\t\tfirePos = handPosition + facingDir * -0.5 + screenOffset
\t\t\t\t\tlocal rayCastDir = (centeredFirePos - firePos):normalize()
\t\t\t\t\tlocal firePosTest = firePos + rayCastDir * -0.3
\t\t\t\t\tlocal obstructed = sm.physics.raycast( firePosTest, centeredFirePos, self.tool:getOwner().character )
\t\t\t\t\tif obstructed or self.tool:isInFirstPersonView() then firePos = centeredFirePos end
\t\t\t\tend
\t\t\t\tlocal params = {
\t\t\t\t\tfakePosition = firePos,
\t\t\t\t\tposition = firePos,
\t\t\t\t\tfireVel = dir * fireVelocity,
\t\t\t\t\towner = self.tool:getOwner()
\t\t\t\t}
\t\t\t\tif self.tool:isLocal() then setFpAnimation( self.fpAnimations, "equip", 0.2 ) end
\t\t\t\tsetTpAnimation( self.tpAnimations, "equip", 5.0 )
\t\t\t\tself.network:sendToServer( "sv_n_spawn", params )
\t\t\tend"""
    return replace_span(
        text,
        "\t\t\tif self.tool:getOwner().character then\n\t\t\t\tlocal facingDir = sm.localPlayer.getDirection()",
        "\t\t\t\tself.network:sendToServer( \"sv_n_spawn\", params )\n\t\t\tend",
        new,
        "Cornade tracked throw",
    )


def patch_clay(text: str) -> str:
    old = """\t\tif primaryState == sm.tool.interactState.start then
\t\t\tlocal firstPerson = self.tool:isInFirstPersonView()
\t\t\tlocal cameraPos = sm.camera.getPosition()
\t\t\tlocal cameraDir = sm.camera.getDirection()
\t\t\tlocal maxRange = 5.0
\t\t\tif not firstPerson then
\t\t\t\tlocal raycastPos = sm.camera.getPosition() + sm.camera.getDirection() * sm.camera.getDirection():dot( GetOwnerPosition( self.tool ) - sm.camera.getPosition() )
\t\t\t\thit, result = sm.localPlayer.getRaycast( maxRange, raycastPos, sm.camera.getDirection() )
\t\t\telse 
\t\t\t\thit, result = sm.physics.raycast( cameraPos, cameraPos + cameraDir * maxRange,self.tool:getOwner():getCharacter() )
\t\t\t\t
\t\t\tend"""
    new = """\t\tif primaryState == sm.tool.interactState.start then
\t\t\tlocal firstPerson = self.tool:isInFirstPersonView()
\t\t\tlocal cameraPos = sm.camera.getPosition()
\t\t\tlocal cameraDir = sm.camera.getDirection()
\t\t\tlocal maxRange = 5.0
\t\t\tlocal vrPose, vrActive = nil, false
\t\t\tif Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end
\t\t\tif vrActive then
\t\t\t\thit, result = sm.physics.raycast( vrPose.position, vrPose.position + vrPose.direction * maxRange, self.tool:getOwner():getCharacter() )
\t\t\telseif not firstPerson then
\t\t\t\tlocal raycastPos = sm.camera.getPosition() + sm.camera.getDirection() * sm.camera.getDirection():dot( GetOwnerPosition( self.tool ) - sm.camera.getPosition() )
\t\t\t\thit, result = sm.localPlayer.getRaycast( maxRange, raycastPos, sm.camera.getDirection() )
\t\t\telse
\t\t\t\thit, result = sm.physics.raycast( cameraPos, cameraPos + cameraDir * maxRange, self.tool:getOwner():getCharacter() )
\t\t\tend"""
    return replace_once(text, old, new, "Clay tracked placement ray")


def patch_extinguisher(text: str) -> str:
    text = replace_once(
        text,
        "\tif self.tool:isLocal() then\n"
        "\t\tlocal dir = sm.localPlayer.getDirection()\n"
        "\t\tlocal firePos = self.tool:getFpBonePos( \"pejnt_muzzle\" )\n\n"
        "\t\teffectPos = firePos + dir * 0.2\n\n"
        "\t\trot = sm.vec3.getRotation( sm.vec3.new( 0, 0, 1 ), dir )",
        "\tif self.tool:isLocal() then\n"
        "\t\tlocal vrPose, vrActive = nil, false\n"
        "\t\tif Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end\n"
        "\t\tlocal dir = vrActive and vrPose.direction:normalize() or sm.localPlayer.getDirection()\n"
        "\t\tlocal firePos = vrActive and vrPose.position or self.tool:getFpBonePos( \"pejnt_muzzle\" )\n\n"
        "\t\teffectPos = firePos + dir * 0.2\n\n"
        "\t\trot = sm.vec3.getRotation( sm.vec3.new( 0, 0, 1 ), dir )",
        "Extinguisher tracked effect",
    )
    old = """\t\tlocal firstPerson = self.tool:isInFirstPersonView()

\t\tlocal dir = sm.localPlayer.getDirection()

\t\tlocal firePos = self:calculateFirePosition()
\t\tlocal fakePosition = self:calculateTpMuzzlePos()
\t\tlocal fakePositionSelf = fakePosition
\t\tif firstPerson then
\t\t\tfakePositionSelf = self:calculateFpMuzzlePos()
\t\tend

\t\t-- Aim assist
\t\tif not firstPerson then
\t\t\tlocal raycastPos = sm.camera.getPosition() + sm.camera.getDirection() * sm.camera.getDirection():dot( GetOwnerPosition( self.tool ) - sm.camera.getPosition() )
\t\t\tlocal hit, result = sm.localPlayer.getRaycast( 250, raycastPos, sm.camera.getDirection() )
\t\t\tif hit then
\t\t\t\tlocal norDir = sm.vec3.normalize( result.pointWorld - firePos )
\t\t\t\tlocal dirDot = norDir:dot( dir )

\t\t\t\tif dirDot > 0.96592583 then -- max 15 degrees off
\t\t\t\t\tdir = norDir
\t\t\t\telse
\t\t\t\t\tlocal radsOff = math.asin( dirDot )
\t\t\t\t\tdir = sm.vec3.lerp( dir, norDir, math.tan( radsOff ) / 3.7320508 ) -- if more than 15, make it 15
\t\t\t\tend
\t\t\tend
\t\tend

\t\tdir = dir:rotate( math.rad( 0.955 ), sm.camera.getRight() ) -- 50 m sight calibration"""
    new = """\t\tlocal firstPerson = self.tool:isInFirstPersonView()
\t\tlocal vrPose, vrActive = nil, false
\t\tif Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end
\t\tlocal dir = vrActive and vrPose.direction:normalize() or sm.localPlayer.getDirection()
\t\tlocal firePos = vrActive and vrPose.position or self:calculateFirePosition()
\t\tlocal fakePosition = vrActive and vrPose.position or self:calculateTpMuzzlePos()
\t\tlocal fakePositionSelf = fakePosition
\t\tif firstPerson and not vrActive then fakePositionSelf = self:calculateFpMuzzlePos() end

\t\t-- Keep the stock camera aim assist and 50 m sight correction on desktop only.
\t\tif not vrActive then
\t\t\tif not firstPerson then
\t\t\t\tlocal raycastPos = sm.camera.getPosition() + sm.camera.getDirection() * sm.camera.getDirection():dot( GetOwnerPosition( self.tool ) - sm.camera.getPosition() )
\t\t\t\tlocal hit, result = sm.localPlayer.getRaycast( 250, raycastPos, sm.camera.getDirection() )
\t\t\t\tif hit then
\t\t\t\t\tlocal norDir = sm.vec3.normalize( result.pointWorld - firePos )
\t\t\t\t\tlocal dirDot = norDir:dot( dir )
\t\t\t\t\tif dirDot > 0.96592583 then
\t\t\t\t\t\tdir = norDir
\t\t\t\t\telse
\t\t\t\t\t\tlocal radsOff = math.asin( dirDot )
\t\t\t\t\t\tdir = sm.vec3.lerp( dir, norDir, math.tan( radsOff ) / 3.7320508 )
\t\t\t\t\tend
\t\t\t\tend
\t\t\tend
\t\t\tdir = dir:rotate( math.rad( 0.955 ), sm.camera.getRight() )
\t\tend"""
    return replace_once(text, old, new, "Extinguisher tracked projectile")


def patch_carry(text: str) -> str:
    text = replace_once(
        text,
        "\tlocal aimRange = 7.5\n\tlocal success, result = sm.localPlayer.getRaycast( aimRange )",
        "\tlocal aimRange = 7.5\n"
        "\tlocal vrPose, vrActive = nil, false\n"
        "\tif Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end\n"
        "\tlocal success, result\n"
        "\tif vrActive then\n"
        "\t\tsuccess, result = sm.physics.raycast( vrPose.position, vrPose.position + vrPose.direction * aimRange, character )\n"
        "\telse\n"
        "\t\tsuccess, result = sm.localPlayer.getRaycast( aimRange )\n"
        "\tend",
        "Carry insert ray",
    )
    old = """\tif not (( primaryState == sm.tool.interactState.start and characterShape ) or secondaryState == sm.tool.interactState.start) then
\t\treturn false
\tend

\tlocal dropRange = 7.5
\tlocal success, result = sm.localPlayer.getRaycast( dropRange )

\tlocal fraction = success and result.fraction or 1

\tlocal aimPosition = sm.localPlayer.getRaycastStart() + sm.localPlayer.getDirection() * dropRange * fraction
\tlocal params = {
\t\tcontainerA = playerCarry, itemA = carryUuid, quantityA = 1,
\t\taimPosition = aimPosition,
\t\tcamUp = sm.camera.getUp(), camRight = sm.camera.getRight(), camDirection = sm.camera.getDirection(),
\t\traycastNormal = result.normalWorld, color = playerCarryColor
\t}"""
    new = """\tlocal vrPose, vrActive = nil, false
\tif Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end
\tif not (( primaryState == sm.tool.interactState.start and ( characterShape or vrActive ) ) or secondaryState == sm.tool.interactState.start) then
\t\treturn false
\tend

\tlocal dropRange = 7.5
\tlocal rayStart = vrActive and vrPose.position or sm.localPlayer.getRaycastStart()
\tlocal rayDirection = vrActive and vrPose.direction:normalize() or sm.localPlayer.getDirection()
\tlocal success, result
\tif vrActive then
\t\tsuccess, result = sm.physics.raycast( rayStart, rayStart + rayDirection * dropRange, self.tool:getOwner().character )
\telse
\t\tsuccess, result = sm.localPlayer.getRaycast( dropRange )
\tend

\tlocal fraction = success and result.fraction or 1
\tlocal aimPosition = rayStart + rayDirection * dropRange * fraction
\tlocal params = {
\t\tcontainerA = playerCarry, itemA = carryUuid, quantityA = 1,
\t\taimPosition = aimPosition,
\t\tcamUp = vrActive and vrPose.up or sm.camera.getUp(),
\t\tcamRight = vrActive and vrPose.right or sm.camera.getRight(),
\t\tcamDirection = rayDirection,
\t\traycastNormal = result.normalWorld, color = playerCarryColor
\t}"""
    text = replace_once(text, old, new, "Carry drop pose")
    text = replace_once(
        text,
        "\tlocal worldPosition, worldRotation = sm.localPlayer.getConstructionPlacement()\n"
        "\tif worldPosition ~= nil and worldRotation ~= nil then",
        "\t-- The native VR ray hook feeds the current action pose into the engine's\n"
        "\t-- latest player ray.  Keep using the stock construction-placement helper\n"
        "\t-- for VR as well so carried bodies (including multi-shape/large items) get\n"
        "\t-- the same grid snapping and collision validation as desktop placement.\n"
        "\tlocal worldPosition, worldRotation = sm.localPlayer.getConstructionPlacement()\n"
        "\tif worldPosition ~= nil and worldRotation ~= nil then",
        "Carry desktop construction placement",
    )
    return replace_once(
        text,
        "\tself:cl_tryTumbleDrop( character, playerCarry, carryUuid, characterShape, playerCarryColor )\n\n"
        "    if primaryState == sm.tool.interactState.start and characterShape or secondaryState == sm.tool.interactState.start  then",
        "\tself:cl_tryTumbleDrop( character, playerCarry, carryUuid, characterShape, playerCarryColor )\n\n"
        "\tlocal _, vrActive = nil, false\n"
        "\tif Chapter2VR and Chapter2VR.actionPose then _, vrActive = Chapter2VR.actionPose() end\n"
        "    if primaryState == sm.tool.interactState.start and ( characterShape or vrActive ) or secondaryState == sm.tool.interactState.start then",
        "Carry right-trigger drop",
    )


def patch_fertilizer_effect(text: str) -> str:
    marker = "function Fertilizer.onUse( self )\n"
    injected = marker + (
        "\tlocal vrPose, vrActive = nil, false\n"
        "\tif Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end\n"
    )
    text = replace_once(text, marker, injected, "Fertilizer effect pose")
    old = """\tif self.tool:isLocal() and self.tool:isInFirstPersonView() then
\t\tlocal effectPos = sm.localPlayer.getFpBonePos( "jnt_fertilizer" )
\t\tif effectPos then
\t\t\tlocal rot = sm.vec3.getRotation( sm.vec3.new( 0, 0, 1 ), sm.localPlayer.getDirection() )"""
    new = """\tif vrActive then
\t\tlocal rot = sm.vec3.getRotation( sm.vec3.new( 0, 0, 1 ), vrPose.direction )
\t\tsm.effect.playEffect( "Itemtool - FPFertilizerUse", vrPose.position, nil, rot )
\telseif self.tool:isLocal() and self.tool:isInFirstPersonView() then
\t\tlocal effectPos = sm.localPlayer.getFpBonePos( "jnt_fertilizer" )
\t\tif effectPos then
\t\t\tlocal rot = sm.vec3.getRotation( sm.vec3.new( 0, 0, 1 ), sm.localPlayer.getDirection() )"""
    return replace_once(text, old, new, "Fertilizer tracked visual")


def patch_common_rays(name: str, text: str) -> str:
    if name in {"Planter.lua", "Fertilizer.lua", "Feeder.lua", "KeyTool.lua", "ResourceTool.lua"}:
        text = replace_once(
            text,
            "local success, result = sm.localPlayer.getLatestRaycast()",
            tracked_latest_raycast(),
            f"{name} tracked action ray",
        )
    elif name == "SoilBag.lua":
        text = replace_once(
            text,
            "local valid, result = sm.localPlayer.getLatestRaycast()",
            tracked_latest_raycast().replace("local success, result", "local valid, result").replace("success, result =", "valid, result ="),
            "SoilBag tracked construction ray",
        )
    return text


def patch_file(name: str, stock: str) -> str:
    class_name, adapter, args = TOOLS[name]
    text = inject_adapter(stock, class_name, adapter, args)
    text = patch_common_rays(name, text)
    if name == "Bucket.lua":
        text = patch_bucket(text)
    elif name == "Glowstick.lua":
        text = patch_glowstick(text)
    elif name == "Cornade.lua":
        text = patch_cornade(text)
    elif name == "ClayTool.lua":
        text = patch_clay(text)
    elif name == "ExtinguisherTool.lua":
        text = patch_extinguisher(text)
    elif name == "CarryTool.lua":
        text = patch_carry(text)
    elif name == "Fertilizer.lua":
        text = patch_fertilizer_effect(text)
    return "-- Generated from the supported stock build by generate_held_item_payload.py.\n" + text


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("game_root", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()

    stock_dir = args.game_root / "Survival" / "Scripts" / "game" / "tools"
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name in TOOLS:
        source = stock_dir / name
        if not source.is_file():
            raise FileNotFoundError(source)
        stock = source.read_text(encoding="utf-8-sig").replace("\r\n", "\n")
        generated = patch_file(name, stock)
        (args.output_dir / name).write_text(generated, encoding="utf-8", newline="\n")
        print(f"generated {name}")


if __name__ == "__main__":
    main()
