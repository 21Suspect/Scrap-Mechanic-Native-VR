dofile( "$GAME_DATA/Scripts/game/BasePlayer.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/QuestManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_camera.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_constants.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/util/Timer.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/NativeVRConfig.lua" )
dofile( "$SURVIVAL_DATA/Scripts/util.lua" )
dofile( "$SURVIVAL_DATA/scripts/game/quest_util.lua" )


SurvivalPlayer = class( BasePlayer )


local StatsTickRate = 40

local VrInteractiveSwitch = sm.uuid.new( "7cf717d7-d167-4f2d-a6e7-6b2c70aa3986" )
local VrInteractiveButton = sm.uuid.new( "1e8d93a4-506b-470d-9ada-9c0a321e2db5" )
local VrElevatorButton = sm.uuid.new( "a553bf2f-3a66-404a-b4c6-ce9e7b73f9d4" )
local VrElevatorCallButton = sm.uuid.new( "61bdf048-c09f-4cf5-8b47-35ba28c0580c" )

local VrHeldToolProxies = {
	["bb641a4f-e391-441c-bc6d-0ae21a069476"] = sm.uuid.new( "cf41bd2d-8bd3-432b-8a92-a36eda7e7740" ),
	["8c7efc37-cd7c-4262-976e-39585f8527bf"] = sm.uuid.new( "85fc5f11-722b-4723-91f2-293a81ad0800" ),
	["c60b9627-fc2b-4319-97c5-05921cb976c6"] = sm.uuid.new( "731c6a84-7ae7-439d-a620-128076f9985c" ),
	["fdb8b8be-96e7-4de0-85c7-d2f42e4f33ce"] = sm.uuid.new( "836e98a2-8c8c-4a6b-9239-54acdd4f4736" ),
	["c5ea0c2f-185b-48d6-b4df-45c386a575cc"] = sm.uuid.new( "2d7f1278-ac93-4039-9eb2-d31715ea10ff" ),
	["f6250bf4-9726-406f-a29a-945c06e460e5"] = sm.uuid.new( "3faf624b-0a95-452f-b6cd-9930ad1731c5" ),
	["9fde0601-c2ba-4c70-8d5c-2a7a9fdd122b"] = sm.uuid.new( "b633c3ee-2cda-4096-989a-60e90cd220aa" ),
	["3384010e-bc1c-42bb-83ef-dbc78a1f9636"] = sm.uuid.new( "1226cfd3-4fc8-4f9e-b21a-0c9576e2ef2f" ),
	["798c2c81-1f8e-481b-8c32-b71b5dc5511a"] = sm.uuid.new( "798c2c81-1f8e-481b-8c32-b71b5dc5511a" ),
	["103fc4e6-7e57-465e-a86d-983343415877"] = sm.uuid.new( "103fc4e6-7e57-465e-a86d-983343415877" ),
	["2e792123-4a10-4cc6-b9ef-c5a518655cb4"] = sm.uuid.new( "2e792123-4a10-4cc6-b9ef-c5a518655cb4" ),
	["3a3280e4-03b6-4a4d-9e02-e348478213c9"] = sm.uuid.new( "3a3280e4-03b6-4a4d-9e02-e348478213c9" ),
	["ac0b5b0a-14e1-4b31-8944-0a351fbfcc67"] = sm.uuid.new( "ac0b5b0a-14e1-4b31-8944-0a351fbfcc67" ),
	["9a3e478c-2224-44fa-887c-239965bd05ad"] = sm.uuid.new( "9a3e478c-2224-44fa-887c-239965bd05ad" )
}

-- Hand-local origins used by both the visible native models and gameplay rays.
-- Coordinates are right, up, and local +Z; the latter is opposite hand-forward.
local VrActionLocalOffsets = {
	["8c7efc37-cd7c-4262-976e-39585f8527bf"] = { -0.152, -0.035, -0.280 }, -- Connect laser
	["c60b9627-fc2b-4319-97c5-05921cb976c6"] = { -0.120, -0.040, -0.295 }, -- Paint laser
	["fdb8b8be-96e7-4de0-85c7-d2f42e4f33ce"] = { -0.035, -0.035, -0.225 }, -- Weld laser
	["8f190ce2-3a59-423e-8483-a7aa67bd5bc0"] = { 0.000, -0.035, -0.120 }, -- Survival lift
	["5cc12f03-275e-4c8e-b013-79fc0f913e1b"] = { 0.000, -0.035, -0.120 }  -- Creative lift
}
local VrGunMuzzleLocalOffsets = {
	-- Derived from the installed gun mesh barrel tips after the final native
	-- 0.145 scale and calibrated tool translation are applied.
	["c5ea0c2f-185b-48d6-b4df-45c386a575cc"] = { -0.198, -0.035, -0.466 }, -- Spudgun
	["f6250bf4-9726-406f-a29a-945c06e460e5"] = { -0.199, -0.035, -0.503 }, -- Shotgun
	["9fde0601-c2ba-4c70-8d5c-2a7a9fdd122b"] = { -0.198, -0.035, -0.509 }  -- Gatling
}
local VrNativeRenderedTools = {
	["bb641a4f-e391-441c-bc6d-0ae21a069476"] = true, -- Hammer
	["8c7efc37-cd7c-4262-976e-39585f8527bf"] = true, -- Connect tool
	["c60b9627-fc2b-4319-97c5-05921cb976c6"] = true, -- Paint tool
	["fdb8b8be-96e7-4de0-85c7-d2f42e4f33ce"] = true, -- Weld tool
	["c5ea0c2f-185b-48d6-b4df-45c386a575cc"] = true, -- Spudgun
	["f6250bf4-9726-406f-a29a-945c06e460e5"] = true, -- Shotgun
	["9fde0601-c2ba-4c70-8d5c-2a7a9fdd122b"] = true  -- Gatling
}
local VrToolLaserItems = {
	["8c7efc37-cd7c-4262-976e-39585f8527bf"] = true, -- Connect tool
	["c60b9627-fc2b-4319-97c5-05921cb976c6"] = true, -- Paint tool
	["fdb8b8be-96e7-4de0-85c7-d2f42e4f33ce"] = true  -- Weld tool
}
local VrToolPointerName = "vr_held_tool_pointer"
local VrToolPointerShape = sm.uuid.new( "e981c337-1c8a-449c-8602-1dd990cbba3a" )

local PerSecond = StatsTickRate / 40
local PerMinute = StatsTickRate / ( 40 * 60 )

local FoodRecoveryThreshold = 5 -- Recover hp when food is above this value
local FastFoodRecoveryThreshold = 50 -- Recover hp fast when food is above this value
local HpRecovery = 50 * PerMinute
local FastHpRecovery = 75 * PerMinute
local FoodCostPerHpRecovery = 0.2
local FastFoodCostPerHpRecovery = 0.2

local FoodCostPerStamina = 0.02
local WaterCostPerStamina = 0.1
local SprintStaminaCost = 0.7 / 40 -- Per tick while sprinting
local CarryStaminaCost = 1.4 / 40 -- Per tick while carrying

local FoodLostPerSecond = 100 / 3.5 / 24 / 60
local WaterLostPerSecond = 100 / 2.5 / 24 / 60

local BreathLostPerTick = ( 100 / 60 ) / 40

local FatigueDamageHp = 1 * PerSecond
local FatigueDamageWater = 2 * PerSecond
local DrownDamage = 5
local DrownDamageCooldown = 40

local RespawnTimeout = 60 * 40

local RespawnFadeDuration = 0.45
local RespawnEndFadeDuration = 0.45

local RespawnFadeTimeout = 5.0
local RespawnDelay = RespawnFadeDuration * 40
local RespawnEndDelay = 1.0 * 40

local BaguetteSteps = 9

function SurvivalPlayer.server_onCreate( self )
	self.sv = {}
	self.sv.saved = self.storage:load()
	self.sv.saved = self.sv.saved or {}
	self.sv.saved.stats = self.sv.saved.stats or {
		hp = 100, maxhp = 100,
		food = 100, maxfood = 100,
		water = 100, maxwater = 100,
		breath = 100, maxbreath = 100
	}
	if self.sv.saved.isConscious == nil then self.sv.saved.isConscious = true end
	if self.sv.saved.hasRevivalItem == nil then self.sv.saved.hasRevivalItem = false end
	if self.sv.saved.isNewPlayer == nil then self.sv.saved.isNewPlayer = true end
	if self.sv.saved.inChemical == nil then self.sv.saved.inChemical = false end
	if self.sv.saved.inOil == nil then self.sv.saved.inOil = false end
	if self.sv.saved.tutorialsWatched == nil then self.sv.saved.tutorialsWatched = {} end
	self.storage:save( self.sv.saved )

	self:sv_init()
	self.network:setClientData( self.sv.saved )
end

