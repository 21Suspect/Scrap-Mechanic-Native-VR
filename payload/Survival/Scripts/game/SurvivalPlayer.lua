dofile( "$GAME_DATA/Scripts/game/BasePlayer.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/QuestManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_constants.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/util/Timer.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/NativeVRConfig.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/Chapter2VR.lua" )
dofile( "$SURVIVAL_DATA/Scripts/util.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/quest_util.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/TutorialManager.lua" )

---@class SurvivalPlayer : BasePlayer
---@field sv table
---@field cl table
SurvivalPlayer = class( BasePlayer )

local StatsTickRate = 40

local PerSecond = StatsTickRate / 40
local PerMinute = PerSecond / 60

local MaxHp = 100
local HpRecoveryInterruptionTicks = 40 * 5
--local HpRecoveryPerk = 10 * PerMinute

SurvivalPlayer.BuffBonusHealthIncrease = 25
SurvivalPlayer.BuffHammerSpeedMult = 1.25
SurvivalPlayer.BuffFallProtectionMult = 0.6
SurvivalPlayer.BuffHighJumpMult = 1.25

local BreathLostPerTick = ( 100 / 60 ) / 40

local DrownDamage = 5
local DrownDamageCooldown = 40

local SpawnDamageCooldown = 2 * 40
local RespawnTimeout = 60 * 40
local TrashbotBhDamageCooldownTicks = 15

local RespawnFadeDuration = 0.45
local RespawnEndFadeDuration = 0.45

local RespawnFadeTimeout = 2.5
local RespawnDelay = RespawnFadeDuration * 40
local RespawnEndDelay = 1.0 * 40

local EjectFadeDuration = 1.0
local EjectEndFadeDuration = 0.6
local EjectDelayTicks = 18
local EjectFadeTimeout = 10.0

local BaguetteSteps = 9

local HealthWarningThreshold = 25
local OxygenWarningThreshold = 25

SurvivalPlayer.Perks = {
	BonusHealth = 1,
	HammerSpeed = 2,
	FallProtection = 3,
	HighJump = 4
}

local StatusPanelGui = {}
StatusPanelGui.root = sm.json.open( "$SURVIVAL_DATA/Gui/JsonGuis/StatusPanel.gui" )
StatusPanelGui.index = IndexWidgets( StatusPanelGui.root )

StatusPanelGui.bar = {}
StatusPanelGui.bar["Health"] = { baseWidth = 122, border = 2 }
StatusPanelGui.bar["HealthLoss"] = { baseWidth = 122, border = 2 }
StatusPanelGui.bar["HealthGain"] = { baseWidth = 122, border = 2 }

StatusPanelGui.buff = DeepCopy( StatusPanelGui.index["BuffBase"] )

local OxygenPanelGui = {}
OxygenPanelGui.root = sm.json.open( "$SURVIVAL_DATA/Gui/JsonGuis/OxygenPanel.gui" )
OxygenPanelGui.index = IndexWidgets( OxygenPanelGui.root )

OxygenPanelGui.bar = {}
OxygenPanelGui.bar["Oxygen"] = { baseWidth = 122, border = 2 }

local UnstuckPopupGui = {}
UnstuckPopupGui.root = sm.json.open( "$GAME_DATA/Gui/JsonGuis/PopUp_YN.gui" )
UnstuckPopupGui.index = IndexWidgets( UnstuckPopupGui.root )
UnstuckPopupGui.index["Title"].Caption = "#{MENU_YN_TITLE_ARE_YOU_SURE}"
UnstuckPopupGui.index["Message"].Caption = "#{MENU_YN_MESSAGE_UNSTUCK}"
UnstuckPopupGui.index["Yes"].onClick = "cl_e_unstuckYes"
UnstuckPopupGui.index["No"].onClick = "cl_e_unstuckNo"


local SubmersibleSeats = {
	obj_interactive_turretseat_05_sphere
}

local harvestItems =
{
	tostring( obj_harvest_wood ),
	tostring( obj_harvest_wood2 ),
	tostring( obj_harvest_metal ),
	tostring( obj_harvest_metal2 ),
	tostring( obj_harvest_stone ),
	tostring( obj_harvest_crystal )
}

local AchievementVelocityTarget = 100
for _, achievement in ipairs( sm.json.open( "$GAME_DATA/Achievements/achievements.achievementset" ).achievementList ) do
	if achievement.steamAPIkey == "reach_velocity" then
		for _, dependency in ipairs( achievement.dependencies ) do
			if dependency.stat == "seatedVelocity" then
				AchievementVelocityTarget = dependency.target
				break
			end
		end
		break
	end
end
local function SetBarWidth( panel, name, value, max )
	local bar = panel.bar[name]
	local width = math.floor( clamp( value / max, 0.0, 1.0 ) * bar.baseWidth ) + bar.border * 2
	if panel.index[name] == nil then
		sm.log.error( "Missing widget: " .. name )
	end
	panel.index[name].width = width
	panel.index[name].Visible = value > 0
end


function SurvivalPlayer.server_onCreate( self )
	self.sv = {}
	self.sv.saved = self.storage:load()
	self.sv.saved = self.sv.saved or {}
	self.sv.saved.stats = self.sv.saved.stats or {
		hp = MaxHp, maxhp = MaxHp,
		breath = MaxHp, maxbreath = MaxHp,
		perks = {}
	}

	if self.player.publicData == nil then
		self.player.publicData = {}
	end

	if self.sv.saved.isConscious == nil then
		self.sv.saved.isConscious = true
	end
	if self.sv.saved.hasRevivalItem == nil then self.sv.saved.hasRevivalItem = false end
	if self.sv.saved.isNewPlayer == nil then self.sv.saved.isNewPlayer = true end
	if self.sv.saved.inChemical == nil then self.sv.saved.inChemical = false end
	if self.sv.saved.inOil == nil then self.sv.saved.inOil = false end
	if self.sv.saved.stats.perks == nil then self.sv.saved.stats.perks = {} end

	-- Cleanup deprecated
	self.sv.saved.tutorialsWatched = nil

	self.storage:save( self.sv.saved )

	self.player.publicData.perks = self.sv.saved.stats.perks

	self:sv_init()
	Chapter2VR.serverCreate( self )
	self.network:setClientData( self.sv.saved )
