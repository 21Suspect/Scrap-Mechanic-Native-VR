dofile( "$GAME_DATA/Scripts/game/managers/EffectManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/UnitManager.lua" )
dofile( "$GAME_DATA/Scripts/game/managers/KinematicManager.lua" )
dofile( "$GAME_DATA/Scripts/game/managers/TileStorageManager.lua" )
dofile( "$GAME_DATA/Scripts/game/managers/WeatherManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/util/recipes.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_projectiles.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_meleeattacks.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_constants.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/WorldManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/scriptableObjects/GlowstickManager.lua" )




CreativeGame = class( nil )
CreativeGame.enableLimitedInventory = false
CreativeGame.enableRestrictions = true
CreativeGame.enableFuelConsumption = false
CreativeGame.enableAmmoConsumption = false
CreativeGame.enableUpgrade = true
CreativeGame.enableRecipes = false

local TimeSyncInterval = 400
local WeatherConditions = {
	rain = "Rain",
	thunder = "ThunderRain",
	clear = "Clear",
	cloudy = "Clouds01",
	drizzle = "Drizzle",
}

function CreativeGame.server_onCreate( self )
	self.sv = {}

	g_kinematicManager = KinematicManager()
	g_kinematicManager:sv_onCreate()

	g_unitManager = UnitManager()
	g_unitManager:sv_onCreate( nil, { aggroCreations = true } )

	g_beaconManager = BeaconManager()
	g_beaconManager:sv_onCreate()

	WorldManager.Sv_OnCreate()

	self.sv.weatherManager = sm.scriptableObject.createScriptableObject( sm.uuid.new( "46e23051-c20b-4929-9df1-7f4f838a3802" ), { permanentForecast = "CloudyDay", resetForecast = true } )

	self.sv.saved = self.storage:load()
	if self.sv.saved == nil then
		local legacyCreativeWorld = sm.world.getLegacyCreativeWorld()
		if legacyCreativeWorld then
			self.sv.saved = {}
			self.sv.saved.data = self.data
			self.sv.saved.world = legacyCreativeWorld
			self.storage:save( self.sv.saved )
		else
			self.sv.saved = {}
			self.sv.saved.data = self.data
			self.sv.saved.world = sm.world.createWorld( self.worldScriptFilename, self.worldScriptClass, { worldFile = self.data.worldFile }, self.data.seed )
			self.storage:save( self.sv.saved )
		end
	end

	if not sm.exists( self.sv.saved.world ) then
		sm.world.loadWorld( self.sv.saved.world )
	end

	self.sv.time = sm.storage.load( STORAGE_CHANNEL_TIME )
	if self.sv.time then
		print( "Loaded timeData:" )
		print( self.sv.time )
	else
		self.sv.time = { timeOfDay = 0.5, timeProgress = false }
	end
	self.sv.time.timeProgress = self.sv.time.timeProgress or false
	self.sv.timeSyncTick = 0
	sm.storage.save( STORAGE_CHANNEL_TIME, self.sv.time )

	self.network:setClientData( { time = self.sv.time } )

	self:loadCraftingRecipes()
	g_godMode = true
	g_disableScrapHarvest = true
end

function CreativeGame.loadCraftingRecipes( self )
	local recipeSets = sm.json.open( "$SURVIVAL_DATA/CraftingRecipes/craftbot/craftbot.json" )
	recipeSets.sawtable = "$SURVIVAL_DATA/CraftingRecipes/sawtable.json"
	recipeSets.portablecrafter = "$SURVIVAL_DATA/CraftingRecipes/portablecrafter.json"
	LoadCraftingRecipes( recipeSets )
end