function SurvivalPlayer.server_onRefresh( self )
	self:sv_init()
	self.network:setClientData( self.sv.saved )
end

function SurvivalPlayer.sv_init( self )
	BasePlayer.sv_init( self )
	self.sv.staminaSpend = 0

	self.sv.statsTimer = Timer()
	self.sv.statsTimer:start( StatsTickRate )

	self.sv.drownTimer = Timer()
	self.sv.drownTimer:stop()

	self.sv.spawnparams = {}
	self.sv.vrHands = {
		sequence = -1,
		hands = {},
		interactionDown = {},
		interactionCooldowns = {},
		touching = {},
		lastTick = -1000
	}
end

function SurvivalPlayer.client_onCreate( self )
	BasePlayer.client_onCreate( self )
	self.cl = self.cl or {}
	if self.player == sm.localPlayer.getPlayer() then
		if g_survivalHud then
			g_survivalHud:open()
		end

		self.cl.hungryEffect = sm.effect.createEffect( "Mechanic - StatusHungry" )
		self.cl.thirstyEffect = sm.effect.createEffect( "Mechanic - StatusThirsty" )
		self.cl.underwaterEffect = sm.effect.createEffect( "Mechanic - StatusUnderwater" )
		self.cl.followCutscene = 0.0
		self.cl.tutorialsWatched = {}
	end

	self:cl_init()
end

function SurvivalPlayer.client_onRefresh( self )
	self:cl_init()

	sm.gui.hideGui( false )
	sm.camera.setCameraState( sm.camera.state.default )
	sm.localPlayer.setLockedControls( false )
end

function SurvivalPlayer.cl_init( self )
	sm.debugDraw.removeArrow( VrToolPointerName )
	if self.cl.vrHeldToolEffect then
		self.cl.vrHeldToolEffect:stop()
	end
	if self.cl.vrToolPointerEffect then
		self.cl.vrToolPointerEffect:stop()
	end
	self.useCutsceneCamera = false
	self.progress = 0
	self.nodeIndex = 1
	self.currentCutscene = {}

	self.cl.revivalChewCount = 0
	self.cl.vrHandPhysicsTimer = 0.0
	self.cl.vrHandPhysicsSequence = -1
	self.cl.vrHammerSwingSequence = 0
	self.cl.vrHammerSwingFreshTimer = 1.0
	self.cl.vrHeldToolEffect = nil
	self.cl.vrHeldToolProxy = nil
	self.cl.vrHeldToolFreshTimer = 1.0
	self.cl.vrHeldToolTargetPosition = nil
	self.cl.vrHeldToolPosition = nil
	self.cl.vrHeldToolVelocity = sm.vec3.zero()
	self.cl.vrHeldToolTargetRotation = nil
	self.cl.vrHeldToolRotation = nil
	self.cl.vrNativeToolItem = nil
	self.cl.vrSeated = nil
	self.cl.vrFirstPerson = nil
	self.cl.vrToolPointerEffect = nil
	g_vrOpticalGunTrigger = false
	g_vrPrimaryActionAvailable = false
	g_vrPrimaryActionDown = false
	g_vrHammerSwingDirection = nil
	g_vrHammerSwingFreshTimer = 1.0
	g_vrActionActive = false
	g_vrActionOrigin = nil
	g_vrActionDirection = nil
	g_vrGunAimActive = false
	g_vrGunDirection = nil
	g_vrGunMuzzlePosition = nil
	g_vrToolPointerOrigin = nil
	g_vrToolPointerDirection = nil
	g_vrToolPointerEnabled = false
end

function SurvivalPlayer.client_onClientDataUpdate( self, data )
	BasePlayer.client_onClientDataUpdate( self, data )
	if sm.localPlayer.getPlayer() == self.player then

		if self.cl.stats == nil then self.cl.stats = data.stats end -- First time copy to avoid nil errors

		if g_survivalHud then
			g_survivalHud:setSliderData( "Health", data.stats.maxhp * 10 + 1, data.stats.hp * 10 )
			g_survivalHud:setSliderData( "Food", data.stats.maxfood * 10 + 1, data.stats.food * 10 )
			g_survivalHud:setSliderData( "Water", data.stats.maxwater * 10 + 1, data.stats.water * 10 )
			g_survivalHud:setSliderData( "Breath", data.stats.maxbreath * 10 + 1, data.stats.breath * 10 )
		end

		if self.cl.hasRevivalItem ~= data.hasRevivalItem then
			self.cl.revivalChewCount = 0
		end

		if self.player.character then
			local charParam = self.player:isMale() and 1 or 2
			self.cl.underwaterEffect:setParameter( "char", charParam )
			self.cl.hungryEffect:setParameter( "char", charParam )
			self.cl.thirstyEffect:setParameter( "char", charParam )

			if data.stats.breath <= 15 and not self.cl.underwaterEffect:isPlaying() and data.isConscious then
				self.cl.underwaterEffect:start()
			elseif ( data.stats.breath > 15 or not data.isConscious ) and self.cl.underwaterEffect:isPlaying() then
				self.cl.underwaterEffect:stop()
			end
			if data.stats.food <= 5 and not self.cl.hungryEffect:isPlaying() and data.isConscious then
				self.cl.hungryEffect:start()
			elseif ( data.stats.food > 5 or not data.isConscious ) and self.cl.hungryEffect:isPlaying() then
				self.cl.hungryEffect:stop()
			end
			if data.stats.water <= 5 and not self.cl.thirstyEffect:isPlaying() and data.isConscious then
				self.cl.thirstyEffect:start()
			elseif ( data.stats.water > 5 or not data.isConscious ) and self.cl.thirstyEffect:isPlaying() then
				self.cl.thirstyEffect:stop()
			end
		end

		if data.stats.food <= 5 and self.cl.stats.food > 5 then
			sm.gui.displayAlertText( "#{ALERT_HUNGER}", 5 )
		end
		if data.stats.water <= 5 and self.cl.stats.water > 5 then
			sm.gui.displayAlertText( "#{ALERT_THIRST}", 5 )
		end

		if data.stats.hp < self.cl.stats.hp and data.stats.breath == 0 then
			sm.gui.displayAlertText( "#{DAMAGE_BREATH}", 1 )
		elseif data.stats.hp < self.cl.stats.hp and data.stats.food == 0 then
			sm.gui.displayAlertText( "#{DAMAGE_HUNGER}", 1 )
		elseif data.stats.hp < self.cl.stats.hp and data.stats.water == 0 then
			sm.gui.displayAlertText( "#{DAMAGE_THIRST}", 1 )
		end

		self.cl.stats = data.stats
		self.cl.isConscious = data.isConscious
		self.cl.hasRevivalItem = data.hasRevivalItem

		sm.localPlayer.setBlockSprinting( data.stats.food == 0 or data.stats.water == 0 )

		for tutorialKey, _ in pairs( data.tutorialsWatched ) do
			-- Merge saved tutorials and avoid resetting client tutorials
			self.cl.tutorialsWatched[tutorialKey] = true
		end
		if not g_disableTutorialHints then
			if not self.cl.tutorialsWatched["hunger"] then
				if data.stats.water < 60 or data.stats.food < 60 then
					if not self.cl.tutorialGui then
						self.cl.tutorialGui = sm.gui.createGuiFromLayout( "$GAME_DATA/Gui/Layouts/Tutorial/PopUp_Tutorial.layout", true, { isHud = true, isInteractive = false, needsCursor = false } )
						self.cl.tutorialGui:setText( "TextTitle", "#{TUTORIAL_HUNGER_AND_THIRST_TITLE}" )
						self.cl.tutorialGui:setText( "TextMessage", "#{TUTORIAL_HUNGER_AND_THIRST_MESSAGE}" )
						local dismissText = string.format( sm.gui.translateLocalizationTags( "#{TUTORIAL_DISMISS}" ), sm.gui.getKeyBinding( "Use" ) )
						self.cl.tutorialGui:setText( "TextDismiss", dismissText )
						self.cl.tutorialGui:setImage( "ImageTutorial", "gui_tutorial_image_hunger.png" )
						self.cl.tutorialGui:setOnCloseCallback( "cl_onCloseTutorialHungerGui" )
						self.cl.tutorialGui:open()
					end
				end
			end
		end
	end
end

