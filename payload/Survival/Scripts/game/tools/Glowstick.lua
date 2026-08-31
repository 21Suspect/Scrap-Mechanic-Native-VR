-- Generated from the supported stock build by generate_held_item_payload.py.
dofile( "$GAME_DATA/Scripts/game/AnimationUtil.lua" )
dofile( "$SURVIVAL_DATA/Scripts/util.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_shapes.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_projectiles.lua" )

Glowstick = class()

local renderables =   {"$SURVIVAL_DATA/Character/Char_Glowstick/char_glowstick.rend" }
local renderablesTp = {"$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_tp_glowstick.rend", "$SURVIVAL_DATA/Character/Char_Glowstick/char_glowstick_tp_animlist.rend"}
local renderablesFp = {"$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_fp_glowstick.rend", "$SURVIVAL_DATA/Character/Char_Glowstick/char_glowstick_fp_animlist.rend"}

local currentRenderablesTp = {}
local currentRenderablesFp = {}

sm.tool.preloadRenderables( renderables )
sm.tool.preloadRenderables( renderablesTp )
sm.tool.preloadRenderables( renderablesFp )

function Glowstick.client_onCreate( self )
	self:cl_init()
end

function Glowstick.client_onRefresh( self )
	self:cl_init()
end

function Glowstick.cl_init( self )
	if self.tool:isEquipped() then
		self:cl_loadAnimations()
	end
	self.spineWeight = 0
end

function Glowstick.cl_loadAnimations( self )
	
	self.tpAnimations = createTpAnimations(
		self.tool,
		{
			idle = { "glowstick_idle" },
			use = { "glowstick_use", { nextAnimation = "idle" } },
			sprint = { "glowstick_sprint" },
			pickup = { "glowstick_pickup", { nextAnimation = "idle" } },
			putdown = { "glowstick_putdown" }
		
		}
	)
	local movementAnimations = {
	
		idle = "glowstick_idle",
		
		runFwd = "glowstick_run_fwd",
		runBwd = "glowstick_run_bwd",

		sprint = "glowstick_sprint",
		sprintLeft = "glowstick_sprint_left",
		sprintRight = "glowstick_sprint_right",
		
		jump = "glowstick_jump_start",
		jumpUp = "glowstick_jump_up",
		jumpDown = "glowstick_jump_down",
		
		land = "glowstick_jump_land",
		landFwd = "glowstick_jump_land_fwd",
		landBwd = "glowstick_jump_land_bwd",
		landLeft = "glowstick_jump_land_left",
		landRight = "glowstick_jump_land_right",

		crouchIdle = "glowstick_crouch_idle",
		crouchFwd = "glowstick_crouch_fwd",
		crouchBwd = "glowstick_crouch_bwd"
	}

	for name, animation in pairs( movementAnimations ) do
		self.tool:setMovementAnimation( name, animation )
	end

	if self.tool:isLocal() then
		self.fpAnimations = createFpAnimations(
			self.tool,
			{
				idle = { "glowstick_idle", { looping = true } },
				use = { "glowstick_use", { nextAnimation = "idle" } },
				equip = { "glowstick_pickup", { nextAnimation = "idle" } },
				unequip = { "glowstick_putdown" }
			}
		)
	end
	setTpAnimation( self.tpAnimations, "idle", 5.0 )
	self.blendTime = 0.2
	
end

