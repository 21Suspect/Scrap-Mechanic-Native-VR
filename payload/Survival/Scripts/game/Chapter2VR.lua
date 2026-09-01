-- Additive Chapter 2 bridge. The stock 1.0 SurvivalPlayer remains the owner of
-- survival state; these functions only exchange validated VR tracking data.
Chapter2VR = Chapter2VR or {}

local VrInteractiveSwitch = sm.uuid.new( "7cf717d7-d167-4f2d-a6e7-6b2c70aa3986" )
local VrInteractiveButton = sm.uuid.new( "1e8d93a4-506b-470d-9ada-9c0a321e2db5" )
local VrElevatorButton = sm.uuid.new( "a553bf2f-3a66-404a-b4c6-ce9e7b73f9d4" )
local VrElevatorCallButton = sm.uuid.new( "61bdf048-c09f-4cf5-8b47-35ba28c0580c" )

local VrActionLocalOffsets = {
	["8c7efc37-cd7c-4262-976e-39585f8527bf"] = { -0.152, -0.035, -0.280 },
	["c60b9627-fc2b-4319-97c5-05921cb976c6"] = { -0.120, -0.040, -0.295 },
	["fdb8b8be-96e7-4de0-85c7-d2f42e4f33ce"] = { -0.035, -0.035, -0.225 },
	["8f190ce2-3a59-423e-8483-a7aa67bd5bc0"] = { 0.000, -0.035, -0.120 },
	["5cc12f03-275e-4c8e-b013-79fc0f913e1b"] = { 0.000, -0.035, -0.120 },
	["798c2c81-1f8e-481b-8c32-b71b5dc5511a"] = { 0.000, -0.035, -0.170 },
	["103fc4e6-7e57-465e-a86d-983343415877"] = { 0.000, -0.035, -0.170 },
	["2e792123-4a10-4cc6-b9ef-c5a518655cb4"] = { 0.000, -0.035, -0.170 },
	["cc80b6e0-f756-4036-9cd6-77af13a6de36"] = { 0.000, -0.035, -0.170 },
	["3a3280e4-03b6-4a4d-9e02-e348478213c9"] = { 0.000, -0.030, -0.140 },
	["e3bdeea5-d349-4d08-9b5a-5695ea05537e"] = { 0.000, -0.035, -0.150 },
	["6395a2f1-4169-4a7e-be15-a9864cb6ce7e"] = { 0.000, -0.035, -0.160 },
	["d2fab7ef-21db-4681-a22a-cd4f278fc355"] = { -0.090, -0.035, -0.310 }
}

local VrBaseWorldStatePath = "$GAME_DATA/NativeVR/world_state.json"
local VrBasePlayerStatePath = "$GAME_DATA/NativeVR/player_state.json"
local VrBaseHandBridgePath = "$GAME_DATA/NativeVR/hand_physics.json"
local VrBaseGunFireDebugPath = "$GAME_DATA/NativeVR/gun_fire_debug.json"
-- A Custom Game script executes under that Custom Game's content id. Scrap
-- Mechanic deliberately rejects writes into $GAME_DATA from that caller even
-- when the imported script itself lives in Survival. The native add-on finds
-- this root-level state marker and mirrors hand tracking back into the same
-- content id, keeping the bridge legal without modifying the Custom Game.
local VrContentWorldStatePath = "$CONTENT_DATA/ScrapMechanicVR_world_state.json"
local VrContentPlayerStatePath = "$CONTENT_DATA/ScrapMechanicVR_player_state.json"
local VrContentHandBridgePath = "$CONTENT_DATA/ScrapMechanicVR_hand_physics.json"
local VrContentGunFireDebugPath = "$CONTENT_DATA/ScrapMechanicVR_gun_fire_debug.json"
local VrHandBridgePath = VrBaseHandBridgePath
local VrGunFireDebugPath = VrBaseGunFireDebugPath
local VrContentBridgeActive = false
local VrDirectBridgeFreshTicks = 10
local VrDirectPoseFreshTicks = 6
local VrDirectAuthorityFreshTicks = 12
local VrPrimaryBridgeFreshTicks = 8
local VrInactiveBridgePollSeconds = 0.25

-- These are the same calibrated barrel-tip offsets used by the native tracked
-- tool renderer. Keeping a validated copy here makes projectile aiming depend
-- only on the live right-hand pose, not on the timing of the native tool-state
-- classifier packet.
local VrGunMuzzleLocalOffsets = {
	["c5ea0c2f-185b-48d6-b4df-45c386a575cc"] = { -0.198, -0.035, -0.466 },
	["f6250bf4-9726-406f-a29a-945c06e460e5"] = { -0.199, -0.035, -0.503 },
	["9fde0601-c2ba-4c70-8d5c-2a7a9fdd122b"] = { -0.198, -0.035, -0.509 },
	["d51ec758-057b-4263-bd16-7a731e149480"] = { -0.234, -0.050, -0.384 },
	["a2a2bb33-a841-4b23-88da-b758063d9206"] = { -0.198, -0.035, -0.426 },
	["6993e5df-6852-4e84-88ae-df49f765e784"] = { -0.085, -0.035, -0.424 },
	["3a3280e4-03b6-4a4d-9e02-e348478213c9"] = { 0.000, -0.035, -0.180 },
	["e3bdeea5-d349-4d08-9b5a-5695ea05537e"] = { 0.000, -0.035, -0.180 },
	["d2fab7ef-21db-4681-a22a-cd4f278fc355"] = { -0.090, -0.035, -0.310 }
}