function SurvivalPlayer.cl_e_tryPickupItemTutorial( self )
	if not g_disableTutorialHints then
		if not self.cl.tutorialsWatched["pickupitem"] then
			if not self.cl.tutorialGui then
				self.cl.tutorialGui = sm.gui.createGuiFromLayout( "$GAME_DATA/Gui/Layouts/Tutorial/PopUp_Tutorial.layout", true, { isHud = true, isInteractive = false, needsCursor = false } )
				self.cl.tutorialGui:setText( "TextTitle", "#{TUTORIAL_PICKUP_ITEM_TITLE}" )
				self.cl.tutorialGui:setText( "TextMessage", "#{TUTORIAL_PICKUP_ITEM_MESSAGE}" )
				local dismissText = string.format( sm.gui.translateLocalizationTags( "#{TUTORIAL_DISMISS}" ), sm.gui.getKeyBinding( "Use" ) )
				self.cl.tutorialGui:setText( "TextDismiss", dismissText )
				self.cl.tutorialGui:setImage( "ImageTutorial", "gui_tutorial_image_pickup_items.png" )
				self.cl.tutorialGui:setOnCloseCallback( "cl_onCloseTutorialPickupItemGui" )
				self.cl.tutorialGui:open()
			end
		end
	end
end

function SurvivalPlayer.cl_onCloseTutorialHungerGui( self )
	self.cl.tutorialsWatched["hunger"] = true
	self.network:sendToServer( "sv_e_watchedTutorial", { tutorialKey = "hunger" } )
	self.cl.tutorialGui = nil
end

function SurvivalPlayer.cl_onCloseTutorialPickupItemGui( self )
	self.cl.tutorialsWatched["pickupitem"] = true
	self.network:sendToServer( "sv_e_watchedTutorial", { tutorialKey = "pickupitem" } )
	self.cl.tutorialGui = nil
end

function SurvivalPlayer.sv_e_watchedTutorial( self, params, player )
	self.sv.saved.tutorialsWatched[params.tutorialKey] = true
	self.storage:save( self.sv.saved )
	self.network:setClientData( self.sv.saved )
end

function SurvivalPlayer.cl_localPlayerUpdate( self, dt )
	BasePlayer.cl_localPlayerUpdate( self, dt )
	self:cl_updateCamera( dt )
	self:cl_updateVrHandPhysics( dt )
	self:cl_renderVrHeldTool( dt )
	self:cl_renderVrToolPointer()

	local character = self.player:getCharacter()
	if character and not self.cl.isConscious then
		local keyBindingText =  sm.gui.getKeyBinding( "Use", true )
		if self.cl.hasRevivalItem then
			if self.cl.revivalChewCount < BaguetteSteps then
				sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_EAT} ("..self.cl.revivalChewCount.."/10)" )
			else
				sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_REVIVE}" )
			end
		else
			sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_RESPAWN}" )
		end
	end

	if character then
		self.cl.underwaterEffect:setPosition( character.worldPosition )
		self.cl.hungryEffect:setPosition( character.worldPosition )
		self.cl.thirstyEffect:setPosition( character.worldPosition )
	end
end

function SurvivalPlayer.cl_updateVrHandPhysics( self, dt )
	local settings = g_nativeVrConfig.vrHands
	if not settings or not settings.enabled or self.player ~= sm.localPlayer.getPlayer() then
		return
	end
	local character = self.player:getCharacter()
	local lockingInteractable = character and character:getLockingInteractable() or nil
	local seated = lockingInteractable ~= nil and lockingInteractable:hasSeat()
	if self.cl.vrSeated ~= seated then
		self.cl.vrSeated = seated
		sm.log.info( seated and "SCRAPVR_SEATED 1" or "SCRAPVR_SEATED 0" )
	end
	local firstPerson = sm.localPlayer.isInFirstPersonView()
	if self.cl.vrFirstPerson ~= firstPerson then
		self.cl.vrFirstPerson = firstPerson
		sm.log.info( firstPerson and "SCRAPVR_FIRST_PERSON 1" or "SCRAPVR_FIRST_PERSON 0" )
	end
	self.cl.vrHandPhysicsTimer = ( self.cl.vrHandPhysicsTimer or 0.0 ) + dt
	self.cl.vrHeldToolFreshTimer = ( self.cl.vrHeldToolFreshTimer or 1.0 ) + dt
	self.cl.vrHammerSwingFreshTimer = ( self.cl.vrHammerSwingFreshTimer or 1.0 ) + dt
	g_vrHammerSwingFreshTimer = self.cl.vrHammerSwingFreshTimer
	if self.cl.vrHammerSwingFreshTimer > 0.6 then
		g_vrHammerSwingDirection = nil
	end
	if self.cl.vrHeldToolFreshTimer > 0.8 then
		if self.cl.vrHeldToolEffect and self.cl.vrHeldToolEffect:isPlaying() then
			self.cl.vrHeldToolEffect:stop()
		end
		self.cl.vrHeldToolTargetPosition = nil
		self.cl.vrHeldToolPosition = nil
		self.cl.vrHeldToolTargetRotation = nil
		self.cl.vrHeldToolRotation = nil
		g_vrOpticalGunTrigger = false
		g_vrPrimaryActionAvailable = false
		g_vrPrimaryActionDown = false
		g_vrActionActive = false
		g_vrActionOrigin = nil
		g_vrActionDirection = nil
		g_vrGunAimActive = false
		g_vrGunDirection = nil
		g_vrGunMuzzlePosition = nil
		g_vrToolPointerEnabled = false
	end
	if self.cl.vrHandPhysicsTimer < 0.025 then
		return
	end
	self.cl.vrHandPhysicsTimer = 0.0
	local path = "$GAME_DATA/NativeVR/hand_physics.json"
	if not sm.json.fileExists( path ) then
		return
	end
	local ok, data = pcall( sm.json.open, path )
	if not ok or type( data ) ~= "table" or type( data.sequence ) ~= "number" or
		data.sequence == self.cl.vrHandPhysicsSequence then
		return
	end
	self.cl.vrHandPhysicsSequence = data.sequence
	g_vrOpticalGunTrigger = data.opticalGunTrigger == true
	g_vrPrimaryActionAvailable = type( data.right ) == "table" and data.right.active == true
	g_vrPrimaryActionDown = g_vrPrimaryActionAvailable and data.right.interact == true
	if type( data.hammerSwingSequence ) == "number" and
		data.hammerSwingSequence > ( self.cl.vrHammerSwingSequence or 0 ) then
		self.cl.vrHammerSwingSequence = data.hammerSwingSequence
		local swing = data.hammerSwingDirection
		if type( swing ) == "table" and type( swing.x ) == "number" and
			type( swing.y ) == "number" and type( swing.z ) == "number" then
			local direction = sm.vec3.new( swing.x, swing.y, swing.z )
			if direction:length() > 0.5 then
				g_vrHammerSwingDirection = direction:normalize()
				self.cl.vrHammerSwingFreshTimer = 0.0
				g_vrHammerSwingFreshTimer = 0.0
			end
		end
	end
	self:cl_updateVrHeldTool( data )
	self.network:sendToServer( "sv_n_vrHandPhysics", data )
end

