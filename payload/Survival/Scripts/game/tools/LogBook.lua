-- Generated from the supported stock build by generate_held_item_payload.py.
dofile "$GAME_DATA/Scripts/game/AnimationUtil.lua"
dofile "$SURVIVAL_DATA/Scripts/game/managers/BabyWocManager.lua"
dofile "$SURVIVAL_DATA/Scripts/game/managers/BeaconManager.lua"
dofile "$SURVIVAL_DATA/Scripts/game/managers/QuestManager.lua"
dofile "$SURVIVAL_DATA/Scripts/game/survival_logs.lua"
dofile "$SURVIVAL_DATA/Scripts/game/managers/DialogManager.lua"

local GuiData = dofile( "$SURVIVAL_DATA/Gui/JsonGuis/LogbookJson.gui" )
local Tabs = {
	Quests = "QuestsTab",
	Dialogue = "DialogueTab",
	Items = "ItemsTab",
	Beacons = "BeaconsTab",
	Garage = "GarageTab",
	GaragePlaceHolder = "GaragePlaceholder Tab"
}


local renderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_logbook/char_logbook.rend" }
local renderablesTp = { "$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_tp_logbook.rend", "$SURVIVAL_DATA/Character/Char_Tools/Char_logbook/char_logbook_tp_animlist.rend" }
local renderablesFp = { "$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_fp_logbook.rend", "$SURVIVAL_DATA/Character/Char_Tools/Char_logbook/char_logbook_fp_animlist.rend" }

local SpeakerToImage = {
	["Phe"] = "Logbook_New/gui_logbook_dialogs_npc_phe.png",
	["Hubert"] = "Logbook_New/gui_logbook_dialogs_npc_hubert.png",
	["Lorenzo"] = "Logbook_New/gui_logbook_dialogs_npc_lorenzo.png",
	["Recording"] = "Logbook_New/gui_logbook_icon_audio.png",
	["Mysterious"] = "Logbook_New/gui_logbook_dialogs_npc_mysterious.png",
	["Axobot"] = "Logbook_New/gui_logbook_dialogs_npc_axobot.png",
	[DialogSpeakerName.Phe] = "Logbook_New/gui_logbook_dialogs_npc_phe.png",
	[DialogSpeakerName.Hubert] = "Logbook_New/gui_logbook_dialogs_npc_hubert.png",
	[DialogSpeakerName.Lorenzo] = "Logbook_New/gui_logbook_dialogs_npc_lorenzo.png",
	[DialogSpeakerName.Recording] = "Logbook_New/gui_logbook_icon_audio.png",
	[DialogSpeakerName.Mysterious] = "Logbook_New/gui_logbook_dialogs_npc_mysterious.png",
	[DialogSpeakerName.Axobot] = "Logbook_New/gui_logbook_dialogs_npc_axobot.png"
}

sm.tool.preloadRenderables( renderables )
sm.tool.preloadRenderables( renderablesTp )
sm.tool.preloadRenderables( renderablesFp )

local LogBookVersion = 1

local log_entries = dofile( "$SURVIVAL_DATA/Logs/log_entries.json" ).logList


local function resourceToImage( uuid )
	if uuid == ITEMS.obj_crates_banana then
		return "gui_hideout_vegtable_banana.png"

	elseif uuid == ITEMS.obj_crates_blueberry then
		return "gui_hideout_vegtable_blueberry.png"

	elseif uuid == ITEMS.obj_crates_orange then
		return "gui_hideout_vegtable_orange.png"

	elseif uuid == ITEMS.obj_crates_pineapple then
		return "gui_hideout_vegtable_pinapple.png"

	elseif uuid == ITEMS.obj_crates_carrot then
		return "gui_hideout_vegtable_carrot.png"

	elseif uuid == ITEMS.obj_crates_redbeet then
		return "gui_hideout_vegtable_redbeet.png"

	elseif uuid == ITEMS.obj_crates_tomato then
		return "gui_hideout_vegtable_tomato.png"

	elseif uuid == ITEMS.obj_crates_broccoli then
		return "gui_hideout_vegtable_broccoli.png"

	elseif uuid == ITEMS.obj_survivalobject_farmerball then
		return "gui_hideout_farmball.png"
	end

	return ""
end

local NoRewardScrollPadding = 94 -- Semi magical value because there isn't anything good to base it on
local DialogPageHeight = 2500 -- Height budget used to pack dialog feed entries onto a page

LogBook = class()
LogBook.equipWhileSeated = true

function LogBook.sv_getSaved( self, playerId )
	local storage = self.storage:load() or { version = LogBookVersion, playerData = {} }

	-- Migrate old flat save format to per-player format
	if storage.playerData == nil then
		local oldBeacons = storage.mapActiveBeacons or {}
		local oldReadLogs = storage.mapReadLogs or {}
		storage.playerData = {}
		storage.playerData[playerId] = { mapActiveBeacons = oldBeacons, mapReadLogs = oldReadLogs, trackedLog = sm.uuid.getNil(), showFirstQuest = false }
		storage.mapActiveBeacons = nil
		storage.mapReadLogs = nil
		self.storage:save( storage )
	end

	if storage.playerData[playerId] == nil then
		storage.playerData[playerId] = { mapActiveBeacons = {}, mapReadLogs = { [LOGS.log_crashedship] = true }, trackedLog = sm.uuid.getNil(), showFirstQuest = true }
		local saveCopy = DeepCopy( storage )
		saveCopy.playerData[playerId].showFirstQuest = false
		self.storage:save( saveCopy )
	end
	return storage
end

function LogBook.server_onCreate( self )
	local owner = self.tool:getOwner()
	local clientData = self:sv_getSaved( owner.id ).playerData[owner.id]
	self.network:setClientData( clientData )
end

function LogBook.sv_n_activeBeacon( self, beacon, player )
	local saved = self:sv_getSaved( player.id )
	saved.playerData[player.id].mapActiveBeacons[beacon.id] = beacon.active
	self.storage:save( saved )
end

function LogBook.sv_n_recordingStarted( self, data )
	QuestManager.Sv_SendEvent( QuestEvent.RecordingStarted, data )
end

function LogBook.sv_n_readLog( self, logUuid, player )
	local saved = self:sv_getSaved( player.id )
	saved.playerData[player.id].mapReadLogs[logUuid] = true
	QuestManager.Sv_SendEvent( QuestEvent.LogBookReadLog, { log = logUuid } )
	self.storage:save( saved )
end

function LogBook.sv_n_logBookOpened( self, position )
	QuestManager.Sv_SendEvent( QuestEvent.LogBookOpen )
	sm.effect.playEffect( "Logbook - Open", position )
end

function LogBook.sv_n_waypointTracked( self, data, player )
	local saved = self:sv_getSaved( player.id )
	if not saved.playerData[player.id].trackedLog then
		saved.playerData[player.id].trackedLog = sm.uuid.getNil()
	end
	if data.visible then
		saved.playerData[player.id].trackedLog = data.uuid
	else
		saved.playerData[player.id].trackedLog = sm.uuid.getNil()
	end
	QuestManager.Sv_SendEvent( QuestEvent.LogBookWaypointTracked, { log = data.uuid } )
	self.storage:save( saved )
end

function LogBook.sv_n_whistleClicked( self, _, player )
	BabyWocManager.Sv_CallFollower( player )
end

--------------------------------------------------------------------------------

local function adjustDivider( divider, containerWidth )
	local dividerTextBox = FindWidget( divider, "DividerText" )
	local calculatedTextWidth, calculatedTextHeight = sm.gui.computeTextSize( dividerTextBox.Caption, dividerTextBox.FontName, dividerTextBox.TextAlign, containerWidth )
	dividerTextBox.width = math.floor( calculatedTextWidth )
	dividerTextBox.height = math.floor( calculatedTextHeight )

	local lineLength = (divider.width - calculatedTextWidth) / 2
	local divider1 = FindWidget( divider, "DividerLine1" )
	divider1.width = math.floor( lineLength )
	local divider2 = FindWidget( divider, "DividerLine2" )
	divider2.width = math.floor( lineLength )
end

local function loadDialogSet( path, dialogTable, language )
	local dialogSet = sm.json.open( path )
	local function parseDialog( dialogContent )
		if dialogContent.hideInLogbook then
			return
		end
		local uuidString = tostring( dialogContent.uuid )
		local title = dialogContent.title
		local speaker = dialogContent.speaker
		dialogTable[uuidString] = { speaker = speaker, title = title, entries = {}, entriesByName = {} }

		for i, entry in ipairs( dialogContent.entries ) do
			if entry.hideInLogbook then
				goto continue
			end
			local bubbles = {}
			local text = ""
			for _, line in ipairs( entry.lines ) do
				if line.text then -- avoid old format
					if text == "" then
						text = line.text
					else
						text = text .. " " .. line.text
					end
				else
					print( "Logbook: Old format detected in dialog " .. uuidString )
				end
				if string.len( text ) > 250 or line.breakLine then
					table.insert( bubbles, text )
					text = ""
				end
			end
			if string.len( text ) > 0 then
				table.insert( bubbles, text )
			end
			local entryData = { speaker = entry.speaker, bubbles = bubbles, name = entry.name }
			dialogTable[uuidString].entries[i] = entryData
			if entry.name then
				dialogTable[uuidString].entriesByName[entry.name] = entryData
			end
			::continue::
		end
	end
	for _, dialog in ipairs( dialogSet.dialogSet ) do
		local languagePath = dialog:gsub( "#{LANGUAGE}", language )
		if not sm.json.fileExists( languagePath ) then
			languagePath = dialog:gsub( "#{LANGUAGE}", "English" )
		end
		if sm.json.fileExists( languagePath ) then
			local dialogContent = sm.json.open( languagePath )
			if dialogContent.list then
				for _, dialogEntry in ipairs( dialogContent.list ) do
					parseDialog( dialogEntry )
				end
			else
				parseDialog( dialogContent )
			end
		end
	end
end

local function loadDialogSets( dialogTable, language )
	if sm.json.fileExists( "$SURVIVAL_DATA/Dialog/dialogset.json" ) then
		loadDialogSet( "$SURVIVAL_DATA/Dialog/dialogset.json", dialogTable, language )
	end
end