local VrToolLaserItems = {
	["8c7efc37-cd7c-4262-976e-39585f8527bf"] = true,
	["c60b9627-fc2b-4319-97c5-05921cb976c6"] = true,
	["fdb8b8be-96e7-4de0-85c7-d2f42e4f33ce"] = true
}

local function clearGunAim()
	g_vrGunAimActive = false
	g_vrGunMuzzlePosition = nil
	g_vrGunDirection = nil
	g_vrGunItem = nil
end

local function clearTrackedAim()
	g_vrPrimaryActionAvailable = false
	g_vrPrimaryActionDown = false
	g_vrActionActive = false
	g_vrActionOrigin = nil
	g_vrActionDirection = nil
	g_vrActionUp = nil
	g_vrActionRight = nil
	g_vrToolPointerEnabled = false
	g_vrToolPointerOrigin = nil
	g_vrToolPointerDirection = nil
	clearGunAim()
end

local function clearClientAim()
	clearTrackedAim()
	g_vrBridgeActive = false
	g_vrBridgeFreshTimer = 1.0
	g_vrBridgeLastTick = -1000
end

local function activateContentBridge()
	if VrContentBridgeActive then return end
	VrContentBridgeActive = true
	VrHandBridgePath = VrContentHandBridgePath
	VrGunFireDebugPath = VrContentGunFireDebugPath
	sm.log.warning( "SCRAPVR_CUSTOM_CONTENT_BRIDGE active=1 reason=content_id_sandbox" )
end

local function resetContentBridge()
	VrContentBridgeActive = false
	VrHandBridgePath = VrBaseHandBridgePath
	VrGunFireDebugPath = VrBaseGunFireDebugPath
	Chapter2VR.bridgeWriteErrorTick = nil
end

local function saveBridgePayload( payload, basePath, contentPath, label )
	local targetPath = VrContentBridgeActive and contentPath or basePath
	local ok, errorMessage = pcall( sm.json.save, payload, targetPath )
	if not ok and not VrContentBridgeActive then
		activateContentBridge()
		ok, errorMessage = pcall( sm.json.save, payload, contentPath )
	end
	if not ok then
		local tick = sm.game.getCurrentTick()
		if Chapter2VR.bridgeWriteErrorTick == nil or tick - Chapter2VR.bridgeWriteErrorTick >= 200 then
			Chapter2VR.bridgeWriteErrorTick = tick
			sm.log.error( "SCRAPVR_" .. label .. "_WRITE_FAILED " .. tostring( errorMessage ) )
		end
	end
	return ok
end

local function publishWorldState( active )
	local ok = saveBridgePayload( {
		version = 1,
		active = active == true,
		tick = sm.game.getCurrentTick()
	}, VrBaseWorldStatePath, VrContentWorldStatePath, "WORLD_STATE" )
	if ok then
		sm.log.warning( "SCRAPVR_WORLD_STATE active=" .. tostring( active == true ) )
	end
end

local function publishPlayerState( active, seated, firstPerson, activeItem, activeAdapter )
	Chapter2VR.playerStateSequence = ( Chapter2VR.playerStateSequence or 0 ) + 1
	saveBridgePayload( {
		version = 1,
		sequence = Chapter2VR.playerStateSequence,
		active = active == true,
		seated = seated == true,
		firstPerson = firstPerson == true,
		activeItem = activeItem or "00000000-0000-0000-0000-000000000000",
		activeAdapter = activeAdapter or ""
	}, VrBasePlayerStatePath, VrContentPlayerStatePath, "PLAYER_STATE" )
end

local function validNumber( value )
	return type( value ) == "number" and value == value and math.abs( value ) < 1000000
end