function SurvivalPlayer.cl_updateVrHeldTool( self, data )
	local settings = g_nativeVrConfig.vrHands
	local hand = data.right
	local activeItem = tostring( sm.localPlayer.getActiveItem() )
	if self.cl.vrNativeToolItem ~= activeItem then
		self.cl.vrNativeToolItem = activeItem
		-- sm.log writes through the engine log immediately. Plain Lua print is
		-- buffered for long periods and cannot serve as a live native bridge.
		sm.log.info( "SCRAPVR_NATIVE_TOOL " .. activeItem )
	end
	if not settings.renderSelectedTool or type( hand ) ~= "table" or hand.active ~= true or
		type( hand.x ) ~= "number" or type( hand.y ) ~= "number" or type( hand.z ) ~= "number" or
		type( hand.fx ) ~= "number" or type( hand.fy ) ~= "number" or type( hand.fz ) ~= "number" then
		-- Keep the previous proxy through transient inactive samples. The freshness
		-- timeout above removes it only after sustained tracking loss.
		return
	end

	local forward = sm.vec3.new( hand.fx, hand.fy, hand.fz )
	if forward:length() < 0.5 then return end
	forward = forward:normalize()
	local up = type( hand.ux ) == "number" and sm.vec3.new( hand.ux, hand.uy, hand.uz ) or sm.vec3.new( 0, 0, 1 )
	up = up - forward * up:dot( forward )
	if up:length() < 0.25 then
		up = sm.vec3.new( 0, 0, 1 ) - forward * forward.z
	end
	if up:length() < 0.25 then up = sm.vec3.new( 1, 0, 0 ) end
	up = up:normalize()
	local right = forward:cross( up )
	if right:length() < 0.25 then return end
	right = right:normalize()
	local handPosition = sm.vec3.new( hand.x, hand.y, hand.z )
	local function handLocalPosition( offset )
		return handPosition + right * offset[1] + up * offset[2] - forward * offset[3]
	end

	local actionOffset = VrActionLocalOffsets[activeItem] or { 0.000, -0.035, -0.120 }
	g_vrActionActive = true
	g_vrActionOrigin = handLocalPosition( actionOffset )
	g_vrActionDirection = forward

	local muzzleOffset = VrGunMuzzleLocalOffsets[activeItem]
	g_vrGunAimActive = muzzleOffset ~= nil
	g_vrGunDirection = muzzleOffset and forward or nil
	g_vrGunMuzzlePosition = muzzleOffset and handLocalPosition( muzzleOffset ) or nil

	local laserOffset = VrToolLaserItems[activeItem] and VrActionLocalOffsets[activeItem] or nil
	g_vrToolPointerOrigin = laserOffset and handLocalPosition( laserOffset ) or nil
	g_vrToolPointerDirection = laserOffset and forward or nil
	g_vrToolPointerEnabled = laserOffset ~= nil

	local proxy = VrHeldToolProxies[activeItem]
	if not proxy then
		-- Gameplay rays remain valid for lift/block/item tools even when no
		-- custom held-model proxy exists.
		self.cl.vrHeldToolFreshTimer = 0.0
		return
	end

	local toolForward = settings.toolMirrorAim and -forward or forward
	up = up:rotate( settings.toolRollCorrection or 0.0, toolForward )

	local samplePosition = sm.vec3.new( hand.x, hand.y, hand.z ) +
		forward * settings.toolForwardOffset + up * settings.toolUpOffset
	-- These installed tools are drawn directly in the OpenXR hand pass. Retain
	-- the Lua aim vectors for authoritative gun/tool actions, but retire the
	-- delayed ShapeRenderable and debug pointer visuals.
	if VrNativeRenderedTools[activeItem] then
		if self.cl.vrHeldToolEffect and self.cl.vrHeldToolEffect:isPlaying() then
			self.cl.vrHeldToolEffect:stop()
		end
		self.cl.vrHeldToolEffect = nil
		self.cl.vrHeldToolProxy = nil
		self.cl.vrHeldToolTargetPosition = nil
		self.cl.vrHeldToolPosition = nil
		self.cl.vrHeldToolTargetRotation = nil
		self.cl.vrHeldToolRotation = nil
		g_vrToolPointerEnabled = false
		self.cl.vrHeldToolFreshTimer = 0.0
		return
	end

	if self.cl.vrHeldToolProxy ~= proxy then
		if self.cl.vrHeldToolEffect then self.cl.vrHeldToolEffect:stop() end
		self.cl.vrHeldToolEffect = sm.effect.createEffect( "ShapeRenderable" )
		self.cl.vrHeldToolEffect:setParameter( "uuid", proxy )
		self.cl.vrHeldToolEffect:setScale( sm.vec3.new( 0.25, 0.25, 0.25 ) )
		self.cl.vrHeldToolProxy = proxy
		self.cl.vrHeldToolTargetPosition = nil
		self.cl.vrHeldToolPosition = nil
		self.cl.vrHeldToolTargetRotation = nil
		self.cl.vrHeldToolRotation = nil
	end
	local sampleElapsed = math.max( 0.001, math.min( self.cl.vrHeldToolFreshTimer or 0.025, 0.1 ) )
	if self.cl.vrHeldToolTargetPosition then
		local velocity = ( samplePosition - self.cl.vrHeldToolTargetPosition ) / sampleElapsed
		local speed = velocity:length()
		self.cl.vrHeldToolVelocity = speed > 8.0 and velocity * ( 8.0 / speed ) or velocity
	else
		self.cl.vrHeldToolVelocity = sm.vec3.zero()
	end

	-- Preserve the complete tracked-hand basis. Pointing direction alone loses roll
	-- and lets the proxy flip or face sideways as the wrist rotates.
	local localForward = sm.vec3.new( 0, 1, 0 )
	local localUp = sm.vec3.new( 0, 0, 1 )
	local swing = sm.vec3.getRotation( localForward, toolForward )
	local swungUp = swing * localUp
	local rollSin = toolForward:dot( swungUp:cross( up ) )
	local rollCos = math.max( -1.0, math.min( 1.0, swungUp:dot( up ) ) )
	local handRotation = sm.quat.angleAxis( math.atan2( rollSin, rollCos ), toolForward ) * swing
	self.cl.vrHeldToolTargetPosition = samplePosition
	self.cl.vrHeldToolTargetRotation = handRotation *
		sm.quat.angleAxis( settings.toolRotationOffset, sm.vec3.new( 1, 0, 0 ) )
	if not self.cl.vrHeldToolPosition then self.cl.vrHeldToolPosition = samplePosition end
	if not self.cl.vrHeldToolRotation then self.cl.vrHeldToolRotation = self.cl.vrHeldToolTargetRotation end
	self.cl.vrHeldToolFreshTimer = 0.0
end

function SurvivalPlayer.cl_renderVrHeldTool( self, dt )
	if not self.cl.vrHeldToolEffect or not self.cl.vrHeldToolTargetPosition or
		not self.cl.vrHeldToolTargetRotation or ( self.cl.vrHeldToolFreshTimer or 1.0 ) > 0.8 then
		return
	end
	-- Do not extrapolate optical/controller samples: a single tracking correction
	-- otherwise throws the proxy past the hand. Fast interpolation removes the
	-- 42 Hz file-bridge stepping without adding visible controller lag.
	local predictedPosition = self.cl.vrHeldToolTargetPosition
	local positionBlend = math.min( 1.0, dt * 75.0 )
	local rotationBlend = math.min( 1.0, dt * 60.0 )
	self.cl.vrHeldToolPosition = self.cl.vrHeldToolPosition +
		( predictedPosition - self.cl.vrHeldToolPosition ) * positionBlend
	self.cl.vrHeldToolRotation = sm.quat.slerp(
		self.cl.vrHeldToolRotation, self.cl.vrHeldToolTargetRotation, rotationBlend )
	self.cl.vrHeldToolEffect:setPosition( self.cl.vrHeldToolPosition )
	self.cl.vrHeldToolEffect:setRotation( self.cl.vrHeldToolRotation )
	if not self.cl.vrHeldToolEffect:isPlaying() then self.cl.vrHeldToolEffect:start() end
end

function SurvivalPlayer.cl_renderVrToolPointer( self )
	local settings = g_nativeVrConfig.vrHands
	local show = settings and settings.toolPointerEnabled and g_vrToolPointerEnabled == true and
		g_vrToolPointerOrigin and g_vrToolPointerDirection and
		( self.cl.vrHeldToolFreshTimer or 1.0 ) <= 0.8
	if not show then
		if self.cl.vrToolPointerEffect and self.cl.vrToolPointerEffect:isPlaying() then
			self.cl.vrToolPointerEffect:stop()
		end
		return
	end
	local direction = g_vrToolPointerDirection:normalize()
	local startPosition = g_vrToolPointerOrigin
	local finish = startPosition + direction * settings.toolPointerRange
	local hit, result = sm.localPlayer.getRaycast(
		settings.toolPointerRange, startPosition, direction )
	if hit and result and result.pointWorld then finish = result.pointWorld end
	local length = math.max( 0.02, ( finish - startPosition ):length() )
	if not self.cl.vrToolPointerEffect then
		self.cl.vrToolPointerEffect = sm.effect.createEffect( "ShapeRenderable" )
		self.cl.vrToolPointerEffect:setParameter( "uuid", VrToolPointerShape )
	end
	self.cl.vrToolPointerEffect:setPosition( startPosition + direction * ( length * 0.5 ) )
	self.cl.vrToolPointerEffect:setRotation(
		sm.vec3.getRotation( sm.vec3.new( 0, 1, 0 ), direction ) )
	local thickness = settings.toolPointerThickness or 0.008
	self.cl.vrToolPointerEffect:setScale( sm.vec3.new( thickness, length, thickness ) )
	if not self.cl.vrToolPointerEffect:isPlaying() then
		self.cl.vrToolPointerEffect:start()
	end
end

function SurvivalPlayer.sv_n_vrHandPhysics( self, params, player )
	local settings = g_nativeVrConfig.vrHands
	local character = self.player:getCharacter()
	if not settings or not settings.enabled or player ~= self.player or not character or
		type( params ) ~= "table" or type( params.sequence ) ~= "number" or
		params.sequence <= self.sv.vrHands.sequence then
		return
	end
	local tick = sm.game.getCurrentTick()
	local accepted = {}
	for _, name in ipairs( { "left", "right" } ) do
		local hand = params[name]
		local acceptedHand = false
		if type( hand ) == "table" and hand.active == true and type( hand.x ) == "number" and
			type( hand.y ) == "number" and type( hand.z ) == "number" then
			local position = sm.vec3.new( hand.x, hand.y, hand.z )
			if ( position - character.worldPosition ):length() <= settings.maximumReach then
				local previous = self.sv.vrHands.hands[name]
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
					position = position,
					velocity = velocity,
					tick = tick,
					interaction = interaction,
					pressed = interaction and self.sv.vrHands.interactionDown[name] ~= true
				}
				self.sv.vrHands.interactionDown[name] = interaction
				acceptedHand = true
			end
		end
		if not acceptedHand then
			self.sv.vrHands.interactionDown[name] = false
		end
	end
	self.sv.vrHands.sequence = params.sequence
	self.sv.vrHands.hands = accepted
	self.sv.vrHands.lastTick = tick