function LogBook.client_onCreate( self )
	self.cl = {}
	if self.tool:isLocal() then
		if self.cl.jsonGui == nil then
			self.cl.jsonGui = sm.jsonGui.createGui( { handleKeySetup = "Logbook", name = "Logbook", openCloseAudio = false } )
		end

		self.cl.dialogSets = {}
		loadDialogSets( self.cl.dialogSets, sm.gui.getCurrentLanguage() )

		self.cl.logData = {}
		self.cl.trackedLogUuid = sm.uuid.getNil()

		for _, value in ipairs( log_entries ) do
			self.cl.logData[value.uuid] = value
		end

		self.cl.guiData = DeepCopy( GuiData )

		self.cl.sidePanel = FindWidget( self.cl.guiData, "SidePanel" )
		self.cl.mainPanel = FindWidget( self.cl.guiData, "MainPanel" )

		self.cl.timeText = FindWidget( self.cl.mainPanel, "TimeText" )
		self.cl.daysText = FindWidget( self.cl.mainPanel, "DaysText" )

		self.cl.photoLogSidePanel = FindWidget( self.cl.sidePanel, "PhotoLogSidePanel" )
		self.cl.photoLogSidePanelDescription = FindWidget( self.cl.photoLogSidePanel, "LogItemDescription" )
		self.cl.photoLogSidePanelTitle = FindWidget( self.cl.photoLogSidePanel, "PhotoLogTitle" )
		self.cl.photoLogSidePanelWaypointButton = FindWidget( self.cl.photoLogSidePanel, "SetWaypointButton" )
		self.cl.photoLogSidePanelWaypointButtonCaption = FindWidget( self.cl.photoLogSidePanel, "WaypointButtonCaption" )

		self.cl.photoLogSidePanelWaypointVisibility = FindWidget( self.cl.photoLogSidePanel, "WaypointVisibilityButton" )
		self.cl.photoLogSidePanelIcon = FindWidget( self.cl.photoLogSidePanel, "PhotoLogIcon" )
		self.cl.photoLogSidePanelPhoto = FindWidget( self.cl.photoLogSidePanel, "Photo" )
		self.cl.photoLogSidePanelWaypointEffectBox = FindWidget( self.cl.photoLogSidePanel, "WaypointEffectBox" )

		self.cl.audioLogSidePanel = FindWidget( self.cl.sidePanel, "AudioLogSidePanel" )
		self.cl.audioLogSidePanelTitle = FindWidget( self.cl.audioLogSidePanel, "AudioLogTitle" )
		self.cl.audioLogSidePanelDescription = FindWidget( self.cl.audioLogSidePanel, "AudioDescription" )
		self.cl.audioLogSidePanelButton = FindWidget( self.cl.audioLogSidePanel, "AudioLogButton" )

		self.cl.questSidePanel = FindWidget( self.cl.sidePanel, "QuestSidePanel" )
		self.cl.questSidePanelIcon = FindWidget( self.cl.questSidePanel, "QuestSidePanelIcon" )
		self.cl.questSidePanelTitle = FindWidget( self.cl.questSidePanel, "QuestSidePanelTitle" )
		self.cl.questDescriptionScrollView = FindWidget( self.cl.questSidePanel, "QuestDescriptionScrollView" )
		self.cl.questDescScrollHeight = shallowcopy( self.cl.questDescriptionScrollView.height )
		self.cl.questDescriptionPhotoPanelBase = table.remove( self.cl.questDescriptionScrollView.Childs, 1 )

		self.cl.builderGuideHolder = FindWidget( self.cl.questSidePanel, "BuilderGuideHolder" )
		self.cl.builderGuideDescription = FindWidget( self.cl.builderGuideHolder, "BuilderGuideQuestDescription" )
		self.cl.builderGuideScrollView = FindWidget( self.cl.builderGuideHolder, "BuilderGuideScrollView" )
		self.cl.builderGuideScrollBaseItem = table.remove( self.cl.builderGuideScrollView.Childs, 1 )

		self.cl.farmerSidePanel = FindWidget( self.cl.sidePanel, "FarmerSidePanel" )
		self.cl.farmerSideResourceImage = FindWidget( self.cl.farmerSidePanel, "ResourceImage" )
		self.cl.farmerSidePanelRewardImage = FindWidget( self.cl.farmerSidePanel, "RewardImage" )
		self.cl.farmerSidePanelTitle = FindWidget( self.cl.sidePanel, "FarmerSidePanelTitle" )
		self.cl.farmerScrollView = FindWidget( self.cl.farmerSidePanel, "FarmerDescriptionScrollView" )
		self.cl.farmerImageBase = DeepCopy( FindWidget( self.cl.farmerSidePanel, "FarmerDescriptionPhotoPanel" ) )
		self.cl.farmerDescriptionBase = DeepCopy( FindWidget( self.cl.farmerSidePanel, "FarmerDescriptionTextBox" ) )
		self.cl.farmerTaskBase = DeepCopy( FindWidget( self.cl.farmerSidePanel, "FarmerTaskPanel" ) )

		self.cl.questSidePanelDescriptionBase = FindWidget( self.cl.questDescriptionScrollView, "QuestDescriptionTextBox" )
		self.cl.questSidePanelTaskPanel = FindWidget( self.cl.questDescriptionScrollView, "QuestTaskPanel" )
		self.cl.questSidePanelTaskBase = FindWidget( self.cl.questDescriptionScrollView, "QuestTaskBase" )

		self.cl.questRewardText = FindWidget( self.cl.questSidePanel, "RewardsText" )
		self.cl.questReward1 = FindWidget( self.cl.questSidePanel, "QuestReward1" )
		self.cl.questReward2 = FindWidget( self.cl.questSidePanel, "QuestReward2" )
		self.cl.questRewardText.Visible = false
		self.cl.questReward1.Visible = false
		self.cl.questReward2.Visible = false

		self.cl.questContentPanel = FindWidget( self.cl.mainPanel, "QuestContentPanel" )
		self.cl.activeQuestPanel = FindWidget( self.cl.questContentPanel, "ActiveQuestPanel" )
		self.cl.completedQuestPanel = FindWidget( self.cl.questContentPanel, "CompletedQuestPanel" )
		self.cl.questBase = PopBaseItem( self.cl.activeQuestPanel )
		self.cl.activeQuestDivider = FindWidget( self.cl.questContentPanel, "ActiveQuestDivider" )
		self.cl.activeQuestDividerHeight = self.cl.activeQuestDivider.height
		self.cl.activeQuestPanelHeight = self.cl.activeQuestPanel.height
		self.cl.completedQuestDivider = FindWidget( self.cl.questContentPanel, "CompletedQuestDivider" )
		self.cl.completedQuestDividerHeight = self.cl.completedQuestDivider.height
		adjustDivider( self.cl.completedQuestDivider, self.cl.questContentPanel.width )
		adjustDivider( self.cl.activeQuestDivider, self.cl.questContentPanel.width )

		self.cl.dialogContainer = FindWidget( self.cl.mainPanel, "DialogContainer" )
		self.cl.dialogContentPanel = FindWidget( self.cl.dialogContainer, "DialogContentPanel" )
		self.cl.dialogFeedDividerBase = PopBaseItem( self.cl.dialogContentPanel )
		self.cl.dialogFeedBubbleBase = PopBaseItem( self.cl.dialogContentPanel )
		self.cl.defaultSpeechBubbleHeight = FindWidget( self.cl.dialogFeedBubbleBase, "SpeechBubble" ).height
		local dialogBubbleText = FindWidget( self.cl.dialogFeedBubbleBase, "SpeechBubbleText" )
		self.cl.defaultSpeechBubbleTextHeight = dialogBubbleText.height
		self.cl.dialogBubbleTextWidth = dialogBubbleText.width
		self.cl.dialogBubbleTextFont = dialogBubbleText.FontName
		self.cl.dialogBubbleTextAlign = dialogBubbleText.TextAlign
		self.cl.dialogFeedDividerHeight = self.cl.dialogFeedDividerBase.height
		self.cl.dialogFeedBubbleSpacing = self.cl.dialogFeedBubbleBase.height - self.cl.defaultSpeechBubbleHeight
		self.cl.dialogPageHeight = DialogPageHeight

		adjustDivider( FindWidget( self.cl.dialogContainer, "DialogDivider" ), self.cl.dialogContainer.width )

		self.cl.dialogNavigationContainer = FindWidget( self.cl.dialogContainer, "DialogNavigationContainer" )
		self.cl.dialogNavigation = FindWidget( self.cl.dialogNavigationContainer, "DialogNavigation" )
		self.cl.dialogArrowLeft = FindWidget( self.cl.dialogNavigation, "DialogArrowLeft" )
		self.cl.dialogArrowRight = FindWidget( self.cl.dialogNavigation, "DialogArrowRight" )
		self.cl.dialogIndicators = FindWidget( self.cl.dialogNavigation, "DialogIndicators" )
		self.cl.dialogPageDotBase = PopBaseItem( self.cl.dialogIndicators )
		self.cl.dialogArrowLeft.onClick = "cl_dialogPrevPage"
		self.cl.dialogArrowRight.onClick = "cl_dialogNextPage"
		self.cl.dialogPages = {}
		self.cl.dialogPageIndex = 1
		self.cl.dialogLastFeedCount = 0
		self.cl.dialogMeasureCache = {}

		self.cl.logItemContentPanel = FindWidget( self.cl.mainPanel, "ItemContentPanel" )
		self.cl.logItemGrid = FindWidget( self.cl.logItemContentPanel, "ItemGrid" )
		self.cl.logItemBase = PopBaseItem( self.cl.logItemGrid )
		adjustDivider( FindWidget( self.cl.logItemContentPanel, "LogItemsDivider" ), self.cl.logItemContentPanel.width )

		self.cl.beaconContentPanel = FindWidget( self.cl.mainPanel, "BeaconContentPanel" )
		self.cl.beaconBase = PopBaseItem( self.cl.beaconContentPanel )
		adjustDivider( FindWidget( self.cl.beaconContentPanel, "DividerBase" ), self.cl.beaconContentPanel.width )

		self.cl.garageContentPanel = FindWidget( self.cl.mainPanel, "GarageContentPanel" )
		self.cl.garagePlaceholderContentPanel = FindWidget( self.cl.mainPanel, "GarageContentPanelEmptyState" )
		self.cl.garageItemsHolder = FindWidget( self.cl.garageContentPanel, "GarageItemsHolder" )
		self.cl.garageShowcase = FindWidget( self.cl.garageItemsHolder, "GarageShowcase" )
		self.cl.garageResourceHolder = FindWidget( self.cl.garageItemsHolder, "GarageResourceHolder" )
		self.cl.garagePreview = FindWidget( self.cl.garageContentPanel, "GaragePreview" )
		--self.cl.garagePreview.BlueprintPreview = "$CONTENT_32de754b-476e-42fa-8d86-817a5e543b7b/blueprint.json"
		self.cl.garageShowcaseTitle = FindWidget( self.cl.garageContentPanel, "GarageShowcaseTitle" )
		self.cl.garageProgressBar = FindWidget( self.cl.garageContentPanel, "ProgressBar" )
		self.cl.garageProgressBarBackground = FindWidget( self.cl.garageContentPanel, "ProgressBarBackground" )
		self.cl.garageResourceBase = PopBaseItem( self.cl.garageResourceHolder )
		adjustDivider( FindWidget( self.cl.garageContentPanel, "GarageDivider" ), self.cl.garageContentPanel.width )
		adjustDivider( FindWidget( self.cl.garagePlaceholderContentPanel, "GarageDivider" ), self.cl.garagePlaceholderContentPanel.width )

		self.cl.tabGroup = FindWidget( self.cl.mainPanel, "TabsHorizontalGroup" )
		self.cl.questTab = FindWidget( self.cl.tabGroup, Tabs.Quests )
		self.cl.dialogueTab = FindWidget( self.cl.tabGroup, Tabs.Dialogue )
		self.cl.logItemsTab = FindWidget( self.cl.tabGroup, Tabs.Items )
		self.cl.beaconsTab = FindWidget( self.cl.tabGroup, Tabs.Beacons )
		self.cl.garageTab = FindWidget( self.cl.tabGroup, Tabs.Garage )

		self.cl.currentTab = Tabs.Quests

		self.cl.mapReadLogs = { [LOGS.log_crashedship] = true }
		
		self.cl.updateLogItemGui = false
		self.cl.garagePreviewInitialized = false
		self.cl.updateQuestGui = false
		self.cl.updateDialogGui = false
		self.cl.updateGarageGui = false

		self.cl.highlightRequests = {}

		self.cl.notificationSound = sm.effect.createEffect2D( "Gui - LogbookNotification" )
    	self.cl.garageChestRevisions = {}

	end

	self:client_onRefresh()
