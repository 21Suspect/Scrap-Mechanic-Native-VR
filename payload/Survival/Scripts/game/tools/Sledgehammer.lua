dofile( "$GAME_DATA/Scripts/game/AnimationUtil.lua" )
dofile( "$SURVIVAL_DATA/Scripts/util.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_meleeattacks.lua" )

local Renderables = {
	"$GAME_DATA/Character/Char_Tools/Char_sledgehammer/char_sledgehammer.rend"
}

local RenderablesTp = {"$GAME_DATA/Character/Char_Male/Animations/char_male_tp_sledgehammer.rend", "$GAME_DATA/Character/Char_Tools/Char_sledgehammer/char_sledgehammer_tp_animlist.rend"}
local RenderablesFp = {"$GAME_DATA/Character/Char_Tools/Char_sledgehammer/char_sledgehammer_fp_animlist.rend"}

sm.tool.preloadRenderables( Renderables )
sm.tool.preloadRenderables( RenderablesTp )
sm.tool.preloadRenderables( RenderablesFp )

local Damage = 20
local Range = 3.0

---@class Sledgehammer : ToolClass
---@field isLocal boolean
---@field animationsLoaded boolean
---@field equipped boolean
---@field swingCooldowns table
---@field fpAnimations table
---@field tpAnimations table
Sledgehammer = class()

Sledgehammer.swingCount = 2
Sledgehammer.mayaFrameDuration = 1.0/30.0
Sledgehammer.freezeDuration = 0.075

Sledgehammer.swings = { "sledgehammer_attack1", "sledgehammer_attack2" }
Sledgehammer.swingFrames = { 4.2 * Sledgehammer.mayaFrameDuration, 4.2 * Sledgehammer.mayaFrameDuration }
Sledgehammer.swingExits = { "sledgehammer_exit1", "sledgehammer_exit2" }

function Sledgehammer.client_onCreate( self )
	self.isLocal = self.tool:isLocal()
	self:init()
end

function Sledgehammer.client_onRefresh( self )
	self:init()
	self:loadAnimations()
end

function Sledgehammer.init( self )

	self.attackCooldownTimer = 0.0
	self.spreadCooldownTimer = 0.0
	self.freezeTimer = 0.0
	self.pendingRaycastFlag = false
	self.nextAttackFlag = false
	self.currentSwing = 1

	self.swingCooldowns = {}
	for i = 1, self.swingCount do
		self.swingCooldowns[i] = 0.0
	end

	self.dispersionFraction = 0.001

	self.blendTime = 0.2
	self.blendSpeed = 10.0

	self.sharedCooldown = 0.0
	self.hitCooldown = 1.0
	self.blockCooldown = 0.5
	self.swing = false
	self.block = false

	self.wantBlockSprint = false

	if self.animationsLoaded == nil then
		self.animationsLoaded = false
	end
end

