-- Generated from the supported stock build by generate_held_item_payload.py.
dofile( "$GAME_DATA/Scripts/game/AnimationUtil.lua")
dofile( "$SURVIVAL_DATA/Scripts/util.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_shapes.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_projectiles.lua" )

Bucket = class()

local emptyRenderables = {
	"$SURVIVAL_DATA/Character/Char_bucket/char_bucket_empty.rend"
}

local waterRenderables = {
	"$SURVIVAL_DATA/Character/Char_bucket/char_bucket_full.rend"
}

local chemicalRenderables = {
	"$SURVIVAL_DATA/Character/Char_bucket/char_bucket_full_chemical.rend"
}
local oilRenderables = {
	"$SURVIVAL_DATA/Character/Char_bucket/char_bucket_full_oil.rend"
}

local renderablesTp = {"$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_tp_bucket.rend", "$SURVIVAL_DATA/Character/Char_bucket/char_bucket_tp_animlist.rend"}
local renderablesFp = {"$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_fp_bucket.rend", "$SURVIVAL_DATA/Character/Char_bucket/char_bucket_fp_animlist.rend"}

local currentRenderablesTp = {}
local currentRenderablesFp = {}

sm.tool.preloadRenderables( emptyRenderables )
sm.tool.preloadRenderables( waterRenderables )
sm.tool.preloadRenderables( renderablesTp )
sm.tool.preloadRenderables( renderablesFp )

local FireCooldown = 0.40
local FireVelocity = 10.0
local SpreadDeg = 10


function Bucket.client_onCreate( self )
	self:client_onRefresh()
end

function Bucket.client_onRefresh( self )
	if self.tool:isLocal() then
		self.activeItem = nil
		self.wasOnGround = true
	end
	if self.tool:isEquipped() then
		self:cl_updateBucketRenderables( nil )
		self:loadAnimations()
	end
end

