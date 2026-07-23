dofile "$GAME_DATA/Scripts/game/AnimationUtil.lua"

Lift = class()

local VrLastLiftPlacementTicks = {}

function Lift.client_onCreate( self )
	self:client_init()
end

function Lift.client_onRefresh( self )
	self:client_init()
end

function Lift.client_onDestroy()
	sm.visualization.setCreationVisible( false )
	sm.visualization.setLiftVisible( false )
end

function Lift.client_init( self )
	self.liftPos = sm.vec3.new( 0, 0, 0 )
	self.hoverBodies = {}
	self.selectedBodies = {}
	self.rotationIndex = 0
	self.vrPlaceHeld = false
end

function Lift.client_onEquippedUpdate( self, primaryState, secondaryState )
	if self.tool:isLocal() and self.equipped and sm.localPlayer.getPlayer():getCharacter() then
		local success, raycastResult
		if g_vrActionActive == true and g_vrActionOrigin and g_vrActionDirection and
			g_vrActionDirection:length() > 0.5 then
			success, raycastResult = sm.localPlayer.getRaycast(
				7.5, g_vrActionOrigin, g_vrActionDirection:normalize() )
		else
			success, raycastResult = sm.localPlayer.getRaycast( 7.5 )
		end
		self:client_interact( primaryState, secondaryState, raycastResult )
	end
	return true, false
end

function Lift.checkPlaceable( self, raycastResult )
	if raycastResult.valid then
		if raycastResult.type == "lift" or raycastResult.type == "character" then
			return false
		end
		if raycastResult.type == "body" then
			local body = raycastResult:getBody()
			if body:isOnLift() or body:isDynamic() then
				return false
			end
		end
		return true
	end
	return false
end

