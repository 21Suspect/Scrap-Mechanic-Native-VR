-- Generated from the supported stock build by generate_held_item_payload.py.
dofile( "$GAME_DATA/Scripts/game/AnimationUtil.lua" )
dofile( "$SURVIVAL_DATA/Scripts/util.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_shapes.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_projectiles.lua" )

Cornade = class()

local renderables = { "$SURVIVAL_DATA/Character/Char_Cornade/char_cornade.rend" }
local renderablesTp = { "$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_tp_cornade.rend",
	"$SURVIVAL_DATA/Character/Char_Cornade/char_cornade_tp_animlist.rend" }
local renderablesFp = { "$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_fp_cornade.rend",
	"$SURVIVAL_DATA/Character/Char_Cornade/char_cornade_fp_animlist.rend" }

local currentRenderablesTp = {}
local currentRenderablesFp = {}

sm.tool.preloadRenderables( renderables )
sm.tool.preloadRenderables( renderablesTp )
sm.tool.preloadRenderables( renderablesFp )

function Cornade.client_onCreate( self )
	self:cl_init()
end

function Cornade.client_onRefresh( self )
	self:cl_init()
end

function Cornade.cl_init( self )
	if self.tool:isEquipped() then
		self:cl_loadAnimations()
	end
	self.lifetime = CORNADE_LIFETIME
	self.thrown = false
	self.minInstantThrowTime = 0.5
	self.spineWeight = 0
	self.jointWeight = 0
end

function Cornade.cl_loadAnimations( self )
	self.tpAnimations = createTpAnimations(
		self.tool,
		{
			idle = { "cornade_idle", { looping = true } },
			use = { "cornade_use", { nextAnimation = "holdActive" } },
			holdActive = { "cornade_holdActive", { looping = true } },
			throw = { "cornade_throw", { nextAnimation = "equip" } },
			equip = { "cornade_pickup", { nextAnimation = "idle" } },
			unequip = { "cornade_putdown" }

		}
	)

	self.spinAnimation = {
		info = self.tool:getAnimationInfo( "cornade_spinbend" ),
		time = 0.0,
		weight = 0.0,
		playRate = 3.0,
		looping = true,
		nextAnimation = nil
	}
	local movementAnimations = {

		idle = "cornade_idle",

		runFwd = "cornade_run_fwd",
		runBwd = "cornade_run_bwd",

		sprint = "cornade_sprint",
		sprintLeft = "cornade_sprint_left",
		sprintRight = "cornade_sprint_right",

		jump = "cornade_jump_start",
		jumpUp = "cornade_jump_up",
		jumpDown = "cornade_jump_down",

		land = "cornade_jump_land",
		landFwd = "cornade_jump_land_fwd",
		landBwd = "cornade_jump_land_bwd",
		landLeft = "cornade_jump_land_left",
		landRight = "cornade_jump_land_right",

		crouchIdle = "cornade_crouch_idle",
		crouchFwd = "cornade_crouch_fwd",
		crouchBwd = "cornade_crouch_bwd"
	}
	for name, animation in pairs( movementAnimations ) do
		self.tool:setMovementAnimation( name, animation )
	end

	if self.tool:isLocal() then
		self.fpAnimations = createFpAnimations(
			self.tool,
			{
				idle = { "cornade_idle", { looping = true } },
				use = { "cornade_use", { nextAnimation = "holdActive" } },
				holdActive = { "cornade_holdActive", { looping = true } },
				throw = { "cornade_throw" },
				equip = { "cornade_pickup", { nextAnimation = "idle" } },
				unequip = { "cornade_putdown" }
			}
		)
	end
	setTpAnimation( self.tpAnimations, "idle", 5.0 )
	self.blendTime = 0.2
end

