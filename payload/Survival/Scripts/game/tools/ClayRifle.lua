dofile( "$GAME_DATA/Scripts/game/AnimationUtil.lua" )
dofile( "$SURVIVAL_DATA/Scripts/util.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_shapes.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_projectiles.lua" )

local Damage = 0.0
ClayRifle = class()
local renderables = {
	"$GAME_DATA/Character/Char_Tools/Char_claygun/char_claygun.rend",
}

local renderablesTp = {"$GAME_DATA/Character/Char_Male/Animations/char_male_tp_claygun.rend", "$GAME_DATA/Character/Char_Tools/Char_claygun/char_claygun_tp_animlist.rend"}
local renderablesFp = {"$GAME_DATA/Character/Char_Tools/Char_claygun/char_claygun_fp_animlist.rend"}

local AmmoConsumeInterval = 0.075

sm.tool.preloadRenderables( renderables )
sm.tool.preloadRenderables( renderablesTp )
sm.tool.preloadRenderables( renderablesFp )

function ClayRifle.client_onCreate( self )
	self.shootEffect = sm.effect.createEffect( "Claygun - BasicMuzzel" )
	self.shootEffectFP = sm.effect.createEffect( "Claygun - FPBasicMuzzel" )
	self.windupEffect = sm.effect.createEffect( "Claygun - Spin" )
end

function ClayRifle.client_onRefresh( self )
	self:loadAnimations()
end

function ClayRifle.loadAnimations( self )

	self.tpAnimations = createTpAnimations(
		self.tool,
		{
			shoot = { "claygun_shoot", { crouch = "claygun_crouch_shoot" } },
			idle = { "claygun_idle" },
			pickup = { "claygun_pickup", { nextAnimation = "idle" } },
			putdown = { "claygun_putdown" }
		}
	)
	local movementAnimations = {
		idle = "claygun_idle",
		idleRelaxed = "claygun_relax",

		sprint = "claygun_sprint",
		sprintLeft = "claygun_sprint_left",
		sprintRight = "claygun_sprint_right",

		runFwd = "claygun_run_fwd",
		runBwd = "claygun_run_bwd",

		jump = "claygun_jump",
		jumpUp = "claygun_jump_up",
		jumpDown = "claygun_jump_down",

		land = "claygun_jump_land",
		landFwd = "claygun_jump_land_fwd",
		landBwd = "claygun_jump_land_bwd",
		landLeft = "claygun_jump_land_left",
		landRight = "claygun_jump_land_right",

		crouchIdle = "claygun_crouch_idle",
		crouchFwd = "claygun_crouch_fwd",
		crouchBwd = "claygun_crouch_bwd"
	}

	for name, animation in pairs( movementAnimations ) do
		self.tool:setMovementAnimation( name, animation )
	end

	setTpAnimation( self.tpAnimations, "idle", 5.0 )
	if self.tool:isLocal() then
		self.fpAnimations = createFpAnimations(
			self.tool,
			{
				equip = { "claygun_pickup", { nextAnimation = "idle" } },
				unequip = { "claygun_putdown" },

				idle = { "claygun_idle", { looping = true } },
				shoot = { "claygun_shoot", { nextAnimation = "idle" } },

				sprintInto = { "claygun_sprint_into", { nextAnimation = "sprintIdle",  blendNext = 0.2 } },
				sprintExit = { "claygun_sprint_exit", { nextAnimation = "idle",  blendNext = 0 } },
				sprintIdle = { "claygun_sprint_idle", { looping = true } },
			}
		)
	end

	self.normalFireMode = {
		fireCooldown = 0.05,
		spreadCooldown = 0.18,
		spreadIncrement = 1.95,
		spreadMinAngle = 0.25,
		spreadMaxAngle = 24,
		fireVelocity =  30.0,

		minDispersionStanding = 0.01,
		minDispersionCrouching = 0.01,

		maxMovementDispersion = 0.4,
		jumpDispersionMultiplier = 2,
	}

	self.fireCooldownTimer = 0.0
	self.spreadCooldownTimer = 0.0

	self.movementDispersion = 0.0

	self.sprintCooldownTimer = 0.0
	self.sprintCooldown = 0.3

	self.blendTime = 0.2

	self.jointWeight = 0.0
	self.spineWeight = 0.0

	self.gatlingActive = false
	self.gatlingBlendSpeedIn = 4.0
	self.gatlingBlendSpeedOut = 0.375
	self.gatlingWeight = 0.0
	self.gatlingTurnSpeed = ( 1 / self.normalFireMode.fireCooldown ) / 3
	self.gatlingTurnFraction = 0.0
	self.ammoConsumeTimer = 0.0