function CreativeGame.server_onFixedUpdate( self, timeStep )
	if self.sv.time.timeProgress then
		self.sv.time.timeOfDay = self.sv.time.timeOfDay + ( timeStep / DAYCYCLE_TIME )
	end

	WeatherManager.Sv_SetTimeOfDay( self.sv.time.timeOfDay )

	self.sv.timeSyncTick = self.sv.timeSyncTick + 1
	if self.sv.timeSyncTick >= TimeSyncInterval then
		self.sv.timeSyncTick = 0
		sm.storage.save( STORAGE_CHANNEL_TIME, self.sv.time )
		self.network:setClientData( { time = self.sv.time } )
	end

	g_unitManager:sv_onFixedUpdate()
end

function CreativeGame.server_onPlayerJoined( self, player, newPlayer )
	WeatherManager.Sv_PlayerJoined( player )
	if newPlayer then
		self.sv.saved.world:loadCell( 0, 0, player, "sv_createNewPlayer" )
	end
end

function CreativeGame.sv_createNewPlayer( self, world, x, y, player )
	local params = { player = player, x = 16, y = 16 }
	sm.event.sendToWorld( self.sv.saved.world, "sv_e_spawnNewCharacter", params )
end

function CreativeGame.client_onCreate( self )
	if not sm.isHost then
		self:loadCraftingRecipes()
	end

	self.cl = { time = { timeOfDay = 0.5, timeProgress = false } }

	WorldManager.Cl_OnCreate()
	self.cl.renderManager = sm.clientScriptableObject.createScriptableObject( sm.uuid.new( "54563daa-dd25-4f43-9e49-7e58bd59f66a" ) )
	
	g_radioTransmitter = sm.effect.createEffect( "Radio - Transmitter" )
	g_radioTransmitter:setWorldAny()
	g_boomboxTransmitter = sm.effect.createEffect( "Boombox - Transmitter" )
	g_boomboxTransmitter:setWorldAny()

	sm.game.bindChatCommand( "/noaggro", { { "bool", "enable", true } }, "cl_onChatCommand", "Toggles the player as a target" )
	sm.game.bindChatCommand( "/noaggrocreations", { { "bool", "enable", true } }, "cl_onChatCommand", "Toggles whether the Tapebots will shoot at creations" )
	sm.game.bindChatCommand( "/aggroall", {}, "cl_onChatCommand", "All hostile units will be made aware of the player's position" )
	sm.game.bindChatCommand( "/popcapsules", { { "string", "filter", true } }, "cl_onChatCommand", "Opens all capsules. An optional filter controls which type of capsules to open: 'bot', 'animal'" )
	sm.game.bindChatCommand( "/killall", {}, "cl_onChatCommand", "Kills all spawned units" )
	sm.game.bindChatCommand( "/dropscrap", {}, "cl_onChatCommand", "Toggles the scrap loot from Haybots" )
	sm.game.bindChatCommand( "/place", { { "string", "harvestable", false } }, "cl_onChatCommand", "Places a harvestable at the aimed position. Must be placed on the ground. The harvestable parameter controls which harvestable to place: 'stone', 'tree', 'birch', 'leafy', 'spruce', 'pine'" )
	sm.game.bindChatCommand( "/restrictions", { { "bool", "enable", true } }, "cl_onChatCommand", "Toggles restrictions on creations" )
	sm.game.bindChatCommand( "/timeofday", { { "number", "timeOfDay", true } }, "cl_onChatCommand", "Sets the time of the day as a fraction (0.5=mid day)" )
	sm.game.bindChatCommand( "/timeprogress", { { "bool", "enabled", true } }, "cl_onChatCommand", "Enables or disables time progress" )
	sm.game.bindChatCommand( "/weather", { { "string", "condition", false, { "rain", "thunder", "clear", "cloudy", "drizzle" } } }, "cl_onChatCommand", "Sets the weather condition" )
	sm.game.bindChatCommand( "/day", {}, "cl_onChatCommand", "Sets time of day to day" )
	sm.game.bindChatCommand( "/night", {}, "cl_onChatCommand", "Sets time of day to night" )











	if sm.isHost then
		self.clearEnabled = false
		sm.game.bindChatCommand( "/allowclear", { { "bool", "enable", true } }, "cl_onChatCommand", "Enabled/Disables the /clear command" )
		sm.game.bindChatCommand( "/clear", {}, "cl_onChatCommand", "Remove all shapes in the world. It must first be enabled with /allowclear" )
	end

	if g_unitManager == nil then
		assert( not sm.isHost )
		g_unitManager = UnitManager()
	end

	if g_beaconManager == nil then
		assert( not sm.isHost )
		g_beaconManager = BeaconManager()
	end
	g_beaconManager:cl_onCreate()

	-- Compass HUD
	self.cl.compassEnabled = sm.game.getSettingBoolean( "CompassHud" )
	g_compassHud = sm.gui.createCompassHudGui()
	assert(g_compassHud)
	self:cl_compassHudEnable( self.cl.compassEnabled )