end

function SurvivalPlayer.sv_updateVrHandControls( self, settings, state, tick )
	if not settings.interactionEnabled then
		return
	end

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
				local allowedDistance = previousTouch and previousTouch.interactable == interactable and
					searchRadius or settings.interactionRadius
				local distance = ( shape.worldPosition - hand.position ):length()
				if interactable and distance <= allowedDistance and distance <= nearestDistance then
					nearestTouch = { interactable = interactable, uuid = uuid, shape = shape }
					nearestDistance = distance
				end
			end
		end
		-- A creation and its tracked controller can move a few centimetres between
		-- network samples while the player is seated. Keep the old contact latched
		-- through that jitter and require a real, sustained withdrawal before the
		-- same switch can arm again.
		if not nearestTouch and previousTouch and previousTouch.shape and
			sm.exists( previousTouch.shape ) then
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
				local alreadyTouchedByOtherHand = false
				for otherName, otherTouch in pairs( currentTouches ) do
					if otherName ~= name and otherTouch.interactable == interactable then
						alreadyTouchedByOtherHand = true
						break
					end
				end
				for otherName, otherTouch in pairs( state.touching ) do
					if not alreadyTouchedByOtherHand and otherName ~= name and
						otherTouch.interactable == interactable then
						alreadyTouchedByOtherHand = true
						break
					end
				end
				local nextAllowedTick = state.interactionCooldowns[interactable] or 0
				if not alreadyTouchedByOtherHand and tick >= nextAllowedTick then
					state.interactionCooldowns[interactable] = tick + settings.interactionCooldownTicks
					if nearestTouch.uuid == VrInteractiveSwitch then
						-- Keep the stock native lever component. Replacing it with a Lua
						-- scripted interactable breaks engine-owned controller/seat behavior.
						interactable:setActive( not interactable:isActive() )
					elseif nearestTouch.uuid == VrInteractiveButton then
						-- Drive only the generic active state from VR touch; the native
						-- button remains responsible for its normal connections and bearings.
						interactable:setActive( true )
					elseif nearestTouch.uuid == VrElevatorButton or nearestTouch.uuid == VrElevatorCallButton then
						sm.event.sendToInteractable( interactable, "sv_e_vrInteract" )
					end
				end
			end
		end
	end

	for _, previousTouch in pairs( state.touching ) do
		if previousTouch.uuid == VrInteractiveButton then
			local stillTouched = false
			for _, currentTouch in pairs( currentTouches ) do
				if currentTouch.interactable == previousTouch.interactable then
					stillTouched = true
					break
				end
			end
			if not stillTouched and sm.exists( previousTouch.interactable ) then
				previousTouch.interactable:setActive( false )
			end
		end
	end
	state.touching = currentTouches
end

function SurvivalPlayer.sv_updateVrHandPhysics( self )
	local settings = g_nativeVrConfig.vrHands
	local state = self.sv.vrHands
	local tick = sm.game.getCurrentTick()
	if not settings or not settings.enabled or not state then
		return
	end
	self:sv_updateVrHandControls( settings, state, tick )
	if tick - state.lastTick > 8 then return end
	local occupiedSeatBodyId = nil
	local character = self.player:getCharacter()
	if character then
		local lockingInteractable = character:getLockingInteractable()
		if lockingInteractable and lockingInteractable:hasSeat() then
			local seatShape = lockingInteractable:getShape()
			local seatBody = seatShape and seatShape:getBody() or nil
			if seatBody then occupiedSeatBodyId = seatBody:getId() end
		end
	end
	local pushedBodies = {}
	for _, hand in pairs( state.hands ) do
		if hand.velocity:length() > 0.05 then
			local nearbyShapes = sm.shape.shapesInSphere( hand.position, settings.contactRadius )
			local checked = 0
			for _, shape in ipairs( nearbyShapes ) do
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

function SurvivalPlayer.client_onInteract( self, character, state )
	if state == true then

		--self:cl_startCutscene( { effectName = "DollyZoomCutscene", worldPosition = character.worldPosition, worldRotation = sm.quat.identity() } )
		--self:cl_startCutscene( camera_test )
		--self:cl_startCutscene( camera_test_joint )
		--self:cl_startCutscene( camera_wakeup_ground )
		--self:cl_startCutscene( camera_approach_crash )
		--self:cl_startCutscene( camera_wakeup_crash )
		--self:cl_startCutscene( camera_wakeup_bed )

		if self.cl.tutorialGui and self.cl.tutorialGui:isActive() then
			self.cl.tutorialGui:close()
		end

		if not self.cl.isConscious then
			if self.cl.hasRevivalItem then
				if self.cl.revivalChewCount >= BaguetteSteps then
					self.network:sendToServer( "sv_n_revive" )
				end
				self.cl.revivalChewCount = self.cl.revivalChewCount + 1
				self.network:sendToServer( "sv_onEvent", { type = "character", data = "chew" } )
			else
				self.network:sendToServer( "sv_n_tryRespawn" )
			end
		end
	end
end

function SurvivalPlayer.server_onFixedUpdate( self, dt )
	BasePlayer.server_onFixedUpdate( self, dt )
	self:sv_updateVrHandPhysics()

	if g_survivalDev and not self.sv.saved.isConscious and not self.sv.saved.hasRevivalItem then
		if sm.container.canSpend( self.player:getInventory(), obj_consumable_longsandwich, 1 ) then
			if sm.container.beginTransaction() then
				sm.container.spend( self.player:getInventory(), obj_consumable_longsandwich, 1, true )
				if sm.container.endTransaction() then
					self.sv.saved.hasRevivalItem = true
					self.player:sendCharacterEvent( "baguette" )
					self.network:setClientData( self.sv.saved )
				end
			end
		end
	end

	-- Delays the respawn so clients have time to fade to black
	if self.sv.respawnDelayTimer then
		self.sv.respawnDelayTimer:tick()
		if self.sv.respawnDelayTimer:done() then
			self:sv_e_respawn()
			self.sv.respawnDelayTimer = nil
		end
	end

	-- End of respawn sequence
	if self.sv.respawnEndTimer then
		self.sv.respawnEndTimer:tick()
		if self.sv.respawnEndTimer:done() then
			self.network:sendToClient( self.player, "cl_n_endFadeToBlack", { duration = RespawnEndFadeDuration } )
			self.sv.respawnEndTimer = nil;
		end
	end

	-- If respawn failed, restore the character
	if self.sv.respawnTimeoutTimer then
		self.sv.respawnTimeoutTimer:tick()
		if self.sv.respawnTimeoutTimer:done() then
			self:sv_e_onSpawnCharacter()
		end
	end

	local character = self.player:getCharacter()
	-- Update breathing
	if character then
		if character:isDiving() then
			self.sv.saved.stats.breath = math.max( self.sv.saved.stats.breath - BreathLostPerTick, 0 )
			if self.sv.saved.stats.breath == 0 then
				self.sv.drownTimer:tick()
				if self.sv.drownTimer:done() then
					if self.sv.saved.isConscious then
						print( "'SurvivalPlayer' is drowning!" )
						self:sv_takeDamage( DrownDamage, "drown" )
					end
					self.sv.drownTimer:start( DrownDamageCooldown )
				end
			end
		else
			self.sv.saved.stats.breath = self.sv.saved.stats.maxbreath
			self.sv.drownTimer:start( DrownDamageCooldown )
		end

		-- Spend stamina on sprinting
		if character:isSprinting() then
			self.sv.staminaSpend = self.sv.staminaSpend + SprintStaminaCost
		end

		-- Spend stamina on carrying
		if not self.player:getCarry():isEmpty() then
			self.sv.staminaSpend = self.sv.staminaSpend + CarryStaminaCost
		end
	end

	-- Update stamina, food and water stats
	if character and self.sv.saved.isConscious and not g_godMode then
		self.sv.statsTimer:tick()
		if self.sv.statsTimer:done() then
			self.sv.statsTimer:start( StatsTickRate )

			-- Recover health from food
			if self.sv.saved.stats.food > FoodRecoveryThreshold then
				local fastRecoveryFraction = 0

				-- Fast recovery when food is above fast threshold
				if self.sv.saved.stats.food > FastFoodRecoveryThreshold then
					local recoverableHp = math.min( self.sv.saved.stats.maxhp - self.sv.saved.stats.hp, FastHpRecovery )
					local foodSpend = math.min( recoverableHp * FastFoodCostPerHpRecovery, math.max( self.sv.saved.stats.food - FastFoodRecoveryThreshold, 0 ) )
					local recoveredHp = foodSpend / FastFoodCostPerHpRecovery

					self.sv.saved.stats.hp = math.min( self.sv.saved.stats.hp + recoveredHp, self.sv.saved.stats.maxhp )
					self.sv.saved.stats.food = self.sv.saved.stats.food - foodSpend
					fastRecoveryFraction = ( recoveredHp ) / FastHpRecovery
				end

				-- Normal recovery
				local recoverableHp = math.min( self.sv.saved.stats.maxhp - self.sv.saved.stats.hp, HpRecovery * ( 1 - fastRecoveryFraction ) )
				local foodSpend = math.min( recoverableHp * FoodCostPerHpRecovery, math.max( self.sv.saved.stats.food - FoodRecoveryThreshold, 0 ) )
				local recoveredHp = foodSpend / FoodCostPerHpRecovery

				self.sv.saved.stats.hp = math.min( self.sv.saved.stats.hp + recoveredHp, self.sv.saved.stats.maxhp )
				self.sv.saved.stats.food = self.sv.saved.stats.food - foodSpend
			end

			-- Spend water and food on stamina usage
			self.sv.saved.stats.water = math.max( self.sv.saved.stats.water - self.sv.staminaSpend * WaterCostPerStamina, 0 )
			self.sv.saved.stats.food = math.max( self.sv.saved.stats.food - self.sv.staminaSpend * FoodCostPerStamina, 0 )
			self.sv.staminaSpend = 0

			-- Decrease food and water with time
			self.sv.saved.stats.food = math.max( self.sv.saved.stats.food - FoodLostPerSecond, 0 )
			self.sv.saved.stats.water = math.max( self.sv.saved.stats.water - WaterLostPerSecond, 0 )

			local fatigueDamageFromHp = false
			if self.sv.saved.stats.food <= 0 then
				self:sv_takeDamage( FatigueDamageHp, "fatigue" )
				fatigueDamageFromHp = true
			end
			if self.sv.saved.stats.water <= 0 then
				if not fatigueDamageFromHp then
					self:sv_takeDamage( FatigueDamageWater, "fatigue" )
				end
			end

			self.storage:save( self.sv.saved )
			self.network:setClientData( self.sv.saved )
		end
	end