end

function SurvivalPlayer.server_onRefresh( self )
	self:sv_init()
	self.network:setClientData( self.sv.saved )
end

function SurvivalPlayer.sv_init( self )
	BasePlayer.sv_init( self )

	self.sv.statsTimer = Timer()
	self.sv.statsTimer:start( StatsTickRate )

	self.sv.recoveryInterruptionTimer = Timer()
	self.sv.recoveryInterruptionTimer:start()

	self.sv.drownTimer = Timer()
	self.sv.drownTimer:stop()

	self.sv.spawnparams = {}
end

function SurvivalPlayer.sv_n_onEvent( self, eventParams )
	if eventParams.type == "character" then
		self.player:sendCharacterEvent( eventParams.data )
	end
end

function SurvivalPlayer.sv_n_vrHandPhysics( self, params, player )
	Chapter2VR.serverReceive( self, params, player )
end

function SurvivalPlayer.client_onCreate( self )
	BasePlayer.client_onCreate( self )
	self.cl = self.cl or {}
	if self.player == sm.localPlayer.getPlayer() then
		if g_survivalHud then
			g_survivalHud:open()
		end

		self.cl.statusPanelGui = sm.jsonGui.createGui( { isHud = true, isInteractive = false, needsCursor = true, layer = "Middle" } )
		self.cl.oxygenPanelGui = sm.jsonGui.createGui( { isHud = true, isInteractive = false, needsCursor = false } )

		self.cl.underwaterEffect = sm.effect.createEffect( "Mechanic - StatusUnderwater" )
		self.cl.raidCompletedEffect = sm.effect.createEffect2D ( "audio:event:/ui/raid/end" )

	end

	self:cl_init()
	Chapter2VR.clientCreate( self )
end

function SurvivalPlayer.client_onDestroy( self )
	Chapter2VR.clientDestroy( self )
	if self.player == sm.localPlayer.getPlayer() and self.cl then
		if self.cl.raidCompletedEffect then
			self.cl.raidCompletedEffect:destroy()
			self.cl.raidCompletedEffect = nil
		end
	end
end

function SurvivalPlayer.client_onRefresh( self )
	self:cl_init()
end

function SurvivalPlayer.cl_init( self )
	self.cl.revivalChewCount = 0
end

function SurvivalPlayer.client_onClientDataUpdate( self, data )
	BasePlayer.client_onClientDataUpdate( self, data )
	self.player.clientPublicData.perks = data.stats.perks
	if sm.localPlayer.getPlayer() == self.player then

		if self.cl.stats == nil then self.cl.stats = data.stats end -- First time copy to avoid nil errors

		if self.cl.hasRevivalItem ~= data.hasRevivalItem then
			self.cl.revivalChewCount = 0
		end

		if self.player.character then
			local charParam = self.player:isMale() and 1 or 2
			self.cl.underwaterEffect:setParameter( "char", charParam )

			if data.stats.breath <= 15 and not self.cl.underwaterEffect:isPlaying() and data.isConscious then
				self.cl.underwaterEffect:start()
			elseif ( data.stats.breath > 15 or not data.isConscious ) and self.cl.underwaterEffect:isPlaying() then
				self.cl.underwaterEffect:stop()
			end
		end

		if data.stats.breath <= 0 and self.cl.stats.breath > 0 then
			NotificationManager.Cl_AddGenericNotification( "#{DAMAGE_BREATH}", 5, true )
		end

		self.cl.newPerks = self.cl.newPerks or {}
		for key, _ in pairs( data.stats.perks ) do
			if self.cl.stats.perks[key] == nil then
				self.cl.newPerks[key] = true
				TutorialManager.Cl_TutorialEvent( TutorialEvent.FoodPerks )
			end
		end

		self.cl.stats = data.stats
		self.cl.isConscious = data.isConscious
		self.cl.hasRevivalItem = data.hasRevivalItem
		self.cl.statsAge = 0
	end

	self.cl.respawnBlocked = data.respawnBlocked
end

function SurvivalPlayer.sv_n_unstuck( self )
	local character = self.player:getCharacter()
	if not character then
		return
	end
	self.sv.saved.stats.hp = 0
	self.sv.respawnInteractionAttempted = false
	self.sv.saved.isConscious = false
	g_respawnManager:sv_clearBeds( self.player )
	self:sv_clearPerks()
	character:setTumbling( true )
	character:setDowned( true )
	sm.effect.playEffect( "Mechanic - Ko", character.worldPosition )
	self:sv_dropCarryItem()
	self.storage:save( self.sv.saved )
	self.network:setClientData( self.sv.saved )
end

function SurvivalPlayer.cl_e_unstuck( self )
	self.cl.unstuckPopUp = sm.jsonGui.createGui( { isInteractive = true, needsCursor = true } )
	self.cl.unstuckPopUp:render( UnstuckPopupGui.root )
end

function SurvivalPlayer.sv_e_raidCompleted( self )
	self.network:sendToClient( self.player, "cl_n_raidCompleted" )
end

function SurvivalPlayer.cl_n_raidCompleted( self )
	if self.player ~= sm.localPlayer.getPlayer() or self.cl.raidCompletedEffect == nil then
		return
	end
	self.cl.raidCompletedEffect:start()
end

function SurvivalPlayer.cl_e_unstuckYes( self )
	self.network:sendToServer( "sv_n_unstuck" )
	if self.cl.unstuckPopUp then
		self.cl.unstuckPopUp:close()
		self.cl.unstuckPopUp = nil
	end
end

function SurvivalPlayer.cl_e_unstuckNo( self )
	if self.cl.unstuckPopUp then
		self.cl.unstuckPopUp:close()
		self.cl.unstuckPopUp = nil
	end
end

function SurvivalPlayer.sv_e_setBlockRespawn( self, blocked )
	self.sv.saved.respawnBlocked = blocked
	self.storage:save( self.sv.saved )
end

local ToolTutorialMap = {
	[tostring(ITEMS.tool_connect)] = TutorialEvent.Connection,
	[tostring(ITEMS.tool_weld)] = TutorialEvent.Weld,
}