end

function CreativeGame.client_onFixedUpdate( self, dt )
	local compassSetting = sm.game.getSettingBoolean( "CompassHud" )
	if self.cl.compassEnabled ~= compassSetting then
		self.cl.compassEnabled = compassSetting
		self:cl_compassHudEnable( self.cl.compassEnabled )
	end
end

function CreativeGame.cl_compassHudEnable( self, enable )
	if enable == true then
		g_compassHud:open()
	else
		g_compassHud:close()
	end
end

function CreativeGame.client_onClientDataUpdate( self, clientData )
	self.cl.time = clientData.time
	self:cl_updateTimeOfDay()
end

function CreativeGame.client_onUpdate( self, dt )
	if self.cl.time.timeProgress then
		self.cl.time.timeOfDay = self.cl.time.timeOfDay + ( dt / DAYCYCLE_TIME )
	end
	self:cl_updateTimeOfDay()
end

function CreativeGame.cl_updateTimeOfDay( self )
	local timeOfDay = math.fmod( self.cl.time.timeOfDay, 1 )
	sm.game.setTimeOfDay( timeOfDay )

	local index = 1
	while index < #DAYCYCLE_LIGHTING_TIMES and timeOfDay >= DAYCYCLE_LIGHTING_TIMES[index + 1] do
		index = index + 1
	end

	local light = DAYCYCLE_LIGHTING_VALUES[index]
	if index < #DAYCYCLE_LIGHTING_TIMES then
		local fraction = ( timeOfDay - DAYCYCLE_LIGHTING_TIMES[index] ) / ( DAYCYCLE_LIGHTING_TIMES[index + 1] - DAYCYCLE_LIGHTING_TIMES[index] )
		light = sm.util.lerp( DAYCYCLE_LIGHTING_VALUES[index], DAYCYCLE_LIGHTING_VALUES[index + 1], fraction )
	end
	sm.render.setOutdoorLighting( light )
	-- Mirror the value supplied to the renderer: the getter can remain zero
	-- in a different Lua context. Throttle disk writes to the player bridge rate.
	local vrTick = sm.game.getCurrentTick()
	if not self.cl.vrLightingTick or vrTick - self.cl.vrLightingTick >= 10 then
		self.cl.vrLightingTick = vrTick
		local ok, err = pcall( sm.json.save, { cycle = light, time = timeOfDay, tick = vrTick }, "$GAME_DATA/NativeVR/lighting_state.json" )
		if not ok and not self.cl.vrLightingError then sm.log.warning( "SCRAPVR_LIGHTING_WRITE " .. tostring( err ) ); self.cl.vrLightingError = true end
	end

	if WeatherManager.Get() then
		WeatherManager.Get():cl_setTimeOfDay( self.cl.time.timeOfDay )
	end
end

function CreativeGame.client_showMessage( self, params )
	sm.gui.chatMessage( params )
end

function CreativeGame.client_onLoadingScreenLifted( self )
	EffectManager.Cl_OnLoadingScreenLifted()
end

function CreativeGame.client_onUnstuck( self )
	sm.event.sendToPlayer( sm.localPlayer.getPlayer(), "cl_e_unstuck" )
end