end

function SurvivalPlayer.server_onInventoryChanges( self, container, changes )
	QuestManager.Sv_OnEvent( QuestEvent.InventoryChanges, { container = container, changes = changes } )

	local obj_interactive_builderguide = sm.uuid.new( "e83a22c5-8783-413f-a199-46bc30ca8dac" )
	if not g_survivalDev then
		if FindInventoryChange( changes, obj_interactive_builderguide ) > 0 then
			self.network:sendToClient( self.player, "cl_n_onMessage", { message = "#{ALERT_BUILDERGUIDE_NOT_ON_LIFT}", displayTime = 3 } )
			QuestManager.Sv_TryActivateQuest( "quest_builder_guide" )
		end
		--if FindInventoryChange( changes, blk_scrapwood ) > 0 then
		--	QuestManager.Sv_TryActivateQuest( "quest_acquire_test" )
		--end
	end
	self.network:sendToClient( self.player, "cl_n_onInventoryChanges", { container = container, changes = changes } )
			
end

function SurvivalPlayer.sv_e_staminaSpend( self, stamina )
	if not g_godMode then
		if stamina > 0 then
			self.sv.staminaSpend = self.sv.staminaSpend + stamina
		end
	end
end

function SurvivalPlayer.sv_takeDamage( self, damage, source )
	if damage > 0 then
		damage = damage * GetDifficultySettings().playerTakeDamageMultiplier
		local character = self.player:getCharacter()
		local lockingInteractable = character:getLockingInteractable()
		if lockingInteractable and lockingInteractable:hasSeat() then
			lockingInteractable:setSeatCharacter( character )
		end

		if not g_godMode and self.sv.damageCooldown:done() then
			if self.sv.saved.isConscious then
				self.sv.saved.stats.hp = math.max( self.sv.saved.stats.hp - damage, 0 )

				print( "'SurvivalPlayer' took:", damage, "damage.", self.sv.saved.stats.hp, "/", self.sv.saved.stats.maxhp, "HP" )

				if source then
					self.network:sendToClients( "cl_n_onEvent", { event = source, pos = character:getWorldPosition(), damage = damage * 0.01 } )
				else
					self.player:sendCharacterEvent( "hit" )
				end

				if self.sv.saved.stats.hp <= 0 then
					print( "'SurvivalPlayer' knocked out!" )
					self.sv.respawnInteractionAttempted = false
					self.sv.saved.isConscious = false
					character:setTumbling( true )
					character:setDowned( true )
				end

				self.storage:save( self.sv.saved )
				self.network:setClientData( self.sv.saved )
			end
		else
			print( "'SurvivalPlayer' resisted", damage, "damage" )
		end
	end
end

function SurvivalPlayer.sv_n_revive( self )
	local character = self.player:getCharacter()
	if not self.sv.saved.isConscious and self.sv.saved.hasRevivalItem and not self.sv.spawnparams.respawn then
		print( "SurvivalPlayer", self.player.id, "revived" )
		self.sv.saved.stats.hp = self.sv.saved.stats.maxhp
		self.sv.saved.stats.food = self.sv.saved.stats.maxfood
		self.sv.saved.stats.water = self.sv.saved.stats.maxwater
		self.sv.saved.isConscious = true
		self.sv.saved.hasRevivalItem = false
		self.storage:save( self.sv.saved )
		self.network:setClientData( self.sv.saved )
		self.network:sendToClient( self.player, "cl_n_onEffect", { name = "Eat - EatFinish", host = self.player.character } )
		if character then
			character:setTumbling( false )
			character:setDowned( false )
		end
		self.sv.damageCooldown:start( 40 )
		self.player:sendCharacterEvent( "revive" )
	end
end

function SurvivalPlayer.sv_e_respawn( self )
	if self.sv.spawnparams.respawn then
		if not self.sv.respawnTimeoutTimer then
			self.sv.respawnTimeoutTimer = Timer()
			self.sv.respawnTimeoutTimer:start( RespawnTimeout )
		end
		return
	end
	if not self.sv.saved.isConscious then
		g_respawnManager:sv_performItemLoss( self.player )
		self.sv.spawnparams.respawn = true

		sm.event.sendToGame( "sv_e_respawn", { player = self.player } )
	else
		print( "SurvivalPlayer must be unconscious to respawn" )
	end
end

function SurvivalPlayer.sv_n_tryRespawn( self )
	if not self.sv.saved.isConscious and not self.sv.respawnDelayTimer and not self.sv.respawnInteractionAttempted then
		self.sv.respawnInteractionAttempted = true
		self.sv.respawnEndTimer = nil;
		self.network:sendToClient( self.player, "cl_n_startFadeToBlack", { duration = RespawnFadeDuration, timeout = RespawnFadeTimeout } )
		
		self.sv.respawnDelayTimer = Timer()
		self.sv.respawnDelayTimer:start( RespawnDelay )
	end
end

function SurvivalPlayer.sv_e_onSpawnCharacter( self )
	if self.sv.saved.isNewPlayer then
		-- Intro cutscene for new player
		if not g_survivalDev then
			--self:sv_e_startLocalCutscene( "camera_approach_crash" )
		end
	elseif self.sv.spawnparams.respawn then
		local playerBed = g_respawnManager:sv_getPlayerBed( self.player )
		if playerBed and playerBed.shape and sm.exists( playerBed.shape ) and playerBed.shape.body:getWorld() == self.player.character:getWorld() then
			-- Attempt to seat the respawned character in a bed
			self.network:sendToClient( self.player, "cl_seatCharacter", { shape = playerBed.shape  } )
		else
			-- Respawned without a bed
			--self:sv_e_startLocalCutscene( "camera_wakeup_ground" )
		end

		self.sv.respawnEndTimer = Timer()
		self.sv.respawnEndTimer:start( RespawnEndDelay )
	
	end

	if self.sv.saved.isNewPlayer or self.sv.spawnparams.respawn then
		print( "SurvivalPlayer", self.player.id, "spawned" )
		if self.sv.saved.isNewPlayer then
			self.sv.saved.stats.hp = self.sv.saved.stats.maxhp
			self.sv.saved.stats.food = self.sv.saved.stats.maxfood
			self.sv.saved.stats.water = self.sv.saved.stats.maxwater
		else
			self.sv.saved.stats.hp = 30
			self.sv.saved.stats.food = 30
			self.sv.saved.stats.water = 30
		end
		self.sv.saved.isConscious = true
		self.sv.saved.hasRevivalItem = false
		self.sv.saved.isNewPlayer = false
		self.storage:save( self.sv.saved )
		self.network:setClientData( self.sv.saved )

		self.player.character:setTumbling( false )
		self.player.character:setDowned( false )
		self.sv.damageCooldown:start( 40 )
	else
		-- SurvivalPlayer rejoined the game
		if self.sv.saved.stats.hp <= 0 or not self.sv.saved.isConscious then
			self.player.character:setTumbling( true )
			self.player.character:setDowned( true )
		end
	end

	self.sv.respawnInteractionAttempted = false
	self.sv.respawnDelayTimer = nil
	self.sv.respawnTimeoutTimer = nil
	self.sv.spawnparams = {}

	sm.event.sendToGame( "sv_e_onSpawnPlayerCharacter", self.player )
