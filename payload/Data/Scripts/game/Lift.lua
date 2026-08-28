dofile "$GAME_DATA/Scripts/game/AnimationUtil.lua"

Lift = class()

local PlacableHarvestables = {
	hvs_soil
}

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
	self.selectedBodiesInitTick = 0
	self.rotationIndex = 0
	self.worldAllowsLift = true
	self.needsWorldUpdate = true

	self.liftCollisionLevel = -1
	self.liftCollisionTestLevel = 0
	-- Latched visualization state: only updated when a full collision sweep
	-- completes, so the displayed level/validity lags one sweep behind and
	-- doesn't flicker as we probe one level per frame.
	self.liftDisplayLevel = -1
	self.liftDisplayHasResult = false
end

function Lift.client_onEquippedUpdate( self, primaryState, secondaryState )
	if self.tool:isLocal() and self.equipped and sm.localPlayer.getPlayer():getCharacter() then
		if Chapter2VR and Chapter2VR.primaryState then
			primaryState = Chapter2VR.primaryState( self, primaryState )
		end
		local success, raycastResult
		if g_vrActionActive == true and g_vrActionOrigin and g_vrActionDirection and
			g_vrActionDirection:length() > 0.5 then
			success, raycastResult = sm.localPlayer.getRaycast(
				7.5, g_vrActionOrigin, g_vrActionDirection:normalize() )
		else
			success, raycastResult = sm.localPlayer.getLatestRaycast()
		end
		self:client_interact( primaryState, secondaryState, raycastResult )
	end
	return true, false
end