function Sledgehammer.loadAnimations( self )

	self.tpAnimations = createTpAnimations(
		self.tool,
		{
			equip = { "sledgehammer_pickup", { nextAnimation = "idle" } },
			unequip = { "sledgehammer_putdown" },
			idle = {"sledgehammer_idle", { looping = true } },
			idleRelaxed = {"sledgehammer_idle_relaxed", { looping = true } },

			sledgehammer_attack1 = { "sledgehammer_attack1", { nextAnimation = "sledgehammer_exit1" } },
			sledgehammer_attack2 = { "sledgehammer_attack2", { nextAnimation = "sledgehammer_exit2" } },
			sledgehammer_exit1 = { "sledgehammer_exit1", { nextAnimation = "idle" } },
			sledgehammer_exit2 = { "sledgehammer_exit2", { nextAnimation = "idle" } },

			guardInto = { "sledgehammer_guard_into", { nextAnimation = "guardIdle" } },
			guardIdle = { "sledgehammer_guard_idle", { looping = true } },
			guardExit = { "sledgehammer_guard_exit", { nextAnimation = "idle" } },

			guardBreak = { "sledgehammer_guard_break", { nextAnimation = "idle" } }--,
			--guardHit = { "sledgehammer_guard_hit", { nextAnimation = "guardIdle" } }
			--guardHit is missing for tp


		}
	)
	local movementAnimations = {
		idle = "sledgehammer_idle",
		idleRelaxed = "sledgehammer_idle_relaxed",

		runFwd = "sledgehammer_run_fwd",
		runBwd = "sledgehammer_run_bwd",

		sprint = "sledgehammer_sprint",
		sprintLeft = "sledgehammer_sprint_left",
		sprintRight = "sledgehammer_sprint_right",

		jump = "sledgehammer_jump",
		jumpUp = "sledgehammer_jump_up",
		jumpDown = "sledgehammer_jump_down",

		land = "sledgehammer_jump_land",
		landFwd = "sledgehammer_jump_land_fwd",
		landBwd = "sledgehammer_jump_land_bwd",
		landLeft = "sledgehammer_jump_land_left",
		landRight = "sledgehammer_jump_land_right",

		crouchIdle = "sledgehammer_crouch_idle",
		crouchFwd = "sledgehammer_crouch_fwd",
		crouchBwd = "sledgehammer_crouch_bwd"

	}

	self.dispersionData = {
		spreadCooldown = 0.2,
		spreadIncrement = 2.6,
		spreadTimerMultiplier = 0.8,
		minDispersionStanding = 0.05,
		dispersionScale = 0.3
	}

	for name, animation in pairs( movementAnimations ) do
		self.tool:setMovementAnimation( name, animation )
	end

	setTpAnimation( self.tpAnimations, "idle", 5.0 )

	if self.isLocal then
		self.fpAnimations = createFpAnimations(
			self.tool,
			{
				equip = { "sledgehammer_pickup", { nextAnimation = "idle" } },
				unequip = { "sledgehammer_putdown" },
				idle = { "sledgehammer_idle",  { looping = true } },

				sprintInto = { "sledgehammer_sprint_into", { nextAnimation = "sprintIdle" } },
				sprintIdle = { "sledgehammer_sprint_idle", { looping = true } },
				sprintExit = { "sledgehammer_sprint_exit", { nextAnimation = "idle" } },

				sledgehammer_attack1 = { "sledgehammer_attack1", { nextAnimation = "sledgehammer_exit1" } },
				sledgehammer_attack2 = { "sledgehammer_attack2", { nextAnimation = "sledgehammer_exit2" } },
				sledgehammer_exit1 = { "sledgehammer_exit1", { nextAnimation = "idle" } },
				sledgehammer_exit2 = { "sledgehammer_exit2", { nextAnimation = "idle" } },

				guardInto = { "sledgehammer_guard_into", { nextAnimation = "guardIdle" } },
				guardIdle = { "sledgehammer_guard_idle", { looping = true } },
				guardExit = { "sledgehammer_guard_exit", { nextAnimation = "idle" } },

				guardBreak = { "sledgehammer_guard_break", { nextAnimation = "idle" } },
				guardHit = { "sledgehammer_guard_hit", { nextAnimation = "guardIdle" } }

			}
		)
		setFpAnimation( self.fpAnimations, "idle", 0.0 )
	end
	--self.swingCooldowns[1] = self.fpAnimations.animations["sledgehammer_attack1"].info.duration
	self.swingCooldowns[1] = 0.6
	--self.swingCooldowns[2] = self.fpAnimations.animations["sledgehammer_attack2"].info.duration
	self.swingCooldowns[2] = 0.6

	self.animationsLoaded = true

end