end

function SurvivalPlayer.cl_n_onInventoryChanges( self, params )
	if params.container == sm.localPlayer.getInventory() then
		for i, item in ipairs( params.changes ) do
			if item.difference > 0 then
				g_survivalHud:addToPickupDisplay( item.uuid, item.difference )
			end
		end
	end
end

function SurvivalPlayer.cl_seatCharacter( self, params )
	if sm.exists( params.shape ) then
		params.shape.interactable:setSeatCharacter( self.player.character )
	end
end

function SurvivalPlayer.sv_e_debug( self, params )
	if params.hp then
		self.sv.saved.stats.hp = params.hp
	end
	if params.water then
		self.sv.saved.stats.water = params.water
	end
	if params.food then
		self.sv.saved.stats.food = params.food
	end
	self.storage:save( self.sv.saved )
	self.network:setClientData( self.sv.saved )
end

function SurvivalPlayer.sv_e_eat( self, edibleParams )
	if edibleParams.hpGain then
		self:sv_restoreHealth( edibleParams.hpGain )
	end
	if edibleParams.foodGain then
		self:sv_restoreFood( edibleParams.foodGain )

		self.network:sendToClient( self.player, "cl_n_onEffect", { name = "Eat - EatFinish", host = self.player.character } )
	end
	if edibleParams.waterGain then
		self:sv_restoreWater( edibleParams.waterGain )
		-- self.network:sendToClient( self.player, "cl_n_onEffect", { name = "Eat - DrinkFinish", host = self.player.character } )
	end
	self.storage:save( self.sv.saved )
	self.network:setClientData( self.sv.saved )
end

function SurvivalPlayer.sv_e_feed( self, params )
	if not self.sv.saved.isConscious and not self.sv.saved.hasRevivalItem then
		if sm.container.beginTransaction() then
			sm.container.spend( params.playerInventory, params.foodUuid, 1, true )
			if sm.container.endTransaction() then
				self.sv.saved.hasRevivalItem = true
				self.player:sendCharacterEvent( "baguette" )
				self.network:setClientData( self.sv.saved )
			end
		end
	end
end

function SurvivalPlayer.sv_restoreHealth( self, health )
	if self.sv.saved.isConscious then
		self.sv.saved.stats.hp = self.sv.saved.stats.hp + health
		self.sv.saved.stats.hp = math.min( self.sv.saved.stats.hp, self.sv.saved.stats.maxhp )
		print( "'SurvivalPlayer' restored:", health, "health.", self.sv.saved.stats.hp, "/", self.sv.saved.stats.maxhp, "HP" )
	end
end

function SurvivalPlayer.sv_restoreFood( self, food )
	if self.sv.saved.isConscious then
		food = food * ( 0.8 + ( self.sv.saved.stats.maxfood - self.sv.saved.stats.food ) / self.sv.saved.stats.maxfood * 0.2 )
		self.sv.saved.stats.food = self.sv.saved.stats.food + food
		self.sv.saved.stats.food = math.min( self.sv.saved.stats.food, self.sv.saved.stats.maxfood )
		print( "'SurvivalPlayer' restored:", food, "food.", self.sv.saved.stats.food, "/", self.sv.saved.stats.maxfood, "FOOD" )
	end
end

function SurvivalPlayer.sv_restoreWater( self, water )
	if self.sv.saved.isConscious then
		water = water * ( 0.8 + ( self.sv.saved.stats.maxwater - self.sv.saved.stats.water ) / self.sv.saved.stats.maxwater * 0.2 )
		self.sv.saved.stats.water = self.sv.saved.stats.water + water
		self.sv.saved.stats.water = math.min( self.sv.saved.stats.water, self.sv.saved.stats.maxwater )
		print( "'SurvivalPlayer' restored:", water, "water.", self.sv.saved.stats.water, "/", self.sv.saved.stats.maxwater, "WATER" )
	end
end

function SurvivalPlayer.server_onShapeRemoved( self, removedShapes )
	local numParts = 0
	local numBlocks = 0
	local numJoints = 0



	for _, removedShapeType in ipairs( removedShapes ) do
		if removedShapeType.type == "block"  then
			numBlocks = numBlocks + removedShapeType.amount
		elseif removedShapeType.type == "part"  then
			numParts = numParts + removedShapeType.amount
		elseif removedShapeType.type == "joint"  then
			numJoints = numJoints + removedShapeType.amount




		end
	end

	local staminaSpend = numParts + numJoints + math.sqrt( numBlocks )
	--self:sv_e_staminaSpend( staminaSpend )
end


-- Camera
function SurvivalPlayer.cl_updateCamera( self, dt )
	if self.cl.cutsceneEffect then

		local cutscenePos = self.cl.cutsceneEffect:getCameraPosition()
		local cutsceneRotation = self.cl.cutsceneEffect:getCameraRotation()
		local cutsceneFOV = self.cl.cutsceneEffect:getCameraFov()
		if cutscenePos == nil then cutscenePos = sm.camera.getPosition() end
		if cutsceneRotation == nil then cutsceneRotation = sm.camera.getRotation() end
		if cutsceneFOV == nil then cutsceneFOV = sm.camera.getFov() end

		if self.cl.cutsceneEffect:isPlaying() then
			self.cl.followCutscene = math.min( self.cl.followCutscene + dt / CUTSCENE_FADE_IN_TIME, 1.0 )
		else
			self.cl.followCutscene = math.max( self.cl.followCutscene - dt / CUTSCENE_FADE_OUT_TIME, 0.0 )
		end

		local lerpedCameraPosition = sm.vec3.lerp( sm.camera.getDefaultPosition(), cutscenePos, self.cl.followCutscene )
		local lerpedCameraRotation = sm.quat.slerp( sm.camera.getDefaultRotation(), cutsceneRotation, self.cl.followCutscene )
		local lerpedCameraFOV = lerp( sm.camera.getDefaultFov(), cutsceneFOV, self.cl.followCutscene )
		print(self.cl.followCutscene)
		sm.camera.setPosition( lerpedCameraPosition )
		sm.camera.setRotation( lerpedCameraRotation )
		sm.camera.setFov( lerpedCameraFOV )

		if self.cl.followCutscene <= 0.0 and not self.cl.cutsceneEffect:isPlaying() then
			sm.gui.hideGui( false )
			sm.camera.setCameraState( sm.camera.state.default )
			--sm.localPlayer.setLockedControls( false )
			self.cl.cutsceneEffect:destroy()
			self.cl.cutsceneEffect = nil
		end
	else
		self.cl.followCutscene = 0.0
	end
end

function SurvivalPlayer.cl_startCutscene( self, params )
	self.cl.cutsceneEffect = sm.effect.createEffect( params.effectName )
	if params.worldPosition then
		self.cl.cutsceneEffect:setPosition( params.worldPosition )
	end
	if params.worldRotation then
		self.cl.cutsceneEffect:setRotation( params.worldRotation )
	end
	self.cl.cutsceneEffect:start()
	sm.gui.hideGui( true )
	sm.camera.setCameraState( sm.camera.state.cutsceneTP )
	--sm.localPlayer.setLockedControls( true )

	--local camPos = self.cl.cutsceneEffect:getCameraPosition()
	--local camDir = self.cl.cutsceneEffect:getCameraDirection()
	--if camPos and camDir then
	--	sm.camera.setPosition( camPos )
	--	if camDir:length() > FLT_EPSILON then
	--		sm.camera.setDirection( camDir )
	--	end
	--end
end

function SurvivalPlayer.sv_e_startCutscene( self, params )
	self.network:sendToClient( self.player, "cl_startCutscene", params )
end

function SurvivalPlayer.client_onCancel( self )
	BasePlayer.client_onCancel( self )
	g_effectManager:cl_cancelAllCinematics()
end