function Lift.client_interact( self, primaryState, secondaryState, raycastResult )
	local targetBody = nil

	if self.importBodies then
		self.selectedBodies = self.importBodies
		self.importBodies = nil
	end

	--Clear states
	if secondaryState ~= sm.tool.interactState.null then
		self.hoverBodies = {}
		self.selectedBodies = {}

		sm.tool.forceTool( nil )
		self.forced = false
	end
	
	--Raycast
	if raycastResult.valid then
		if raycastResult.type == "joint" then
			targetBody = raycastResult:getJoint().shapeA.body
		elseif raycastResult.type == "body" then
			targetBody = raycastResult:getBody()
		end
		
		local liftPos = raycastResult.pointWorld * 4
		self.liftPos = sm.vec3.new( math.floor( liftPos.x + 0.5 ), math.floor( liftPos.y + 0.5 ), math.floor( liftPos.z + 0.5 ) )
	end
	
	local isSelectable = false
	local isCarryable = false
	if self.selectedBodies[1] then
		if sm.exists( self.selectedBodies[1] ) and self.selectedBodies[1]:isDynamic() and self.selectedBodies[1]:isLiftable() then
			local isLiftable = true
			isCarryable = true
			for _, body in ipairs( self.selectedBodies[1]:getCreationBodies() ) do
				for _, shape in ipairs( body:getShapes() ) do
					if not shape.liftable then
						isLiftable = false
						break
					end
				end
				if not body:isDynamic() or not isLiftable then
					isCarryable = false
					break
				end
			end
		end
	elseif targetBody then
		if targetBody:isDynamic() and targetBody:isLiftable() then
			local isLiftable = true
			isSelectable = true
			for _, body in ipairs( targetBody:getCreationBodies() ) do
				for _, shape in ipairs( body:getShapes() ) do
					if not shape.liftable then
						isLiftable = false
						break
					end
				end
				if not body:isDynamic() or not isLiftable then
					isSelectable = false
					break
				end
			end
		end
	end
		
	--Hover
	if isSelectable and #self.selectedBodies == 0 then
		self.hoverBodies = targetBody:getCreationBodies()
	else
		self.hoverBodies = {}
	end

	-- Unselect invalid bodies
	if #self.selectedBodies > 0 and not isCarryable and not self.forced then
		self.selectedBodies = {}
	end

	--Check lift collision and if placeable surface
	local isPlaceable = self:checkPlaceable(raycastResult) 
	
	--Lift level
	local okPosition, liftLevel = sm.tool.checkLiftCollision( self.selectedBodies, self.liftPos, self.rotationIndex )
	isPlaceable = isPlaceable and okPosition

	--Pickup
	if primaryState == sm.tool.interactState.stop or primaryState == sm.tool.interactState.null then
		self.vrPlaceHeld = false
	end
	if primaryState == sm.tool.interactState.start and not self.vrPlaceHeld then
		self.vrPlaceHeld = true

		if isSelectable and #self.selectedBodies == 0 then
			self.selectedBodies = self.hoverBodies
			self.hoverBodies = {}
		elseif isPlaceable then
			local placeLiftParams = { player = sm.localPlayer.getPlayer(), selectedBodies = self.selectedBodies, liftPos = self.liftPos, liftLevel = liftLevel, rotationIndex = self.rotationIndex }
			self.network:sendToServer( "server_placeLift", placeLiftParams )
			self.selectedBodies = {}
		end

		sm.tool.forceTool( nil )
		self.forced = false
	end

	--Visualization
	sm.visualization.setCreationValid( isPlaceable, false )
	sm.visualization.setLiftValid( isPlaceable )

	if raycastResult.valid then
		local showLift = #self.hoverBodies == 0
		sm.visualization.setLiftPosition( self.liftPos * 0.25 )
		sm.visualization.setLiftLevel( liftLevel )
		sm.visualization.setLiftVisible( showLift )
		
		if #self.selectedBodies > 0 then
			sm.visualization.setCreationBodies( self.selectedBodies )
			sm.visualization.setCreationFreePlacement( true )
			sm.visualization.setCreationFreePlacementPosition( self.liftPos * 0.25 + sm.vec3.new(0,0,0.5) + sm.vec3.new(0,0,0.25) * liftLevel )
			sm.visualization.setCreationFreePlacementRotation( self.rotationIndex )
			sm.visualization.setCreationVisible( true )
			
			sm.gui.setInteractionText( "", sm.gui.getKeyBinding( "Create", true ), "#{INTERACTION_PLACE_LIFT_ON_GROUND}" )
		elseif #self.hoverBodies > 0 then
			sm.visualization.setCreationBodies( self.hoverBodies )
			sm.visualization.setCreationFreePlacement( false )		
			sm.visualization.setCreationValid( true, true )
			sm.visualization.setLiftValid( true )
			sm.visualization.setCreationVisible( true )
			
			sm.gui.setInteractionText( "", sm.gui.getKeyBinding( "Create", true ), "#{INTERACTION_PLACE_CREATION_ON_LIFT}" )
		else
			sm.visualization.setCreationBodies( {} )
			sm.visualization.setCreationFreePlacement( false )
			sm.visualization.setCreationVisible( false )
			
			if isPlaceable then
				sm.gui.setInteractionText( "", sm.gui.getKeyBinding( "Create", true ), "#{INTERACTION_PLACE_LIFT}" )
			end
		end
	else
		sm.visualization.setCreationVisible( false )
		sm.visualization.setLiftVisible( false )
	end
end

function Lift.client_onToggle( self, backwards )
	
	local nextRotationIndex = self.rotationIndex
	if backwards then
		nextRotationIndex = nextRotationIndex - 1
	else
		nextRotationIndex = nextRotationIndex + 1
	end
	if nextRotationIndex == 4 then
		nextRotationIndex = 0
	elseif nextRotationIndex == -1 then
		nextRotationIndex = 3
	end
	self.rotationIndex = nextRotationIndex

	return true
end

function Lift.client_onEquip( self )
	self.equipped = true
	self:client_init()
end

function Lift.client_onUnequip( self )
	self.equipped = false
	sm.visualization.setCreationBodies( {} )
	sm.visualization.setCreationVisible( false )
	sm.visualization.setLiftVisible( false )
	self.forced = false
end

function Lift.client_onForceTool( self, bodies )
	self.equipped = true
	self.importBodies = bodies
	self.forced = true
end

function Lift.server_placeLift( self, placeLiftParams )
	if not placeLiftParams or not placeLiftParams.player then
		return
	end
	local tick = sm.game.getCurrentTick()
	local playerId = placeLiftParams.player:getId()
	local lastTick = VrLastLiftPlacementTicks[playerId]
	-- The native lift registration completes after the request. Reject duplicate
	-- start edges during that window so one squeeze cannot race several lifts in.
	if lastTick and tick - lastTick < 12 then
		return
	end
	VrLastLiftPlacementTicks[playerId] = tick
	sm.player.placeLift( placeLiftParams.player, placeLiftParams.selectedBodies, placeLiftParams.liftPos, placeLiftParams.liftLevel, placeLiftParams.rotationIndex )
end
