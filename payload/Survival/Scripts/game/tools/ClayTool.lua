-- Generated from the supported stock build by generate_held_item_payload.py.
dofile "$GAME_DATA/Scripts/game/AnimationUtil.lua"
dofile "$SURVIVAL_DATA/Scripts/util.lua"
dofile "$SURVIVAL_DATA/Scripts/game/survival_shapes.lua"

ClayTool = class()

local bucketRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_clay/char_clay.rend" }

local renderablesTp = { "$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_tp_bucket.rend", "$SURVIVAL_DATA/Character/Char_bucket/char_bucket_tp_animlist.rend" }
local renderablesFp = { "$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_fp_bucket.rend", "$SURVIVAL_DATA/Character/Char_bucket/char_bucket_fp_animlist.rend" }


sm.tool.preloadRenderables( bucketRenderables )
sm.tool.preloadRenderables( renderablesTp )
sm.tool.preloadRenderables( renderablesFp )

function ClayTool.client_onCreate( self )
	self.effect = sm.effect.createEffect( "ShapeRenderable" )
	self.effect:setParameter( "uuid", sm.uuid.new("42c8e4fc-0c38-4aa8-80ea-1835dd982d7c") )
	self.effect:setParameter( "visualization", true )
	self.effect:setScale( sm.vec3.new( sm.construction.constants.subdivideRatio, sm.construction.constants.subdivideRatio, sm.construction.constants.subdivideRatio ) )
	self:client_onRefresh()
end

function ClayTool.client_onRefresh( self )
	if self.tool:isEquipped() then
		self:cl_loadAnimations()
	end
end

function ClayTool.cl_loadAnimations( self )

	self.tpAnimations = createTpAnimations(
		self.tool,
		{
			idle = { "bucket_idle", { looping = true } },
			use = { "bucket_use_full", { nextAnimation = "idle" } },
			pickup = { "bucket_pickup", { nextAnimation = "idle" } },
			putdown = { "bucket_putdown" }
		
		}
	)
	local movementAnimations = {

		idle = "bucket_idle",
		
		runFwd = "bucket_run",
		runBwd = "bucket_runbwd",
		
		sprint = "bucket_sprint_idle",
		sprintLeft = "bucket_sprint_left",
		sprintRight = "bucket_sprint_right",

		jump = "bucket_jump",
		jumpUp = "bucket_jump_up",
		jumpDown = "bucket_jump_down",

		land = "bucket_jump_land",
		landFwd = "bucket_jump_land_fwd",
		landBwd = "bucket_jump_land_bwd",
		landLeft = "bucket_jump_land_left",
		landRight = "bucket_jump_land_right",

		crouchIdle = "bucket_crouch_idle",
		crouchFwd = "bucket_crouch_run",
		crouchBwd = "bucket_crouch_runbwd"
	}
	
	for name, animation in pairs( movementAnimations ) do
		self.tool:setMovementAnimation( name, animation )
	end
	
	if self.tool:isLocal() then
		self.fpAnimations = createFpAnimations(
			self.tool,
			{
				idle = { "bucket_idle", { looping = true } },
				use = { "bucket_use_full", { nextAnimation = "idle" } },
				
				sprintInto = { "bucket_sprint_into", { nextAnimation = "sprintIdle",  blendNext = 0.2 } },
				sprintIdle = { "bucket_sprint_idle", { looping = true } },
				sprintExit = { "bucket_sprint_exit", { nextAnimation = "idle",  blendNext = 0 } },
				
				jump = { "bucket_jump", { nextAnimation = "idle" } },
				land = { "bucket_jump_land", { nextAnimation = "idle" } },
				
				equip = { "bucket_pickup", { nextAnimation = "idle" } },
				unequip = { "bucket_putdown" }
			}
		)
	end
	setTpAnimation( self.tpAnimations, "idle", 5.0 )
	self.blendTime = 0.2
	
end

