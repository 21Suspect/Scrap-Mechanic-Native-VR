-- Seat.lua --
dofile("$SURVIVAL_DATA/Scripts/game/survival_constants.lua")
dofile("$SURVIVAL_DATA/Scripts/game/survival_shapes.lua")
dofile("$SURVIVAL_DATA/Scripts/game/survival_units.lua")

Seat = class()
Seat.maxChildCount = 10
Seat.connectionOutput = sm.interactable.connectionType.seated
Seat.colorNormal = sm.color.new( 0x0da02aff )
Seat.colorHighlight = sm.color.new( 0x0da02aff )
Seat.upgradeCostItem = ITEMS.obj_consumable_component
Seat.upgradeTextDefaultColor = "#9f9e9e"
Seat.upgradeTextNumberColor = "#c4f42b"
Seat.bearingGuiTitle = "#{SEAT_UPGRADE_TITLE}"
Seat.Levels = {
	[tostring(ITEMS.obj_scrap_seat)] = { maxConnections = 2 },
	[tostring(ITEMS.obj_interactive_seat_01)] = { maxConnections = 2, upgrade = ITEMS.obj_interactive_seat_02, cost = 1, levelNumber = 1 },
	[tostring(ITEMS.obj_interactive_seat_02)] = { maxConnections = 4, upgrade = ITEMS.obj_interactive_seat_03, cost = 1, levelNumber = 2 },
	[tostring(ITEMS.obj_interactive_seat_03)] = { maxConnections = 6, upgrade = ITEMS.obj_interactive_seat_04, cost = 1, levelNumber = 3 },
	[tostring(ITEMS.obj_interactive_seat_04)] = { maxConnections = 8, upgrade = ITEMS.obj_interactive_seat_05, cost = 1,  levelNumber = 4 },
	[tostring(ITEMS.obj_interactive_seat_05)] = { maxConnections = 10, levelNumber = 5 },
}

function Seat.initTinkerGui( tinkerGuiJson, levels )
	local tinkerGui = {}
	tinkerGui.json = tinkerGuiJson
	ReplaceSubLayouts( tinkerGui.json )
	
	local connections = FindWidget( tinkerGui.json, "Connections" )
	tinkerGui.fill = FindWidget( connections, "Fill" )
	tinkerGui.steps = {
		{ FindWidget( connections, "Step1Default" ), FindWidget( connections, "Step1Active" ) },
		{ FindWidget( connections, "Step2Default" ), FindWidget( connections, "Step2Active" ) },
		{ FindWidget( connections, "Step3Default" ), FindWidget( connections, "Step3Active" ) },
		{ FindWidget( connections, "Step4Default" ), FindWidget( connections, "Step4Active" ) },
		{ FindWidget( connections, "Step5Default" ), FindWidget( connections, "Step5Active" ) }
	}
	tinkerGui.stepLabels = {
		{ FindWidget( connections, "LabelStep1Default" ), FindWidget( connections, "LabelStep1Active" ) },
		{ FindWidget( connections, "LabelStep2Default" ), FindWidget( connections, "LabelStep2Active" ) },
		{ FindWidget( connections, "LabelStep3Default" ), FindWidget( connections, "LabelStep3Active" ) },
		{ FindWidget( connections, "LabelStep4Default" ), FindWidget( connections, "LabelStep4Active" ) },
		{ FindWidget( connections, "LabelStep5Default" ), FindWidget( connections, "LabelStep5Active" ) }
	}
	tinkerGui.stepTitles = {
		[3] = FindWidget( connections, "Step3Title", false ),
		[4] = FindWidget( connections, "Step4Title", false ),
		[5] = FindWidget( connections, "Step5Title", false )
	}

	for _, level in pairs( levels ) do
		if level.levelNumber then
			local label = tinkerGui.stepLabels[level.levelNumber]
			local maxConnections = level.maxConnections
			label[1].Caption = tostring( maxConnections )
			label[2].Caption = tostring( maxConnections )
		end
	end
	
	tinkerGui.title = FindWidget( tinkerGui.json, "Title" )
	tinkerGui.levelText = FindWidget( tinkerGui.json, "LevelText" )
	tinkerGui.mainIcon = FindWidget( tinkerGui.json, "MainIcon" )
	tinkerGui.upgradeInfo = FindWidget( tinkerGui.json, "UpgradeInfo" )
	
	tinkerGui.backPanel = FindWidget( tinkerGui.json, "BackPanel" )
	tinkerGui.upgradePanel = FindWidget( tinkerGui.json, "UpgradePanel" )
	
	tinkerGui.upgradeButton = FindWidget( tinkerGui.json, "UpgradeButton" )
	tinkerGui.upgradeText = FindWidget( tinkerGui.json, "UpgradeText" )
	tinkerGui.upgradeCost = FindWidget( tinkerGui.json, "UpgradeCost" )
	tinkerGui.upgradeIcon = FindWidget( tinkerGui.json, "UpgradeIcon" )
	
	tinkerGui.backPanelDefaultHeight = tinkerGui.backPanel.height
	tinkerGui.backPanelHeightNonUpgradable = 332
	
	return tinkerGui