local function resolveHandPose( data )
	local hand = data and data.right
	if type( hand ) ~= "table" or hand.active ~= true or not validNumber( hand.x ) or
		not validNumber( hand.y ) or not validNumber( hand.z ) or not validNumber( hand.fx ) or
		not validNumber( hand.fy ) or not validNumber( hand.fz ) then return nil end
	local forward = sm.vec3.new( hand.fx, hand.fy, hand.fz )
	if forward:length() < 0.5 then return nil end
	forward = forward:normalize()
	local up = validNumber( hand.ux ) and validNumber( hand.uy ) and validNumber( hand.uz ) and
		sm.vec3.new( hand.ux, hand.uy, hand.uz ) or sm.vec3.new( 0, 0, 1 )
	up = up - forward * up:dot( forward )
	if up:length() < 0.25 then up = sm.vec3.new( 0, 0, 1 ) - forward * forward.z end
	if up:length() < 0.25 then up = sm.vec3.new( 1, 0, 0 ) end
	up = up:normalize()
	local right = forward:cross( up )
	if right:length() < 0.25 then return nil end
	return {
		hand = hand,
		position = sm.vec3.new( hand.x, hand.y, hand.z ),
		forward = forward, up = up, right = right:normalize()
	}
end

local function resolveActionPose( data )
	local actionAim = data and data.actionAim
	if type( actionAim ) ~= "table" or actionAim.active ~= true then return nil end
	return resolveHandPose( { right = actionAim } )
end

local function handLocalPosition( pose, offset )
	return pose.position + pose.right * offset[1] + pose.up * offset[2] - pose.forward * offset[3]
end

local function handLocalDirection( pose, offset )
	local direction = pose.right * offset[1] + pose.up * offset[2] - pose.forward * offset[3]
	if direction:length() < 0.5 then return pose.forward end
	return direction:normalize()
end

local function resolveGunMuzzleOffset( activeItem, muzzle )
	if type( muzzle ) == "table" and muzzle.active == true and muzzle.item == activeItem and
		validNumber( muzzle.x ) and validNumber( muzzle.y ) and validNumber( muzzle.z ) then
		return { muzzle.x, muzzle.y, muzzle.z }, "native_calibration"
	end
	local offset = VrGunMuzzleLocalOffsets[activeItem]
	if offset then return offset, "lua_calibration" end
	return nil, "gun_calibration_inactive"
end

local function resolveGunDirection( pose, muzzle )
	if type( muzzle ) == "table" and validNumber( muzzle.dx ) and
		validNumber( muzzle.dy ) and validNumber( muzzle.dz ) then
		return handLocalDirection( pose, { muzzle.dx, muzzle.dy, muzzle.dz } )
	end
	return pose.forward
end

function Chapter2VR.serverCreate( self )
	self.sv.vrHands = {
		sequence = -1,
		hands = {},
		interactionDown = {},
		interactionCooldowns = {},
		touching = {},
		lastTick = -1000
	}
end

function Chapter2VR.clientCreate( self )
	self.cl.vrHandTimer = 0.0
	self.cl.vrHandSequence = -1
	self.cl.vrHandBridgePrimed = false
	self.cl.vrNativeToolItem = nil
	self.cl.vrHandFreshTimer = 1.0
	self.cl.vrBridgeFreshTimer = 1.0
	self.cl.vrGunFreshTimer = 1.0
	self.cl.vrGunAimItem = nil
	self.cl.vrPlayerStateTimer = 0.0
	self.cl.vrSeated = nil
	self.cl.vrFirstPerson = nil
	self.cl.vrActiveAdapter = ""
	if self.player == sm.localPlayer.getPlayer() then
		resetContentBridge()
		publishWorldState( true )
		publishPlayerState( false, false, false, nil )
		sm.log.warning( "SCRAPVR_BRIDGE_CREATE" )
	end
	clearClientAim()
end

local function updateToolAim( self, data )
	local activeItem = tostring( sm.localPlayer.getActiveItem() )
	self.cl.vrBridgeFreshTimer = 0.0
	g_vrBridgeFreshTimer = 0.0
	g_vrBridgeLastTick = sm.game.getCurrentTick()
	g_vrBridgeActive = data.vrActive == true
	local pose = resolveHandPose( data )
	if not pose then
		clearTrackedAim()
		return
	end
	local hand = pose.hand

	-- Keep the proven visual/per-item origin for throws, sprays and placements,
	-- but take direction from OpenXR's semantic aim pose. Using the calibrated
	-- glove orientation for generic actions caused hammer and dismantle rays to
	-- hit near the feet on profiles whose grip and aim poses differ substantially.
	local actionPose = resolveActionPose( data ) or pose
	local actionOffset = VrActionLocalOffsets[activeItem] or { 0.000, -0.035, -0.120 }
	g_vrActionActive = true
	g_vrActionOrigin = handLocalPosition( pose, actionOffset )
	g_vrActionDirection = actionPose.forward
	g_vrActionUp = actionPose.up
	g_vrActionRight = actionPose.right
	g_vrPrimaryActionAvailable = true
	g_vrPrimaryActionDown = hand.interact == true

	local laserOffset = VrToolLaserItems[activeItem] and VrActionLocalOffsets[activeItem] or nil
	g_vrToolPointerEnabled = laserOffset ~= nil
	g_vrToolPointerOrigin = laserOffset and handLocalPosition( pose, laserOffset ) or nil
	g_vrToolPointerDirection = laserOffset and pose.forward or nil

	local muzzleOffset, muzzleSource = resolveGunMuzzleOffset( activeItem, data.gunMuzzle )
	if muzzleOffset then
		g_vrGunAimActive = true
		g_vrGunMuzzlePosition = handLocalPosition( pose, muzzleOffset )
		g_vrGunDirection = resolveGunDirection( pose, data.gunMuzzle )
		g_vrGunItem = activeItem
		self.cl.vrGunFreshTimer = 0.0
		if self.cl.vrGunAimItem ~= activeItem then
			self.cl.vrGunAimItem = activeItem
			sm.log.warning( "SCRAPVR_GUN_MUZZLE_READY " .. activeItem .. " source=" .. muzzleSource )
		end
	else
		clearGunAim()
		self.cl.vrGunAimItem = nil
	end
	self.cl.vrHandFreshTimer = 0.0