function Bucket.loadAnimations( self )
	
	self.tpAnimations = createTpAnimations(
		self.tool,
		{
			idle = { "bucket_idle", { looping = true } },
			use = { "bucket_use_full", { nextAnimation = "idle" } },
			useempty = { "bucket_use_empty", { nextAnimation = "idle" } },
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

	setTpAnimation( self.tpAnimations, "idle", 5.0 )

	if self.tool:isLocal() then
		self.fpAnimations = createFpAnimations(
			self.tool,
			{
				idle = { "bucket_idle", { looping = true } },
				use = { "bucket_use_full", { nextAnimation = "idle" } },
				useempty = { "bucket_use_empty", { nextAnimation = "idle" } },
				
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
	
	self.fireCooldownTimer = 0.0
	self.blendTime = 0.2
end

function Bucket.client_onUpdate( self, dt )

	-- First person animation	
	local isSprinting =  self.tool:isSprinting() 
	local isCrouching =  self.tool:isCrouching() 
	local isOnGround =  self.tool:isOnGround()
	
	if self.tool:isLocal() then
		if self.equipped then
			if isSprinting and self.fpAnimations.currentAnimation ~= "sprintInto" and self.fpAnimations.currentAnimation ~= "sprintIdle" then
				swapFpAnimation( self.fpAnimations, "sprintExit", "sprintInto", 0.0 )
			elseif not self.tool:isSprinting() and ( self.fpAnimations.currentAnimation == "sprintIdle" or self.fpAnimations.currentAnimation == "sprintInto" ) then
				swapFpAnimation( self.fpAnimations, "sprintInto", "sprintExit", 0.0 )
			end
			
			if not isOnGround and self.wasOnGround and self.fpAnimations.currentAnimation ~= "jump" then
				swapFpAnimation( self.fpAnimations, "land", "jump", 0.2 )
			elseif isOnGround and not self.wasOnGround and self.fpAnimations.currentAnimation ~= "land" then
				swapFpAnimation( self.fpAnimations, "jump", "land", 0.2 )
			end
			
		end
		updateFpAnimations( self.fpAnimations, self.equipped, dt )
		
		self.wasOnGround = isOnGround
	end
	
	if not self.equipped then
		if self.wantEquipped then
			self.wantEquipped = false
			self.equipped = true
		end
		return
	end
	if self.tool:isLocal() then
		local activeItem = sm.localPlayer.getActiveItem()
		if self.activeItem ~= activeItem then
			self.activeItem = activeItem
	
			self.network:sendToServer( "server_network_updateBucketRenderables", activeItem )
		end
	end

	local crouchWeight = self.tool:isCrouching() and 1.0 or 0.0
	local normalWeight = 1.0 - crouchWeight 
	local totalWeight = 0.0
	
	for name, animation in pairs( self.tpAnimations.animations ) do
		animation.time = animation.time + dt

		if name == self.tpAnimations.currentAnimation then
			animation.weight = math.min( animation.weight + ( self.tpAnimations.blendSpeed * dt ), 1.0 )
			
			if animation.time >= animation.info.duration - self.blendTime then
				if ( name == "use" or name == "useempty" ) then
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
	
	-- Timers
	self.fireCooldownTimer = math.max( self.fireCooldownTimer - dt, 0.0 )
end


function Bucket.client_onToggle( self )
	return false
end

function Bucket.client_onEquip( self, animate )
	if self.tool:isLocal() then
		if animate then
			sm.effect.playEffect( "audio:event:/tools/waterbucket_pickup", self.tool:getPosition() )
		end
		self.activeItem = nil
	end
	self:cl_updateBucketRenderables( nil )
	
	self:loadAnimations()
	
	self.wantEquipped = true
	self.aiming = false
	
	setTpAnimation( self.tpAnimations, "pickup", 0.0001 )
	if self.tool:isLocal() then
		swapFpAnimation( self.fpAnimations, "unequip", "equip", 0.2 )
	end

end

function Bucket.server_network_updateBucketRenderables( self, bucketUid )
	self.network:sendToClients( "cl_updateBucketRenderables", bucketUid )
end

function Bucket.cl_updateBucketRenderables( self, bucketUid )

	local bucketRenderables = {}
	if bucketUid == obj_tool_bucket_empty then
		bucketRenderables = emptyRenderables
	elseif bucketUid == obj_tool_bucket_water then
		bucketRenderables = waterRenderables
	elseif bucketUid == obj_tool_bucket_chemical then
		bucketRenderables = chemicalRenderables
	elseif bucketUid == obj_tool_bucket_oil then
		bucketRenderables = oilRenderables
	end

	currentRenderablesTp = {}
	currentRenderablesFp = {}
	
	for k,v in pairs( renderablesTp ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( renderablesFp ) do currentRenderablesFp[#currentRenderablesFp+1] = v end
	for k,v in pairs( bucketRenderables ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( bucketRenderables ) do currentRenderablesFp[#currentRenderablesFp+1] = v end
	

	local color = sm.item.getShapeDefaultColor( obj_tool_bucket_empty )

	self.tool:setTpRenderables( currentRenderablesTp )
	self.tool:setTpColor( color );

	if self.tool:isLocal() then
		-- Sets bucket renderable, change this to change the mesh
		self.tool:setFpRenderables( currentRenderablesFp )
		self.tool:setFpColor( color );
	end

end

function Bucket.client_onUnequip( self, animate )
	if sm.exists( self.tool ) then
		if self.tool:isLocal() then
			if animate then
				sm.effect.playEffect( "audio:event:/tools/waterbucket_putdown", self.tool:getPosition() )
			end
			self.activeItem = nil
		end
		setTpAnimation( self.tpAnimations, "putdown" )
		if self.tool:isLocal() and self.fpAnimations.currentAnimation ~= "unequip" then
			swapFpAnimation( self.fpAnimations, "equip", "unequip", 0.2 )
		end
	end
	self.wantEquipped = false
	self.equipped = false
end

function Bucket.sv_n_onUse( self, params )
	if params.projectileUuid == projectile_water then
		sm.container.beginTransaction()
		sm.container.spendFromSlot( params.container, params.selectedSlot, obj_tool_bucket_water, 1, true )
		sm.container.collectToSlot( params.container, params.selectedSlot, obj_tool_bucket_empty, 1, true )
		if sm.container.endTransaction() then
			self.network:sendToClients( "cl_n_onUse", params )
		end
	elseif params.projectileUuid == projectile_chemical then
		sm.container.beginTransaction()
		sm.container.spendFromSlot( params.container, params.selectedSlot, obj_tool_bucket_chemical, 1, true )
		sm.container.collectToSlot( params.container, params.selectedSlot, obj_tool_bucket_empty, 1, true )
		if sm.container.endTransaction() then
			self.network:sendToClients( "cl_n_onUse", params )
		end
	elseif params.projectileUuid == projectile_oil then
		sm.container.beginTransaction()
		sm.container.spendFromSlot( params.container, params.selectedSlot, obj_tool_bucket_oil, 1, true )
		sm.container.collectToSlot( params.container, params.selectedSlot, obj_tool_bucket_empty, 1, true )
		if sm.container.endTransaction() then
			self.network:sendToClients( "cl_n_onUse", params )
		end
	elseif params.projectileUuid == nil then
		sm.container.beginTransaction()
		if params.fromContainer then
			sm.container.spend( params.fromContainer, params.fill, 1, true )
		end
		sm.container.spendFromSlot( params.container, params.selectedSlot, obj_tool_bucket_empty, 1, true )
		if params.fill == obj_consumable_water then
			sm.container.collectToSlot( params.container, params.selectedSlot, obj_tool_bucket_water, 1, true )
		elseif params.fill == obj_consumable_chemical then
			sm.container.collectToSlot( params.container, params.selectedSlot, obj_tool_bucket_chemical, 1, true )
		elseif params.fill == obj_resource_crudeoil then
			sm.container.collectToSlot( params.container, params.selectedSlot, obj_tool_bucket_oil, 1, true )
		end
		if sm.container.endTransaction() then
			self.network:sendToClients( "cl_n_onUse", params )
		end
	end
end

function Bucket.cl_n_onUse( self, params )
	if self.tool:isLocal() then
		if params.projectileUuid then
			local dir = sm.noise.gunSpread( params.dir, SpreadDeg )
			local fakePosSelf = self:calculateTpMuzzlePos()
			sm.projectile.projectileAttack( params.projectileUuid, 0, params.firePos, dir * FireVelocity, sm.localPlayer.getPlayer(), params.fakePos, fakePosSelf )
		end
	else
		self:onUse( params )
	end
end

function Bucket.onUse( self, params )
	self.tpAnimations.animations.idle.time = 0
	if params.projectileUuid then
		setTpAnimation( self.tpAnimations, "use", 10.0 )
		sm.effect.playHostedEffect("Bucket - Throw", self.tool:getOwner():getCharacter() )
	else
		setTpAnimation( self.tpAnimations, "useempty", 10.0 )
		sm.effect.playHostedEffect("Bucket - Fill", self.tool:getOwner():getCharacter() )
		local prefix = "Water - "
		if params.fill == obj_consumable_chemical then
			prefix = "Chemical - "
		elseif params.fill == obj_resource_crudeoil then
			prefix = "Oil - "
		end

		sm.effect.playEffect(prefix.."BucketGetWater", params.aimPoint )
	end
end

function Bucket.calculateFirePosition( self )
	local crouching = self.tool:isCrouching()
	local firstPerson = self.tool:isInFirstPersonView()
	local dir = sm.localPlayer.getDirection()
	local pitch = math.asin( dir.z )		
	local right = sm.localPlayer.getRight()
	
	local fireOffset = sm.vec3.new( 0.0, 0.0, 0.0 )

	if crouching then
		fireOffset.z = 0.15
	else
		fireOffset.z = 0.45
	end

	if firstPerson then
		if not self.aiming then
			fireOffset = fireOffset + right * 0.05
		end
	else
		fireOffset = fireOffset + right * 0.25		
		fireOffset = fireOffset:rotate( math.rad( pitch ), right )
	end
	local firePosition = GetOwnerPosition( self.tool ) + fireOffset
	return firePosition
end

function Bucket.calculateTpMuzzlePos( self )
	local crouching = self.tool:isCrouching()
	local dir = sm.localPlayer.getDirection()
	local pitch = math.asin( dir.z )		
	local right = sm.localPlayer.getRight()
	local up = right:cross(dir)
	
	local fakeOffset = sm.vec3.new( 0.0, 0.0, 0.0 )
	
	--General offset
	fakeOffset = fakeOffset + right * 0.25
	fakeOffset = fakeOffset + dir * 0.5
	fakeOffset = fakeOffset + up * 0.25
	
	--Action offset
	local pitchFraction = pitch / ( math.pi * 0.5 )
	if crouching then
		fakeOffset = fakeOffset + dir * 0.2
		fakeOffset = fakeOffset + up * 0.1
		fakeOffset = fakeOffset - right * 0.05
		
		if pitchFraction > 0.0 then
			fakeOffset = fakeOffset - up * 0.2 * pitchFraction
		else
			fakeOffset = fakeOffset + up * 0.1 * math.abs( pitchFraction )
		end		
	else
		fakeOffset = fakeOffset + up * 0.1 *  math.abs( pitchFraction )		
	end
	
	local fakePosition = fakeOffset + GetOwnerPosition( self.tool )
	return fakePosition
end

-- Interact
function Bucket.client_onEquippedUpdate( self, primaryState, secondaryState, forceBuildActive )
	if Chapter2VR then
		if Chapter2VR.markAdapter then Chapter2VR.markAdapter( "bucket" ) end
		if Chapter2VR.primaryState then primaryState = Chapter2VR.primaryState( self, primaryState ) end
	end

	local activeItem = sm.localPlayer.getActiveItem()
	if activeItem == obj_tool_bucket_empty and not forceBuildActive then
		local rayStart = sm.localPlayer.getRaycastStart()
		local rayDir = sm.localPlayer.getDirection()
		local vrPose, vrActive = nil, false
		if Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end
		if vrActive then
			rayStart = vrPose.position
			rayDir = vrPose.direction:normalize()
		end
		local success, result = sm.physics.raycast( rayStart, rayStart + rayDir * 7.5, sm.localPlayer.getPlayer().character, bit.bor( sm.physics.filter.default, sm.physics.filter.areaTrigger ) )

		if success then
			local fill
			local fromContainer

			if result.type == "areaTrigger" then
				local trigger = result:getAreaTrigger()
				if trigger and sm.exists( trigger ) then
					local triggerUserData = trigger:getUserData()
					if triggerUserData and triggerUserData.water then
						fill = obj_consumable_water
					end
					if triggerUserData and triggerUserData.chemical then
						fill = obj_consumable_chemical
					end
					if triggerUserData and triggerUserData.oil then
						fill = obj_resource_crudeoil
					end
				end
			elseif result.type == "body" then
				local shape = result:getShape()
				if shape and shape.uuid == obj_container_water then
					fromContainer = shape.interactable:getContainer()
					if sm.container.canSpend( fromContainer, obj_consumable_water, 1 ) then
						fill = obj_consumable_water
					end
				elseif shape.uuid == obj_container_chemical then
					fromContainer = shape.interactable:getContainer()
					if sm.container.canSpend( fromContainer, obj_consumable_chemical, 1 ) then
						fill = obj_consumable_chemical
					end
				end
			end

			if fill then
				local keyBindingText =  sm.gui.getKeyBinding( "Create", true )
				sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_FILL}" )
				
				if primaryState == sm.tool.interactState.start then
					self:cl_fill( { fill = fill, fromContainer = fromContainer, aimPoint = result.pointWorld } )
				end
				UpdateForceBuildText()
				return true, false
			end
		end
		UpdateForceBuildText()
		return true, false

	elseif activeItem == obj_tool_bucket_water and not forceBuildActive then
		local keyBindingText =  sm.gui.getKeyBinding( "Create", true )
		sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_THROW}" )
		
		if primaryState == sm.tool.interactState.start then
			self:cl_throw( activeItem )
		end
		UpdateForceBuildText()
		return true, false

	elseif activeItem == obj_tool_bucket_chemical and not forceBuildActive then
		local keyBindingText =  sm.gui.getKeyBinding( "Create", true )
		sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_THROW}" )
		
		if primaryState == sm.tool.interactState.start then
			self:cl_throw( activeItem )
		end
		UpdateForceBuildText()
		return true, false
	elseif activeItem == obj_tool_bucket_oil and not forceBuildActive then
		local keyBindingText =  sm.gui.getKeyBinding( "Create", true )
		sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_THROW}" )
		
		if primaryState == sm.tool.interactState.start then
			self:cl_throw( activeItem )
		end
		UpdateForceBuildText()
		return true, false
	end
	return false, false
	
end

function Bucket.cl_fill( self, params )
	if sm.game.getLimitedInventory() then
		params.container = sm.localPlayer.getInventory()
	else
		params.container = sm.localPlayer.getHotbar()
	end
	params.selectedSlot = sm.localPlayer.getSelectedHotbarSlot()
	self.network:sendToServer( "sv_n_onUse", params )
	self:onUse( params )
	setFpAnimation( self.fpAnimations, "useempty", 0.25 )
end

function Bucket.cl_throw( self, activeItem )
	if self.tool:getOwner().character == nil then
		return
	end

	local projectileUuid = nil
	if activeItem == obj_tool_bucket_water then
		projectileUuid = projectile_water
	elseif activeItem == obj_tool_bucket_chemical then
		projectileUuid = projectile_chemical
	elseif activeItem == obj_tool_bucket_oil then
		projectileUuid = projectile_oil
	else
		error( "invalid item" )
	end
	
	if self.fireCooldownTimer <= 0.0 then
		if sm.container.canSpend( sm.localPlayer.getInventory(), activeItem, 1 ) then
			local firstPerson = self.tool:isInFirstPersonView()
			local vrPose, vrActive = nil, false
			if Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end
			local dir = vrActive and vrPose.direction:normalize() or sm.localPlayer.getDirection()
			local firePos = vrActive and vrPose.position or self:calculateFirePosition()
			local fakePos = vrActive and vrPose.position or self:calculateTpMuzzlePos()
			if not vrActive then
				local forward = sm.vec3.new( 0, 0, 1 ):cross( sm.localPlayer.getRight() )
				local pitchScale = forward:dot( dir )
				dir = dir:rotate( math.rad( pitchScale * 18 ), sm.camera.getRight() )
			end
			
			-- Timers
			self.fireCooldownTimer = FireCooldown

			-- Send TP shoot over network and dircly to self
			
			local params = { projectileUuid = projectileUuid, firePos = firePos, fakePos = fakePos, dir = dir, selectedSlot = sm.localPlayer.getSelectedHotbarSlot() }
			if sm.game.getLimitedInventory() then
				params.container = sm.localPlayer.getInventory()
			else
				params.container = sm.localPlayer.getHotbar()
			end
			self.network:sendToServer( "sv_n_onUse", params )
			self:onUse( params )
			
			-- Play FP shoot animation
			setFpAnimation( self.fpAnimations, "use", 0.25 )
		end
	end
end