end

Seat.tinkerGuiJson = Seat.initTinkerGui( sm.json.open( "$SURVIVAL_DATA/Gui/JsonGuis/SeatUpgrade.gui" ), Seat.Levels )
Seat.tinkerGuiJson.title.Caption = "#{SEAT_UPGRADE_TITLE}"

local SpeedPerStep = 1 / math.rad( 27 ) / 3

-- Chapter 2 VR seat routing. The actual Seat callback runs in the seat's
-- Logic Task, which is not guaranteed to share Chapter2VR.lua's global table.
-- Keep this guard in Seat.lua itself so a live firearm trigger cannot be
-- consumed by a seat switch before the equipped tool sees it.
local ScrapVrSeatGunItems = {
	["c5ea0c2f-185b-48d6-b4df-45c386a575cc"] = true, -- spudgun
	["f6250bf4-9726-406f-a29a-945c06e460e5"] = true, -- shotgun
	["9fde0601-c2ba-4c70-8d5c-2a7a9fdd122b"] = true, -- gatling
	["d51ec758-057b-4263-bd16-7a731e149480"] = true, -- scrap spudgun
	["a2a2bb33-a841-4b23-88da-b758063d9206"] = true, -- launcher
	["6993e5df-6852-4e84-88ae-df49f765e784"] = true -- clay gun
}
local ScrapVrSeatGunActionDown = false

local function ScrapVrSeatPrimaryAction( controllerAction )
	return controllerAction == sm.interactable.actions.attack or
		controllerAction == sm.interactable.actions.create or
		controllerAction == sm.interactable.actions.item0 or
		controllerAction == sm.interactable.actions.item1
end

local function ScrapVrSeatSessionActive()
	local nativePose = ScrapVRProjectilePoseNative
	if type( nativePose ) == "function" then
		local ok, authoritative = pcall( nativePose )
		if ok and authoritative == true then return true end
	end
	return g_vrBridgeActive == true
end

local function ScrapVrSeatGunActionFiltered( controllerAction, state )
	if not ScrapVrSeatPrimaryAction( controllerAction ) then return false end
	local player = sm.localPlayer.getPlayer()
	local character = player and player:getCharacter() or nil
	local seated = character ~= nil and character:isSeated()
	local activeGun = ScrapVrSeatGunItems[tostring( sm.localPlayer.getActiveItem() )] == true
	local vrGun = seated and activeGun and ScrapVrSeatSessionActive()
	local filter = ( state == true and vrGun ) or ( state == false and ( ScrapVrSeatGunActionDown or vrGun ) )
	if not filter then return false end
	local nextDown = state == true
	if nextDown ~= ScrapVrSeatGunActionDown then
		sm.log.warning( "SCRAPVR_SEATED_GUN_ACTION state=" .. ( nextDown and "1" or "0" ) ..
			" action=" .. tostring( controllerAction ) .. " source=Seat.lua" )
	end
	ScrapVrSeatGunActionDown = nextDown
	return true