end

function Chapter2VR.clientUpdate( self, dt )
	if self.player ~= sm.localPlayer.getPlayer() then return end
	local character = self.player:getCharacter()
	local locking = character and character:getLockingInteractable() or nil
	local seated = locking ~= nil and locking:hasSeat()
	local activeItem = tostring( sm.localPlayer.getActiveItem() )
	local adapterFresh = Chapter2VR.activeAdapterTick ~= nil and
		sm.game.getCurrentTick() - Chapter2VR.activeAdapterTick <= 4 and
		Chapter2VR.activeAdapterItem == activeItem
	local activeAdapter = adapterFresh and Chapter2VR.activeAdapterName or ""
	local toolChanged = self.cl.vrNativeToolItem ~= activeItem
	local adapterChanged = self.cl.vrActiveAdapter ~= activeAdapter
	local seatedChanged = self.cl.vrSeated ~= seated
	local firstPerson = sm.localPlayer.isInFirstPersonView()
	local firstPersonChanged = self.cl.vrFirstPerson ~= firstPerson
	-- Keep readable transition markers for diagnostics; the native renderer now
	-- consumes the authoritative player-state JSON published below.
	if toolChanged then
		self.cl.vrNativeToolItem = activeItem
		sm.log.warning( "SCRAPVR_NATIVE_TOOL " .. activeItem )
	end
	if adapterChanged then self.cl.vrActiveAdapter = activeAdapter end
	if seatedChanged then
		self.cl.vrSeated = seated
		sm.log.warning( seated and "SCRAPVR_SEATED 1" or "SCRAPVR_SEATED 0" )
	end
	if firstPersonChanged then
		self.cl.vrFirstPerson = firstPerson
		sm.log.warning( firstPerson and "SCRAPVR_FIRST_PERSON 1" or "SCRAPVR_FIRST_PERSON 0" )
	end
	self.cl.vrPlayerStateTimer = ( self.cl.vrPlayerStateTimer or 0.0 ) + dt
	if toolChanged or adapterChanged or seatedChanged or firstPersonChanged or self.cl.vrPlayerStateTimer >= 0.25 then
		self.cl.vrPlayerStateTimer = 0.0
		publishPlayerState( true, seated, firstPerson, activeItem, activeAdapter )
	end

	self.cl.vrHandFreshTimer = ( self.cl.vrHandFreshTimer or 1.0 ) + dt
	self.cl.vrBridgeFreshTimer = ( self.cl.vrBridgeFreshTimer or 1.0 ) + dt
	self.cl.vrGunFreshTimer = ( self.cl.vrGunFreshTimer or 1.0 ) + dt
	g_vrBridgeFreshTimer = self.cl.vrBridgeFreshTimer
	if self.cl.vrGunFreshTimer > 0.12 then clearGunAim() end
	if self.cl.vrHandFreshTimer > 0.8 then clearClientAim() end
	self.cl.vrHandTimer = ( self.cl.vrHandTimer or 0.0 ) + dt
	local bridgePollSeconds = self.cl.vrHandBridgeActive == true and 0.02 or VrInactiveBridgePollSeconds
	if self.cl.vrHandTimer < bridgePollSeconds then return end
	self.cl.vrHandTimer = 0.0
	-- fileExists is backed by the game's resource catalog and can remain false
	-- for this native add-on's runtime-created file. Open it directly and let
	-- pcall handle the short periods where the bridge is genuinely absent.
	local ok, data = pcall( sm.json.open, VrHandBridgePath )
	self.cl.vrHandBridgeActive = ok and type( data ) == "table" and data.vrActive == true
	if not ok or type( data ) ~= "table" or type( data.sequence ) ~= "number" or
		data.sequence == self.cl.vrHandSequence then return end
	-- A previous VR run may have ended without deleting its last bridge file.
	-- Ignore the first observed sequence and require a live sequence advance;
	-- the native bridge publishes every 20 ms while VR hands are active.
	if self.cl.vrHandBridgePrimed ~= true then
		self.cl.vrHandBridgePrimed = true
		self.cl.vrHandSequence = data.sequence
		return
	end
	self.cl.vrHandSequence = data.sequence
	updateToolAim( self, data )
	self.network:sendToServer( "sv_n_vrHandPhysics", data )