function Glowstick.client_onUpdate( self, dt )

	-- First person animation	
	local isSprinting =  self.tool:isSprinting() 
	local isCrouching =  self.tool:isCrouching() 
	
	if self.tool:isLocal() then
		updateFpAnimations( self.fpAnimations, self.equipped, dt )
	end
	
	if self.equipped and not sm.exists( self.glowEffect ) then
		self.glowEffect = sm.effect.createEffect( "Glowstick - Hold" )
	end

	if sm.exists( self.glowEffect ) then
		if self.equipped and not self.glowEffect:isPlaying() then
			self.glowEffect:start()
		elseif not self.equipped and self.glowEffect:isPlaying() then
			self.glowEffect:stop()
		end

		if self.tool:isLocal() and self.tool:isInFirstPersonView() then
			self.glowEffect:detach();
			self.glowEffect:setPosition( self.tool:getFpBonePos( "jnt_right_hand" ) )

			self.glowEffect:setParameter( "smoothing", 0 )

		else
			self.glowEffect:setHost( self.tool:getOwner():getCharacter(), "jnt_right_hand" )

			local offsetX = 0.0
			local offsetY = 0.2
			local charFwd = sm.localPlayer.getDirection()
			local charFwdFlat = sm.vec3.new( charFwd.x, charFwd.y, 0 ):normalize()
			local charRight = charFwdFlat:cross( sm.vec3.new( 0, 0, 1 ) )
			local character = self.tool:getOwner():getCharacter()
			local speed = character:getVelocity();
			if character then
				local velocity = character:getVelocity()
				speed = velocity:length();
				local rightSpeed = velocity:dot( charRight )
				local fwdSpeed = velocity:dot( charFwdFlat )
				
				offsetX = offsetX + rightSpeed * 0.1
				offsetY = offsetY + ( math.max( -rightSpeed, 0 ) + math.max( fwdSpeed, 0 ) ) * 0.1
			end
			offsetX = clamp( offsetX, -1.0, 1.0 )
			offsetY = clamp( offsetY, -1.0, 1.0 )
			local worldOffset = charRight * offsetX + charFwdFlat * offsetY
			worldOffset.z = 0.2;

			self.glowEffect:setParameter( "smoothing", 0.1 + speed * 1 )
			self.glowEffect:setParameter( "smoothingDeadzone", 0.012 )
			self.glowEffect:setParameter( "smoothingOffset", worldOffset )
			self.glowEffect:setParameter( "smoothingAvoidanceClearance", 0.10 )
			self.glowEffect:setParameter( "smoothingBoneAvoidanceRadius", 0.25 )
		end
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
			
			if animation.looping == true then
				if animation.time >= animation.info.duration then
					animation.time = animation.time - animation.info.duration
				end
			end
			if animation.time >= animation.info.duration - self.blendTime and not animation.looping then
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
	
	local signedDt = isSprinting and -dt or dt
    self.spineWeight = clamp( self.spineWeight + ( 10.0 * signedDt ), 0.0, 1.0 )
	
	local playerDir = self.tool:getSmoothDirection()
	local angle = math.asin( playerDir:dot( sm.vec3.new( 0, 0, 1 ) ) ) / (math.pi / 2)
	local finalAngle = ( 0.5 + angle * 0.5 )
	local idleBendWeight = self.spineWeight
	self.tool:updateAnimation( "glowstick_idle_updownbend", finalAngle, idleBendWeight )
end