--[[
function SurvivalPlayer.cl_updateCamera( self, dt )

	if self.useCutsceneCamera then
		local cameraPath = self.currentCutscene.cameraPath
		local cameraAttached = self.currentCutscene.cameraAttached
		if #cameraPath > 1 then
			if cameraPath[self.nodeIndex+1] then
				local prevNode = cameraPath[self.nodeIndex]
				local nextNode = cameraPath[self.nodeIndex+1]

				local prevPosition = prevNode.position
				local nextPosition = nextNode.position
				local prevDirection = prevNode.direction
				local nextDirection = nextNode.direction

				if prevNode.type == "playerSpace" then
					prevPosition = sm.camera.getDefaultPosition()
				end
				if nextNode.type == "playerSpace" then
					nextPosition = nextNode.position + sm.camera.getDefaultPosition()
					-- Set player to look in the same direction as the player node
					if cameraPath[self.nodeIndex].direction then
						sm.localPlayer.setDirection( cameraPath[self.nodeIndex+1].direction )
					end
				end

				if nextNode.lerpTime > 0 then
					self.progress = self.progress + dt / nextNode.lerpTime
				else
					self.progress = 1
				end

				if self.progress >= 1 then

					-- Trigger events in the next node
					if nextNode.events then
						for _, eventParams in pairs( nextNode.events ) do
							if eventParams.type == "character" then
								eventParams.character = self.player.character
							end
							self.network:sendToServer( "sv_onEvent", eventParams )
						end
					end

					self.nodeIndex = self.nodeIndex + 1
					local upcomingNextNode = cameraPath[self.nodeIndex+1]
					if upcomingNextNode then
						self.progress = ( self.progress - 1.0 ) * nextNode.lerpTime / upcomingNextNode.lerpTime
						self.progress = math.max( math.min( self.progress, 1.0 ), 0 )
						prevPosition = nextNode.position
						nextPosition = upcomingNextNode.position
						prevDirection = nextNode.direction
						nextDirection = upcomingNextNode.direction
						if nextNode.type == "playerSpace" then
							prevPosition = sm.camera.getDefaultPosition()
						end
						if upcomingNextNode.type == "playerSpace" then
							nextPosition = nextPosition +  sm.camera.getDefaultPosition()
							-- Set player to look in the same direction as the player node
							if cameraPath[self.nodeIndex].direction then
								sm.localPlayer.setDirection( cameraPath[self.nodeIndex+1].direction )
							end
						end
					else
						--Finished the cutscene
						self.progress = 0
						self.nodeIndex = 1
						if self.currentCutscene.nextCutscene then
							self:cl_startCutscene( camera_cutscenes[self.currentCutscene.nextCutscene] )
						else
							self.useCutsceneCamera = false
							sm.gui.hideGui( false )
							sm.camera.setCameraState( sm.camera.state.default )
							sm.localPlayer.setLockedControls( false )
						end
					end
				end

				local camPos = sm.vec3.lerp( prevPosition, nextPosition, self.progress )
				local camDir = sm.vec3.lerp( prevDirection, nextDirection, self.progress )

				sm.camera.setPosition( camPos )
				sm.camera.setDirection( camDir )
			end
		elseif cameraAttached then

			if self.progress >= 1 then
				--Finished the cutscene
				self.progress = 0
				self.nodeIndex = 1
				if self.currentCutscene.nextCutscene then
					self:cl_startCutscene( camera_cutscenes[self.currentCutscene.nextCutscene] )
				else
					self.useCutsceneCamera = false
					sm.gui.hideGui( false )
					sm.camera.setCameraState( sm.camera.state.default )
					sm.localPlayer.setLockedControls( false )
				end
			else
				local character = self.player:getCharacter()
				if character then
					sm.camera.setCameraState( sm.camera.state.cutsceneFP )
					local camPos = character:getTpBonePos( cameraAttached.jointName )
					local camDir = character:getTpBoneRot( cameraAttached.jointName ) * cameraAttached.initialDirection

					sm.camera.setPosition( camPos )
					sm.camera.setDirection( camDir )
				end
			end
			self.progress = self.progress + dt / cameraAttached.attachTime

		else
			self:cl_startCutscene( nil )
		end
	end

end


function SurvivalPlayer.cl_startCutscene( self, cutsceneInfo )
	if cutsceneInfo then
		self.useCutsceneCamera = true
		sm.gui.hideGui( true )
		sm.camera.setCameraState( cutsceneInfo.cameraState )
		if cutsceneInfo.cameraPullback then
			sm.camera.setCameraPullback( cutsceneInfo.cameraPullback.standing, cutsceneInfo.cameraPullback.seated )
		end

		sm.localPlayer.setLockedControls( true )

		if self.useCutsceneCamera then
			-- Set camera nodes to follow
			self.currentCutscene = {}
			self.currentCutscene.cameraAttached = cutsceneInfo.attached
			local cameraPath = {}
			local characterPosition = sm.vec3.new( 0, 0, 0 )
			local characterDirection = sm.vec3.new( 0, 1, 0 )
			local character = self.player.character
			if character then
				characterPosition = character.worldPosition + sm.vec3.new( 0, 0, character:getHeight() * 0.5 )
				characterDirection = character:getDirection()
			else
				characterPosition = sm.localPlayer.getRaycastStart()
				characterDirection = sm.localPlayer.getDirection()
			end

			-- Get character heading
			characterDirection.z = 0
			if characterDirection:length() >= FLT_EPSILON then
				characterDirection = characterDirection:normalize()
			else
				characterDirection = sm.vec3.new( 0, 1, 0 )
			end

			-- Prepare a world direction and positon for each camera node
			if cutsceneInfo.nodes then
				for _, node in pairs( cutsceneInfo.nodes ) do
					local updatedNode = {}
					if node.type == "localSpace" then
						local right = characterDirection:cross( sm.vec3.new( 0, 0, 1 ) )
						local pitchedDirection = sm.vec3.rotate( characterDirection, math.rad( node.pitch ), right )
						updatedNode.direction = sm.vec3.rotateZ( pitchedDirection, -math.rad( node.yaw ) )
						updatedNode.position = characterPosition + sm.vec3.getRotation( sm.vec3.new( 0, 1, 0 ), characterDirection ) * node.position
					elseif node.type == "playerSpace" then
						local right = sm.localPlayer.getDirection():cross( sm.vec3.new( 0, 0, 1 ) )
						local pitchedDirection = sm.vec3.rotate( sm.localPlayer.getDirection(), math.rad( node.pitch ), right )
						updatedNode.direction = sm.vec3.rotateZ( pitchedDirection, -math.rad( node.yaw ) )

						--updatedNode.position = sm.camera.getDefaultPosition() + sm.vec3.getRotation( sm.vec3.new( 0, 1, 0 ), sm.localPlayer.getDirection() ) * node.position
						updatedNode.position = sm.vec3.getRotation( sm.vec3.new( 0, 1, 0 ), sm.localPlayer.getDirection() ) * node.position
					else
						updatedNode.position = node.position
						updatedNode.direction = node.direction
					end
					updatedNode.type = node.type
					updatedNode.lerpTime = node.lerpTime
					updatedNode.events = node.events
					cameraPath[#cameraPath+1] = updatedNode
				end
			end

			if #cameraPath > 0 then
				-- Trigger events in the first node
				if cameraPath[1] then
					if cameraPath[1].events then
						for _, eventParams in pairs( cameraPath[1].events ) do
							if eventParams.type == "character" then
								eventParams.character = self.player.character
							end
							self.network:sendToServer( "sv_onEvent", eventParams )
						end
					end
				end
			elseif self.currentCutscene.cameraAttached then
				-- Trigger events
				if self.currentCutscene.cameraAttached.events then
					for _, eventParams in pairs( self.currentCutscene.cameraAttached.events ) do
						if eventParams.type == "character" then
							eventParams.character = self.player.character
						end
						self.network:sendToServer( "sv_onEvent", eventParams )
					end
				end
			end

			self.currentCutscene.cameraPath = cameraPath
			self.currentCutscene.nextCutscene = cutsceneInfo.nextCutscene
			self.currentCutscene.canSkip = cutsceneInfo.canSkip
		end
	else
		self.useCutsceneCamera = false
		sm.gui.hideGui( false )
		sm.camera.setCameraState( sm.camera.state.default )
		sm.localPlayer.setLockedControls( false )
		self.progress = 0
		self.nodeIndex = 1
	end
end

function SurvivalPlayer.cl_startLocalCutscene( self, params )
	if params.player == sm.localPlayer.getPlayer() then
		self:cl_startCutscene( camera_cutscenes[params.cutsceneInfoName] )
	end
end

function SurvivalPlayer.sv_e_startLocalCutscene( self, cutsceneInfoName )
	local params = { player = self.player, cutsceneInfoName = cutsceneInfoName }
	self.network:sendToClients( "cl_startLocalCutscene", params )
end

function SurvivalPlayer.client_onCancel( self )

	if self.useCutsceneCamera and self.currentCutscene.canSkip then
		if self.currentCutscene.nextCutscene then
			self:cl_startCutscene( camera_cutscenes[self.currentCutscene.nextCutscene] )
		else
			self.useCutsceneCamera = false
			sm.gui.hideGui( false )
			sm.camera.setCameraState( sm.camera.state.default )
			sm.localPlayer.setLockedControls( false )
			self.progress = 0
			self.nodeIndex = 1
		end
	end
	
end
]]