function CreativeGame.cl_onClearConfirmButtonClick( self, name )
	if name == "Yes" then
		self.cl.confirmClearGui:close()
		self.network:sendToServer( "sv_clear" )
	elseif name == "No" then
		self.cl.confirmClearGui:close()
	end
	self.cl.confirmClearGui = nil
end

function CreativeGame.sv_clear( self, _, player )
	if player.character and sm.exists( player.character ) then
		sm.event.sendToWorld( player.character:getWorld(), "sv_e_clear" )
	end
end

function CreativeGame.cl_onChatCommand( self, params )
	if params[1] == "/place" then
		local range = 7.5
		local success, result = sm.localPlayer.getRaycast( range )
		if success then
			params.aimPosition = result.pointWorld
		else
			params.aimPosition = sm.localPlayer.getRaycastStart() + sm.localPlayer.getDirection() * range
		end
		self.network:sendToServer( "sv_n_onChatCommand", params )
	elseif params[1] == "/allowclear" then
		local clearEnabled = not self.clearEnabled
		if type( params[2] ) == "boolean" then
			clearEnabled = params[2]
		end
		self.clearEnabled = clearEnabled
		sm.gui.chatMessage( "/clear is " .. ( self.clearEnabled and "Enabled" or "Disabled" ) )
	elseif params[1] == "/clear" then
		if self.clearEnabled then
			self.clearEnabled = false
			self.cl.confirmClearGui = sm.gui.createGuiFromLayout( "$GAME_DATA/Gui/Layouts/PopUp/PopUp_YN.layout" )
			self.cl.confirmClearGui:setButtonCallback( "Yes", "cl_onClearConfirmButtonClick" )
			self.cl.confirmClearGui:setButtonCallback( "No", "cl_onClearConfirmButtonClick" )
			self.cl.confirmClearGui:setText( "Title", "#{MENU_YN_TITLE_ARE_YOU_SURE}" )
			self.cl.confirmClearGui:setText( "Message", "#{MENU_YN_MESSAGE_CLEAR_MENU}" )
			self.cl.confirmClearGui:open()
		else
			sm.gui.chatMessage( "/clear is disabled. It must first be enabled with /allowclear" )
		end



























	else
		self.network:sendToServer( "sv_n_onChatCommand", params )
	end
end







function CreativeGame.sv_n_onChatCommand( self, params, player )
	if params[1] == "/noaggro" then
		local aggro = not sm.game.getEnableAggro()
		if type( params[2] ) == "boolean" then
			aggro = not params[2]
		end
		sm.game.setEnableAggro( aggro )
		self.network:sendToClients( "client_showMessage", "AGGRO: " .. ( aggro and "On" or "Off" ) )
	elseif params[1] == "/noaggrocreations" then
		local aggroCreations = not g_unitManager:sv_getHostSettings().aggroCreations
		if type( params[2] ) == "boolean" then
			aggroCreations = not params[2]
		end
		g_unitManager:sv_setHostSettings( { aggroCreations = aggroCreations } )
		self.network:sendToClients( "client_showMessage", "AGGRO CREATIONS: " .. ( aggroCreations and "On" or "Off" ) )
	elseif params[1] == "/popcapsules" then
		g_unitManager:sv_openCapsules( params[2] )
	elseif params[1] == "/dropscrap" then
		local disableScrapHarvest = not g_disableScrapHarvest
		if type( params[2] ) == "boolean" then
			disableScrapHarvest = not params[2]
		end
		g_disableScrapHarvest = disableScrapHarvest
		self.network:sendToClients( "client_showMessage", "SCRAP LOOT: " .. ( g_disableScrapHarvest and "Off" or "On" ) )
	elseif params[1] == "/restrictions" then
		local restrictions = not sm.game.getEnableRestrictions()
		if type( params[2] ) == "boolean" then
			restrictions = params[2]
		end
		sm.game.setEnableRestrictions( restrictions )
		self.network:sendToClients( "client_showMessage", "RESTRICTIONS: " .. ( restrictions and "On" or "Off" ) )
	elseif params[1] == "/timeofday" then
		self:sv_setTimeOfDay( params[2] )
	elseif params[1] == "/timeprogress" then
		self:sv_setTimeProgress( params[2] )
	elseif params[1] == "/weather" then
		local condition = WeatherConditions[string.lower( params[2] )]
		if condition then
			WeatherManager.Sv_StartCondition( condition )
			self.network:sendToClients( "client_showMessage", "Weather: " .. params[2] )
		end
	elseif params[1] == "/day" then
		self:sv_setTimeOfDay( 0.5 )
	elseif params[1] == "/night" then
		self:sv_setTimeOfDay( 0.0 )
	else
		if sm.exists( player.character ) then
			params.player = player
			sm.event.sendToWorld( player.character:getWorld(), "sv_e_onChatCommand", params )
		end
	end