function Glowstick.client_onEquip( self, animate )
	
	self.wantEquipped = true
	
	currentRenderablesTp = {}
	currentRenderablesFp = {}
	
	for k,v in pairs( renderablesTp ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( renderablesFp ) do currentRenderablesFp[#currentRenderablesFp+1] = v end
	for k,v in pairs( renderables ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( renderables ) do currentRenderablesFp[#currentRenderablesFp+1] = v end
	
	self.tool:setTpRenderables( currentRenderablesTp )
	if self.tool:isLocal() then
		if animate then
			sm.effect.playEffect( "audio:event:/tools/glowstick/pickup", self.tool:getPosition() )
		end
		self.tool:setFpRenderables( currentRenderablesFp )
	end
	
	self:cl_loadAnimations()
	
	setTpAnimation( self.tpAnimations, "pickup", 0.0001 )
	if self.tool:isLocal() then
		swapFpAnimation( self.fpAnimations, "unequip", "equip", 0.2 )
	end

end

function Glowstick.client_onUnequip( self, animate )
	self.wantEquipped = false
	self.equipped = false
	self.pendingThrowFlag = false
	if sm.exists( self.tool ) then
		setTpAnimation( self.tpAnimations, "putdown" )
		if self.tool:isLocal() and self.fpAnimations.currentAnimation ~= "unequip" then
			swapFpAnimation( self.fpAnimations, "equip", "unequip", 0.2 )
		end
	
		if animate then
			sm.effect.playEffect( "audio:event:/tools/glowstick/putdown", self.tool:getPosition() )
		end
	end
end

-- Start


-- Interact
function Glowstick.client_onEquippedUpdate( self, primaryState, secondaryState, forceBuildActive )
	if Chapter2VR then
		if Chapter2VR.markAdapter then Chapter2VR.markAdapter( "glowstick" ) end
		if Chapter2VR.primaryState then primaryState = Chapter2VR.primaryState( self, primaryState ) end
	end

	if self.pendingThrowFlag then
		local time = 0.0
		local frameTime = 0.0
		if self.fpAnimations.currentAnimation == "use" then
			time = self.fpAnimations.animations["use"].time
			frameTime = 1.15
		end
		if time >= frameTime and frameTime ~= 0 then
			self.pendingThrowFlag = false
			if self.tool:getOwner().character then
				local vrFirePos, vrDirection, vrAuthoritative = nil, nil, false
				if Chapter2VR and Chapter2VR.projectileFirePose then
					vrFirePos, vrDirection, vrAuthoritative = Chapter2VR.projectileFirePose( self.tool, true )
				end
				if vrAuthoritative and not vrFirePos then return true, true end
				local vrActive = vrFirePos ~= nil and vrDirection ~= nil
				local facingDir = vrActive and vrDirection:normalize() or sm.localPlayer.getDirection()
				local modifier = math.sqrt( math.max( 0, 1 - ( facingDir.z * facingDir.z ) ) )
				local maxVelocity = 25.0
				local minVelocity = 15.0
				local fireVelocity = minVelocity + ( maxVelocity - minVelocity ) * modifier
				local dir = facingDir
				local handPosition = vrActive and vrFirePos or self.tool:getTpBonePos("jnt_right_hand")
				local firePos = handPosition
				if not vrActive then
					dir = facingDir:rotate( math.rad( 6.943279 ) * modifier, sm.camera.getRight() )
					local centeredFirePos = GetOwnerPosition(self.tool) + sm.vec3.new(0, 0, 0.5)
					local right = facingDir:cross(sm.vec3.new(0,0,1))
					local screenOffset = right * 0.4
					firePos = handPosition + facingDir * -0.5 + screenOffset
					local rayCastDir = (centeredFirePos - firePos):normalize()
					local firePosTest = firePos + rayCastDir * -0.3
					local hit = sm.physics.raycast(firePosTest, centeredFirePos, self.tool:getOwner().character)
					if hit then firePos = handPosition end
					if self.tool:isInFirstPersonView() then firePos = centeredFirePos end
				end
				sm.projectile.projectileAttack( projectile_glowstick, 0, firePos, dir * fireVelocity, self.tool:getOwner(), handPosition )
			end
		end
		return true, true
	elseif not forceBuildActive then
		if primaryState == sm.tool.interactState.start then
			local activeItem = sm.localPlayer.getActiveItem()
			if sm.container.canSpend( sm.localPlayer.getInventory(), activeItem, 1 ) then
				self:onUse()
				self.pendingThrowFlag = true
				self.network:sendToServer( "sv_n_onThrowAnim" )
			end
		end
		return true, false
	end
	
	return false, false
	
end

function Glowstick.onUse( self )
	if self.tool:isLocal() then
		setFpAnimation( self.fpAnimations, "use", 0.25 )
	end
	setTpAnimation( self.tpAnimations, "use", 10.0 )

	sm.effect.playHostedEffect( "Glowstick - Throw", self.tool:getOwner():getCharacter() )
end

function Glowstick.cl_n_onThrowAnim( self )
	if not self.tool:isLocal() and self.tool:isEquipped() then
		self:onUse()
	end
end

function Glowstick.sv_n_onThrowAnim( self, params, player )
	self.network:sendToClients( "cl_n_onThrowAnim" )
end