end

function Chapter2VR.serverReceive( self, params, player )
	local settings = g_nativeVrConfig.vrHands
	local state = self.sv.vrHands
	local character = self.player:getCharacter()
	if not settings or not settings.enabled or not state or player ~= self.player or not character or
		type( params ) ~= "table" or type( params.sequence ) ~= "number" or
		params.sequence <= state.sequence then return end
	local tick = sm.game.getCurrentTick()
	local accepted = {}
	for _, name in ipairs( { "left", "right" } ) do
		local hand = params[name]
		local acceptedHand = false
		if type( hand ) == "table" and hand.active == true and validNumber( hand.x ) and
			validNumber( hand.y ) and validNumber( hand.z ) then
			local position = sm.vec3.new( hand.x, hand.y, hand.z )
			if ( position - character.worldPosition ):length() <= settings.maximumReach then
				local previous = state.hands[name]
				local interaction = hand.interact == true
				local velocity = sm.vec3.zero()
				if previous then
					local elapsedTicks = math.max( 1, tick - previous.tick )
					velocity = ( position - previous.position ) * ( 40.0 / elapsedTicks )
					local speed = velocity:length()
					if speed > settings.maximumHandSpeed then
						velocity = velocity * ( settings.maximumHandSpeed / speed )
					end
				end
				accepted[name] = {
					position = position, velocity = velocity, tick = tick, interaction = interaction,
					pressed = interaction and state.interactionDown[name] ~= true
				}
				state.interactionDown[name] = interaction
				acceptedHand = true
			end
		end
		if not acceptedHand then state.interactionDown[name] = false end
	end
	state.sequence = params.sequence
	state.hands = accepted
	state.lastTick = tick
end

local function updateTouchControls( settings, state, tick, player )
	if not settings.interactionEnabled then return end
	local currentTouches = {}
	local hands = tick - state.lastTick <= 8 and state.hands or {}
	local searchRadius = settings.interactionRadius + settings.touchReleasePadding
	local releaseRadius = searchRadius + ( settings.touchReleaseHysteresis or 0.08 )
	local releaseDebounceTicks = settings.touchReleaseDebounceTicks or 12
	for name, hand in pairs( hands ) do
		local previousTouch = state.touching[name]
		local nearestTouch = nil
		local nearestDistance = searchRadius
		for _, shape in ipairs( sm.shape.shapesInSphere( hand.position, searchRadius ) ) do
			local uuid = shape:getShapeUuid()
			-- The stock switch and button are engine-native lever/button types.
			-- Calling interactable:setActive on either from Lua is forbidden and an
			-- unhandled error disables this player bridge until the world reloads.
			-- They remain usable through the tracked ray and the game's normal
			-- interaction input; physical touch is limited to scripted elevators.
			if uuid == VrElevatorButton or uuid == VrElevatorCallButton then
				local interactable = shape:getInteractable()
				local allowed = previousTouch and previousTouch.interactable == interactable and
					searchRadius or settings.interactionRadius
				local shapeDistance = ( shape.worldPosition - hand.position ):length()
				if interactable and shapeDistance <= allowed and shapeDistance <= nearestDistance then
					nearestTouch = { interactable = interactable, uuid = uuid, shape = shape }
					nearestDistance = shapeDistance
				end
			end
		end
		if not nearestTouch and previousTouch and previousTouch.shape and sm.exists( previousTouch.shape ) then
			local releaseDistance = ( previousTouch.shape.worldPosition - hand.position ):length()
			if releaseDistance <= releaseRadius then
				previousTouch.missingSince = nil
				nearestTouch = previousTouch
			elseif not previousTouch.missingSince then
				previousTouch.missingSince = tick
				nearestTouch = previousTouch
			elseif tick - previousTouch.missingSince < releaseDebounceTicks then
				nearestTouch = previousTouch
			end
		end
		if nearestTouch then
			currentTouches[name] = nearestTouch
			if not previousTouch or previousTouch.interactable ~= nearestTouch.interactable then
				local interactable = nearestTouch.interactable
				local alreadyTouched = false
				for otherName, touch in pairs( currentTouches ) do
					if otherName ~= name and touch.interactable == interactable then alreadyTouched = true end
				end
				for otherName, touch in pairs( state.touching ) do
					if otherName ~= name and touch.interactable == interactable then alreadyTouched = true end
				end
				local nextAllowed = state.interactionCooldowns[interactable] or 0
				if not alreadyTouched and tick >= nextAllowed then
					state.interactionCooldowns[interactable] = tick + settings.interactionCooldownTicks
					sm.event.sendToInteractable( interactable, "sv_e_vrInteract", { player = player } )
				end
			end
		end
	end
	state.touching = currentTouches
end