end

function Seat.server_onFixedUpdate( self )
	self.interactable:setActive( self.interactable:getSeatCharacter() ~= nil )
end

function Seat.sv_n_tryUpgrade( self, _, player )
	local level = self.Levels[tostring( self.shape:getShapeUuid() )]
	if level and level.upgrade then
		if sm.game.getEnableUpgrade() then
			local inventory = player:getInventory()
			local quantity = sm.container.totalQuantity( inventory, self.upgradeCostItem )
			if quantity >= level.cost then
				if sm.container.beginTransaction() then
					sm.container.spend( inventory, self.upgradeCostItem, level.cost, true )
					if sm.container.endTransaction() then
						-- Passing quantity here, since inventory might not have been synced properly when cl_n_onUpgrade arrives on client, and we would show incorrect numbers in UI.
						self.network:sendToClients( "cl_n_onUpgrade", { upgrade = level.upgrade, quantity = quantity - level.cost, player = player } )
						self.shape:replaceShape( level.upgrade )
						return true
					end
				end
			end
		end
	end

	return false
end

function Seat.client_onCreate( self )
	self.cl = {}
	self.cl.seatedCharacter = nil
	self.cl.renderTinker = false
end

function Seat.client_onDestroy( self )
	if self.cl.gui then
		self.cl.gui:clearOnCloseCallback()
		self.cl.gui:close()
	end

	if self.cl.tinkerGui then
		self.cl.tinkerGui:clearOnCloseCallback()
		self.cl.tinkerGui:close()
	end

	if self.cl.bearingGui then
		self.cl.bearingGui:clearOnCloseCallback()
		self.cl.bearingGui:close()
	end
end

function Seat.client_onUpdate( self, dt )
	-- Update gui upon character change in seat
	local seatedCharacter = self.interactable:getSeatCharacter()
	if self.cl.seatedCharacter ~= seatedCharacter then
		if seatedCharacter and seatedCharacter:getPlayer() and seatedCharacter:getPlayer():getId() == sm.localPlayer.getId() then
			self.cl.gui = sm.gui.createSeatGui( true )
			self.cl.gui:open()
		else
			if self.cl.gui then
				self.cl.gui:close()
				self.cl.gui = nil
			end
		end
		self.cl.seatedCharacter = seatedCharacter 
	end

	-- Update gui upon toolbar updates
	if self.cl.gui then
		local interactables = self.interactable:getSeatInteractables()
		for i=1, 10 do
			local value = interactables[i]
			if value and value:getConnectionInputType() == sm.interactable.connectionType.seated then
				self.cl.gui:setGridItem( "ButtonGrid", i-1, {
					["itemId"] = tostring( value:getShape():getShapeUuid() ),
					["active"] = value:isActive()
				})
			else
				self.cl.gui:setGridItem( "ButtonGrid", i-1, nil )
			end
		end
	end

	if self.cl.tinkerGui and self.cl.renderTinker == true then
		self.cl.tinkerGui:render( self.tinkerGuiJson.json )
		self.cl.renderTinker = false
	end
end

function Seat.cl_seat( self )
	if sm.localPlayer.getPlayer() and sm.localPlayer.getPlayer():getCharacter() then
		self.interactable:setSeatCharacter( sm.localPlayer.getPlayer():getCharacter() )
	end
end

function Seat.client_canInteract( self, character )
	if character:getCharacterType() == unit_mechanic and not character:isTumbling() then
		return true
	end
	return false
end

function Seat.client_onInteract( self, character, state )
	if state then
		self:cl_seat()
		if self.shape.interactable:getSeatCharacter() ~= nil then
			NotificationManager.Cl_AddGenericNotification( "#{ALERT_DRIVERS_SEAT_OCCUPIED}", 4.0 )
		end
	end
end