function Sledgehammer.client_onUpdate( self, dt )

	if not self.animationsLoaded then
		return
	end

	local ownerPlayer = self.tool:getOwner()

	local deltaTime = dt
	if ( self.fpAnimations and self.fpAnimations.currentAnimation == self.swings[self.currentSwing] )
			or self.tpAnimations.currentAnimation == self.swings[self.currentSwing] then
		if ownerPlayer.clientPublicData.perks[SurvivalPlayer.Perks.HammerSpeed] == true then
			deltaTime = SurvivalPlayer.BuffHammerSpeedMult * dt
		end
	end

	--synchronized update
	self.attackCooldownTimer = math.max( self.attackCooldownTimer - deltaTime, 0.0 )
	self.spreadCooldownTimer = math.max( self.spreadCooldownTimer - deltaTime * self.dispersionData.spreadTimerMultiplier, 0.0 )

	--standard third person updateAnimation
	updateTpAnimations( self.tpAnimations, self.equipped, deltaTime )

	--update
	if self.isLocal then

		if self.fpAnimations.currentAnimation == self.swings[self.currentSwing] then
			self:updateFreezeFrame(self.swings[self.currentSwing], deltaTime)
		end

		local preAnimation = self.fpAnimations.currentAnimation

		updateFpAnimations( self.fpAnimations, self.equipped, deltaTime )

		if preAnimation ~= self.fpAnimations.currentAnimation then

			-- Ended animation - re-evaluate what next state is

			local keepBlockSprint = false
			local endedSwing = preAnimation == self.swings[self.currentSwing] and self.fpAnimations.currentAnimation == self.swingExits[self.currentSwing]
			if self.nextAttackFlag == true and endedSwing == true then
				-- Ended swing with next attack flag

				-- Next swing
				self.currentSwing = self.currentSwing + 1
				if self.currentSwing > self.swingCount then
					self.currentSwing = 1
				end
				local params = { name = self.swings[self.currentSwing] }
				self.network:sendToServer( "server_startEvent", params )
				sm.effect.playHostedEffect( "Mechanic - Sledgehammer Swing", ownerPlayer:getCharacter() )
				self.pendingRaycastFlag = true
				self.nextAttackFlag = false
				self.attackCooldownTimer = self.swingCooldowns[self.currentSwing]
				self.spreadCooldownTimer = math.min( self.spreadCooldownTimer + self.dispersionData.spreadIncrement, self.dispersionData.spreadCooldown )
				keepBlockSprint = true

			elseif isAnyOf( self.fpAnimations.currentAnimation, { "guardInto", "guardIdle", "guardExit", "guardBreak", "guardHit" } )  then
				keepBlockSprint = true
			end

			--Stop sprint blocking
			self.tool:setBlockSprint(keepBlockSprint)
		end

		local isSprinting =  self.tool:isSprinting()
		if isSprinting and self.fpAnimations.currentAnimation == "idle" and self.attackCooldownTimer <= 0 and not isAnyOf( self.fpAnimations.currentAnimation, { "sprintInto", "sprintIdle" } ) then
			local params = { name = "sprintInto" }
			self:client_startLocalEvent( params )
		end

		if ( not isSprinting and isAnyOf( self.fpAnimations.currentAnimation, { "sprintInto", "sprintIdle" } ) ) and self.fpAnimations.currentAnimation ~= "sprintExit" then
			local params = { name = "sprintExit" }
			self:client_startLocalEvent( params )
		end

		self.spreadCooldownTimer = clamp( self.spreadCooldownTimer, 0.0, self.swingCooldowns[self.currentSwing] )
		local max = math.min( self.spreadCooldownTimer + self.dispersionData.spreadIncrement, self.dispersionData.spreadCooldown )
		local spreadLinear = clamp( self.spreadCooldownTimer / max, 0.0, 1.0 )

		local spreadFactor = self.swingCooldowns[self.currentSwing] > 0.0 and clamp( self.spreadCooldownTimer / self.swingCooldowns[self.currentSwing], 0.0, 1.0 ) or 0.0
		local curve = spreadLinear < 0.5 and ( 2.0 * spreadLinear ) or ( 2.0 * ( 1.0 - spreadLinear ) )

		self.tool:setDispersionFraction( clamp( self.dispersionData.minDispersionStanding + spreadFactor * curve, 0.0, 1.0 ) )
		self.tool:setCrossHairType( 2 )
	end

end

function Sledgehammer.updateFreezeFrame( self, state, dt )
	local p = 1 - math.max( math.min( self.freezeTimer / self.freezeDuration, 1.0 ), 0.0 )
	local playRate = p * p * p * p
	self.fpAnimations.animations[state].playRate = playRate
	self.freezeTimer = math.max( self.freezeTimer - dt, 0.0 )
end

function Sledgehammer.server_startEvent( self, params )
	self.network:sendToClients( "client_startLocalEvent", params )
end

function Sledgehammer.client_startLocalEvent( self, params )
	self:client_handleEvent( params )
end

function Sledgehammer.client_handleEvent( self, params )

	-- Setup animation data on equip
	if params.name == "equip" then
		self.equipped = true
		--self:loadAnimations()
	elseif params.name == "unequip" then
		self.equipped = false
	end

	if not self.animationsLoaded then
		return
	end

	--Maybe not needed
-------------------------------------------------------------------

	-- Third person animations
	local tpAnimation = self.tpAnimations.animations[params.name]
	if tpAnimation then
		local isSwing = false
		for i = 1, self.swingCount do
			if self.swings[i] == params.name then
				self.tpAnimations.animations[self.swings[i]].playRate = 1
				isSwing = true
			end
		end

		local blend = not isSwing
		setTpAnimation( self.tpAnimations, params.name, blend and 0.2 or 0.0 )
	end

	-- First person animations
	if self.isLocal then
		local isSwing = false

		for i = 1, self.swingCount do
			if self.swings[i] == params.name then
				self.fpAnimations.animations[self.swings[i]].playRate = 1
				isSwing = true
			end
		end

		if isSwing or isAnyOf( params.name, { "guardInto", "guardIdle", "guardExit", "guardBreak", "guardHit" } ) then
			self.tool:setBlockSprint( true )
		else
			self.tool:setBlockSprint( false )
		end

		if params.name == "guardInto" then
			swapFpAnimation( self.fpAnimations, "guardExit", "guardInto", 0.2 )
		elseif params.name == "guardExit" then
			swapFpAnimation( self.fpAnimations, "guardInto", "guardExit", 0.2 )
		elseif params.name == "sprintInto" then
			swapFpAnimation( self.fpAnimations, "sprintExit", "sprintInto", 0.2 )
		elseif params.name == "sprintExit" then
			swapFpAnimation( self.fpAnimations, "sprintInto", "sprintExit", 0.2 )
		else
			local blend = not ( isSwing or isAnyOf( params.name, { "equip", "unequip" } ) )
			setFpAnimation( self.fpAnimations, params.name, blend and 0.2 or 0.0 )
		end
	end