end

function LogBook.client_onDestroy( self )
	if sm.exists( self.tool ) and self.tool:isLocal() then
		self.cl.notificationSound:destroy()
	end 
end

function LogBook.client_onRefresh( self )
	if self.tool:isEquipped() then
		self:cl_loadAnimations()
	end
end

function LogBook.client_onClientDataUpdate( self, data, channel )
	if not self.tool:isLocal() then
		return
	end
	self.cl.mapReadLogs = data.mapReadLogs
	for id, active in pairs( data.mapActiveBeacons ) do
		g_beaconManager:cl_setBeaconVisible( id, active )
	end
	self.cl.showFirstQuest = data.showFirstQuest

	for uuid, logData in pairs( self.cl.logData ) do
		if uuid == data.trackedLog then
			if logData.compassIcon and logData.location then
				local locations = LogEntryManager.Cl_GetLocations()
				if locations then
					local locationData = locations[logData.location]
					self:cl_toggleWaypoint( logData.location, locationData.pos, locationData.world, logData.compassIcon, logData.billboardIndex )
					self.cl.trackedLogUuid = uuid
					break
				end
			end
		end
	end
end

function LogBook.cl_checkLogRevision( self )
	if self.cl.logEntryRevision == nil or self.cl.logEntryRevision < LogEntryManager.Cl_GetLogRevision() then
		local anyUnread = false
		for _, uuid in ipairs( LogEntryManager.Cl_GetLogItems() ) do
			local read = self.cl.mapReadLogs[uuid] or false
			if read == false then
				anyUnread = true
				break
			end
		end
		if g_survivalHud then
			g_survivalHud:setVisible( "LogbookNotification", anyUnread )
		end

		if self.cl.logEntryRevision ~= nil and anyUnread then
			if not self.cl.notificationSound:isPlaying() then
				self.cl.notificationSound:start()
			end
		end
		self.cl.logEntryRevision = LogEntryManager.Cl_GetLogRevision()
		self.cl.updateLogItemGui = true
	end
end

function LogBook.cl_updateGui( self, force )
	local ticks = sm.game.getServerTick()
	local days = math.floor( ticks / (1440 * 40) )

	if self.cl.days ~= days then
		self.cl.days = days
		self.cl.daysText.Caption = tostring( days )
		self.cl.render = true
	end

	-- Time
	local fTimeOfDay = sm.game.getTimeOfDay()
	local hours = fTimeOfDay * 24;
	local minutes = (hours % 1) * 60;
	local hour1 = math.floor( hours / 10 );
	local hour2 = math.floor( (hours) - hour1 * 10 )
	local minute1 = math.floor( (minutes / 10) )
	local minute2 = math.floor( (minutes) - minute1 * 10 )

	local clockString = hour1 .. hour2 .. ":" .. minute1 .. minute2

	if self.cl.clock ~= clockString then
		self.cl.clock = clockString
		self.cl.timeText.Caption = clockString
		self.cl.render = true
	end

	if self.cl.questTrackerRevision == nil or self.cl.questTrackerRevision < QuestManager.Cl_getQuestTrackerRevision() then
		self.cl.questTrackerRevision = QuestManager.Cl_getQuestTrackerRevision()
		self.cl.updateQuestGui = true
	end

	local dialogPublicData = SafeNestedAccess( g_dialogManagerSelf, "scriptableObject", "clientPublicData" )
	if dialogPublicData then
		if dialogPublicData.completionRevision > 0 then
			if self.cl.dialogRevision == nil or dialogPublicData.completionRevision > self.cl.dialogRevision then
				self.cl.dialogRevision = dialogPublicData.completionRevision
				self.cl.updateDialogGui = true
			end
		end
	end

	self:cl_checkLogRevision()

	local garage = sm.garage.getGarage( GARAGE_IDS.SCRAP_CITY_GARAGE )
	local garageContainers = GarageImportManager.Cl_GetContainers( GARAGE_IDS.SCRAP_CITY_GARAGE )
	if garage and garageContainers then
		local revision = garage:getTrackingRevision()
		if self.cl.garageTrackingRevision == nil or revision ~= self.cl.garageTrackingRevision then
			self.cl.garageTrackingRevision = revision
			self.cl.updateGarageGui = true

			if self.cl.currentTab == Tabs.Garage or self.cl.currentTab == Tabs.GaragePlaceHolder then
				local hasActiveTracking = garage:hasActiveTracking()
				self.cl.garageContentPanel.Visible = hasActiveTracking
				self.cl.garagePlaceholderContentPanel.Visible = not hasActiveTracking
				self.cl.currentTab = hasActiveTracking and Tabs.Garage or Tabs.GaragePlaceHolder
			end
		end

		local currentRevisions = {}
		for _, container in ipairs( garageContainers ) do
			if sm.exists( container ) then
				local currentRevision = container:getRevision()
				if currentRevision ~= self.cl.garageChestRevisions[container.id] then
					self.cl.updateGarageGui = true
				end
				currentRevisions[container.id] = currentRevision
			end
		end
		self.cl.garageChestRevisions = currentRevisions
	end
	self:cl_updateLogItemGui( force )
	self:cl_updateBeaconGui( force )
	self:cl_updateQuestGui( force )
	self:cl_updateDialogGui( force )
	self:cl_updateGarageGui( force )
end

function LogBook.client_onEquippedUpdate( self, primaryState, secondaryState )
	if Chapter2VR then
		if Chapter2VR.markAdapter then Chapter2VR.markAdapter( "logbook" ) end
		if Chapter2VR.primaryState then primaryState = Chapter2VR.primaryState( self, primaryState ) end
	end
	if self.cl.equipped and self.tool:isLocal() then
		if not self.cl.jsonGui or (self.cl.jsonGui and not self.cl.jsonGui:isActive()) then
			sm.tool.forceTool( nil )
			self.cl.equipped = false
			if self.cl.jsonGui then
				self.cl.jsonGui:close()
			end
		else
			self:cl_updateGui()
		end
	elseif self.tool:isLocal() then
		-- checking log entries even when the logbook is closed, for notifications
		self:cl_checkLogRevision()
	end

	if self.cl.render == true and self.tool:isLocal() then
		self.cl.jsonGui:render( self.cl.guiData )
		if not self.cl.garagePreviewInitialized then
			local previewBox = self.cl.jsonGui:getWidget( "GaragePreview" )
			if previewBox then
				local garage = sm.garage.getGarage(GARAGE_IDS.SCRAP_CITY_GARAGE)
				if garage then 
					local name , data = garage:getTrackedBlueprint()
					if data and name then
						previewBox:setPreview( data )
					end
					self.cl.garagePreviewInitialized = true
				end
			end
		end
		self.cl.render = false
	end

	if not self.cl.equipped then
		if self.cl.wantsEquip then
			self.cl.wantsEquip = false
			self.cl.equipped = true
		end
	end

	return false, false
end

function LogBook.client_onUpdate( self, dt )
	-- First person animation
	local isCrouching = self.tool:isCrouching()

	if self.tool:isLocal() then
		updateFpAnimations( self.fpAnimations, self.cl.equipped, dt )
	end

	local crouchWeight = isCrouching and 1.0 or 0.0
	local normalWeight = 1.0 - crouchWeight
	local totalWeight = 0.0

	if not self.tpAnimations then
		return
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
			if animation.time >= animation.info.duration - self.cl.blendTime and not animation.looping then
				if (name == "putdown") then
					self.cl.equipped = false
				elseif animation.nextAnimation ~= "" then
					setTpAnimation( self.tpAnimations, animation.nextAnimation, 0.001 )
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
end