function Seat.cl_updateTinkerGui( self, forceLevel, quantity )
	local shapeUuid = forceLevel or self.shape:getShapeUuid()
	local level = self.Levels[tostring( shapeUuid )]
	self.tinkerGuiJson.levelText.Caption = "#{LEVEL} " .. level.levelNumber

	-- icons
	local resource, group, name = sm.gui.getItemIconFromUuid( shapeUuid )
	self.tinkerGuiJson.mainIcon.ImageResource = resource
	self.tinkerGuiJson.mainIcon.ImageGroup = group
	self.tinkerGuiJson.mainIcon.ImageName = name

	self.tinkerGuiJson.upgradePanel.Visible = sm.game.getEnableUpgrade() and level.cost ~= nil
	if self.tinkerGuiJson.upgradePanel.Visible then
		local inventory = sm.localPlayer.getPlayer():getInventory()
		local availableKits = quantity or sm.container.totalQuantity( inventory, self.upgradeCostItem )
		self.tinkerGuiJson.upgradeCost.Caption = FormatGuiNumberCount( availableKits, level.cost, true )
		
		self.tinkerGuiJson.upgradeButton.Enabled = level.cost <= availableKits
		self.tinkerGuiJson.backPanel.Skin = "BackgroundEngine"
		self.tinkerGuiJson.backPanel.height = self.tinkerGuiJson.backPanelDefaultHeight
	else
		self.tinkerGuiJson.upgradeButton.Enabled = false
		self.tinkerGuiJson.backPanel.Skin = "BackgroundEngineNoUpgrade"	
		self.tinkerGuiJson.backPanel.height = self.tinkerGuiJson.backPanelHeightNonUpgradable
	end
	
	self.tinkerGuiJson.upgradeInfo.Caption = ""
	if level.upgrade then
		local nextLevel = self.Levels[tostring( level.upgrade )]
		self.tinkerGuiJson.upgradeInfo.Caption = self:cl_getUpgradeInfoCaption( level, nextLevel )

		local resource, group, name = sm.gui.getItemIconFromUuid( level.upgrade )
		self.tinkerGuiJson.upgradeIcon.ImageResource = resource
		self.tinkerGuiJson.upgradeIcon.ImageGroup = group
		self.tinkerGuiJson.upgradeIcon.ImageName = name
	end

	for _, _level in pairs( self.Levels ) do
		local stepTitle = self.tinkerGuiJson.stepTitles[_level.levelNumber]
		if stepTitle then
			stepTitle.Caption = _level.stepTitle and stepTitle.Caption or ""
			local active = _level.levelNumber <= level.levelNumber
			stepTitle.TextColour = active and "1 1 1 1" or "0.5 0.5 0.5 0.5"
		end
	end

	for i, widgets in ipairs( self.tinkerGuiJson.steps ) do
		local active = i <= level.levelNumber
		widgets[1].Visible = not active
		widgets[2].Visible = active
	end

	local firstStep = self.tinkerGuiJson.steps[1][1]
	local activeStep = self.tinkerGuiJson.steps[level.levelNumber][1]
	self.tinkerGuiJson.fill.width = ( activeStep.x - firstStep.x )
end

function Seat.cl_getUpgradeInfoCaption( self, level, nextLevel )
	local baseCaption = self.upgradeTextDefaultColor
		.. "#{CONTROLLER_UPGRADE_Connections} "
		.. self.upgradeTextNumberColor
		.. "+"
		.. ( nextLevel.maxConnections - level.maxConnections )

	return level.allowAdjustingJoints ~= nextLevel.allowAdjustingJoints and baseCaption
		.. self.upgradeTextDefaultColor
		.. "\n#{CONTROLLER_UPGRADE_Settings} "
		.. self.upgradeTextNumberColor
		.. "#{UNLOCKED}" or baseCaption
end

function Seat.client_canTinker( self, character )
	local shapeUuid = self.shape:getShapeUuid()
	return self.shape.usable and self.Levels[tostring( shapeUuid )] and self.Levels[tostring( shapeUuid )].levelNumber ~= nil