function Cornade.client_onUpdate( self, dt )
	-- First person animation
	local isSprinting = self.tool:isSprinting()

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

	self.spinAnimation.time = self.spinAnimation.time + dt
	if self.pinPulledFlag then
		local isFpView = self.tool:isInFirstPersonView()
		local fpIsPlaying = self.cornadeActivationEffectFp:isPlaying()
		local tpIsPlaying = self.cornadeActivationEffectTp:isPlaying()

		local swapFirstPersonEffect = isFpView and not fpIsPlaying
		local swapThirdPersonEffect = not isFpView and not tpIsPlaying

		if swapFirstPersonEffect then
			-- fpEffect is started with delay
			self.cornadeActivationEffectTp:stopImmediate()
		elseif swapThirdPersonEffect then
			self.cornadeActivationEffectTp:start( 0 )
			self.cornadeActivationEffectFp:stop()
		end

		if not self.cornadeActivationEffectAudio:isPlaying() then
			self.cornadeActivationEffectAudio:start()
		end

		self.spinAnimation.weight = math.min( self.spinAnimation.weight + (self.tpAnimations.blendSpeed * dt), 1.0 )
		if self.spinAnimation.time >= self.spinAnimation.info.duration then
			self.spinAnimation.time = self.spinAnimation.time - self.spinAnimation.info.duration
		end
	else
		self.spinAnimation.weight = math.max( self.spinAnimation.weight - (self.tpAnimations.blendSpeed * dt), 0.0 )
	end

	for name, animation in pairs( self.tpAnimations.animations ) do
		animation.time = animation.time + dt
		if name == self.tpAnimations.currentAnimation then
			animation.weight = math.min( animation.weight + (self.tpAnimations.blendSpeed * dt), 1.0 )
			if animation.looping == true then
				if animation.time >= animation.info.duration then
					animation.time = animation.time - animation.info.duration
				end
			end


			if animation.time >= animation.info.duration - self.blendTime and not animation.looping then
				if (name == "throw") then
					setTpAnimation( self.tpAnimations, "equip", 5.0 )
				elseif animation.nextAnimation ~= "" then
					setTpAnimation( self.tpAnimations, animation.nextAnimation, 5.0 )
				end
			end
		else
			animation.weight = math.max( animation.weight - (self.tpAnimations.blendSpeed * dt), 0.0 )
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
	if self.pinPulledFlag then
		self.tool:updateAnimation( self.spinAnimation.info.name, self.spinAnimation.time,
			self.spinAnimation.weight )
	end

	local playerDir = self.tool:getSmoothDirection()
	local angle = math.asin( playerDir:dot( sm.vec3.new( 0, 0, 1 ) ) ) / (math.pi / 2)

	if (not isSprinting) then
		self.spineWeight = math.min( self.spineWeight + (10.0 * dt), 1.0 )
	else
		self.spineWeight = math.max( self.spineWeight - (10.0 * dt), 0.0 )
	end

	local finalAngle = (0.5 + angle * 0.5)
	local idleBendWeight = 0
	local holdBendWeight = 0
	if self.pinPulledFlag then
		holdBendWeight = self.spineWeight
	else
		idleBendWeight = self.spineWeight
	end
	self.tool:updateAnimation( "cornade_hold_updownbend", finalAngle, holdBendWeight )
	self.tool:updateAnimation( "cornade_idle_updownbend", finalAngle, idleBendWeight )
end