function LogBook.client_onEquip( self )
	local position = sm.localPlayer.getPlayer():getCharacter().worldPosition
	self.network:sendToServer( "sv_n_logBookOpened", position )
	self.cl.wantsEquip = true

	local currentRenderablesTp = {}
	concat( currentRenderablesTp, renderablesTp )
	concat( currentRenderablesTp, renderables )

	local currentRenderablesFp = {}
	concat( currentRenderablesFp, renderablesFp )
	concat( currentRenderablesFp, renderables )

	self.tool:setTpRenderables( currentRenderablesTp )

	if self.tool:isLocal() then
		self.tool:setFpRenderables( currentRenderablesFp )

		local dialogFeed = SafeNestedAccess( g_dialogManagerSelf, "scriptableObject", "clientPublicData", "dialogFeed" )
		self.cl.dialogueTab.Enabled = dialogFeed and #dialogFeed > 0 or false
		local beacons = g_beaconManager:cl_getBeacons()

		self.cl.beaconsTab.Enabled = next( beacons ) ~= nil or ProgressionManager.Cl_HasCraftedBeacon()
		local garage = sm.garage.getGarage( GARAGE_IDS.SCRAP_CITY_GARAGE )
		self.cl.garageTab.Enabled = garage ~= nil

		self:cl_updateGui( true )
		self:cl_onTabClicked( self.cl.currentTab )

		if self.cl.render == true then
			self.cl.jsonGui:render( self.cl.guiData )
			self.cl.render = false
		end
	end

	self:cl_loadAnimations()
	setTpAnimation( self.tpAnimations, "pickup", 0.0001 )

	if self.tool:isLocal() then
		swapFpAnimation( self.fpAnimations, "unequip", "equip", 0.2 )
	end
end

function LogBook.sv_n_playCloseSound( self, position )
	sm.effect.playEffect( "Logbook - Close", position )
	QuestManager.Sv_SendEvent( QuestEvent.LogBookClose )
end

function LogBook.client_onUnequip( self )
	local position = sm.localPlayer.getPlayer():getCharacter().worldPosition
	self.network:sendToServer( "sv_n_playCloseSound", position )
	self.cl.wantsEquip = false
	if sm.exists( self.tool ) then
		setTpAnimation( self.tpAnimations, "useExit" )
		if self.tool:isLocal() and self.fpAnimations.currentAnimation ~= "unequip" and self.fpAnimations.currentAnimation ~= "useExit" then
			swapFpAnimation( self.fpAnimations, "equip", "useExit", 0.2 )
		end
	end
	if self.cl.jsonGui then
		self.cl.jsonGui:close()
	end
end

function LogBook.cl_loadAnimations( self )
	-- TP
	self.tpAnimations = createTpAnimations(
		self.tool,
		{
			idle = { "logbook_use_idle", { looping = true } },
			sprint = { "logbook_sprint" },
			pickup = { "logbook_pickup", { nextAnimation = "useInto" } },
			putdown = { "logbook_putdown" },
			useInto = { "logbook_use_into", { nextAnimation = "idle" } },
			useExit = { "logbook_use_exit", { nextAnimation = "putdown" } }
		}
	)

	local movementAnimations = {
		idle = "logbook_use_idle",

		runFwd = "logbook_run_fwd",
		runBwd = "logbook_run_bwd",
		sprint = "logbook_sprint",

		jump = "logbook_jump",
		jumpUp = "logbook_jump_up",
		jumpDown = "logbook_jump_down",

		land = "logbook_jump_land",
		landFwd = "logbook_jump_land_fwd",
		landBwd = "logbook_jump_land_bwd",
		landLeft = "logbook_jump_land_left",
		landRight = "logbook_jump_land_right",

		crouchIdle = "logbook_crouch_idle",
		crouchFwd = "logbook_crouch_fwd",
		crouchBwd = "logbook_crouch_bwd"
	}

	for name, animation in pairs( movementAnimations ) do
		self.tool:setMovementAnimation( name, animation )
	end

	if self.tool:isLocal() then
		-- FP
		self.fpAnimations = createFpAnimations(
			self.tool,
			{
				idle = { "logbook_use_idle", { looping = true } },
				equip = { "logbook_pickup", { nextAnimation = "useInto" } },
				unequip = { "logbook_putdown" },
				useInto = { "logbook_use_into", { nextAnimation = "idle" } },
				useExit = { "logbook_use_exit", { nextAnimation = "unequip" } }
			}
		)
	end

	setTpAnimation( self.tpAnimations, "idle", 5.0 )
	self.cl.blendTime = 0.2
end