end

function Seat.client_onTinker( self, character, state )
	if state then
		self.cl.tinkerGui = sm.jsonGui.createGui( { handleKeySetup = "Tinker" } )
		self:cl_updateTinkerGui()
		self.cl.tinkerGui:render( self.tinkerGuiJson.json )
	end
end

function Seat.cl_onTinkerClose( self, character )
	self.cl.tinkerGui = nil
end

function Seat.cl_onUpgradeClicked( self, buttonName )
	self.network:sendToServer( "sv_n_tryUpgrade" )
end

function Seat.cl_n_onUpgrade( self, params )
	if self.cl.tinkerGui then
		-- Only pass remaining quantity if it's the player that performed the upgrade. The inventory might not have synced yet with the spend() run from server!
		self:cl_updateTinkerGui( params.upgrade, sm.localPlayer.getId() == params.player.id and params.quantity or nil )
		self.cl.tinkerGui:render( self.tinkerGuiJson.json )
		self.cl.tinkerGui:getWidget( "UpgradeEffectIcon" ):startEffect( "upgrade_effect_icon" )
		self.cl.tinkerGui:getWidget( "UpgradeEffectButton" ):startEffect( "upgrade_effect_button" )
	end

	sm.effect.playHostedEffect( "Part - Upgrade", self.interactable )
end

function Seat.cl_onBearingGuiClose( self )
	self.cl.bearingGui = nil
	self.cl.currentJoint = nil
end

function Seat.client_canInteractThroughJoint( self )
	if not self.shape.body.connectable then
		return false
	end
	local level = self.Levels[tostring( self.shape:getShapeUuid() )]
	return level.allowAdjustingJoints ~= nil
end

function Seat.client_onInteractThroughJoint( self, character, state, joint )
	self.cl.bearingGui = sm.gui.createSteeringBearingGui( true )
	self.cl.bearingGui:setText( "Title", self.bearingGuiTitle )
	self.cl.bearingGui:open()
	self.cl.bearingGui:setOnCloseCallback( "cl_onBearingGuiClose" )

	self.cl.currentJoint = joint

	self.cl.bearingGui:setSliderCallback( "LeftAngle", "cl_onLeftAngleChanged" )
	self.cl.bearingGui:setSliderData( "LeftAngle", 120, self.interactable:getSteeringJointLeftAngleLimit( joint ) - 1 )

	self.cl.bearingGui:setSliderCallback( "RightAngle", "cl_onRightAngleChanged")
	self.cl.bearingGui:setSliderData( "RightAngle", 120, self.interactable:getSteeringJointRightAngleLimit( joint ) - 1 )

	self.cl.bearingGui:setSliderCallback( "LeftSpeed", "cl_onLeftSpeedChanged" )
	self.cl.bearingGui:setSliderData( "LeftSpeed", 10, round( self.interactable:getSteeringJointLeftAngleSpeed( joint ) / SpeedPerStep ) - 1 )
	
	self.cl.bearingGui:setSliderCallback( "RightSpeed", "cl_onRightSpeedChanged" )
	self.cl.bearingGui:setSliderData( "RightSpeed", 10, round( self.interactable:getSteeringJointRightAngleSpeed( joint ) / SpeedPerStep ) - 1 )

	local unlocked = self.interactable:getSteeringJointUnlocked( joint )
	local buttonName = unlocked and "Off" or "On"
	self.cl.bearingGui:setButtonState( buttonName, true )
	self.cl.bearingGui:setButtonCallback( "On", "cl_onLockButtonClicked" )
	self.cl.bearingGui:setButtonCallback( "Off", "cl_onLockButtonClicked" )
end

function Seat.client_onChildJointRemoved( self, joint )
	if self.cl.bearingGui and self.cl.currentJoint and self.cl.currentJoint.id == joint.id then
		self.cl.bearingGui:close()
	end
end

function Seat.sv_bearingSettingsChanged( self, settings )
	-- Do we need validation logic here to catch malicious clients? Plant, 2026-04-15
	self.network:setClientData( settings, 1 )