function SurvivalPlayer.client_onFixedUpdate( self, dt )
	BasePlayer.client_onFixedUpdate( self, dt )
	if self.player == sm.localPlayer.getPlayer() then
		if self.cl.stats then
			if self.cl.hpHistory == nil then
				self.cl.hpHistory = {}
				for i = 1, 40 do
					self.cl.hpHistory[i] = 0
				end
			end
			for i = #self.cl.hpHistory, 2, -1 do
				self.cl.hpHistory[i] = self.cl.hpHistory[i - 1]
				if self.cl.hpHistory[i] < self.cl.stats.hp then
					self.cl.hpHistory[i] = self.cl.stats.hp
				end
			end
			self.cl.hpHistory[1] = self.cl.stats.hp
		end

		local activeTool = sm.localPlayer.getActiveItem()
		if activeTool ~= self.cl.lastActiveTool then
			self.cl.lastActiveTool = activeTool
			local toolTutorialEvent = ToolTutorialMap[tostring(activeTool)]
			if toolTutorialEvent then
				TutorialManager.Cl_TutorialEvent( toolTutorialEvent )
			end
		end
	end
end

-- Scrap Mechanic 1.0 registers player callbacks from the concrete class.
-- Keep this callback explicit so the VR bridge cannot be skipped by a change
-- in inherited callback dispatch.
function SurvivalPlayer.client_onUpdate( self, dt )
	if self.player == sm.localPlayer.getPlayer() then
		self:cl_localPlayerUpdate( dt )
	end
end