-- Bubble heights depend only on immutable dialog text and the fixed bubble widget width,
-- so measurements are cached per (uuid, entryName) and shared by pagination and rendering.
function LogBook.cl_getDialogEntryMeasurement( self, feedEntry )
	local uuidString = tostring( feedEntry.uuid )
	local cacheKey = uuidString .. "|" .. tostring( feedEntry.entryName )
	local cached = self.cl.dialogMeasureCache[cacheKey]
	if cached then
		return cached
	end

	local dialogSet = self.cl.dialogSets[uuidString]
	local entry = dialogSet and dialogSet.entriesByName[feedEntry.entryName]
	if not entry then
		return nil
	end

	local bubbles = {}
	local totalHeight = self.cl.dialogFeedDividerHeight
	for _, bubbleText in ipairs( entry.bubbles ) do
		local _, textHeight = sm.gui.computeTextSize( bubbleText, self.cl.dialogBubbleTextFont, self.cl.dialogBubbleTextAlign, self.cl.dialogBubbleTextWidth )
		local expansion = math.max( 0, textHeight - self.cl.defaultSpeechBubbleTextHeight )
		local bubbleHeight = self.cl.defaultSpeechBubbleHeight + expansion
		bubbles[#bubbles + 1] = { bubbleHeight = bubbleHeight, feedBubbleHeight = bubbleHeight + self.cl.dialogFeedBubbleSpacing }
		totalHeight = totalHeight + self.cl.defaultSpeechBubbleHeight + expansion + self.cl.dialogFeedBubbleSpacing
	end

	local measurement = { totalHeight = totalHeight, bubbles = bubbles }
	self.cl.dialogMeasureCache[cacheKey] = measurement
	return measurement
end

function LogBook.cl_measureDialogEntryHeight( self, feedEntry )
	local measurement = self:cl_getDialogEntryMeasurement( feedEntry )
	return measurement and measurement.totalHeight or 0
end

function LogBook.cl_recomputeDialogPages( self, feed )
	local pages = {}
	local usable = self.cl.dialogPageHeight
	local i = 1
	while i <= #feed do
		local pageStart = i
		local used = 0
		while i <= #feed do
			local entryHeight = self:cl_measureDialogEntryHeight( feed[i] )
			if used > 0 and used + entryHeight > usable then
				break
			end
			used = used + entryHeight
			i = i + 1
		end
		if i == pageStart then -- a single entry taller than a page still gets its own page
			i = pageStart + 1
		end
		pages[#pages + 1] = { first = pageStart, last = i - 1 }
	end
	self.cl.dialogPages = pages
end

function LogBook.cl_updateDialogPageDots( self )
	local pageCount = #self.cl.dialogPages

	for i = 1, pageCount do
		local dot
		if self.cl.dialogIndicators.Childs[i] then
			dot = self.cl.dialogIndicators.Childs[i]
		else
			dot = DeepCopy( self.cl.dialogPageDotBase )
			self.cl.dialogIndicators.Childs[#self.cl.dialogIndicators.Childs + 1] = dot
		end
		dot.Visible = true
		dot.Skin = ( i == self.cl.dialogPageIndex ) and "Handbook_Pageindicator_Selected" or "Handbook_Pageindicator_Default"
	end

	while #self.cl.dialogIndicators.Childs > pageCount do
		table.remove( self.cl.dialogIndicators.Childs )
	end

	self.cl.dialogArrowLeft.Enabled = self.cl.dialogPageIndex > 1
	self.cl.dialogArrowRight.Enabled = self.cl.dialogPageIndex < pageCount
	self.cl.dialogNavigationContainer.Visible = ( self.cl.currentTab == "DialogueTab" ) and pageCount > 1
end

function LogBook.cl_updateDialogGui( self, force )
	if self.cl.updateDialogGui or force then
		local dialogPublicData = SafeNestedAccess( g_dialogManagerSelf, "scriptableObject", "clientPublicData" )
		if not dialogPublicData then
			return
		end

		local feed = dialogPublicData.dialogFeed or {}

		self:cl_recomputeDialogPages( feed )
		local pageCount = #self.cl.dialogPages

		if #feed ~= self.cl.dialogLastFeedCount then -- jump to the newest page when the feed changes
			self.cl.dialogPageIndex = math.max( 1, pageCount )
		end
		self.cl.dialogLastFeedCount = #feed
		self.cl.dialogPageIndex = math.min( math.max( self.cl.dialogPageIndex, 1 ), math.max( 1, pageCount ) )

		while #self.cl.dialogContentPanel.Childs > 0 do -- rebuild the feed
			table.remove( self.cl.dialogContentPanel.Childs )
		end

		local page = self.cl.dialogPages[self.cl.dialogPageIndex]
		if page then
			local renderedAny = false
			for i = page.first, page.last do
				local feedEntry = feed[i]
				local uuidString = tostring( feedEntry.uuid )
				local dialogSet = self.cl.dialogSets[uuidString]
				local entry = dialogSet and dialogSet.entriesByName[feedEntry.entryName]
				if entry and #entry.bubbles > 0 then
					if renderedAny then -- no divider above the first rendered entry on the page
						local divider = DeepCopy( self.cl.dialogFeedDividerBase )
						self.cl.dialogContentPanel.Childs[#self.cl.dialogContentPanel.Childs + 1] = divider
					end
					renderedAny = true

					local measurement = self:cl_getDialogEntryMeasurement( feedEntry )
					local speakerImage = SpeakerToImage[entry.speaker or dialogSet.speaker]
					for bubbleIndex, bubbleText in ipairs( entry.bubbles ) do
						local feedBubble = DeepCopy( self.cl.dialogFeedBubbleBase )
						local text = FindWidget( feedBubble, "SpeechBubbleText" )
						text.Caption = bubbleText

						local bubbleMeasurement = measurement.bubbles[bubbleIndex]
						local bubble = FindWidget( feedBubble, "SpeechBubble" )
						bubble.height = bubbleMeasurement.bubbleHeight
						feedBubble.height = bubbleMeasurement.feedBubbleHeight

						local image = FindWidget( feedBubble, "SpeakerImage" )
						image.ImageTexture = speakerImage

						self.cl.dialogContentPanel.Childs[#self.cl.dialogContentPanel.Childs + 1] = feedBubble
					end
				elseif dialogSet == nil then
					print( "no dialog was found for uuid:", uuidString )
				end
			end
		end

		self:cl_updateDialogPageDots()

		self.cl.updateDialogGui = false
		self.cl.render = true
	end
end

function LogBook.cl_dialogPrevPage( self )
	if self.cl.dialogPageIndex > 1 then
		self.cl.dialogPageIndex = self.cl.dialogPageIndex - 1
		self.cl.updateDialogGui = true
		self:cl_updateDialogGui()
	end
end

function LogBook.cl_dialogNextPage( self )
	if self.cl.dialogPageIndex < #self.cl.dialogPages then
		self.cl.dialogPageIndex = self.cl.dialogPageIndex + 1
		self.cl.updateDialogGui = true
		self:cl_updateDialogGui()
	end
end


function LogBook.cl_updateBeaconGui( self, forceUpdate )
	local beacons = g_beaconManager:cl_getBeacons()
	local beaconCount = 0
	for _ in pairs( beacons ) do
		beaconCount = beaconCount + 1
	end

	if not forceUpdate then
		if self.cl.beaconContentPanel.Visible == false or (beaconCount == 0 and #self.cl.beaconContentPanel.Childs == 1) then
			return
		end
	end

	local player = self.tool:getOwner()
	assert( player )
	local character = player:getCharacter()
	if not character then
		return
	end

	beaconCount = 0
	for beaconId, beacon in pairs( beacons ) do
		beaconCount = beaconCount + 1

		local beaconBase
		if self.cl.beaconContentPanel.Childs[beaconCount + 1] then
			beaconBase = self.cl.beaconContentPanel.Childs[beaconCount + 1]
		else
			beaconBase = DeepCopy( self.cl.beaconBase )
			self.cl.beaconContentPanel.Childs[#self.cl.beaconContentPanel.Childs + 1] = beaconBase
		end

		local settings = g_beaconManager:cl_getBeaconSettings( beacon.shape:getId() )

		local beaconImage = FindWidget( beaconBase, "BeaconIconImage" )
		beaconImage.ImageResource = "BeaconIconMap"
		beaconImage.ImageGroup = "BeaconIconMap"
		beaconImage.ImageName = tostring( beacon.iconData.iconIndex )
		beaconImage.Colour = BEACON_COLORS[beacon.iconData.colorIndex]:getGuiColorStr()

		local beaconDistanceText = FindWidget( beaconBase, "BeaconDistanceText" )
		local beaconPosition = WorldMarkerManager.Cl_GetMarkerWorldPosition( beacon.markerName )
		if beaconPosition == nil then
			sm.log.error( "WorldMarkerManager does not know the position of a beacon marker: ", beacon.markerName )
			beaconDistanceText.Caption = "--- m"
		else
			local characterPos = character:getWorldPosition()
			-- Round to nearest to match the compass HUD, which formats the same distance with "%.0f".
			local distance = math.floor( (characterPos - beaconPosition):length() + 0.5 )
			beaconDistanceText.Caption = tostring( distance ) .. " m"
		end

		local beaconToggleButton = FindWidget( beaconBase, "BeaconToggleButton" )
		if settings.visible then
			beaconToggleButton.Skin = "LogbookQuestVisibilityShow"
		else
			beaconToggleButton.Skin = "LogbookQuestVisibilityHide"
		end
		beaconToggleButton.onClick = "cl_onBeaconVisibilityClicked"
		beaconToggleButton.onClickData = { beaconShapeId = beacon.shape:getId() }
	end

	while #self.cl.beaconContentPanel.Childs - 1 > beaconCount do
		table.remove( self.cl.beaconContentPanel.Childs )
	end

	self.cl.render = true
end

-- Control station keys are superseded by later tiers — hide earlier ones from the log list.
local SupersededLogs = {
	[LOGS.log_accesscard_01] = LOGS.log_accesscard_02,
	[LOGS.log_accesscard_02] = LOGS.log_accesscard_03,
	[LOGS.log_accesscard_03] = LOGS.log_accesscard_04,
}

function LogBook.cl_updateLogItemGui( self, forceUpdate )
	if self.cl.updateLogItemGui or forceUpdate then
		local anyUnread = false
		local logItemCount = 0
		for _, uuid in ipairs( LogEntryManager.Cl_GetLogItems() ) do
			-- Skip log entries flagged hidden (e.g. internal keys that gate progression but should not be listed)
			if self.cl.logData[uuid] and self.cl.logData[uuid].hidden then
				goto continue
			end
			-- Skip log entries that are superseded by a later tier the player already has
			local supersededBy = SupersededLogs[uuid]
			if supersededBy and LogEntryManager.Cl_HasLog( supersededBy ) then
				goto continue
			end
			logItemCount = logItemCount + 1

			local logItemBase
			if self.cl.logItemGrid.Childs[logItemCount] then
				logItemBase = self.cl.logItemGrid.Childs[logItemCount]
			else
				logItemBase = DeepCopy( self.cl.logItemBase )
				self.cl.logItemGrid.Childs[#self.cl.logItemGrid.Childs + 1] = logItemBase
			end

			logItemBase.onClick = "cl_selectLog"
			logItemBase.onClickData = { uuid = uuid }
			logItemBase.StateSelected = self.cl.selectedLog ~= nil and uuid == self.cl.selectedLog

			local itemReadImage = FindWidget( logItemBase, "ReadExclamation" )
			local read = self.cl.mapReadLogs[uuid] or false
			itemReadImage.Visible = not read
			if not read then
				local itemEffects = FindWidget( logItemBase, "LogItemEffectBox" )
				itemEffects.Effects[1].PlayState = "Auto playing"
			else
				local itemEffects = FindWidget( logItemBase, "LogItemEffectBox" )
				itemEffects.Effects[1].PlayState = "Stopped"
			end
			if read == false then
				anyUnread = true
			end

			local itemTitle = FindWidget( logItemBase, "LogItemTitle" )
			itemTitle.Caption = self.cl.logData[uuid].title


			local itemImage = FindWidget( logItemBase, "LogItemImage" )
			local image = self.cl.logData[uuid].mainPanelImage or "gui_logbook_icon_photo.png"
			itemImage.ImageTexture = image
			::continue::
		end

		while #self.cl.logItemGrid.Childs > logItemCount do
			table.remove( self.cl.logItemGrid.Childs )
		end

		if self.cl.updateLogItemGui then
			if self.cl.photoLogSidePanel.Visible then
				self:cl_updateLogSidePanel( self.cl.selectedLog )
				self:cl_refreshLogItemSidePanelVisibility()
			end
		end

		self.cl.updateLogItemGui = false

		self.cl.render = true

		if g_survivalHud then
			g_survivalHud:setVisible( "LogbookNotification", anyUnread )
		end
	end
end

function LogBook.cl_updateQuestGui( self, forceUpdate )
	if self.cl.updateQuestGui or forceUpdate then
		local activeQuests = QuestManager.Cl_GetActiveQuests()
		local trackedQuests = QuestManager.Cl_GetTrackedQuests()

		local questCount = 0
		for questName, quest in pairs( activeQuests ) do
			local showQuest = true
			if quest.clientPublicData and quest.clientPublicData.hidden then
				showQuest = false
			end
			if showQuest and sm.exists( quest ) then
				questCount = questCount + 1

				local questBase
				if self.cl.activeQuestPanel.Childs[questCount] then
					questBase = self.cl.activeQuestPanel.Childs[questCount]
				else
					questBase = DeepCopy( self.cl.questBase )
					self.cl.activeQuestPanel.Childs[#self.cl.activeQuestPanel.Childs + 1] = questBase
				end

				local questData = QuestManager.Cl_GetQuestData( questName )
				if not questData then
					questData = { title = "", mainQuest = false, farmerQuest = false }
				end

				questBase.onClick = "cl_selectQuest"
				questBase.onClickData = { questName = questName }
				questBase.StateSelected = questName == self.cl.selectedQuest
				if self.cl.showFirstQuest and questName == "quest_tutorial" then
					self:cl_selectQuest( questBase, { questName = questName } )
					self.cl.showFirstQuest = false
				end

				local questTitle = FindWidget( questBase, "QuestTitle" )
				questTitle.Caption = questData.title

				local questIcon = FindWidget( questBase, "QuestIcon" )
				local questVisibilityButton = FindWidget( questBase, "QuestVisibilityButton" )

				if trackedQuests[questName] ~= nil then
					questVisibilityButton.onClick = "cl_onClickQuestVisibilityHide"
				else
					questVisibilityButton.onClick = "cl_onClickQuestVisibilityShow"
				end
				questVisibilityButton.onClickData = { questName = questName }
				if questData.farmerQuest then
					questIcon.ImageTexture = "gui_logbook_quest_icon_hideout.png"
					questBase.Skin = "LogbookQuestFarmerBG"
					if trackedQuests[questName] ~= nil then
						questVisibilityButton.Skin = "LogbookQuestVisibilityShow"
					else
						questVisibilityButton.Skin = "LogbookQuestVisibilityHide"
					end
				elseif questData.mainQuest then
					questIcon.ImageTexture = "gui_logbook_quest_icon_main.png"
					questBase.Skin = "LogbookQuestMainBG"
					if trackedQuests[questName] ~= nil then
						questVisibilityButton.Skin = "LogbookQuestVisibilityShow"
					else
						questVisibilityButton.Skin = "LogbookQuestVisibilityHide"
					end
				else
					questIcon.ImageTexture = "gui_logbook_quest_icon_side.png"
					questBase.Skin = "LogbookQuestSideBG"
					if trackedQuests[questName] ~= nil then
						questVisibilityButton.Skin = "LogbookQuestVisibilityShow"
					else
						questVisibilityButton.Skin = "LogbookQuestVisibilityHide"
					end
				end
			end
		end

		while #self.cl.activeQuestPanel.Childs > questCount do
			table.remove( self.cl.activeQuestPanel.Childs )
		end

		local hasActiveQuests = questCount > 0
		self.cl.activeQuestPanel.height = hasActiveQuests and self.cl.activeQuestPanelHeight or 0
		self.cl.activeQuestPanel.Visible = hasActiveQuests
		self.cl.activeQuestDivider.height = hasActiveQuests and self.cl.activeQuestDividerHeight or 0
		self.cl.activeQuestDivider.Visible = hasActiveQuests

		local mainQuests = {}
		local traderQuests = {}
		local sideQuests = {}

		-- Sorting priority
		for index, questBase in ipairs( self.cl.activeQuestPanel.Childs ) do
			if questBase.Skin == "LogbookQuestMainBG" then
				mainQuests[#mainQuests + 1] = questBase
			elseif questBase.Skin == "LogbookQuestFarmerBG" then
				traderQuests[#traderQuests + 1] = questBase
			elseif questBase.Skin == "LogbookQuestSideBG" then
				sideQuests[#sideQuests + 1] = questBase
			end
		end

		for index, _ in ipairs( self.cl.activeQuestPanel.Childs ) do
			if not IsEmptyTable( mainQuests ) then
				self.cl.activeQuestPanel.Childs[index] = mainQuests[1]
				table.remove( mainQuests, 1 )
			elseif not IsEmptyTable( traderQuests ) then
				self.cl.activeQuestPanel.Childs[index] = traderQuests[1]
				table.remove( traderQuests, 1 )
			elseif not IsEmptyTable( sideQuests ) then
				self.cl.activeQuestPanel.Childs[index] = sideQuests[1]
				table.remove( sideQuests, 1 )
			end
		end

		local completedQuests = QuestManager.Cl_GetCompletedQuests()
		questCount = 0
		for _, questName in reverse_ipairs( completedQuests ) do
			questCount = questCount + 1

			local questBase
			if self.cl.completedQuestPanel.Childs[questCount] then
				questBase = self.cl.completedQuestPanel.Childs[questCount]
			else
				questBase = DeepCopy( self.cl.questBase )
				self.cl.completedQuestPanel.Childs[#self.cl.completedQuestPanel.Childs + 1] = questBase
			end

			questBase.onClick = "cl_selectQuest"
			questBase.onClickData = { questName = questName }

			local questData = QuestManager.Cl_GetQuestData( questName )
			if questData and questData.title then
				local questTitle = FindWidget( questBase, "QuestTitle" )
				questTitle.Caption = questData.title
			end

			local questIcon = FindWidget( questBase, "QuestIcon" )
			local questVisibilityButton = FindWidget( questBase, "QuestVisibilityButton" )
			questVisibilityButton.Visible = false
			local questCompletedCheck = FindWidget( questBase, "QuestCompletedCheck" )
			questCompletedCheck.Visible = true
			if questData and questData.farmerQuest then
				questIcon.ImageTexture = "gui_logbook_quest_icon_hideout.png"
			elseif questData and questData.mainQuest then
				questIcon.ImageTexture = "gui_logbook_quest_icon_main.png"
			else
				questIcon.ImageTexture = "gui_logbook_quest_icon_side.png"
			end
			questBase.Skin = "LogbookQuestCompleteBG"
		end

		while #self.cl.completedQuestPanel.Childs > questCount do
			table.remove( self.cl.completedQuestPanel.Childs )
		end

		local hasCompletedQuests = questCount > 0
		self.cl.completedQuestDivider.height = hasCompletedQuests and self.cl.completedQuestDividerHeight or 0
		self.cl.completedQuestDivider.Visible = hasCompletedQuests

		self:cl_updateQuestSidePanel( self.cl.selectedQuest )
		self:cl_refreshQuestSidePanelVisibility()

		self.cl.updateQuestGui = false
		self.cl.render = true
	end
end

function LogBook.cl_updateQuestSidePanel( self, questName )
	local questSob = QuestManager.Cl_GetActiveQuest( questName )
	local questData = QuestManager.Cl_GetQuestData( questName )
	local isCompletedQuest = not sm.exists( questSob )
	if isCompletedQuest then
		questSob = nil
	end
	if not questData then
		return
	end

	local mainQuest = questData.mainQuest
	local title = questData.title

	if mainQuest then
		self.cl.questSidePanelIcon.ImageTexture = "gui_logbook_quest_icon_main.png"
	else
		self.cl.questSidePanelIcon.ImageTexture = "gui_logbook_quest_icon_side.png"
	end

	self.cl.questDescriptionScrollView.Visible = true
	self.cl.builderGuideHolder.Visible = false
	
	if questSob and questSob.clientPublicData and questSob.clientPublicData.farmerQuest then
		local reward = questSob.clientPublicData.reward
		if reward then
			if reward.unlockCosmetic then
				self.cl.farmerSidePanelRewardImage.ImageResource = "CustomizationIconMap"
				self.cl.farmerSidePanelRewardImage.ImageGroup = "CustomizationIconMap"
				self.cl.farmerSidePanelRewardImage.ImageName = tostring( reward.item ).."_male"
			else
				local rewardResource, rewardGroup, rewardName = sm.gui.getItemIconFromUuid( reward.item )
				self.cl.farmerSidePanelRewardImage.ImageResource = rewardResource
				self.cl.farmerSidePanelRewardImage.ImageGroup = rewardGroup
				self.cl.farmerSidePanelRewardImage.ImageName = rewardName
			end
		end
		local imageTexture = resourceToImage( questSob.clientPublicData.resource )
		self.cl.farmerSideResourceImage.ImageTexture = imageTexture
		self.cl.farmerSidePanelTitle.Caption = questData.title
		self.cl.farmerScrollView.Childs = {}
		self.cl.farmerScrollView.Childs[1] = DeepCopy( self.cl.farmerImageBase )
		self.cl.farmerScrollView.Childs[2] = DeepCopy( self.cl.farmerDescriptionBase )
		if questSob.clientPublicData.trackerData then
			self.cl.farmerScrollView.Childs[2].Caption = questSob.clientPublicData.trackerData[1].text
			for i = 2, #questSob.clientPublicData.trackerData do
				local trackerBase = DeepCopy( self.cl.farmerTaskBase )
				trackerBase.Visible = true
				if questSob.clientPublicData.trackerData[i].complete then
					trackerBase.Childs[1].ImageTexture = "Quest/icon_checkmark_active.png"
				else
					trackerBase.Childs[1].ImageTexture = "Quest/icon_checkmark_default.png"
				end
				trackerBase.Childs[2].Caption = questSob.clientPublicData.trackerData[i].text
				self.cl.farmerScrollView.Childs[#self.cl.farmerScrollView.Childs + 1] = trackerBase
			end
		end
		self.cl.questSidePanelIcon.ImageTexture = "gui_logbook_quest_icon_hideout.png"
		self.cl.render = true
		return
	elseif questSob and questSob.clientPublicData and questSob.clientPublicData.blueprintPath then
		self.cl.questDescriptionScrollView.Visible = false

		self.cl.builderGuideHolder.Visible = true
		self.cl.builderGuideScrollView.Childs = {}
		self.cl.builderGuideDescription.Caption = questData.builderGuideDescription or ""
		local content = sm.creation.getBlueprintCost( questSob.clientPublicData.blueprintPath )
		local playerInventory = sm.localPlayer.getPlayer():getInventory()
		for _, shapeData in ipairs( content ) do
			local resource, group, name, keyItem, type = sm.gui.getItemIconFromUuid( shapeData.uuid )
			local playerCount = sm.container.totalQuantity( playerInventory, shapeData.uuid )
			self.cl.builderGuideScrollBaseItem.Childs[1].ImageResource = resource
			self.cl.builderGuideScrollBaseItem.Childs[1].ImageGroup = group
			self.cl.builderGuideScrollBaseItem.Childs[1].ImageName = name
			local itemTitle = sm.shape.getShapeTitle( shapeData.uuid )
			self.cl.builderGuideScrollBaseItem.ToolTip.Text = itemTitle
			self.cl.builderGuideScrollBaseItem.Childs[2].Caption = FormatGuiNumberCount( playerCount, shapeData.quantity )
			self.cl.builderGuideScrollBaseItem.Childs[3].ImageName = type
			self.cl.builderGuideScrollView.Childs[#self.cl.builderGuideScrollView.Childs + 1] = DeepCopy( self.cl.builderGuideScrollBaseItem )
		end
	end

	self.cl.questSidePanelTitle.Caption = title

	local hasPhoto = questData.photo ~= nil
	if hasPhoto then
		if self.cl.questDescriptionScrollView.Childs[1].Name ~= "QuestDescriptionPhotoPanel" then
			table.insert( self.cl.questDescriptionScrollView.Childs, 1, self.cl.questDescriptionPhotoPanelBase )
		end
		local photoPanel = self.cl.questDescriptionScrollView.Childs[1]

		local photo = FindWidget( photoPanel, "QuestDescriptionPhoto" )
		photo.ImageTexture = questData.photo
		self.cl.questDescriptionScrollView.Childs[1].ImageTexture = questData.photo
	else
		if self.cl.questDescriptionScrollView.Childs[1].Name == "QuestDescriptionPhotoPanel" then
			table.remove( self.cl.questDescriptionScrollView.Childs, 1 )
		end
	end
	if hasPhoto then
		if self.cl.questDescriptionScrollView.Childs[2] and self.cl.questDescriptionScrollView.Childs[2].Name == "QuestDescriptionTextBox" then
			table.remove( self.cl.questDescriptionScrollView.Childs, 2 )
		end
	else
		if self.cl.questDescriptionScrollView.Childs[1] and self.cl.questDescriptionScrollView.Childs[1].Name == "QuestDescriptionTextBox" then
			table.remove( self.cl.questDescriptionScrollView.Childs, 1 )
		end
	end

	local setRewardImage = function( reward, widget )
		if reward then
			local questRewardIcon1 = FindWidget( widget, "Image" )
			if reward.type == "additionalReward" then
				questRewardIcon1.ImageResource = "CustomizationIconMap"
				questRewardIcon1.ImageGroup = "CustomizationIconMap"
				questRewardIcon1.ImageName = tostring( CUSTOMIZATIONS[reward.name] ) .. "_male"
				questRewardIcon1.ImageTexture = nil
				widget.Visible = true
			elseif reward.type == "image" then
				questRewardIcon1.ImageResource = nil
				questRewardIcon1.ImageGroup = nil
				questRewardIcon1.ImageName = nil
				questRewardIcon1.ImageTexture = reward.name
				widget.Visible = questRewardIcon1.ImageTexture ~= nil
			elseif reward.type == "item" or reward.type == "schematic" then
				local resource, group, name = sm.gui.getItemIconFromUuid( ITEMS[reward.name] )
				questRewardIcon1.ImageResource = resource
				questRewardIcon1.ImageGroup = group
				questRewardIcon1.ImageName = name
				questRewardIcon1.ImageTexture = nil
				widget.Visible = true
			elseif reward.type == "log" then
				local logData = LogEntryManager.Cl_GetLogData( tostring( reward.uuid ) )
				questRewardIcon1.ImageResource = nil
				questRewardIcon1.ImageGroup = nil
				questRewardIcon1.ImageName = nil
				questRewardIcon1.ImageTexture = logData and logData.mainPanelImage or nil
				widget.Visible = questRewardIcon1.ImageTexture ~= nil
			else
				sm.log.error( "Unknown reward type in quest reward:", reward.type )
			end
		else
			widget.Visible = false
		end
	end

	if questData.rewards and not IsEmptyTable( questData.rewards ) then
		self.cl.questRewardText.Visible = true
		setRewardImage( questData.rewards[1], self.cl.questReward1 )
		setRewardImage( questData.rewards[2], self.cl.questReward2 )
		self.cl.questDescriptionScrollView.height = self.cl.questDescScrollHeight
	elseif questSob and questSob.clientPublicData and questSob.clientPublicData.reward then
		self.cl.questRewardText.Visible = true
		setRewardImage( questSob.clientPublicData.reward, self.cl.questReward1 )
		self.cl.questReward2.Visible = false
		self.cl.questDescriptionScrollView.height = self.cl.questDescScrollHeight
	else
		self.cl.questRewardText.Visible = false
		self.cl.questReward1.Visible = false
		self.cl.questReward2.Visible = false
		self.cl.questDescriptionScrollView.height = self.cl.questDescScrollHeight + NoRewardScrollPadding
	end

	local taskCount = 0
	if (questSob and questSob.clientPublicData) or isCompletedQuest then
		local data = {}

		if questSob and questSob.clientPublicData.completedData then
			for _, completedData in ipairs( questSob.clientPublicData.completedData ) do
				data[#data + 1] = { text = completedData, complete = true }
			end
		end
		if isCompletedQuest then
			if questData.progressSteps then
				data = questData.progressSteps
			elseif questData.trackingSteps then
				local trackingSteps = questData.trackingSteps
				local sortList = {}
				for _, stepData in pairs( trackingSteps ) do
					sortList[#sortList + 1] = { text = stepData.text, index = stepData.index, binding = stepData.binding }
				end
				table.sort( sortList, function( a, b ) return a.index < b.index end )
				for _, stepData in ipairs( sortList ) do
					data[#data + 1] = { text = stepData.text, binding = stepData.binding }
				end
			end
		elseif questSob and questSob.clientPublicData.trackerData then
			for _, trackingData in reverse_ipairs( questSob.clientPublicData.trackerData ) do
				data[#data + 1] = trackingData
			end
		elseif questSob and questSob.clientPublicData.progressString then
			data[#data + 1] = { text = questSob.clientPublicData.progressString, complete = false }
		end

		local counter = 1
		for i, trackerData in reverse_ipairs( data ) do
			if i > taskCount then
				taskCount = i
			end
			if isCompletedQuest then
				trackerData.complete = true
			end
			local taskBase
			if self.cl.questSidePanelTaskPanel.Childs[counter] then
				taskBase = self.cl.questSidePanelTaskPanel.Childs[counter]
			else
				taskBase = DeepCopy( self.cl.questSidePanelTaskBase )
				self.cl.questSidePanelTaskPanel.Childs[#self.cl.questSidePanelTaskPanel.Childs + 1] = taskBase
			end

			local taskCounterText = FindWidget( taskBase, "TaskCountText" )
			taskCounterText.Visible = false

			local taskText = FindWidget( taskBase, "TaskText" )

			local checkmark = FindWidget( taskBase, "QuestTaskCheckmark" )

			if trackerData.complete == nil then
				checkmark.Visible = false
				taskText.x = checkmark.x
			else
				taskText.x = 27
				taskText.width = taskBase.width - 27
				checkmark.Visible = true
				if trackerData.complete == true then
					checkmark.ImageTexture = "icon_checkmark_active.png"
				elseif trackerData.complete == false then
					checkmark.ImageTexture = "icon_checkmark_default.png"
				end
			end

			if trackerData.binding then
				taskText.Caption = string.format( sm.gui.translateLocalizationTags( trackerData.text ), sm.gui.getKeyBinding( trackerData.binding, false ) )
			else
				taskText.Caption = trackerData.text
			end
			local _, calculatedTextHeight = sm.gui.computeTextSize( taskText.Caption, taskText.FontName, taskText.TextAlign, taskText.width )
			if taskBase.height <= taskText.y + calculatedTextHeight then
				taskBase.height = taskText.y + calculatedTextHeight
			else
				taskBase.height = 26
			end
			counter = counter + 1
		end
	end

	while #self.cl.questSidePanelTaskPanel.Childs > taskCount do
		table.remove( self.cl.questSidePanelTaskPanel.Childs )
	end

	self.cl.render = true
end

function LogBook.cl_updateGarageGui( self, forceupdate )
	local garage = sm.garage.getGarage(GARAGE_IDS.SCRAP_CITY_GARAGE)
	local garageContainers = GarageImportManager.Cl_GetContainers( GARAGE_IDS.SCRAP_CITY_GARAGE )
	if not garage or not garageContainers then
		return
	end
	if self.cl.updateGarageGui or forceupdate then
		self.cl.garageResourceHolder.Childs = {}
		local name,data = garage:getTrackedBlueprint()
		self.cl.garagePreviewInitialized = false
		if data ~= nil then
			local ItemBase = self.cl.garageResourceBase

			local ItemQuantityWidget = FindWidget( ItemBase, "Quantity" )
			local ItemImageWidget = FindWidget( ItemBase, "Image" )
			local ItemTypeWidget = FindWidget( ItemBase, "Type" )
			self.cl.garageProgressBar.Visible = true
			self.cl.garageProgressBarBackground.Visible = true
			self.cl.garageShowcaseTitle.Caption = name
			local content = sm.creation.getBlueprintCost( data )
			local totalCount = 0
			local currentCount = 0
			
	
			ItemBase.ToolTip = {}

			for _, shapeData in ipairs( content ) do
				local quantityInContainers = 0
				for _, container in pairs( garageContainers ) do
					if sm.exists( container ) then
						quantityInContainers = quantityInContainers + sm.container.totalQuantity( container, shapeData.uuid )
					end
				end
				local resource, group, name, keyItem, type = sm.gui.getItemIconFromUuid( shapeData.uuid )
				ItemImageWidget.ImageResource = resource
				ItemImageWidget.ImageGroup = group
				ItemImageWidget.ImageName = name
				local title = sm.shape.getShapeTitle( shapeData.uuid )
				ItemBase.ToolTip.Text = title
				ItemQuantityWidget.Caption = FormatGuiNumberCount( quantityInContainers, shapeData.quantity )
				ItemTypeWidget.ImageName = type
				totalCount = totalCount + shapeData.quantity
				currentCount = currentCount + math.min( quantityInContainers, shapeData.quantity )
				self.cl.garageResourceHolder.Childs[#self.cl.garageResourceHolder.Childs + 1] = DeepCopy( ItemBase )
			end
			local fraction = totalCount > 0 and currentCount / totalCount or 1
			self.cl.garageProgressBar.width = math.floor( fraction * self.cl.garageProgressBarBackground.width * 0.96 )
		else
			self.cl.garageProgressBar.Visible = false
			self.cl.garageProgressBarBackground.Visible = false
			self.cl.garageShowcaseTitle.Caption = ""
			self.cl.garagePreview.BlueprintPreview = nil
		end
		self.cl.render = true
		self.cl.updateGarageGui = false
	end
end

function LogBook.cl_selectQuest( self, widgetName, data )
	self.cl.questSidePanel.Visible = true
	self.cl.selectedQuest = data.questName
	
	for _, questBase in ipairs( self.cl.activeQuestPanel.Childs ) do
		questBase.StateSelected = questBase.onClickData ~= nil and questBase.onClickData.questName == self.cl.selectedQuest
	end
	self.cl.render = true

	self:cl_updateQuestSidePanel( data.questName )
	self:cl_refreshQuestSidePanelVisibility()
end

function LogBook.cl_updateLogSidePanel( self, uuid )
	if uuid == nil then
		return
	end

	local logData = self.cl.logData[uuid]

	self.cl.photoLogSidePanelDescription.Caption = logData.description
	self.cl.photoLogSidePanelTitle.Caption = logData.title

	self.cl.audioLogSidePanelTitle.Caption = logData.title
	self.cl.audioLogSidePanelDescription.Caption = logData.description
	self.cl.audioLogSidePanelButton.onClickData = { dialogUuid = logData.dialogUuid, entry = logData.entry }

	if logData.location then
		self.cl.photoLogSidePanelWaypointButton.onClick = "cl_onWaypointClicked"
		self.cl.photoLogSidePanelWaypointButton.onClickData = { uuid = uuid }
		self.cl.photoLogSidePanelWaypointButton.Visible = true
		self.cl.photoLogSidePanelWaypointButtonCaption.Caption = "#{LOGBOOK_SET_WAYPOINT}"
	elseif logData.whistle then
		self.cl.photoLogSidePanelWaypointButton.onClick = "cl_onWhistleClicked"
		self.cl.photoLogSidePanelWaypointButton.onClickData = { uuid = uuid }
		self.cl.photoLogSidePanelWaypointButton.Visible = true
		self.cl.photoLogSidePanelWaypointButtonCaption.Caption = "#{LOGBOOK_CALL_BABY_WOC}"
	else
		self.cl.photoLogSidePanelWaypointButton.Visible = false
		self.cl.photoLogSidePanelWaypointButtonCaption.Caption = "#{LOGBOOK_SET_WAYPOINT}"
	end

	if LogEntryManager.Cl_GetIsLogHighlighted( uuid ) then
		self.cl.photoLogSidePanelWaypointEffectBox.Effects[1].PlayState = "Auto playing"
	else
		self.cl.photoLogSidePanelWaypointEffectBox.Effects[1].PlayState = "Stopped"
	end
	
	if logData.location == nil and not logData.whistle then
		self.cl.photoLogSidePanelWaypointVisibility.Skin = "LogbookQuestVisibilityHide"
	else
		if logData.whistle then
			local isActive = BabyWocManager.Cl_IsActive()
			if isActive then
				self.cl.photoLogSidePanelWaypointEffectBox.Effects[2].PlayState = "Auto playing"
				self.cl.photoLogSidePanelWaypointVisibility.Skin = "LogbookBabywocVisibilityShow"
			else
				self.cl.photoLogSidePanelWaypointVisibility.Skin = "LogbookBabywocVisibilityHide"
				self.cl.photoLogSidePanelWaypointEffectBox.Effects[2].PlayState = "Stopped"
			end
		else
			local hidden = WorldMarkerManager.Cl_IsMarkerHidden( logData.location )
			if hidden or hidden == nil then
				self.cl.photoLogSidePanelWaypointVisibility.Skin = "LogbookQuestVisibilityHide"
				self.cl.photoLogSidePanelWaypointEffectBox.Effects[2].PlayState = "Stopped"
			else
				self.cl.photoLogSidePanelWaypointEffectBox.Effects[2].PlayState = "Auto playing"
				self.cl.photoLogSidePanelWaypointVisibility.Skin = "LogbookQuestVisibilityShow"
			end
		end
		
	end
	if logData.whistle then
		self.cl.photoLogSidePanelWaypointVisibility.onClick = "cl_onWhistleClicked"
	else
		self.cl.photoLogSidePanelWaypointVisibility.onClick = "cl_onWaypointClicked"
	end
	self.cl.photoLogSidePanelWaypointVisibility.onClickData = { uuid = uuid }

	self.cl.render = true
end

function LogBook.cl_selectLog( self, widgetName, data )
	self.cl.photoLogSidePanel.Visible = true
	self.cl.selectedLog = data.uuid

	for _, logItemBase in ipairs( self.cl.logItemGrid.Childs ) do
		logItemBase.StateSelected = logItemBase.onClickData ~= nil and logItemBase.onClickData.uuid == self.cl.selectedLog
	end
	self.cl.render = true

	self:cl_updateLogSidePanel( data.uuid )
	self:cl_refreshLogItemSidePanelVisibility()

	if not self.cl.mapReadLogs[data.uuid] then
		self.cl.mapReadLogs[data.uuid] = true
		self.cl.updateLogItemGui = true
		self.network:sendToServer( "sv_n_readLog", data.uuid )
	end

	self.cl.render = true
end

function LogBook.cl_toggleWaypoint( self, name, position, world, compassIcon, billboardIndex )
	local markerHidden = WorldMarkerManager.Cl_IsMarkerHidden( name )
	if markerHidden == true then
		WorldMarkerManager.Cl_ActivateMarker( name )
	elseif markerHidden == false then
		WorldMarkerManager.Cl_HideMarker( name )
	elseif markerHidden == nil then
		if not compassIcon then
			sm.log.warning( "No compass marker icon was sent in for logbook marker: ", name )
		end
		WorldMarkerManager.Cl_CreateOrUpdateMarker( name, {
			effect = "Waypoint",
			position = position,
			world = world,
			textureIndex = billboardIndex or 0,
			compassImage = compassIcon or "icon_compass_spaceship.png",
			compassOnly = true,
			activate = true
		})
	end
end

function LogBook.cl_onWaypointClicked( self, widgetName, data )
	if data.uuid ~= self.cl.trackedLogUuid then
		local logData = self.cl.logData[self.cl.trackedLogUuid]
		if logData and logData.compassIcon and logData.location then
			local locations = LogEntryManager.Cl_GetLocations()
			if locations then
				local locationData = locations[logData.location]
				self:cl_toggleWaypoint( logData.location, locationData.pos, locationData.world, logData.compassIcon, logData.billboardIndex )
			end
		end
	end
	local logData = self.cl.logData[data.uuid]
	if logData.compassIcon and logData.location then
		local locations = LogEntryManager.Cl_GetLocations()
		if locations then
			local locationData = locations[logData.location]
			if locationData then
				self:cl_toggleWaypoint( logData.location, locationData.pos, locationData.world, logData.compassIcon, logData.billboardIndex )
				local markerHidden = WorldMarkerManager.Cl_IsMarkerHidden( logData.location )
				self.network:sendToServer( "sv_n_waypointTracked", { uuid = data.uuid, visible = not markerHidden } )
				if not markerHidden then
					self.cl.trackedLogUuid = data.uuid
				else
					self.cl.trackedLogUuid = sm.uuid.getNil()
				end
			else
				sm.log.error( "No location data found for log entry with uuid: ", data.uuid )
			end
		end
	end

	self:cl_updateLogSidePanel( data.uuid )
end

function LogBook.cl_onWhistleClicked( self, widgetName, data )
	local logData = self.cl.logData[data.uuid]
	if logData.whistle then
		BabyWocManager.Cl_UpdateActiveStatus()
		self.network:sendToServer( "sv_n_whistleClicked" )
	end
	self:cl_updateLogSidePanel( data.uuid )
end

function LogBook.cl_onBeaconVisibilityClicked( self, widgetName, data )
	local settings = g_beaconManager:cl_getBeaconSettings( data.beaconShapeId )
	g_beaconManager:cl_setBeaconVisible( data.beaconShapeId, not settings.visible )
	self.network:sendToServer( "sv_n_activeBeacon", { id = data.beaconShapeId, active = settings.visible } )
end

function LogBook.cl_refreshQuestSidePanelVisibility( self )
	if not self.cl.questSidePanel.Visible then
		return
	end
	local initial = self.cl.sidePanel.Visible
	self.cl.farmerSidePanel.Visible = false
	if self.cl.selectedQuest then
		local questSob = QuestManager.Cl_GetActiveQuest( self.cl.selectedQuest )
		if questSob and questSob.clientPublicData and questSob.clientPublicData.farmerQuest then
			self.cl.questSidePanel.Visible = false
			self.cl.farmerSidePanel.Visible = true
			self.cl.sidePanel.Visible = true
		elseif QuestManager.Cl_IsQuestComplete( self.cl.selectedQuest ) or sm.exists( questSob ) then
			self.cl.sidePanel.Visible = true
		else
			self.cl.sidePanel.Visible = false
		end
	else
		self.cl.sidePanel.Visible = false
	end
	if self.cl.render == false then
		self.cl.render = initial ~= self.cl.sidePanel.Visible
	end
end

function LogBook.cl_refreshLogItemSidePanelVisibility( self )
	local initial = self.cl.sidePanel.Visible

	if self.cl.questSidePanel.Visible then
		return
	end

	if self.cl.selectedLog and not LogEntryManager.Cl_HasLog( self.cl.selectedLog ) then
		self.cl.selectedLog = nil
	end

	if self.cl.selectedLog then
		if self.cl.logData[self.cl.selectedLog].type == "recording" then
			self.cl.audioLogSidePanel.Visible = true
			self.cl.photoLogSidePanel.Visible = false
			-- TODO: set enable state of play button depending on dialog manager
		elseif self.cl.logData[self.cl.selectedLog].type == "photo" then
			self.cl.audioLogSidePanel.Visible = false
			self.cl.photoLogSidePanel.Visible = true
			self.cl.photoLogSidePanelIcon.ImageTexture = self.cl.logData[self.cl.selectedLog].logIcon or "gui_logbook_icon_photo.png"
			self.cl.photoLogSidePanelPhoto.ImageTexture = self.cl.logData[self.cl.selectedLog].sidePanelImage or "gui_logbook_items_photo_example.png"
		end
		self.cl.sidePanel.Visible = true
	else
		self.cl.sidePanel.Visible = false
	end
	if self.cl.render == false then
		self.cl.render = initial ~= self.cl.sidePanel.Visible
	end
end

function LogBook.cl_onTabClicked( self, widgetName )
	self.cl.questContentPanel.Visible = false
	
	self.cl.dialogContainer.Visible = false
	self.cl.logItemContentPanel.Visible = false
	self.cl.garageContentPanel.Visible = false
	self.cl.garagePlaceholderContentPanel.Visible = false
	self.cl.beaconContentPanel.Visible = false

	self.cl.dialogNavigationContainer.Visible = false

	self.cl.photoLogSidePanel.Visible = false
	self.cl.audioLogSidePanel.Visible = false
	self.cl.questSidePanel.Visible = false
	self.cl.farmerSidePanel.Visible = false

	self.cl.questTab.StateSelected = false
	self.cl.dialogueTab.StateSelected = false
	self.cl.logItemsTab.StateSelected = false
	self.cl.beaconsTab.StateSelected = false
	self.cl.garageTab.StateSelected = false


	if widgetName == Tabs.Quests then
		self.cl.currentTab = Tabs.Quests
		self.cl.questTab.StateSelected = true
		self.cl.questContentPanel.Visible = true
		self.cl.questSidePanel.Visible = true
		self:cl_refreshQuestSidePanelVisibility()
		self.cl.updateQuestGui = true
	elseif widgetName == Tabs.Dialogue then
		self.cl.currentTab = Tabs.Dialogue
		self.cl.dialogueTab.StateSelected = true
		self.cl.dialogContainer.Visible = true
		self.cl.sidePanel.Visible = false
		self.cl.updateDialogGui = true
	elseif widgetName == Tabs.Items then
		self.cl.currentTab = Tabs.Items
		self.cl.logItemsTab.StateSelected = true
		self.cl.logItemContentPanel.Visible = true
		self:cl_refreshLogItemSidePanelVisibility()
		self.cl.updateLogItemGui = true
	elseif widgetName == Tabs.Beacons then
		self.cl.currentTab = Tabs.Beacons
		self.cl.beaconsTab.StateSelected = true
		self.cl.beaconContentPanel.Visible = true
		self.cl.sidePanel.Visible = false
	elseif widgetName == Tabs.Garage or widgetName == Tabs.GaragePlaceHolder then
		local garage = sm.garage.getGarage( GARAGE_IDS.SCRAP_CITY_GARAGE)
		if garage ~= nil then 
			local hasActiveTracking = garage:hasActiveTracking()
			if hasActiveTracking then
				self.cl.currentTab = Tabs.Garage
				self.cl.garageTab.StateSelected = true
				self.cl.garageContentPanel.Visible = true
				self.cl.updateGarageGui = true
			else
				self.cl.currentTab = Tabs.GaragePlaceHolder
				self.cl.garageTab.StateSelected = true
				self.cl.garagePlaceholderContentPanel.Visible = true
			end
		end
	end

	self.cl.render = true
end

function LogBook.cl_onGuiClosed( self )
	sm.tool.forceTool( nil )
	self.cl.equipped = false
	self.cl.wantsEquip = false
	self.cl.garagePreviewInitialized = false
	self.cl.dialogMeasureCache = {} -- may change language between openings, which invalidates cached text measurements
end

function LogBook.cl_onClickQuestVisibilityHide( self, widgetName, data )
	QuestManager.Cl_UntrackQuest( data.questName )
end

function LogBook.cl_onClickQuestVisibilityShow( self, widgetName, data )
	QuestManager.Cl_TrackQuest( data.questName )
end

function LogBook.cl_onAudioClick( self, widgetName, data )
	if not DialogManager.Cl_IsRunning() then
		DialogManager.Cl_PlayRecording( data.dialogUuid, data.entry, DialogSpeakerName.Recording, self.tool )
		self.cl.playingRecording = { dialogUuid = data.dialogUuid, entry = data.entry }
		self.network:sendToServer( "sv_n_recordingStarted", self.cl.playingRecording )
	end
end

function LogBook.cl_e_onDialogAbort( self )
	self.cl.playingRecording = nil
end