end

function Seat.client_onClientDataUpdate( self, clientData, channel )
	if channel == 1 and self.cl.bearingGui then
		if clientData.leftAngle then
			self.cl.bearingGui:setSliderPosition( "LeftAngle", clientData.leftAngle )
		end
		
		if clientData.rightAngle then
			self.cl.bearingGui:setSliderPosition( "RightAngle", clientData.rightAngle )
		end
		
		if clientData.leftSpeed then
			self.cl.bearingGui:setSliderPosition( "LeftSpeed", clientData.leftSpeed )
		end
		
		if clientData.rightSpeed then
			self.cl.bearingGui:setSliderPosition( "RightSpeed", clientData.rightSpeed )
		end
		
		if clientData.unlocked ~= nil then
			local buttonName = clientData.unlocked and "Off" or "On"
			self.cl.bearingGui:setButtonState( buttonName, true )
		end
	end
end

function Seat.cl_onLeftAngleChanged( self, sliderName, sliderPos )
	self.network:sendToServer( "sv_bearingSettingsChanged", { joint = self.cl.currentJoint, leftAngle = sliderPos } )
	self.interactable:setSteeringJointLeftAngleLimit( self.cl.currentJoint, sliderPos + 1 )
end

function Seat.cl_onRightAngleChanged( self, sliderName, sliderPos )
	self.network:sendToServer( "sv_bearingSettingsChanged", { joint = self.cl.currentJoint, rightAngle = sliderPos } )
	self.interactable:setSteeringJointRightAngleLimit( self.cl.currentJoint, sliderPos + 1 )
end

function Seat.cl_onLeftSpeedChanged( self, sliderName, sliderPos )
	self.network:sendToServer( "sv_bearingSettingsChanged", { joint = self.cl.currentJoint, leftSpeed = sliderPos } )
	self.interactable:setSteeringJointLeftAngleSpeed( self.cl.currentJoint, ( sliderPos + 1 ) * SpeedPerStep )
end

function Seat.cl_onRightSpeedChanged( self, sliderName, sliderPos )
	self.network:sendToServer( "sv_bearingSettingsChanged", { joint = self.cl.currentJoint, rightSpeed = sliderPos } )
	self.interactable:setSteeringJointRightAngleSpeed( self.cl.currentJoint, ( sliderPos + 1 ) * SpeedPerStep )
end

function Seat.cl_onLockButtonClicked( self, buttonName )
	self.network:sendToServer( "sv_bearingSettingsChanged", { joint = self.cl.currentJoint, unlocked = buttonName == "Off" } )
	self.interactable:setSteeringJointUnlocked( self.cl.currentJoint, buttonName == "Off" )
end