end

function ClayRifle.client_onUpdate( self, dt )

	-- First person animation
	local isSprinting =  self.tool:isSprinting()
	local isCrouching =  self.tool:isCrouching()

	if self.tool:isLocal() then
		if self.equipped then
			if isSprinting and self.fpAnimations.currentAnimation ~= "sprintInto" and self.fpAnimations.currentAnimation ~= "sprintIdle" then
				swapFpAnimation( self.fpAnimations, "sprintExit", "sprintInto", 0.0 )
			elseif not self.tool:isSprinting() and ( self.fpAnimations.currentAnimation == "sprintIdle" or self.fpAnimations.currentAnimation == "sprintInto" ) then
				swapFpAnimation( self.fpAnimations, "sprintInto", "sprintExit", 0.0 )
			end
		end
		updateFpAnimations( self.fpAnimations, self.equipped, dt )
	end

	if not self.equipped then
		if self.wantEquipped then
			self.wantEquipped = false
			self.equipped = true
		end
		return
	end

	local effectPos, rot

	if self.tool:isLocal() then
		local dir = sm.localPlayer.getDirection()
		local firePos = self.tool:getFpBonePos( "pejnt_barrel" )

		effectPos = firePos + dir * 0.2

		rot = sm.vec3.getRotation( sm.vec3.new( 0, 0, 1 ), dir )

		self.shootEffectFP:setPosition( effectPos )
		self.shootEffectFP:setVelocity( self.tool:getMovementVelocity() )
		self.shootEffectFP:setRotation( rot )
	end
	local pos = self.tool:getTpBonePos( "pejnt_barrel" )
	local dir = self.tool:getTpBoneDir( "pejnt_barrel" )

	effectPos = pos + dir * 0.2

	rot = sm.vec3.getRotation( sm.vec3.new( 0, 0, 1 ), dir )

	self.shootEffect:setPosition( effectPos )
	self.shootEffect:setVelocity( self.tool:getMovementVelocity() )
	self.shootEffect:setRotation( rot )
	self.windupEffect:setPosition( effectPos )

	-- Timers
	self.fireCooldownTimer = math.max( self.fireCooldownTimer - dt, 0.0 )
	self.spreadCooldownTimer = math.max( self.spreadCooldownTimer - dt, 0.0 )
	self.sprintCooldownTimer = math.max( self.sprintCooldownTimer - dt, 0.0 )
	self.ammoConsumeTimer = math.max( self.ammoConsumeTimer - dt, 0.0 )


	if self.tool:isLocal() then
		local dispersion = 0.0
		local fireMode = self.normalFireMode

		if isCrouching then
			dispersion = fireMode.minDispersionCrouching
		else
			dispersion = fireMode.minDispersionStanding
		end

		if self.tool:getRelativeMoveDirection():length() > 0 then
			dispersion = dispersion + fireMode.maxMovementDispersion * self.tool:getMovementSpeedFraction()
		end

		local isOnGround = self.tool:isOnGround()
		if not isOnGround then
			dispersion = dispersion * fireMode.jumpDispersionMultiplier
		end

		self.movementDispersion = dispersion

		self.spreadCooldownTimer = clamp( self.spreadCooldownTimer, 0.0, fireMode.spreadCooldown )

		self.tool:setCrossHairType( 0 )

		self.tool:setCrossHairAlpha( 1.0 )
		self.tool:setInteractionTextSuppressed( false )
	end

	-- Sprint block
	local blockSprint = self.sprintCooldownTimer > 0.0
	self.tool:setBlockSprint( blockSprint )

	local playerDir = self.tool:getSmoothDirection()
	local angle = math.asin( playerDir:dot( sm.vec3.new( 0, 0, 1 ) ) ) / ( math.pi / 2 )

	local crouchWeight = self.tool:isCrouching() and 1.0 or 0.0
	local normalWeight = 1.0 - crouchWeight

	local totalWeight = 0.0
	for name, animation in pairs( self.tpAnimations.animations ) do
		animation.time = animation.time + dt

		if name == self.tpAnimations.currentAnimation then
			animation.weight = math.min( animation.weight + ( self.tpAnimations.blendSpeed * dt ), 1.0 )

			if animation.time >= animation.info.duration - self.blendTime then
				if name == "shoot" then
					setTpAnimation( self.tpAnimations, "idle", 10.0 )
				elseif name == "pickup" then
					setTpAnimation( self.tpAnimations,"idle", 0.001 )
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

	-- Third Person joint lock
	local relativeMoveDirection = self.tool:getRelativeMoveDirection()
	if ( ( ( self.tpAnimations.currentAnimation == "shoot" and ( relativeMoveDirection:length() > 0 or isCrouching) ) ) and not isSprinting ) then
		self.jointWeight = math.min( self.jointWeight + ( 10.0 * dt ), 1.0 )
	else
		self.jointWeight = math.max( self.jointWeight - ( 6.0 * dt ), 0.0 )
	end

	if ( not isSprinting ) then
		self.spineWeight = math.min( self.spineWeight + ( 10.0 * dt ), 1.0 )
	else
		self.spineWeight = math.max( self.spineWeight - ( 10.0 * dt ), 0.0 )
	end

	local finalAngle = ( 0.5 + angle * 0.5 )
	self.tool:updateAnimation( "claygun_spine_bend", finalAngle, self.spineWeight )

	local totalOffsetZ = lerp( -22.0, -26.0, crouchWeight )
	local totalOffsetY = lerp( 6.0, 12.0, crouchWeight )
	local crouchTotalOffsetX = clamp( ( angle * 60.0 ) -15.0, -60.0, 40.0 )
	local normalTotalOffsetX = clamp( ( angle * 50.0 ), -45.0, 50.0 )
	local totalOffsetX = lerp( normalTotalOffsetX, crouchTotalOffsetX , crouchWeight )

	local finalJointWeight = ( self.jointWeight )


	self.tool:updateJoint( "jnt_hips", sm.vec3.new( totalOffsetX, totalOffsetY, totalOffsetZ ), 0.35 * finalJointWeight * ( normalWeight ) )

	local crouchSpineWeight = ( 0.35 / 3 ) * crouchWeight

	self.tool:updateJoint( "jnt_spine1", sm.vec3.new( totalOffsetX, totalOffsetY, totalOffsetZ ), ( 0.10 + crouchSpineWeight )  * finalJointWeight )
	self.tool:updateJoint( "jnt_spine2", sm.vec3.new( totalOffsetX, totalOffsetY, totalOffsetZ ), ( 0.10 + crouchSpineWeight ) * finalJointWeight )
	self.tool:updateJoint( "jnt_spine3", sm.vec3.new( totalOffsetX, totalOffsetY, totalOffsetZ ), ( 0.45 + crouchSpineWeight ) * finalJointWeight )
	self.tool:updateJoint( "jnt_head", sm.vec3.new( totalOffsetX, totalOffsetY, totalOffsetZ ), 0.3 * finalJointWeight )

	-- Camera update
	self.tool:updateCamera( 2.8, 30.0, sm.vec3.new( 0.65, 0.0, 0.05 ), 0 )
	self.tool:updateFpCamera( 30.0, sm.vec3.new( 0.0, 0.0, 0.0 ), 0, 1 )

	self:cl_updateGatling( dt )
