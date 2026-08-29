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
	["5cc12f03-275e-4c8e-b013-79fc0f913e1b"] = { 0.000, -0.035, -0.120 }
}

local VrWorldStatePath = "$GAME_DATA/NativeVR/world_state.json"

local VrToolLaserItems = {
	["8c7efc37-cd7c-4262-976e-39585f8527bf"] = true,
	["c60b9627-fc2b-4319-97c5-05921cb976c6"] = true,
	["fdb8b8be-96e7-4de0-85c7-d2f42e4f33ce"] = true
}

local function clearClientAim()
	g_vrPrimaryActionAvailable = false
	g_vrPrimaryActionDown = false
	g_vrActionActive = false
	g_vrActionOrigin = nil
	g_vrActionDirection = nil
	g_vrToolPointerEnabled = false
	g_vrToolPointerOrigin = nil
	g_vrToolPointerDirection = nil
	g_vrHammerSwingDirection = nil
	g_vrHammerSwingFreshTimer = 1.0
end

local function publishWorldState( active )
	local ok, errorMessage = pcall( sm.json.save, {
		version = 1,
		active = active == true,
		tick = sm.game.getCurrentTick()
	}, VrWorldStatePath )
	if ok then
		sm.log.warning( "SCRAPVR_WORLD_STATE active=" .. tostring( active == true ) )
	else
		sm.log.error( "SCRAPVR_WORLD_STATE_WRITE_FAILED " .. tostring( errorMessage ) )
	end
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

local function handLocalPosition( pose, offset )
	return pose.position + pose.right * offset[1] + pose.up * offset[2] - pose.forward * offset[3]
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
	self.cl.vrHammerSwingSequence = 0
	self.cl.vrHammerSwingFreshTimer = 1.0
	self.cl.vrNativeToolItem = nil
	self.cl.vrHandFreshTimer = 1.0
	self.cl.vrSeated = nil
	self.cl.vrFirstPerson = nil
	if self.player == sm.localPlayer.getPlayer() then
		publishWorldState( true )
		sm.log.warning( "SCRAPVR_BRIDGE_CREATE" )
	end
	clearClientAim()
end

local function updateToolAim( self, data )
	local activeItem = tostring( sm.localPlayer.getActiveItem() )
	local pose = resolveHandPose( data )
	if not pose then return end
	local hand = pose.hand

	local actionOffset = VrActionLocalOffsets[activeItem] or { 0.000, -0.035, -0.120 }
	g_vrActionActive = true
	g_vrActionOrigin = handLocalPosition( pose, actionOffset )
	g_vrActionDirection = pose.forward
	g_vrPrimaryActionAvailable = true
	g_vrPrimaryActionDown = hand.interact == true

	local laserOffset = VrToolLaserItems[activeItem] and VrActionLocalOffsets[activeItem] or nil
	g_vrToolPointerEnabled = laserOffset ~= nil
	g_vrToolPointerOrigin = laserOffset and handLocalPosition( pose, laserOffset ) or nil
	g_vrToolPointerDirection = laserOffset and pose.forward or nil
	self.cl.vrHandFreshTimer = 0.0
end

function Chapter2VR.clientUpdate( self, dt )
	if self.player ~= sm.localPlayer.getPlayer() then return end
	local character = self.player:getCharacter()
	local locking = character and character:getLockingInteractable() or nil
	local seated = locking ~= nil and locking:hasSeat()
	local activeItem = tostring( sm.localPlayer.getActiveItem() )
	-- Chapter 2 no longer exposes the old player marker reliably when it is
	-- emitted only after a hand-physics file update. Publish the equipped item
	-- first, on change, so the native hand pass can select the matching mesh.
	if self.cl.vrNativeToolItem ~= activeItem then
		self.cl.vrNativeToolItem = activeItem
		sm.log.warning( "SCRAPVR_NATIVE_TOOL " .. activeItem )
	end
	if self.cl.vrSeated ~= seated then
		self.cl.vrSeated = seated
		sm.log.warning( seated and "SCRAPVR_SEATED 1" or "SCRAPVR_SEATED 0" )
	end
	local firstPerson = sm.localPlayer.isInFirstPersonView()
	if self.cl.vrFirstPerson ~= firstPerson then
		self.cl.vrFirstPerson = firstPerson
		sm.log.warning( firstPerson and "SCRAPVR_FIRST_PERSON 1" or "SCRAPVR_FIRST_PERSON 0" )
	end

	self.cl.vrHandFreshTimer = ( self.cl.vrHandFreshTimer or 1.0 ) + dt
	self.cl.vrHammerSwingFreshTimer = ( self.cl.vrHammerSwingFreshTimer or 1.0 ) + dt
	g_vrHammerSwingFreshTimer = self.cl.vrHammerSwingFreshTimer
	if self.cl.vrHammerSwingFreshTimer > 0.6 then g_vrHammerSwingDirection = nil end
	if self.cl.vrHandFreshTimer > 0.8 then clearClientAim() end
	self.cl.vrHandTimer = ( self.cl.vrHandTimer or 0.0 ) + dt
	if self.cl.vrHandTimer < 0.02 then return end
	self.cl.vrHandTimer = 0.0
	local path = "$GAME_DATA/NativeVR/hand_physics.json"
	if not sm.json.fileExists( path ) then return end
	local ok, data = pcall( sm.json.open, path )
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
	if type( data.hammerSwingSequence ) == "number" and
		data.hammerSwingSequence > ( self.cl.vrHammerSwingSequence or 0 ) then
		self.cl.vrHammerSwingSequence = data.hammerSwingSequence
		local swing = data.hammerSwingDirection
		if type( swing ) == "table" and validNumber( swing.x ) and validNumber( swing.y ) and
			validNumber( swing.z ) then
			local direction = sm.vec3.new( swing.x, swing.y, swing.z )
			if direction:length() > 0.5 then
				g_vrHammerSwingDirection = direction:normalize()
				self.cl.vrHammerSwingFreshTimer = 0.0
				g_vrHammerSwingFreshTimer = 0.0
			end
		end
	end
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
			if uuid == VrInteractiveSwitch or uuid == VrInteractiveButton or
				uuid == VrElevatorButton or uuid == VrElevatorCallButton then
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
					if nearestTouch.uuid == VrInteractiveSwitch then
						interactable:setActive( not interactable:isActive() )
					elseif nearestTouch.uuid == VrInteractiveButton then
						interactable:setActive( true )
					else
						sm.event.sendToInteractable( interactable, "sv_e_vrInteract", { player = player } )
					end
				end
			end
		end
	end
	for _, previousTouch in pairs( state.touching ) do
		if previousTouch.uuid == VrInteractiveButton then
			local stillTouched = false
			for _, currentTouch in pairs( currentTouches ) do
				if currentTouch.interactable == previousTouch.interactable then stillTouched = true end
			end
			if not stillTouched and sm.exists( previousTouch.interactable ) then
				previousTouch.interactable:setActive( false )
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
		clearClientAim()
	end
end

function Chapter2VR.primaryState( toolState, primaryState )
	if g_vrPrimaryActionAvailable == true then
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