function Seat.client_onAction( self, controllerAction, state )
	if ScrapVrSeatGunActionFiltered( controllerAction, state ) then
		-- false lets the equipped firearm receive the same primary input while
		-- preventing Seat from toggling button 0/1.
		return false
	end
	local consumeAction = true
	if state == true then
		if controllerAction == sm.interactable.actions.use or controllerAction == sm.interactable.actions.jump then
			self:cl_seat()
		elseif controllerAction == sm.interactable.actions.item0 or controllerAction == sm.interactable.actions.create then
			self.interactable:pressSeatInteractable( 0 )
		elseif controllerAction == sm.interactable.actions.item1 or controllerAction == sm.interactable.actions.attack then
			self.interactable:pressSeatInteractable( 1 )
		elseif controllerAction == sm.interactable.actions.create then
			self.interactable:pressSeatInteractable( 1 )
		elseif controllerAction == sm.interactable.actions.item2 then
			self.interactable:pressSeatInteractable( 2 )
		elseif controllerAction == sm.interactable.actions.item3 then
			self.interactable:pressSeatInteractable( 3 )
		elseif controllerAction == sm.interactable.actions.item4 then
			self.interactable:pressSeatInteractable( 4 )
		elseif controllerAction == sm.interactable.actions.item5 then
			self.interactable:pressSeatInteractable( 5 )
		elseif controllerAction == sm.interactable.actions.item6 then
			self.interactable:pressSeatInteractable( 6 )
		elseif controllerAction == sm.interactable.actions.item7 then
			self.interactable:pressSeatInteractable( 7 )
		elseif controllerAction == sm.interactable.actions.item8 then
			self.interactable:pressSeatInteractable( 8 )
		elseif controllerAction == sm.interactable.actions.item9 then
			self.interactable:pressSeatInteractable( 9 )
		else
			consumeAction = false
		end
	else
		if controllerAction == sm.interactable.actions.item0 or controllerAction == sm.interactable.actions.create then
			self.interactable:releaseSeatInteractable( 0 )
		elseif controllerAction == sm.interactable.actions.item1 or controllerAction == sm.interactable.actions.attack then
			self.interactable:releaseSeatInteractable( 1 )
		elseif controllerAction == sm.interactable.actions.item2 then
			self.interactable:releaseSeatInteractable( 2 )
		elseif controllerAction == sm.interactable.actions.item3 then
			self.interactable:releaseSeatInteractable( 3 )
		elseif controllerAction == sm.interactable.actions.item4 then
			self.interactable:releaseSeatInteractable( 4 )
		elseif controllerAction == sm.interactable.actions.item5 then
			self.interactable:releaseSeatInteractable( 5 )
		elseif controllerAction == sm.interactable.actions.item6 then
			self.interactable:releaseSeatInteractable( 6 )
		elseif controllerAction == sm.interactable.actions.item7 then
			self.interactable:releaseSeatInteractable( 7 )
		elseif controllerAction == sm.interactable.actions.item8 then
			self.interactable:releaseSeatInteractable( 8 )
		elseif controllerAction == sm.interactable.actions.item9 then
			self.interactable:releaseSeatInteractable( 9 )
		else
			consumeAction = false
		end
	end
	return consumeAction
end

-- Chapter2VR.lua sees this marker and leaves the already-patched callback in
-- place. Older payloads without Seat.lua still receive its fallback wrapper.
Seat.__scrapvrSeatGunActionRouting = true

function Seat.client_getAvailableChildConnectionCount( self, connectionType )
	if bit.band( connectionType, self.connectionOutput ) ~= 0 then
		local level = self.Levels[tostring( self.shape:getShapeUuid() )]
		local currentConnectionCount = #self.interactable:getChildren( self.connectionOutput )
		return level.maxConnections - currentConnectionCount
	end

	return 0
end

Saddle = class( Seat )
Saddle.bearingGuiTitle = "#{SADDLE_UPGRADE_TITLE}"
Saddle.Levels = {
	[tostring(ITEMS.obj_interactive_saddle_01)] = { maxConnections = 3, upgrade = ITEMS.obj_interactive_saddle_02, cost = 1, levelNumber = 1 },
	[tostring(ITEMS.obj_interactive_saddle_02)] = { maxConnections = 4, upgrade = ITEMS.obj_interactive_saddle_03, cost = 1, levelNumber = 2 },
	[tostring(ITEMS.obj_interactive_saddle_03)] = { maxConnections = 6, upgrade = ITEMS.obj_interactive_saddle_04, cost = 1, levelNumber = 3 },
	[tostring(ITEMS.obj_interactive_saddle_04)] = { maxConnections = 8, upgrade = ITEMS.obj_interactive_saddle_05, cost = 1, levelNumber = 4 },
	[tostring(ITEMS.obj_interactive_saddle_05)] = { maxConnections = 10, levelNumber = 5 },
}

Saddle.tinkerGuiJson = Seat.initTinkerGui( sm.json.open( "$SURVIVAL_DATA/Gui/JsonGuis/SeatUpgrade.gui" ), Saddle.Levels )
Saddle.tinkerGuiJson.title.Caption = "#{SADDLE_UPGRADE_TITLE}"