function Chapter2VR.serverUpdate( self )
	local settings = g_nativeVrConfig.vrHands
	local state = self.sv.vrHands
	local tick = sm.game.getCurrentTick()
	if not settings or not settings.enabled or not state then return end
	updateTouchControls( settings, state, tick, self.player )
	if tick - state.lastTick > 8 then return end
	local occupiedSeatBodyId = nil
	local character = self.player:getCharacter()
	if character then
		local locking = character:getLockingInteractable()
		if locking and locking:hasSeat() then
			local seatShape = locking:getShape()
			local seatBody = seatShape and seatShape:getBody() or nil
			if seatBody then occupiedSeatBodyId = seatBody:getId() end
		end
	end
	local pushedBodies = {}
	for _, hand in pairs( state.hands ) do
		if hand.velocity:length() > 0.05 then
			local checked = 0
			for _, shape in ipairs( sm.shape.shapesInSphere( hand.position, settings.contactRadius ) ) do
				checked = checked + 1
				if checked > settings.maximumShapesPerHand then break end
				local body = shape:getBody()
				if body and body:isDynamic() and body:getId() ~= occupiedSeatBodyId and
					not pushedBodies[body] and body.mass <= settings.maximumBodyMass then
					local impulse = hand.velocity * math.min( body.mass, 250.0 ) * settings.impulseScale
					local magnitude = impulse:length()
					if magnitude > settings.maximumImpulse then
						impulse = impulse * ( settings.maximumImpulse / magnitude )
					end
					sm.physics.applyImpulse( body, impulse, true, hand.position - body.worldPosition )
					pushedBodies[body] = true
				end
			end
		end
	end
end

function Chapter2VR.clientDestroy( self )
	if self.player == sm.localPlayer.getPlayer() then
		publishWorldState( false )
		publishPlayerState( false, false, false, nil )
		clearClientAim()
	end
end

function Chapter2VR.markAdapter( name )
	Chapter2VR.activeAdapterName = name or ""
	Chapter2VR.activeAdapterItem = tostring( sm.localPlayer.getActiveItem() )
	Chapter2VR.activeAdapterTick = sm.game.getCurrentTick()
end

function Chapter2VR.actionPose()
	local tick = sm.game.getCurrentTick()
	local bridgeAge = type( g_vrBridgeLastTick ) == "number" and tick - g_vrBridgeLastTick or nil
	local fresh = g_vrBridgeActive == true and g_vrActionActive == true and
		bridgeAge ~= nil and bridgeAge >= 0 and bridgeAge <= VrPrimaryBridgeFreshTicks and
		g_vrActionOrigin ~= nil and g_vrActionDirection ~= nil
	if not fresh then return nil, false end
	return {
		position = g_vrActionOrigin,
		direction = g_vrActionDirection,
		up = g_vrActionUp or sm.vec3.new( 0, 0, 1 ),
		right = g_vrActionRight or sm.vec3.new( 1, 0, 0 )
	}, true
end

-- Use the tracked right hand for an item's world-space action ray while a live
-- VR bridge is authoritative. Outside VR this deliberately returns the stock
-- latest raycast unchanged, so the same patched scripts remain desktop-safe.
function Chapter2VR.actionRaycast( maxRange, ignore, filter )
	local pose, active = Chapter2VR.actionPose()
	if active then
		local direction = pose.direction:normalize()
		local target = pose.position + direction * ( maxRange or 7.5 )
		if filter ~= nil then
			return sm.physics.raycast( pose.position, target, ignore, filter )
		end
		return sm.physics.raycast( pose.position, target, ignore )
	end
	return sm.localPlayer.getLatestRaycast()
end

function Chapter2VR.primaryState( toolState, primaryState )
	-- This check must be independent of Chapter2VR.clientUpdate's dt timer. The
	-- player callback can temporarily stop during seat/headset transitions while
	-- equipped tools continue updating; a cached VR trigger must then expire on
	-- the game's authoritative tick clock instead of suppressing desktop input.
	local tick = sm.game.getCurrentTick()
	local bridgeAge = type( g_vrBridgeLastTick ) == "number" and tick - g_vrBridgeLastTick or nil
	local vrPrimaryFresh = g_vrPrimaryActionAvailable == true and g_vrBridgeActive == true and
		bridgeAge ~= nil and bridgeAge >= 0 and bridgeAge <= VrPrimaryBridgeFreshTicks
	if vrPrimaryFresh then
		if g_vrPrimaryActionDown == true then
			primaryState = toolState.vrPrimaryTriggerDown and sm.tool.interactState.hold or
				sm.tool.interactState.start
			toolState.vrPrimaryTriggerDown = true
		elseif toolState.vrPrimaryTriggerDown then
			toolState.vrPrimaryTriggerDown = false
			primaryState = sm.tool.interactState.stop
		else
			primaryState = sm.tool.interactState.null
		end
	elseif toolState.vrPrimaryTriggerDown then
		toolState.vrPrimaryTriggerDown = false
		primaryState = sm.tool.interactState.stop
	end
	return primaryState