function Lift.cl_checkPlaceable( self, raycastResult )
	if raycastResult.valid then
		if raycastResult.type == "lift" or raycastResult.type == "character" then
			return false
		end
		if raycastResult.type == "body" then
			local body = raycastResult:getBody()
			if ( body:isOnLift() and not body:isOnVirtualLift() ) or body:isDynamic() then
				return false
			end
		end
		if raycastResult.type == "limiter" then
			return false
		end
		if raycastResult.type == "harvestable" then
			if isAnyOf(  raycastResult:getHarvestable().uuid, PlacableHarvestables ) then
				return true
			else
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
		self.selectedBodiesInitTick = sm.game.getCurrentTick()
	end

	--Clear states
	if secondaryState ~= sm.tool.interactState.null then
		self.hoverBodies = {}
		if #self.selectedBodies > 0 then
			if sm.exists( self.selectedBodies[1] ) then
				if self.selectedBodies[1]:isGhost() then
					self.network:sendToServer( "sv_n_removeGhostBody", self.selectedBodies[1] )
				end
			end
		end
		self.selectedBodies = {}
		self.selectedBodiesInitTick = 0

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
	if isSelectable and #self.selectedBodies == 0 and self.worldAllowsLift then
		self.hoverBodies = targetBody:getCreationBodies()
	else
		self.hoverBodies = {}
	end

	-- Unselect invalid bodies
	if #self.selectedBodies > 0 and not isCarryable and not self.forced then
		self.selectedBodies = {}
		self.selectedBodiesInitTick = 0
	end

	--Check lift collision and if placeable surface
	local isPlaceable = self:cl_checkPlaceable( raycastResult )


	local specialPlacement = false
	local specialPlacementPosition = nil
	local specialPlacementInteractable = nil
	if #self.selectedBodies == 0 then
		local shape = raycastResult:getShape()
		if shape and shape.interactable and shape.interactable:getType() == "scripted" and shape.interactable.clientPublicData then
			if shape.interactable.clientPublicData.liftPlacement ~= nil then
				specialPlacementPosition = shape.interactable.clientPublicData.liftPlacement
				specialPlacementInteractable = shape.interactable
				specialPlacement = true
			end
		end
	end

	local maxLevel = sm.tool.getLiftMaxLevel()
	local current = self.liftCollisionLevel
	local testLevel = self.liftCollisionTestLevel
	-- Set whenever a full sweep settles, signalling that `current` is a
	-- stable enough answer to push to the visualization.
	local commitDisplay = false

	if #self.selectedBodies == 0 then
		local valid = sm.tool.checkLiftCollisionAtLevel( self.selectedBodies, self.liftPos, self.rotationIndex, 0, self.selectedBodiesInitTick )
		current = valid and 0 or -1
		testLevel = 0
		commitDisplay = true
	elseif testLevel >= 0 and testLevel < maxLevel then
		local valid = sm.tool.checkLiftCollisionAtLevel( self.selectedBodies, self.liftPos, self.rotationIndex, testLevel, self.selectedBodiesInitTick )
		if current < 0 then
			-- Searching upward for the first valid level
			if valid then
				current = testLevel
				testLevel = current - 1
				if testLevel < 0 then
					testLevel = current -- nothing below; revalidate current next frame
					commitDisplay = true
				end
			else
				testLevel = testLevel + 1
				if testLevel >= maxLevel then
					testLevel = 0 -- restart search from the bottom
					commitDisplay = true -- swept everything; commit invalid result
				end
			end
		elseif testLevel == current then
			-- Revalidating the current best level
			if valid then
				testLevel = current - 1
				if testLevel < 0 then
					testLevel = current -- already at the lowest level; keep revalidating
					commitDisplay = true
				end
			else
				current = -1
				testLevel = 0 -- current became invalid, search again from the bottom
			end
		else
			-- Scanning downward (testLevel < current) trying to find a lower valid level
			if valid then
				current = testLevel
			end
			testLevel = testLevel - 1
			if testLevel < 0 then
				testLevel = current -- finished scan; revalidate current next
				commitDisplay = true
			end
		end
	end

	self.liftCollisionLevel = current
	self.liftCollisionTestLevel = testLevel

	if commitDisplay then
		self.liftDisplayLevel = current
		self.liftDisplayHasResult = true
	end

	local displayLevel = self.liftDisplayLevel
	local okPosition = self.liftDisplayHasResult and displayLevel >= 0
	local liftLevel = okPosition and displayLevel or 0
	isPlaceable = isPlaceable and okPosition

	local player = sm.localPlayer.getPlayer()
	if self.needsWorldUpdate then
		local character = player:getCharacter()
		if character then
			local world = character:getWorld()
			self.needsWorldUpdate = false
			if world and world.clientPublicData then
				if world.clientPublicData.isLiftPlacable ~= nil then
					self.worldAllowsLift = world.clientPublicData.isLiftPlacable
				end
			end
		end
	end
	if not self.worldAllowsLift then
		isPlaceable = false
		isSelectable = false
	end

	--Pickup
	if primaryState == sm.tool.interactState.start and self.worldAllowsLift then

		if isSelectable and #self.selectedBodies == 0 then
			self.selectedBodies = self.hoverBodies
			self.hoverBodies = {}
			self.selectedBodiesInitTick = sm.game.getCurrentTick()
		elseif specialPlacement and self.liftPos == specialPlacementPosition then
			if specialPlacementInteractable and sm.exists( specialPlacementInteractable ) then
				sm.event.sendToInteractable( specialPlacementInteractable, "cl_onLiftPlacement" )
			end
		elseif isPlaceable and not specialPlacement then
			local placeLiftParams = { player = player, selectedBodies = self.selectedBodies, liftPos = self.liftPos, liftLevel = liftLevel, rotationIndex = self.rotationIndex }
			self.network:sendToServer( "server_placeLift", placeLiftParams )
			self.selectedBodies = {}
			self.selectedBodiesInitTick = 0
			self.liftCollisionLevel = -1
			self.liftCollisionTestLevel = 0
		end

		sm.tool.forceTool( nil )
		self.forced = false
	end

	--Visualization

	if specialPlacement then
		if self.liftPos == specialPlacementPosition then
			sm.visualization.setLiftVisible( false )
		else
			sm.visualization.setLiftVisible( true )
			sm.visualization.setLiftValid( false )
			sm.visualization.setLiftPosition( self.liftPos * 0.25 )
		end
		return
	end

	sm.visualization.setCreationValid( isPlaceable, false )
	sm.visualization.setLiftValid( isPlaceable )

	if raycastResult.valid then
		local showLift = #self.hoverBodies == 0
		sm.visualization.setLiftPosition( self.liftPos * 0.25 )
		sm.visualization.setLiftLevel( liftLevel )
		sm.visualization.setLiftVisible( showLift )

		if not self.worldAllowsLift then
			sm.visualization.setCreationBodies( {} )
			sm.visualization.setCreationFreePlacement( false )
			sm.visualization.setCreationVisible( false )
			return
		end
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
	sm.player.placeLift( placeLiftParams.player, placeLiftParams.selectedBodies, placeLiftParams.liftPos, placeLiftParams.liftLevel, placeLiftParams.rotationIndex )
end

function Lift.sv_n_removeGhostBody( self, body )
	body:destroyCreation()
end

function Lift.client_onLocalPlayerChangedWorld( self, world )
	self.needsWorldUpdate = true
end