function Cornade.client_onEquip( self, animate )
	if animate then
		sm.effect.playEffect( "Cornade - Equip", self.tool:getPosition() )
	end
	self.wantEquipped = true
	self.pinPulledFlag = false
	self.pendingThrowFlag = false
	self.cornadeActivationEffectFp = sm.effect.createEffectFirstPerson( "Cornnade - Activation" )
	self.cornadeActivationEffectTp = sm.effect.createEffect( "Cornnade - Activation", self.tool:getOwner():getCharacter(), "root_cornade" )
	self.cornadeActivationEffectTp:setOffsetPosition( sm.vec3.new( 0, 0.25, 0 ) )
	self.cornadeActivationEffectTp:setOffsetRotation( sm.vec3.getRotation( sm.vec3.new( 0, 0, 1 ), sm.vec3.new( 0, 1, 0 ) ) )
	self.cornadeActivationEffectAudio = sm.effect.createEffect( "audio:event:/weapons/cornade/activate", self.tool:getOwner():getCharacter(), "root_cornade" )
	self.cornadeActivationEffectAudio:setOffsetPosition( sm.vec3.new( 0, 0.25, 0 ) )
	self.cornadeActivationEffectAudio:setOffsetRotation( sm.vec3.getRotation( sm.vec3.new( 0, 0, 1 ), sm.vec3.new( 0, 1, 0 ) ) )
	currentRenderablesTp = {}
	currentRenderablesFp = {}

	for k, v in pairs( renderablesTp ) do currentRenderablesTp[#currentRenderablesTp + 1] = v end
	for k, v in pairs( renderablesFp ) do currentRenderablesFp[#currentRenderablesFp + 1] = v end
	for k, v in pairs( renderables ) do currentRenderablesTp[#currentRenderablesTp + 1] = v end
	for k, v in pairs( renderables ) do currentRenderablesFp[#currentRenderablesFp + 1] = v end

	self.tool:setTpRenderables( currentRenderablesTp )
	if self.tool:isLocal() then
		self.tool:setFpRenderables( currentRenderablesFp )
	end

	self:cl_loadAnimations()

	setTpAnimation( self.tpAnimations, "equip", 5 )
	if self.tool:isLocal() then
		swapFpAnimation( self.fpAnimations, "unequip", "equip", 0.2 )
	end
end

function Cornade.client_onUnequip( self, animate )
	self.wantEquipped = false
	self.equipped = false
	self.pendingThrowFlag = false
	self.pinPulledFlag = false
	self.tool:setBlockSprint( false )
	self.cornadeActivationEffectFp:stop()
	self.cornadeActivationEffectTp:stop()
	self.cornadeActivationEffectAudio:stop()
	if sm.exists( self.tool ) then
		setTpAnimation( self.tpAnimations, "unequip", 5 )
		if self.tool:isLocal() and self.fpAnimations.currentAnimation ~= "unequip" then
			swapFpAnimation( self.fpAnimations, "equip", "unequip", 0.2 )
		end
		local playedOutOfAmmo = false
		if self.tool:getOwner() then
			local inventory = self.tool:getOwner():getInventory()
			if inventory and not inventory:canSpend( ITEMS.obj_consumable_cornade, 1 ) then
				playedOutOfAmmo = true
			end
		end
		if not playedOutOfAmmo and animate then
			sm.effect.playEffect( "Cornade - Unequip", self.tool:getPosition() )
		end
	end
end

-- Start

-- Interact
function Cornade.client_onEquippedUpdate( self, primaryState, secondaryState, forceBuildActive )
	if Chapter2VR then
		if Chapter2VR.markAdapter then Chapter2VR.markAdapter( "cornade" ) end
		if Chapter2VR.primaryState then primaryState = Chapter2VR.primaryState( self, primaryState ) end
	end
	self.tool:setBlockSprint( self.pinPulledFlag )

	if self.pinPulledFlag and self.tool:isInFirstPersonView() then
		local pos = self.tool:getFpBonePos( "root_cornade", false )
		local rot = self.tool:getFpBoneRot( "root_cornade", false ) * sm.vec3.getRotation( sm.vec3.new( 0, 0, 1 ), sm.vec3.new( 0, 1, 0 ) )
		self.cornadeActivationEffectFp:setPosition( pos + (rot) * sm.vec3.new( 0, 0, 0.25 ) )
		self.cornadeActivationEffectFp:setRotation( rot )
	end


	if self.pinPulledFlag and self.pendingThrowFlag == false then
		local time = 0.0
		local isFpView = self.tool:isInFirstPersonView()
		local fpIsPlaying = self.cornadeActivationEffectFp:isPlaying()

		if self.fpAnimations.currentAnimation == "use" then
			time = self.fpAnimations.animations["use"].time
			if time > 0.3 and not fpIsPlaying and isFpView then
				self.cornadeActivationEffectFp:start( 0 )
			end

			if time > self.minInstantThrowTime and primaryState ~= sm.tool.interactState.hold then
				self:onUse()
			end
		end

		if self.fpAnimations.currentAnimation == "holdActive" then
			time = self.fpAnimations.animations["holdActive"].time
			if primaryState ~= sm.tool.interactState.hold then
				self:onUse()
			end
		end
		return true, false
	elseif self.pendingThrowFlag then
		local time = 0.0
		local frameTime = 0.0

		if self.fpAnimations.currentAnimation == "throw" then
			time = self.fpAnimations.animations["throw"].time
			frameTime = 0.16
		end

		if time >= frameTime and frameTime ~= 0 then
			local params = { selectedSlot = sm.localPlayer.getSelectedHotbarSlot() }
			self.network:sendToServer( "sv_n_onUse", params )

			self.pendingThrowFlag = false
			self.pinPulledFlag = false
			if self.tool:getOwner().character then
				local vrPose, vrActive = nil, false
				if Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end
				local facingDir = vrActive and vrPose.direction:normalize() or sm.localPlayer.getDirection()
				local modifier = math.sqrt( math.max( 0, 1 - (facingDir.z * facingDir.z) ) )
				local handPosition = vrActive and vrPose.position or self.tool:getTpBonePos( "jnt_right_hand" )
				local maxVelocity = 25.0
				local minVelocity = 15.0
				local fireVelocity = minVelocity + (maxVelocity - minVelocity) * modifier
				local dir = facingDir
				local firePos = handPosition
				if not vrActive then
					dir = facingDir:rotate( math.rad( 6.943279 ) * modifier, sm.camera.getRight() )
					local centeredFirePos = GetOwnerPosition( self.tool ) + sm.vec3.new( 0, 0, 0.5 )
					local right = facingDir:cross( sm.vec3.new( 0, 0, 1 ) )
					local screenOffset = right * -0.2
					firePos = handPosition + facingDir * -0.5 + screenOffset
					local rayCastDir = (centeredFirePos - firePos):normalize()
					local firePosTest = firePos + rayCastDir * -0.3
					local obstructed = sm.physics.raycast( firePosTest, centeredFirePos, self.tool:getOwner().character )
					if obstructed or self.tool:isInFirstPersonView() then firePos = centeredFirePos end
				end
				local params = {
					fakePosition = firePos,
					position = firePos,
					fireVel = dir * fireVelocity,
					owner = self.tool:getOwner()
				}
				if self.tool:isLocal() then setFpAnimation( self.fpAnimations, "equip", 0.2 ) end
				setTpAnimation( self.tpAnimations, "equip", 5.0 )
				self.network:sendToServer( "sv_n_spawn", params )
			end
		end
		return true, true
	elseif not forceBuildActive then
		if primaryState == sm.tool.interactState.start and self.pinPulledFlag == false then
			local activeItem = sm.localPlayer.getActiveItem()
			if sm.container.canSpend( sm.localPlayer.getInventory(), activeItem, 1 ) then
				self:use()
				self.network:sendToServer( "sv_n_use" )
			end
		end


		return true, false
	end

	return false, false
end

function Cornade.sv_spawn( self, params )
	sm.projectile.projectileAttack( projectile_cornade_explosive, 0, params.position, params.fireVel, params.owner,
		params.fakePosition,
		params.fakePosition, nil, nil, self.lifetime )
end

function Cornade.use( self )
	if self.tool:isLocal() then
		setFpAnimation( self.fpAnimations, "use", 0.25 )
	end
	setTpAnimation( self.tpAnimations, "use", 10.0 )
	self.pinPulledFlag = true
end

function Cornade.onUse( self )
	if self.tool:isLocal() then
		setFpAnimation( self.fpAnimations, "throw", 0.75 )
	end
	setTpAnimation( self.tpAnimations, "throw", 5.0 )
	self.pendingThrowFlag = true
	self.cornadeActivationEffectFp:stop()
	self.cornadeActivationEffectTp:stop()
	self.cornadeActivationEffectAudio:stop()
	self.pinPulledFlag = false
	sm.effect.playHostedEffect( "Cornade - Throw", self.tool:getOwner():getCharacter() )
end

function Cornade.cl_n_onUse( self )
	if not self.tool:isLocal() and self.tool:isEquipped() then
		self:onUse()
	end
end

function Cornade.cl_n_use( self )
	if not self.tool:isLocal() and self.tool:isEquipped() then
		self:use()
	end
end

function Cornade.sv_n_use( self, params, player )
	self.network:sendToClients( "cl_n_use", params )
end

function Cornade.sv_n_spawn( self, params, player )
	sm.container.beginTransaction()
	sm.container.spend( player:getInventory(), ITEMS.obj_consumable_cornade, 1, false )
	if (sm.container.endTransaction()) then
		self:sv_spawn( params )
	end
end

function Cornade.sv_n_onUse( self, params, player )
	self.network:sendToClients( "cl_n_onUse", params )
end