end

-- Returns a fresh tracked barrel pose and whether VR is authoritative. A gun
-- must never silently fall back to the desktop camera while a live VR session
-- is present: if tracking or the equipped-tool calibration is not ready, the
-- caller receives authoritative=true with no pose and skips that shot.
local function refreshNativeProjectilePose( tick )
	local nativePose = ScrapVRProjectilePoseNative
	if type( nativePose ) ~= "function" then return false end
	local ok, authoritative, active, px, py, pz, dx, dy, dz, item, sequence = pcall( nativePose )
	if not ok then
		Chapter2VR.directGunReadReason = "native_bridge_call_failed"
		return false
	end

	Chapter2VR.directGunBridgeVrActive = authoritative == true
	Chapter2VR.directGunBridgeAuthoritative = authoritative == true
	Chapter2VR.directGunMuzzleActive = active == true
	Chapter2VR.directGunMuzzleSource = "native_logic_task"
	Chapter2VR.directGunBridgeSequenceObserved = validNumber( sequence ) and sequence or -1
	if authoritative == true then Chapter2VR.directGunAuthorityTick = tick end
	if Chapter2VR.directGunSequence ~= sequence and validNumber( sequence ) then
		Chapter2VR.directGunSequence = sequence
		Chapter2VR.directGunBridgeTick = tick
	end

	if authoritative ~= true then
		Chapter2VR.directGunReadReason = "native_desktop_fallback"
		return true
	end
	if active ~= true then
		Chapter2VR.directGunReadReason = "native_pose_inactive"
		return true
	end
	if not validNumber( px ) or not validNumber( py ) or not validNumber( pz ) or
		not validNumber( dx ) or not validNumber( dy ) or not validNumber( dz ) then
		Chapter2VR.directGunReadReason = "native_pose_invalid"
		return true
	end

	local activeItem = tostring( sm.localPlayer.getActiveItem() )
	if type( item ) ~= "string" or item ~= activeItem then
		Chapter2VR.directGunReadReason = "native_item_mismatch"
		return true
	end
	local direction = sm.vec3.new( dx, dy, dz )
	if direction:length() < 0.5 then
		Chapter2VR.directGunReadReason = "native_direction_invalid"
		return true
	end
	direction = direction:normalize()
	local position = sm.vec3.new( px, py, pz )
	Chapter2VR.directGunPose = {
		position = position,
		direction = direction,
		item = activeItem,
		source = "native_logic_task",
		sequence = sequence,
		tick = tick
	}
	Chapter2VR.directGunReadReason = "tracked_barrel_native_logic_task"

	-- Keep legacy action consumers synchronized without involving the cached
	-- JSON transport in the actual projectile call.
	g_vrBridgeActive = true
	g_vrBridgeFreshTimer = 0.0
	g_vrGunAimActive = true
	g_vrGunMuzzlePosition = position
	g_vrGunDirection = direction
	g_vrGunItem = activeItem
	if Chapter2VR.nativeProjectileBridgeLogged ~= true then
		Chapter2VR.nativeProjectileBridgeLogged = true
		sm.log.warning( "SCRAPVR_PROJECTILE_BRIDGE transport=native_logic_task" )
	end
	return true
end

local function refreshDirectGunPose()
	local tick = sm.game.getCurrentTick()
	if Chapter2VR.directGunReadTick == tick then return tick end
	Chapter2VR.directGunReadTick = tick
	Chapter2VR.directGunReadReason = "bridge_missing"
	Chapter2VR.directGunBridgeVrActive = false
	Chapter2VR.directGunBridgeAuthoritative = false
	Chapter2VR.directGunBridgeSequenceObserved = -1
	Chapter2VR.directGunMuzzleActive = false
	Chapter2VR.directGunMuzzleSource = nil
	if refreshNativeProjectilePose( tick ) then return tick end

	local ok, data = pcall( sm.json.open, VrHandBridgePath )
	if not ok or type( data ) ~= "table" then
		Chapter2VR.directGunReadReason = "bridge_unavailable"
		return tick
	end
	Chapter2VR.directGunBridgeVrActive = data.vrActive == true
	Chapter2VR.directGunBridgeAuthoritative = data.vrAuthoritative == true or data.vrActive == true
	Chapter2VR.directGunBridgeSequenceObserved = type( data.sequence ) == "number" and data.sequence or -1
	Chapter2VR.directGunMuzzleActive = type( data.gunMuzzle ) == "table" and data.gunMuzzle.active == true
	if Chapter2VR.directGunBridgeAuthoritative then
		Chapter2VR.directGunAuthorityTick = tick
	end
	if data.vrActive ~= true or type( data.sequence ) ~= "number" then
		Chapter2VR.directGunReadReason = "bridge_inactive"
		return tick
	end

	if Chapter2VR.directGunSequence ~= data.sequence then
		Chapter2VR.directGunSequence = data.sequence
		Chapter2VR.directGunBridgeTick = tick
	end

	local activeItem = tostring( sm.localPlayer.getActiveItem() )
	local pose = resolveHandPose( data )
	if not pose then
		Chapter2VR.directGunReadReason = "right_hand_inactive"
		return tick
	end
	local muzzleOffset, muzzleSource = resolveGunMuzzleOffset( activeItem, data.gunMuzzle )
	if not muzzleOffset then
		Chapter2VR.directGunReadReason = muzzleSource
		return tick
	end

	local direction = resolveGunDirection( pose, data.gunMuzzle )
	local position = handLocalPosition( pose, muzzleOffset )
	Chapter2VR.directGunPose = {
		position = position,
		direction = direction,
		item = activeItem,
		source = muzzleSource,
		sequence = data.sequence,
		tick = tick
	}
	Chapter2VR.directGunMuzzleSource = muzzleSource
	Chapter2VR.directGunReadReason = "tracked_barrel_" .. muzzleSource

	-- Keep the existing consumers in sync, but do not rely on the player
	-- callback having run before a tool fires.
	g_vrBridgeActive = true
	g_vrBridgeFreshTimer = 0.0
	g_vrGunAimActive = true
	g_vrGunMuzzlePosition = position
	g_vrGunDirection = direction
	g_vrGunItem = activeItem
	return tick