end

function CreativeGame.sv_setTimeOfDay( self, timeOfDay )
	if timeOfDay ~= nil then
		self.sv.time.timeOfDay = math.floor( self.sv.time.timeOfDay ) + sm.util.clamp( timeOfDay, 0.0, 0.9999 )
		sm.storage.save( STORAGE_CHANNEL_TIME, self.sv.time )
		self.network:setClientData( { time = self.sv.time } )
	end
	self.network:sendToClients( "client_showMessage", "Time of day set to " .. self.sv.time.timeOfDay )
end

function CreativeGame.sv_setTimeProgress( self, timeProgress )
	if timeProgress ~= nil then
		self.sv.time.timeProgress = timeProgress
		sm.storage.save( STORAGE_CHANNEL_TIME, self.sv.time )
		self.network:setClientData( { time = self.sv.time } )
	end
	self.network:sendToClients( "client_showMessage", "Time progress: " .. ( self.sv.time.timeProgress and "On" or "Off" ) )
end

-- Beacons
function CreativeGame.sv_e_createBeacon( self, params )
	if sm.exists( params.beacon.world ) then
		sm.event.sendToWorld( params.beacon.world, "sv_e_createBeacon", params )
	else
		sm.log.warning( "CreativeGame.sv_e_createBeacon in a world that doesn't exist" )
	end
end

function CreativeGame.sv_e_destroyBeacon( self, params )
	if sm.exists( params.beacon.world ) then
		sm.event.sendToWorld( params.beacon.world, "sv_e_destroyBeacon", params )
	else
		sm.log.warning( "CreativeGame.sv_e_destroyBeacon in a world that doesn't exist" )
	end
end

function CreativeGame.sv_e_unloadBeacon( self, params )
	if sm.exists( params.beacon.world ) then
		sm.event.sendToWorld( params.beacon.world, "sv_e_unloadBeacon", params )
	else
		sm.log.warning( "CreativeGame.sv_e_unloadBeacon in a world that doesn't exist" )
	end
end

CreativeFlatGame = class( CreativeGame )
CreativeFlatGame.worldScriptFilename = "$GAME_DATA/Scripts/game/worlds/CreativeFlatWorld.lua";
CreativeFlatGame.worldScriptClass = "CreativeFlatWorld";

ClassicCreativeGame = class( CreativeGame )
ClassicCreativeGame.worldScriptFilename = "$GAME_DATA/Scripts/game/worlds/ClassicCreativeTerrainWorld.lua";
ClassicCreativeGame.worldScriptClass = "ClassicCreativeTerrainWorld";

CreativeCustomGame = class( CreativeGame )
CreativeCustomGame.worldScriptFilename = "$GAME_DATA/Scripts/game/worlds/CreativeCustomWorld.lua";
CreativeCustomGame.worldScriptClass = "CreativeCustomWorld";

CreativeTerrainGame = class( CreativeGame )
CreativeTerrainGame.worldScriptFilename = "$GAME_DATA/Scripts/game/worlds/CreativeTerrainWorld.lua";
CreativeTerrainGame.worldScriptClass = "CreativeTerrainWorld";