end

function Sledgehammer.client_onEquippedUpdate( self, primaryState, secondaryState )
	primaryState = Chapter2VR.primaryState( self, primaryState )
	local vrPhysicalSwing = g_vrHammerSwingDirection and
		( g_vrHammerSwingFreshTimer or 1.0 ) <= 0.6
	local vrHandAttack = g_vrActionActive == true and g_vrActionOrigin and
		g_vrActionDirection and g_vrActionDirection:length() > 0.5
	local raycastStart = vrHandAttack and g_vrActionOrigin or sm.localPlayer.getRaycastStart()
	local direction = vrPhysicalSwing and g_vrHammerSwingDirection or
		( vrHandAttack and g_vrActionDirection:normalize() or sm.localPlayer.getDirection() )

	if self.pendingRaycastFlag then
		local time = 0.0
		local frameTime = 0.0
		if self.fpAnimations.currentAnimation == self.swings[self.currentSwing] then
			time = self.fpAnimations.animations[self.swings[self.currentSwing]].time
			frameTime = self.swingFrames[self.currentSwing]
		end
		if time >= frameTime and frameTime ~= 0 then
			self.pendingRaycastFlag = false
			local success, result = sm.localPlayer.getRaycast( Range, raycastStart, direction )
			sm.melee.meleeAttack( melee_sledgehammer, Damage, raycastStart, direction * Range, self.tool:getOwner() )
			if success then
				self.freezeTimer = self.freezeDuration
			end
		end
	end

	--Start attack?
	self.startedSwinging = ( self.startedSwinging or primaryState == sm.tool.interactState.start ) and primaryState ~= sm.tool.interactState.stop and primaryState ~= sm.tool.interactState.null
	if primaryState == sm.tool.interactState.start or ( primaryState == sm.tool.interactState.hold and self.startedSwinging ) then

		--Check if we are currently playing a swing
		if self.fpAnimations.currentAnimation == self.swings[self.currentSwing] then
			if self.attackCooldownTimer < 0.125 then
				self.nextAttackFlag = true
			end
		else
			--Not currently swinging
			--Is the prev attack done?
			if self.attackCooldownTimer <= 0 then
				self.currentSwing = 1
				--Not sprinting and not close to anything
				--Start swinging!
				local params = { name = self.swings[self.currentSwing] }
				self.network:sendToServer( "server_startEvent", params )
				sm.effect.playHostedEffect( "Mechanic - Sledgehammer Swing", self.tool:getOwner():getCharacter() )
				self.pendingRaycastFlag = true
				self.nextAttackFlag = false
				self.attackCooldownTimer = self.swingCooldowns[self.currentSwing]
				self.spreadCooldownTimer = math.min( self.spreadCooldownTimer + self.dispersionData.spreadIncrement, self.dispersionData.spreadCooldown )
			end
		end
	end

	--Secondary destruction
	return true, false

end

function Sledgehammer.client_onEquip( self, animate )

	if animate then
		sm.effect.playHostedEffect( "Mechanic - Sledgehammer Equip", self.tool:getOwner():getCharacter() )
	end

	self.equipped = true

	local rendsTp = shallowcopy( RenderablesTp )
	local rendsFp = shallowcopy( RenderablesFp )
	for _, v in pairs( Renderables ) do
		rendsTp[#rendsTp + 1] = v
		rendsFp[#rendsFp + 1] = v
	end

	self.tool:setTpRenderables( rendsTp )

	self:init()
	self:loadAnimations()

	setTpAnimation( self.tpAnimations, "equip", 0.0001 )

	if self.isLocal then
		self.tool:setFpRenderables( rendsFp )
		swapFpAnimation( self.fpAnimations, "unequip", "equip", 0.2 )
	end
end

function Sledgehammer.client_onUnequip( self, animate )

	self.equipped = false
	if sm.exists( self.tool ) then
		if animate then
			sm.effect.playHostedEffect( "Mechanic - Sledgehammer Unequip", self.tool:getOwner():getCharacter() )
		end
		setTpAnimation( self.tpAnimations, "unequip" )
		if self.isLocal and self.fpAnimations.currentAnimation ~= "unequip" then
			swapFpAnimation( self.fpAnimations, "equip", "unequip", 0.2 )
		end
	end
end