end

local function writeGunFireDiagnostic( firePos, direction, authoritative, reason )
	local cameraDirection = sm.localPlayer.getDirection()
	local source = firePos and "vr_barrel" or ( authoritative and "blocked" or "pc_fallback" )
	local payload = {
		tick = sm.game.getCurrentTick(),
		item = tostring( sm.localPlayer.getActiveItem() ),
		sequence = Chapter2VR.directGunBridgeSequenceObserved or Chapter2VR.directGunSequence or -1,
		source = source,
		reason = reason,
		vrActive = Chapter2VR.directGunBridgeVrActive == true,
		vrAuthoritative = Chapter2VR.directGunBridgeAuthoritative == true,
		gunMuzzleActive = Chapter2VR.directGunMuzzleActive == true,
		muzzleCalibration = Chapter2VR.directGunMuzzleSource,
		vrDirection = direction and { x = direction.x, y = direction.y, z = direction.z } or nil,
		cameraDirection = { x = cameraDirection.x, y = cameraDirection.y, z = cameraDirection.z },
		muzzlePosition = firePos and { x = firePos.x, y = firePos.y, z = firePos.z } or nil
	}
	pcall( sm.json.save, payload, VrGunFireDebugPath )
	local marker = source .. ":" .. tostring( reason )
	if Chapter2VR.lastGunFireMarker ~= marker then
		Chapter2VR.lastGunFireMarker = marker
		sm.log.warning( "SCRAPVR_GUN_FIRE source=" .. source .. " reason=" .. tostring( reason ) )
	end
end

function Chapter2VR.gunFirePose( tool, firing )
	local tick = refreshDirectGunPose()
	local bridgeTick = Chapter2VR.directGunBridgeTick
	local authorityTick = Chapter2VR.directGunAuthorityTick
	local authoritative = authorityTick ~= nil and tick - authorityTick <= VrDirectAuthorityFreshTicks
	local bridgeFresh = bridgeTick ~= nil and tick - bridgeTick <= VrDirectBridgeFreshTicks
	local cached = Chapter2VR.directGunPose
	local activeItem = tostring( sm.localPlayer.getActiveItem() )
	local poseFresh = cached ~= nil and cached.item == activeItem and
		tick - cached.tick <= VrDirectPoseFreshTicks
	local reason = Chapter2VR.directGunReadReason or "unknown"
	local currentPoseReady = reason == "tracked_barrel_native_calibration" or
		reason == "tracked_barrel_lua_calibration" or
		reason == "tracked_barrel_native_logic_task"

	if authoritative and bridgeFresh and poseFresh and currentPoseReady then
		local direction = cached.direction
		local owner = tool and tool:getOwner() or nil
		local character = owner and owner.character or nil
		if direction:length() >= 0.5 and
			( not character or ( cached.position - character.worldPosition ):length() <= 3.0 ) then
			direction = direction:normalize()
			if firing then writeGunFireDiagnostic( cached.position, direction, true, reason ) end
			return cached.position, direction, true
		end
		reason = "pose_sanity_failed"
	end

	if firing then writeGunFireDiagnostic( nil, nil, authoritative, reason ) end
	return nil, nil, authoritative
end

-- Thrown Glowsticks/Cornades and the Fire Extinguisher use the identical fresh,
-- authoritative calibrated-pose contract as guns. Keeping this named wrapper
-- makes the tool scripts explicit without maintaining a second aim pipeline.
function Chapter2VR.projectileFirePose( tool, firing )
	return Chapter2VR.gunFirePose( tool, firing )
end