function SurvivalPlayer.cl_localPlayerUpdate( self, dt )


















	BasePlayer.cl_localPlayerUpdate( self, dt )
	Chapter2VR.clientUpdate( self, dt )

	local character = self.player:getCharacter()
	if character and not self.cl.isConscious then
		local keyBindingText =  sm.gui.getKeyBinding( "Use", true )
		if self.cl.hasRevivalItem then
			if self.cl.revivalChewCount < BaguetteSteps then
				sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_EAT} ("..self.cl.revivalChewCount.."/10)" )
			else
				sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_REVIVE}" )
			end
		elseif self.cl.respawnBlocked ~= true then
			sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_RESPAWN}" )
		end
	end

	if character then
		self.cl.underwaterEffect:setPosition( character.worldPosition )
	end

	if character and self.cl.stats then
		self.cl.statsAge = self.cl.statsAge + dt
		local FlashTimeInterval = 0.9
		self.cl.flashTimeFraction = self.cl.flashTimeFraction or 0
		local lowFlash = false
		local highFlash = false
		if self.cl.resetFlashTime then
			self.cl.flashTimeFraction = 0.0
			lowFlash = true
		else
			if self.cl.flashTimeFraction < 1 and self.cl.flashTimeFraction + dt / FlashTimeInterval >= 1 then
				lowFlash = true
			end
			if self.cl.flashTimeFraction < 0.5 and self.cl.flashTimeFraction + dt / FlashTimeInterval >= 0.5 then
				highFlash = true
			end
			self.cl.flashTimeFraction = ( self.cl.flashTimeFraction + dt / FlashTimeInterval ) % 1
		end
		self.cl.resetFlashTime = true

		local flash = math.cos( ( self.cl.flashTimeFraction + 0.5 ) * math.pi * 2 ) * 0.5 + 0.5

		self.cl.hpLoss = self.cl.hpLoss or self.cl.stats.hp
		if self.cl.hpHistory then
			local delayedHp = self.cl.hpHistory[40]
			if self.cl.hpLoss > delayedHp then
				self.cl.hpLoss = math.max( self.cl.hpLoss - self.cl.stats.maxhp * dt, delayedHp )
			elseif self.cl.hpLoss < delayedHp then
				self.cl.hpLoss = self.cl.stats.hp
			end
		end

		local screenWidth, screenHeight = sm.jsonGui.getViewSize()
		StatusPanelGui.root.x = math.floor( -screenWidth / 2 + StatusPanelGui.root.width * 0.5 )
		StatusPanelGui.root.y = math.floor( screenHeight / 2 - StatusPanelGui.root.height * 0.5 )

		SetBarWidth( StatusPanelGui, "Health", self.cl.stats.hp, self.cl.stats.maxhp )
		SetBarWidth( StatusPanelGui, "HealthLoss", self.cl.hpLoss, self.cl.stats.maxhp )

		local activeItem = sm.localPlayer.getActiveItem()
		local edible = sm.item.getEdible( activeItem )
		if edible and not character:isSeated() then
			local hpGain = edible.hpGain or 0
			SetBarWidth( StatusPanelGui, "HealthGain", self.cl.stats.hp + hpGain, self.cl.stats.maxhp )
		else
			SetBarWidth( StatusPanelGui, "HealthGain", self.cl.stats.hp, self.cl.stats.maxhp )
		end

		-- Health warning flash
		if self.cl.stats.hp > 0 and self.cl.stats.hp < HealthWarningThreshold then
			if not StatusPanelGui.index["HealthIconGlow"].Visible and lowFlash then
				StatusPanelGui.index["HealthIconGlow"].Visible = true
			end
			self.cl.resetFlashTime = false
		else
			if StatusPanelGui.index["HealthIconGlow"].Visible and lowFlash then
				StatusPanelGui.index["HealthIconGlow"].Visible = false
			end
		end
		if StatusPanelGui.index["HealthIconGlow"].Visible then
			StatusPanelGui.index["HealthIconGlow"].Alpha = flash
			self.cl.resetFlashTime = false
		end

		-- Oxygen
		OxygenPanelGui.root.x = 0
		OxygenPanelGui.root.y = math.floor( -screenHeight / 2 + OxygenPanelGui.root.height * 0.5 )

		OxygenPanelGui.index["OxygenPanel"].Visible = self.cl.stats.breath < self.cl.stats.maxbreath

		if OxygenPanelGui.index["OxygenPanel"].Visible then
			local beathEstimate = self.cl.stats.breath - BreathLostPerTick * 40 * self.cl.statsAge
			SetBarWidth( OxygenPanelGui, "Oxygen", beathEstimate, self.cl.stats.maxbreath )

			-- Oxygen update flash
			if self.cl.stats.breath < self.cl.stats.maxbreath then
				if not self.cl.flashBreath and highFlash then
					self.cl.flashBreath = true
				end
				self.cl.resetFlashTime = false
			else
				if self.cl.flashBreath and highFlash then
					self.cl.flashBreath = false
				end
			end
			if self.cl.flashBreath then
				OxygenPanelGui.index["OxygenIcon"].Alpha = flash * 0.5 + 0.5
				self.cl.resetFlashTime = false
			else
				OxygenPanelGui.index["OxygenIcon"].Alpha = 1.0
			end

			-- Oxygen warning flash
			if self.cl.stats.breath < OxygenWarningThreshold then
				if not OxygenPanelGui.index["OxygenIconGlow"].Visible and lowFlash then
					OxygenPanelGui.index["OxygenIconGlow"].Visible = true
				end
				self.cl.resetFlashTime = false
			else
				if OxygenPanelGui.index["OxygenIconGlow"].Visible and lowFlash then
					OxygenPanelGui.index["OxygenIconGlow"].Visible = false
				end
			end
			if OxygenPanelGui.index["OxygenIconGlow"].Visible then
				OxygenPanelGui.index["OxygenIconGlow"].Alpha = flash
				self.cl.resetFlashTime = false
			end
		else
			self.cl.flashBreath = nil
			OxygenPanelGui.index["OxygenIconGlow"].Visible = false
		end

		-- Perks
		local count = 0
		local BuffHolderChilds = StatusPanelGui.index["BuffHolder"].Childs

		for key, _ in pairs( self.cl.stats.perks ) do
			count = count + 1

			local buffBase
			if BuffHolderChilds[count] then
				buffBase = BuffHolderChilds[count]
			else
				buffBase = DeepCopy( StatusPanelGui.buff )
				BuffHolderChilds[#BuffHolderChilds+1] = buffBase
			end

			if key == SurvivalPlayer.Perks.BonusHealth then
				buffBase.Childs[1].Skin = "StatusPanelBuffBonusHealth"
				buffBase.Childs[1].ToolTip.Text = "#{STATUS_PANEL_BUFF_BONUS_HEALTH}"
			elseif key == SurvivalPlayer.Perks.HammerSpeed then
				buffBase.Childs[1].Skin = "StatusPanelBuffHammer"
				buffBase.Childs[1].ToolTip.Text = "#{STATUS_PANEL_BUFF_HAMMER_SPEED}"
			elseif key == SurvivalPlayer.Perks.FallProtection then
				buffBase.Childs[1].Skin = "StatusPanelBuffFallDamage"
				buffBase.Childs[1].ToolTip.Text = "#{STATUS_PANEL_BUFF_FALL_PROTECTION}"
			elseif key == SurvivalPlayer.Perks.HighJump then
				buffBase.Childs[1].Skin = "StatusPanelBuffJump"
				buffBase.Childs[1].ToolTip.Text = "#{STATUS_PANEL_BUFF_HIGH_JUMP}"
			else
				buffBase.Childs[1].Skin = "WhiteSkin"
			end

			if self.cl.newPerks and self.cl.newPerks[key] == true then
				buffBase.Childs[1].Childs[1].Effects[1].PlayState = "Auto play once"
				buffBase.Childs[1].Childs[1].Effects[1].ResetPlayOnce = true
				self.cl.newPerks[key] = nil
			else
				buffBase.Childs[1].Childs[1].Effects[1].PlayState = "Auto play off"
			end
		end

		while #BuffHolderChilds > count do
			table.remove( BuffHolderChilds )
		end

		if self.cl.statusPanelGui then
			self.cl.statusPanelGui:render( StatusPanelGui.root )
		end
		if self.cl.oxygenPanelGui then
			self.cl.oxygenPanelGui:render( OxygenPanelGui.root )
		end
	end





















end

function SurvivalPlayer.client_onInteract( self, character, state )
	BasePlayer.client_onInteract( self, character, state )
	if state == true then
		if not self.cl.isConscious then
			if self.cl.hasRevivalItem then
				if self.cl.revivalChewCount >= BaguetteSteps then
					self.network:sendToServer( "sv_n_revive" )
				end
				self.cl.revivalChewCount = self.cl.revivalChewCount + 1
				self.network:sendToServer( "sv_n_onEvent", { type = "character", data = "chew" } )
			elseif self.cl.respawnBlocked ~= true then
				self.network:sendToServer( "sv_n_tryRespawn" )
			end
		end
	end
end

function SurvivalPlayer.server_onFixedUpdate(self, dt)
	BasePlayer.server_onFixedUpdate(self, dt)
	Chapter2VR.serverUpdate( self )





















	-- Delays the respawn so clients have time to fade to black
	if self.sv.respawnDelayTimer then
		self.sv.respawnDelayTimer:tick()
		if self.sv.respawnDelayTimer:done() then
			self:sv_e_respawn()
			self.sv.respawnDelayTimer = nil
		end
	end

	-- End of respawn sequence
	-- if self.sv.respawnEndTimer then
	-- 	self.sv.respawnEndTimer:tick()
	-- 	if self.sv.respawnEndTimer:done() then
	-- 		self.network:sendToClient( self.player, "cl_n_endFadeToBlack", { duration = RespawnEndFadeDuration } )
	-- 		self.sv.respawnEndTimer = nil;
	-- 	end
	-- end

	-- If respawn failed, restore the character
	if self.sv.respawnTimeoutTimer then
		self.sv.respawnTimeoutTimer:tick()
		if self.sv.respawnTimeoutTimer:done() then
			self:sv_e_onSpawnCharacter()
		end
	end

	-- Eject the player from the warehouse while the fade is active
	if self.sv.warehouseEjectionFadeTick then
		local currentTick = sm.game.getCurrentTick()
		if currentTick >= self.sv.warehouseEjectionFadeTick then
			self.sv.warehouseEjectionFadeTick = nil
			self.sv.saved.stats.hp = 0
			self.sv.saved.isConscious = false
			self:sv_clearPerks()
			self.storage:save( self.sv.saved )
			self.network:setClientData( self.sv.saved )
			sm.event.sendToGame( "sv_e_warehouseEject", { player = self.player, warehouseIndex = self.sv.spawnparams.warehouseIndex } )
		end
	end

	local character = self.player:getCharacter()
	-- Update breathing
	if character then
		local isDiving = character:isDiving() and not ( character:getLockingInteractable() and isAnyOf( character:getLockingInteractable().shape.uuid, SubmersibleSeats ) )
		if isDiving then
			self.sv.saved.stats.breath = math.max( self.sv.saved.stats.breath - BreathLostPerTick, 0 )
			if self.sv.saved.stats.breath == 0 then
				self.sv.drownTimer:tick()
				if self.sv.drownTimer:done() then
					if self.sv.saved.isConscious then
						self:sv_takeDamage( DrownDamage, "drown" )
					end
					self.sv.drownTimer:start( DrownDamageCooldown )
				end
			end
		else
			if	self.sv.saved.stats.breath == 0 then
				sm.effect.playEffect( "Mechanic - Exhausted", self.player.character.worldPosition )
			end
			self.sv.saved.stats.breath = self.sv.saved.stats.maxbreath
			self.sv.drownTimer:start( DrownDamageCooldown )
		end
	end

	if character and self.sv.saved.isConscious and not g_godMode then
		self.sv.statsTimer:tick()
		self.sv.recoveryInterruptionTimer:tick()
		if self.sv.statsTimer:done() and self.sv.recoveryInterruptionTimer:done() then
			self.sv.statsTimer:start( StatsTickRate )

			-- Recover health
			--if self.sv.saved.stats.perks[SurvivalPlayer.Perks.Regeneration] then
			--	self.sv.saved.stats.hp = math.min( self.sv.saved.stats.hp + HpRecoveryPerk, self.sv.saved.stats.maxhp )
			--end

			self.storage:save( self.sv.saved )
			self.network:setClientData( self.sv.saved )
		end
	end
	--Achievement checks
	if character then
		if sm.exists(character) and character:isSeated() then
			local lockingInteractable = character:getLockingInteractable()
			if lockingInteractable and sm.exists(lockingInteractable) and lockingInteractable:hasSeat() then
				local lockingBody = lockingInteractable:getBody()

				local bodyIsNotOnLift = lockingBody and sm.exists(lockingBody) and not lockingBody:isOnLift()
				if bodyIsNotOnLift then
					local currentSeatedSpeed = math.floor(character:getVelocity():length())
					if currentSeatedSpeed >= AchievementVelocityTarget then
						sm.achievement.setf("47dbcdb0-8155-442a-953a-0f7a5baeffc3",currentSeatedSpeed,{ self.player:getId() })
					end
				end
			end
		end
	end















end

local ConnectableTutorialMap = {
	[tostring(ITEMS.obj_interactive_switch)] = true,
	[tostring(ITEMS.obj_interactive_button)] = true,
	[tostring(ITEMS.obj_interactive_logicgate)] = true,
	[tostring(ITEMS.obj_interactive_timer)] = true,
	[tostring(ITEMS.obj_interactive_alarmclock)] = true,
	[tostring(ITEMS.obj_interactive_sensor_01)] = true,
	[tostring(ITEMS.obj_interactive_sensor_02)] = true,
	[tostring(ITEMS.obj_interactive_sensor_03)] = true,
	[tostring(ITEMS.obj_interactive_sensor_04)] = true,
	[tostring(ITEMS.obj_interactive_sensor_05)] = true,

	[tostring(ITEMS.obj_interactive_electricengine_01)] = true,
	[tostring(ITEMS.obj_interactive_electricengine_02)] = true,
	[tostring(ITEMS.obj_interactive_electricengine_03)] = true,
	[tostring(ITEMS.obj_interactive_electricengine_04)] = true,
	[tostring(ITEMS.obj_interactive_electricengine_05)] = true,

	[tostring(ITEMS.obj_interactive_controller_01)] = true,
	[tostring(ITEMS.obj_interactive_controller_02)] = true,
	[tostring(ITEMS.obj_interactive_controller_03)] = true,
	[tostring(ITEMS.obj_interactive_controller_04)] = true,
	[tostring(ITEMS.obj_interactive_controller_05)] = true,

	[tostring(ITEMS.obj_scrap_seat)] = true,
	[tostring(ITEMS.obj_interactive_seat_01)] = true,
	[tostring(ITEMS.obj_interactive_seat_02)] = true,
	[tostring(ITEMS.obj_interactive_seat_03)] = true,
	[tostring(ITEMS.obj_interactive_seat_04)] = true,
	[tostring(ITEMS.obj_interactive_seat_05)] = true,
	[tostring(ITEMS.obj_interactive_saddle_01)] = true,
	[tostring(ITEMS.obj_interactive_saddle_02)] = true,
	[tostring(ITEMS.obj_interactive_saddle_03)] = true,
	[tostring(ITEMS.obj_interactive_saddle_04)] = true,
	[tostring(ITEMS.obj_interactive_saddle_05)] = true,

	[tostring(ITEMS.obj_scrap_driverseat)] = true,
	[tostring(ITEMS.obj_interactive_driverseat_01)] = true,
	[tostring(ITEMS.obj_interactive_driverseat_02)] = true,
	[tostring(ITEMS.obj_interactive_driverseat_03)] = true,
	[tostring(ITEMS.obj_interactive_driverseat_04)] = true,
	[tostring(ITEMS.obj_interactive_driverseat_05)] = true,
	[tostring(ITEMS.obj_interactive_driversaddle_01)] = true,
	[tostring(ITEMS.obj_interactive_driversaddle_02)] = true,
	[tostring(ITEMS.obj_interactive_driversaddle_03)] = true,
	[tostring(ITEMS.obj_interactive_driversaddle_04)] = true,
	[tostring(ITEMS.obj_interactive_driversaddle_05)] = true,

	[tostring(ITEMS.obj_interactive_turretseat_01_sphere)] = true,
	[tostring(ITEMS.obj_interactive_turretseat_02_sphere)] = true,
	[tostring(ITEMS.obj_interactive_turretseat_03_sphere)] = true,
	[tostring(ITEMS.obj_interactive_turretseat_04_sphere)] = true,
	[tostring(ITEMS.obj_interactive_turretseat_05_sphere)] = true,

	[tostring(ITEMS.obj_pneumatic_pump)] = true,
	[tostring(ITEMS.obj_container_chest_looting)] = true
}

local InteractableTutorialMap = {
	[tostring(ITEMS.obj_interactive_gasengine_01)] = TutorialEvent.GasEngine,
	[tostring(ITEMS.obj_interactive_gasengine_02)] = TutorialEvent.GasEngine,
	[tostring(ITEMS.obj_interactive_gasengine_03)] = TutorialEvent.GasEngine,
	[tostring(ITEMS.obj_interactive_gasengine_04)] = TutorialEvent.GasEngine,
	[tostring(ITEMS.obj_interactive_gasengine_05)] = TutorialEvent.GasEngine,

	[tostring(ITEMS.jnt_interactive_piston_01)] = TutorialEvent.Piston,
	[tostring(ITEMS.jnt_interactive_piston_02)] = TutorialEvent.Piston,
	[tostring(ITEMS.jnt_interactive_piston_03)] = TutorialEvent.Piston,
	[tostring(ITEMS.jnt_interactive_piston_04)] = TutorialEvent.Piston,
	[tostring(ITEMS.jnt_interactive_piston_05)] = TutorialEvent.Piston,

	[tostring(ITEMS.jnt_bearing)] = TutorialEvent.Bearing,
	[tostring(ITEMS.jnt_suspensionsport_bearing_01)] = TutorialEvent.Bearing,
	[tostring(ITEMS.jnt_suspensionsport_bearing_02)] = TutorialEvent.Bearing,
	[tostring(ITEMS.jnt_suspensionsport_bearing_03)] = TutorialEvent.Bearing,
	[tostring(ITEMS.jnt_suspensionsport_bearing_04)] = TutorialEvent.Bearing,
	[tostring(ITEMS.jnt_suspensionsport_bearing_05)] = TutorialEvent.Bearing,
	[tostring(ITEMS.jnt_suspensionoffroad_bearing_01)] = TutorialEvent.Bearing,
	[tostring(ITEMS.jnt_suspensionoffroad_bearing_02)] = TutorialEvent.Bearing,
	[tostring(ITEMS.jnt_suspensionoffroad_bearing_03)] = TutorialEvent.Bearing,
	[tostring(ITEMS.jnt_suspensionoffroad_bearing_04)] = TutorialEvent.Bearing,
	[tostring(ITEMS.jnt_suspensionoffroad_bearing_05)] = TutorialEvent.Bearing,

	[tostring(ITEMS.obj_interactive_prospector)] = TutorialEvent.Prospector
}

function SurvivalPlayer.cl_e_onPlacedInteractable( self, uuid, parentType )
	if sm.physics.types[parentType] == "terrainSurface" then
		if ConnectableTutorialMap[tostring( uuid )] or InteractableTutorialMap[tostring( uuid )] then
			TutorialManager.Cl_TutorialEvent( TutorialEvent.Interactive )
		end
	end

	local tutorialEvent = InteractableTutorialMap[tostring( uuid )]
	if tutorialEvent then
		TutorialManager.Cl_TutorialEvent( tutorialEvent )
	end
end

function SurvivalPlayer.server_onInventoryChanges( self, container, changes )
	QuestManager.Sv_SendEvent( QuestEvent.InventoryChanges, { container = container, changes = changes } )

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

	if FindInventoryChange( changes, ITEMS.obj_resource_coralium ) > 0 then
		RecipeManager.Sv_UnlockRecipe( tostring( ITEMS.obj_resource_refinedcoralium ), g_survivalDev == true )
	end
	if FindInventoryChange( changes, ITEMS.obj_resource_nimbolium ) > 0 then
		RecipeManager.Sv_UnlockRecipe( tostring( ITEMS.obj_resource_refinednimbolium ), g_survivalDev == true )
	end
	if FindInventoryChange( changes, ITEMS.obj_resource_lemonium ) > 0 then
		RecipeManager.Sv_UnlockRecipe( tostring( ITEMS.obj_resource_refinedlemonium ), g_survivalDev == true )
	end
	if FindInventoryChange( changes, ITEMS.obj_resource_sapphire ) > 0 then
		RecipeManager.Sv_UnlockRecipe( tostring( ITEMS.obj_resource_refinedsapphire ), g_survivalDev == true )
	end
	if FindInventoryChange( changes, ITEMS.obj_resource_crystal ) > 0 then
		RecipeManager.Sv_UnlockRecipe( tostring( ITEMS.obj_resource_refinedcrystal ), g_survivalDev == true )
	end

	for _, item in ipairs(changes) do
		if item.difference > 0 then
			sm.achievement.addi( tostring( item.uuid ), item.difference, { self.player.id } )
		end
	end
	self.network:sendToClient(self.player, "cl_n_onInventoryChanges", { container = container, changes = changes })
end

function SurvivalPlayer.sv_dropCarryItem( self )
	local carryContainer = self.player:getCarry()
	if carryContainer then
		if not carryContainer:isEmpty() then
			local spawnPos = self.player.character.worldPosition
			local itemUid = carryContainer:getItem( 0 ).uuid
			if itemUid then
				if sm.container.beginTransaction() then
					if isAnyOf( tostring( itemUid ), harvestItems ) then
						sm.container.spend( carryContainer, itemUid, 1, true )
						if sm.container.endTransaction() then
							sm.shape.createPart( itemUid, spawnPos, sm.quat.identity(), true, true )
						end
					elseif tostring( itemUid ) == tostring( obj_character_worm ) then
						sm.container.spend( carryContainer, itemUid, 1, true )
						if sm.container.endTransaction() then
							sm.unit.createUnit( unit_worm, spawnPos )
						end
					else
						local shape = sm.shape.createPart( obj_heavy_carry, spawnPos, sm.quat.identity(), true, true )
						local lostContainer = shape.interactable:addContainer( 0, carryContainer:getSize() )
						for i = 0, carryContainer:getSize() do
							local item = carryContainer:getItem( i )
							if item.uuid ~= sm.uuid.getNil() then
								sm.container.setItem( lostContainer, i, item.uuid, item.quantity, item.instance )
								sm.container.setItem( carryContainer, i, sm.uuid.getNil(), 0 )
							end
						end
						if not sm.container.endTransaction() then
							sm.shape.destroyShape( shape )
						end
					end
				end
			end
		end
	end
end

local DamageSourceToEvent = {
	-- standard
	["drown"] = "drown",
	["fatigue"] = "fatigue",
	["shock"] = "shock",
	["impact"] = "impact",
	["fire"] = "fire",
	["poison"] = "poison",
	-- custom
	["scannerbot"] = "impact",
	["minerbotprojectile"] = "shock",
	["minerbotexplosion"] = "impact",
	["tapebotprojectile"] = "shock"
}

local SeatSafeDamageSources = {
	["fatigue"] = true,
	["scannerbot"] = true,
	["minerbotprojectile"] = true,
	["minerbotexplosion"] = true,
	["tapebotprojectile"] = true
}

local function GetDamageEvent( source )
	if DamageSourceToEvent[source] then
		return DamageSourceToEvent[source]
	end
	return "impact"
end

function SurvivalPlayer.sv_takeDamage( self, damage, source, typeUuid )
	if self.cl.cameraState == CameraState.CUTSCENE then
		return
	end
	if typeUuid and g_trashbotBulletHellProjectiles[tostring( typeUuid )] then
		if not self.sv.trashbotBhDamageCooldownEndTick or sm.game.getCurrentTick() > self.sv.trashbotBhDamageCooldownEndTick then
			self.sv.trashbotBhDamageCooldownEndTick = sm.game.getCurrentTick() + TrashbotBhDamageCooldownTicks
		else
			return
		end
	end

	if damage > 0 then
		damage = damage * GetDifficultySettings().playerTakeDamageMultiplier
		local character = self.player:getCharacter()
		if not character then
			return
		end

		if not g_godMode and self.sv.damageCooldown:done() then
			if self.sv.saved.isConscious then
				self.sv.saved.stats.hp = math.max( self.sv.saved.stats.hp - damage, 0 )
				self.sv.recoveryInterruptionTimer:start( HpRecoveryInterruptionTicks )

				if source then
					local event = GetDamageEvent( source )
					self.network:sendToClients( "cl_n_onEvent", { event = event, pos = character:getWorldPosition(), damage = damage * 0.01 } )
				else
					self.player:sendCharacterEvent( "hit" )
				end




					if self.sv.saved.stats.hp <= 0 then
						self.sv.respawnInteractionAttempted = false
						self.sv.saved.isConscious = false
						self:sv_clearPerks()
						character:setTumbling( true )
						character:setDowned( true )
						sm.effect.playEffect( "Mechanic - Ko", character.worldPosition )
						self:sv_dropCarryItem()
					end










				self.storage:save( self.sv.saved )
				self.network:setClientData( self.sv.saved )
			end
		end

		local isSeatSafeDamage = SeatSafeDamageSources[source] or false
		if not isSeatSafeDamage then
			if self.sv.saved.isConscious then
				local lockingInteractable = character:getLockingInteractable()
				if lockingInteractable and lockingInteractable:hasSeat() then
					lockingInteractable:setSeatCharacter( character )
				end
			end
		end
	end
end

function SurvivalPlayer.sv_n_revive( self, params )
	params = params or {}
	local character = self.player:getCharacter()
	if not self.sv.saved.isConscious
	    and ( self.sv.saved.hasRevivalItem or params.skipRevivalItem )
		and not self.sv.spawnparams.respawn then
		self.sv.saved.stats.hp = self.sv.saved.stats.maxhp
		self.sv.saved.isConscious = true
		self.sv.saved.hasRevivalItem = false
		self.storage:save( self.sv.saved )
		self.network:setClientData( self.sv.saved )
		if not params.skipRevivalItem then
			self.network:sendToClient( self.player, "cl_n_onEffect", { name = "Eat - EatFinish", host = self.player.character } )
		end
		if character then
			character:setTumbling( false )
			character:setDowned( false )
		end
		self.sv.damageCooldown:start( SpawnDamageCooldown )
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
		if sm.game.getLimitedInventory() then
			if self.sv.spawnparams.drillbotEject then
				g_respawnManager:sv_makeBagAtNamedNode( "TrainStation", self.player )
			else
				g_respawnManager:sv_performItemLoss( self.player )
			end
		end
		self.sv.spawnparams.respawn = true
		sm.event.sendToGame( "sv_e_respawn", { player = self.player } )
	end
end

function SurvivalPlayer.sv_n_tryRespawn( self )
	if not self.sv.saved.isConscious and not self.sv.respawnDelayTimer and not self.sv.respawnInteractionAttempted and not self.sv.spawnparams.warehouseEject then
		self.sv.respawnInteractionAttempted = true
		-- self.sv.respawnEndTimer = nil
		self.network:sendToClient( self.player, "cl_n_startFadeToBlackWaitForWorld", { duration = RespawnFadeDuration, timeout = RespawnFadeTimeout } )
		self.sv.respawnDelayTimer = Timer()
		self.sv.respawnDelayTimer:start( RespawnDelay )
	end
end

function SurvivalPlayer.sv_e_drillbotEject( self )
	self.sv.spawnparams.drillbotEject = true
	self.sv.saved.respawnBlocked = false
	self:sv_n_tryRespawn()
	self.storage:save( self.sv.saved )
	self.network:setClientData( self.sv.saved )
end

function SurvivalPlayer.sv_e_onSpawnCharacter( self )
	if self.sv.spawnparams.respawn then
		local playerBed = g_respawnManager:sv_getPlayerBed( self.player )
		if playerBed and playerBed.shape and sm.exists( playerBed.shape ) and playerBed.shape.body:getWorld() == self.player.character:getWorld() then
			-- Attempt to seat the respawned character in a bed
			self.network:sendToClient( self.player, "cl_seatCharacter", { shape = playerBed.shape  } )
		end

		-- self.sv.respawnEndTimer = Timer()
		-- self.sv.respawnEndTimer:start( RespawnEndDelay )
		self.network:sendToClient( self.player, "cl_n_respawned" )
	elseif self.sv.spawnparams.warehouseEject then
		self:sv_endFadeToBlack( { duration = EjectEndFadeDuration } )
	end

	if self.sv.spawnparams.goop then
		sm.event.sendToCharacter( self.player.character, "sv_e_addStatusEffect", { status = "goop", strength = 1 } )
	end

	if self.sv.saved.isNewPlayer or self.sv.spawnparams.respawn then
		if self.sv.saved.isNewPlayer then
			self.sv.saved.stats.hp = self.sv.saved.stats.maxhp
		else
			self.sv.saved.stats.hp = 30
		end
		self.sv.saved.isConscious = true
		self.sv.saved.hasRevivalItem = false
		self.sv.saved.isNewPlayer = false
		self.storage:save( self.sv.saved )
		self.network:setClientData( self.sv.saved )

		self.player.character:setTumbling( false )
		self.player.character:setDowned( false )
		self.sv.damageCooldown:start( SpawnDamageCooldown )
	elseif self.sv.spawnparams.warehouseEject then
		self.sv.saved.stats.hp = 0
		self.sv.saved.isConscious = false
		self:sv_clearPerks()
		self.sv.saved.hasRevivalItem = false
		self.sv.saved.isNewPlayer = false
		self.storage:save( self.sv.saved )
		self.network:setClientData( self.sv.saved )

		self.player.character:setTumbling( true )
		self.player.character:setDowned( true )
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

function SurvivalPlayer.cl_n_respawned( self )
	TutorialManager.Cl_TutorialEvent( TutorialEvent.KnockedOut )
end

function SurvivalPlayer.cl_n_onEvent( self, data )
	BasePlayer.cl_n_onEvent( self, data )
	if self.player == sm.localPlayer.getPlayer() and self.cl.stats.hp < MaxHp * 0.8 then
		TutorialManager.Cl_TutorialEvent( TutorialEvent.HungerAndThirst )
	end
end

function SurvivalPlayer.sv_e_warehouseEject( self, params )
	self:sv_startFadeToBlack( { duration = EjectFadeDuration, timeout = EjectFadeTimeout } )
	self.sv.warehouseEjectionFadeTick = sm.game.getCurrentTick() + EjectFadeDuration * 40 + EjectDelayTicks
	self.sv.spawnparams.warehouseEject = true
	self.sv.spawnparams.warehouseIndex = params.warehouseIndex
	if sm.exists( self.player.character ) then
		sm.effect.playEffect( "PropaneTank - ExplosionBig", self.player.character.worldPosition  )
	end
end

function SurvivalPlayer.sv_e_minidungeonEject( self )
	self.sv.spawnparams.goop = true
end

function SurvivalPlayer.cl_n_onInventoryChanges( self, params )
	if params.container == sm.localPlayer.getInventory() then
		for _, item in ipairs( params.changes ) do
			if item.difference > 0 then
				if ItemPickupFilter( item.uuid, params.changes, { obj_tool_bucket_chemical, obj_tool_bucket_empty, obj_tool_bucket_water, obj_tool_bucket_oil } ) then
					g_survivalHud:addToPickupDisplay( item.uuid, item.difference )
				end
				if isAnyOf( item.uuid, { obj_outfitpackage_common, obj_outfitpackage_rare, obj_outfitpackage_epic } ) then
					TutorialManager.Cl_TutorialEvent( TutorialEvent.GarmentBox )
				end
				if RecipeManager.IsDisassemblableItem( item.uuid ) and not RecipeManager.Cl_IsUnlocked( tostring( item.uuid ) ) then
					TutorialManager.Cl_TutorialEvent( TutorialEvent.Schematicbot )
				end
			else
				if ItemPickupFilter( item.uuid, params.changes, { obj_tool_bucket_chemical, obj_tool_bucket_empty, obj_tool_bucket_water, obj_tool_bucket_oil } ) then
					g_survivalHud:removeFromPickupDisplay( item.uuid, math.abs(item.difference) )
				end
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
	if params.breath then
		self.sv.saved.stats.breath = params.breath
	end
	self.storage:save( self.sv.saved )
	self.network:setClientData( self.sv.saved )
end

function SurvivalPlayer.sv_e_restoreBreath( self )
	if self.sv and self.sv.saved and self.sv.saved.stats then
		if self.sv.saved.stats.breath < self.sv.saved.stats.maxbreath then
			self.sv.saved.stats.breath = self.sv.saved.stats.maxbreath - 1
			self.storage:save( self.sv.saved )
			self.network:setClientData( self.sv.saved )
		end
	end
end

function SurvivalPlayer.sv_e_eat( self, edibleParams )
	local hp = edibleParams.hpGain
	if hp then
		if hp >= 0 then
			self:sv_restoreHealth( hp )
		else
			-- Passing fatigue here in order to play HurtHunger audio
			self:sv_takeDamage( math.abs( hp ), "fatigue" )
		end
	end
	if edibleParams.grantPerk then
		self:sv_grantPerk()
	end
	self.network:sendToClient( self.player, "cl_n_onEffect", { name = "Eat - EatFinish", host = self.player.character } )

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
		if self.sv.saved.stats.hp < self.sv.saved.stats.maxhp then
			self.network:sendToClient( self.player, "cl_n_onEffect", { name = "Eat - HealthRestored", host = self.player.character } )
		end
		self.sv.saved.stats.hp = self.sv.saved.stats.hp + health
		self.sv.saved.stats.hp = math.min( self.sv.saved.stats.hp, self.sv.saved.stats.maxhp )
	end
end

function SurvivalPlayer.sv_grantPerk( self )
	if self.sv.saved.isConscious then
		local availablePerks = {}
		for _, perk in pairs( SurvivalPlayer.Perks ) do
			if not self.sv.saved.stats.perks[perk] then
				table.insert( availablePerks, perk )
			end
		end
		if #availablePerks > 0 then
			local perk = availablePerks[math.random( 1, #availablePerks )]
			self.sv.saved.stats.perks[perk] = true
			if perk == SurvivalPlayer.Perks.BonusHealth then
				self.sv.saved.stats.maxhp = self.sv.saved.stats.maxhp + SurvivalPlayer.BuffBonusHealthIncrease
				self.sv.saved.stats.hp = math.min( self.sv.saved.stats.hp + SurvivalPlayer.BuffBonusHealthIncrease, self.sv.saved.stats.maxhp )
			end
		end
		self.player.publicData.perks = self.sv.saved.stats.perks
	end
end

















function SurvivalPlayer.sv_clearPerks( self )
	self.sv.saved.stats.perks = {}
	if self.player.publicData then
		self.player.publicData.perks = {}



	end
end

function SurvivalPlayer.client_onCancel( self )
	BasePlayer.client_onCancel( self )
	EffectManager.Cl_CancelAllCinematics()
end