end

function ClayRifle.client_onEquip( self, animate )

	if animate then
		sm.effect.playEffect( "Claygun - Equip", self.tool:getPosition() )
	end

	self.windupEffect:start()

	self.wantEquipped = true
	self.jointWeight = 0.0

	local currentRenderablesTp = {}
	local currentRenderablesFp = {}

	for k,v in pairs( renderablesTp ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( renderablesFp ) do currentRenderablesFp[#currentRenderablesFp+1] = v end
	for k,v in pairs( renderables ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( renderables ) do currentRenderablesFp[#currentRenderablesFp+1] = v end

	self.tool:setTpRenderables( currentRenderablesTp )
	self.tool:setFpRenderables( currentRenderablesFp )

	self:loadAnimations()

	setTpAnimation( self.tpAnimations, "pickup", 0.0001 )

	if self.tool:isLocal() then
		-- Sets ClayRifle renderable, change this to change the mesh
		self.tool:setFpRenderables( currentRenderablesFp )
		swapFpAnimation( self.fpAnimations, "unequip", "equip", 0.2 )
	end
end

function ClayRifle.client_onUnequip( self, animate )

	self.windupEffect:stop()
	self.wantEquipped = false
	self.equipped = false

	if sm.exists( self.tool ) then
		setTpAnimation( self.tpAnimations, "putdown" )
		if self.tool:isLocal() then
			self.tool:setMovementSlowDown( false )
			self.tool:setBlockSprint( false )
			self.tool:setCrossHairAlpha( 1.0 )
			self.tool:setInteractionTextSuppressed( false )
			if self.fpAnimations.currentAnimation ~= "unequip" then
				swapFpAnimation( self.fpAnimations, "equip", "unequip", 0.2 )
			end
		end
		if animate then
			sm.effect.playEffect( "Claygun - Unequip", self.tool:getPosition() )
		end
	end
end

function ClayRifle.sv_n_onShoot( self, params, player )
	if params.consumeAmmo == true then
		sm.container.beginTransaction()
		sm.container.spend( player:getInventory(), ITEMS.obj_consumable_clay, 1, false )
		if not sm.container.endTransaction() then
			return
		end
	end

	self.network:sendToClients( "cl_n_onShoot", params.dir )
end

function ClayRifle.cl_n_onShoot( self, dir )
	if not self.tool:isLocal() and self.tool:isEquipped() then
		self:onShoot( dir )
	end
end

function ClayRifle.onShoot( self, dir )

	setTpAnimation( self.tpAnimations, "shoot", 10.0 )

	if self.tool:isInFirstPersonView() then
		self.shootEffectFP:start()
		setFpAnimation( self.fpAnimations, "shoot", 0.0 )
	else
		self.shootEffect:start()
	end
end

function ClayRifle.calculateFirePosition( self )
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
		fireOffset = fireOffset + right * 0.05
	else
		fireOffset = fireOffset + right * 0.25
		fireOffset = fireOffset:rotate( math.rad( pitch ), right )
	end
	local firePosition = GetOwnerPosition( self.tool ) + fireOffset
	return firePosition
end

function ClayRifle.calculateTpMuzzlePos( self )
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

function ClayRifle.calculateFpMuzzlePos( self )
	local fovScale = ( sm.camera.getFov() - 45 ) / 45

	local up = sm.localPlayer.getUp()
	local dir = sm.localPlayer.getDirection()
	local right = sm.localPlayer.getRight()

	local muzzlePos45 = sm.vec3.new( 0.0, 0.0, 0.0 )
	local muzzlePos90 = sm.vec3.new( 0.0, 0.0, 0.0 )

	muzzlePos45 = muzzlePos45 - up * 0.15
	muzzlePos45 = muzzlePos45 + right * 0.2
	muzzlePos45 = muzzlePos45 + dir * 1.25

	muzzlePos90 = muzzlePos90 - up * 0.15
	muzzlePos90 = muzzlePos90 + right * 0.2
	muzzlePos90 = muzzlePos90 + dir * 0.25

	return self.tool:getFpBonePos( "pejnt_barrel" ) + sm.vec3.lerp( muzzlePos45, muzzlePos90, fovScale )
end

function ClayRifle.cl_updateGatling( self, dt )

	self.gatlingWeight = self.gatlingActive and ( self.gatlingWeight + self.gatlingBlendSpeedIn * dt ) or ( self.gatlingWeight - self.gatlingBlendSpeedOut * dt )
	self.gatlingWeight = math.min( math.max( self.gatlingWeight, 0.0 ), 1.0 )
	local frac
	frac, self.gatlingTurnFraction = math.modf( self.gatlingTurnFraction + self.gatlingTurnSpeed * self.gatlingWeight * dt )

	self.windupEffect:setParameter( "velocity", self.gatlingWeight )
	if self.equipped and not self.windupEffect:isPlaying() then
		self.windupEffect:start()
	elseif not self.equipped and self.windupEffect:isPlaying() then
		self.windupEffect:stop()
	end

	-- Update gatling animation
	if self.tool:isLocal() then
		self.tool:updateFpAnimation( "claygun_spinner_shoot_fp", self.gatlingTurnFraction, 1.0, true )
	end
	self.tool:updateAnimation( "claygun_spinner_shoot_tp", self.gatlingTurnFraction, 1.0 )

	local weight = self.gatlingWeight
	if self.fireCooldownTimer <= 0.0 and weight >= 1.0 and self.gatlingActive then
		self:cl_fire()
	end
end

function ClayRifle.cl_fire( self )
	if self.tool:getOwner().character == nil then
		return
	end

	local shouldConsumeAmmo = sm.game.getEnableAmmoConsumption() and self.ammoConsumeTimer <= 0.0
	local hasAmmo = true
	if shouldConsumeAmmo then
		hasAmmo = sm.container.canSpend( sm.localPlayer.getInventory(), ITEMS.obj_consumable_clay, 1 )
		if hasAmmo then
			self.ammoConsumeTimer = AmmoConsumeInterval
		end
	end

	if hasAmmo then

		local firstPerson = self.tool:isInFirstPersonView()

		local dir = sm.localPlayer.getDirection()

		local firePos = self:calculateFirePosition()
		local fakePosition = self:calculateTpMuzzlePos()
		local fakePositionSelf = fakePosition
		if firstPerson then
			fakePositionSelf = self:calculateFpMuzzlePos()
		end

		-- Aim assist
		if not firstPerson then
			local raycastPos = sm.camera.getPosition() + sm.camera.getDirection() * sm.camera.getDirection():dot( GetOwnerPosition( self.tool ) - sm.camera.getPosition() )
			local hit, result = sm.localPlayer.getRaycast( 250, raycastPos, sm.camera.getDirection() )
			if hit then
				local norDir = sm.vec3.normalize( result.pointWorld - firePos )
				local dirDot = norDir:dot( dir )

				if dirDot > 0.96592583 then -- max 15 degrees off
					dir = norDir
				else
					local radsOff = math.asin( dirDot )
					dir = sm.vec3.lerp( dir, norDir, math.tan( radsOff ) / 3.7320508 ) -- if more than 15, make it 15
				end
			end
		end

		dir = dir:rotate( math.rad( 0.955 ), sm.camera.getRight() ) -- 50 m sight calibration

		-- Spread
		local fireMode = self.normalFireMode
		local recoilDispersion = 1.0 - ( math.max(fireMode.minDispersionCrouching, fireMode.minDispersionStanding ) + fireMode.maxMovementDispersion )

		local spreadFactor = fireMode.spreadCooldown > 0.0 and clamp( self.spreadCooldownTimer / fireMode.spreadCooldown, 0.0, 1.0 ) or 0.0
		spreadFactor = clamp( self.movementDispersion + spreadFactor * recoilDispersion, 0.0, 1.0 )
		local spreadDeg =  fireMode.spreadMinAngle + ( fireMode.spreadMaxAngle - fireMode.spreadMinAngle ) * spreadFactor

		dir = sm.noise.gunSpread( dir, spreadDeg )

		local owner = self.tool:getOwner()
		if owner then
			sm.projectile.projectileAttack( projectile_clay, Damage, firePos, dir * fireMode.fireVelocity, owner, fakePosition, fakePositionSelf )
		end

		-- Timers
		self.fireCooldownTimer = fireMode.fireCooldown
		self.spreadCooldownTimer = math.min( self.spreadCooldownTimer + fireMode.spreadIncrement, fireMode.spreadCooldown )
		self.sprintCooldownTimer = self.sprintCooldown

		-- Send TP shoot over network and dircly to self
		self:onShoot( dir )
		self.network:sendToServer( "sv_n_onShoot", { dir = dir, consumeAmmo = shouldConsumeAmmo } )
	else
		local fireMode = self.normalFireMode
		self.fireCooldownTimer = fireMode.fireCooldown
		sm.audio.play( "PotatoRifle - NoAmmo" )
	end
end

function ClayRifle.client_onEquippedUpdate( self, primaryState, secondaryState, forceBuildActive )
	primaryState = Chapter2VR.primaryState( self, primaryState )
	if ( primaryState == sm.tool.interactState.start or primaryState == sm.tool.interactState.hold ) and not forceBuildActive then
		self.gatlingActive = true
	else
		self.gatlingActive = false
		self.ammoConsumeTimer = 0.0
	end

	if forceBuildActive then
		return false, false
	end

	return true, true
end