function ClayTool.cl_updateBucketRenderables( self )

	local currentRenderablesTp = {}
	local currentRenderablesFp = {}
	
	for k,v in pairs( renderablesTp ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( renderablesFp ) do currentRenderablesFp[#currentRenderablesFp+1] = v end

	for k,v in pairs( bucketRenderables ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( bucketRenderables ) do currentRenderablesFp[#currentRenderablesFp+1] = v end
	
	
	local color = sm.item.getShapeDefaultColor( obj_consumable_clay )

	self.tool:setTpRenderables( currentRenderablesTp )
	self.tool:setTpColor( color );

	if self.tool:isLocal() then
		-- Sets bucket renderable, change this to change the mesh
		self.tool:setFpRenderables( currentRenderablesFp )
		self.tool:setFpColor( color );
	end

end

function ClayTool.client_onDestroy( self )
	self.effect:stop()
end

function ClayTool.client_onUpdate( self, dt )

	-- First person animation	
	local isCrouching =  self.tool:isCrouching() 
	
	if self.tool:isLocal() then
		updateFpAnimations( self.fpAnimations, self.equipped, dt )
	end
	
	if not self.equipped then
		if self.wantEquipped then
			self.wantEquipped = false
			self.equipped = true
		end
		return
	end
	
	local crouchWeight = self.tool:isCrouching() and 1.0 or 0.0
	local normalWeight = 1.0 - crouchWeight 
	local totalWeight = 0.0
	
	for name, animation in pairs( self.tpAnimations.animations ) do
		animation.time = animation.time + dt
	
		if name == self.tpAnimations.currentAnimation then
			animation.weight = math.min( animation.weight + ( self.tpAnimations.blendSpeed * dt ), 1.0 )
			
			if animation.time >= animation.info.duration - self.blendTime then
				if ( name == "use" ) then
					setTpAnimation( self.tpAnimations, "idle", 10.0 )
				elseif name == "pickup" then
					setTpAnimation( self.tpAnimations, "idle", 0.001 )
				elseif animation.nextAnimation ~= "" then
					setTpAnimation( self.tpAnimations, animation.nextAnimation, 0.001 )
				end 
				
			end
		else
			animation.weight = math.max( animation.weight - ( self.tpAnimations.blendSpeed * dt ), 0.0 )
		end
	
		totalWeight = totalWeight + animation.weight
	end
	
	totalWeight = totalWeight == 0 and 1.0 or totalWeight
	for name, animation in pairs( self.tpAnimations.animations ) do
		
		local weight = animation.weight / totalWeight
		if name == "idle" then
			self.tool:updateMovementAnimation( animation.time, weight )
		elseif animation.crouch then
			self.tool:updateAnimation( animation.info.name, animation.time, weight * normalWeight )
			self.tool:updateAnimation( animation.crouch.name, animation.time, weight * crouchWeight )
		else
			self.tool:updateAnimation( animation.info.name, animation.time, weight )
		end
	end
	
end


function ClayTool.client_onEquippedUpdate( self, primaryState, secondaryState, forceBuildActive )
	if Chapter2VR then
		if Chapter2VR.markAdapter then Chapter2VR.markAdapter( "clay" ) end
		if Chapter2VR.primaryState then primaryState = Chapter2VR.primaryState( self, primaryState ) end
	end
	if self.tool:isLocal() then

		if forceBuildActive then
			if self.effect:isPlaying() then
				self.effect:stop()
			end
			return false, false
		end

		if primaryState == sm.tool.interactState.start then
			local firstPerson = self.tool:isInFirstPersonView()
			local cameraPos = sm.camera.getPosition()
			local cameraDir = sm.camera.getDirection()
			local maxRange = 5.0
			local vrPose, vrActive = nil, false
			if Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end
			if vrActive then
				hit, result = sm.physics.raycast( vrPose.position, vrPose.position + vrPose.direction * maxRange, self.tool:getOwner():getCharacter() )
			elseif not firstPerson then
				local raycastPos = sm.camera.getPosition() + sm.camera.getDirection() * sm.camera.getDirection():dot( GetOwnerPosition( self.tool ) - sm.camera.getPosition() )
				hit, result = sm.localPlayer.getRaycast( maxRange, raycastPos, sm.camera.getDirection() )
			else
				hit, result = sm.physics.raycast( cameraPos, cameraPos + cameraDir * maxRange, self.tool:getOwner():getCharacter() )
			end

			if hit then
				local params = {}
				params.hitPos = result.pointWorld
				params.surfaceNormal = result.normalWorld
				self:buildClay()
				self.network:sendToServer( "sv_n_buildClay", params )
				if self.effect:isPlaying() then
					self.effect:stop()
				end
				UpdateForceBuildText()
				return true, false
			end
		end
		
		UpdateForceBuildText()
	end
	return true, false
end

function ClayTool.client_onEquip( self )
	self:cl_updateBucketRenderables()
	self:cl_loadAnimations()
	
	self.wantEquipped = true
	
	setTpAnimation( self.tpAnimations, "pickup", 0.0001 )
	if self.tool:isLocal() then
		swapFpAnimation( self.fpAnimations, "unequip", "equip", 0.2 )
	end
end

function ClayTool.client_onUnequip( self )
	self.effect:stop()

	self.wantEquipped = false
	self.equipped = false
	if sm.exists( self.tool ) then
		setTpAnimation( self.tpAnimations, "putdown" )
		if self.tool:isLocal() and self.fpAnimations.currentAnimation ~= "unequip" then
			swapFpAnimation( self.fpAnimations, "equip", "unequip", 0.2 )
		end
	end
end

function ClayTool.sv_n_buildClay( self, params, player )
	sm.container.beginTransaction()
	sm.container.spend( player:getInventory(), ITEMS.obj_consumable_clay, 1, true, params.slot )
	if sm.container.endTransaction() then
		local material = 0
		sm.world.getCurrentWorld():voxelDensityAddition( params.hitPos, params.surfaceNormal, 2.0, 23, material, sm.world.voxelFilter.all, player )
		self.network:sendToClients( "cl_n_buildClay", params )
	end
end

function ClayTool.cl_n_buildClay( self, params )
	if not self.tool:isLocal() and self.tool:isEquipped() then
		self:buildClay()
	end
end

function ClayTool.buildClay( self )
	if self.tool:isLocal() then
		setFpAnimation( self.fpAnimations, "use", 0.25 )
	end
	setTpAnimation( self.tpAnimations, "use", 10.0 )
end