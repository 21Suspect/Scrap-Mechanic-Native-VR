dofile( "$SURVIVAL_DATA/Scripts/game/managers/BeaconManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/DialogManager.lua" )
dofile( "$GAME_DATA/Scripts/game/managers/EffectManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/ElevatorManager.lua"  )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/MinidungeonElevatorManager.lua"  )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/QuestManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/RespawnManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/UnitManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/WorldManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_constants.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_harvestable.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_shapes.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_units.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_projectiles.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/survival_meleeattacks.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/util/recipes.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/util/Timer.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/QuestEntityManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/RaidManager.lua" )
dofile( "$GAME_DATA/Scripts/game/managers/TileStorageManager.lua" )
dofile( "$GAME_DATA/Scripts/game/managers/KinematicManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/WarehouseManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/UndergroundElevatorManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/ScannerbotManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/RecipeManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/TutorialManager.lua" )
dofile( "$GAME_DATA/Scripts/game/managers/WeatherManager.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/managers/PatrolManager.lua" )
dofile( "$CUSTOMIZATION_DATA/Scripts/game/quest_reward_util.lua" )


















---@class SurvivalGame : GameClass
---@field sv table
---@field cl table
---@field warehouses table
SurvivalGame = class( nil )
SurvivalGame.enableLimitedInventory = true
SurvivalGame.enableRestrictions = true
SurvivalGame.enableFuelConsumption = true
SurvivalGame.enableAmmoConsumption = true
SurvivalGame.enableUpgrade = true

local SyncInterval = 400 -- 400 ticks | 10 seconds

function SurvivalGame.server_onCreate( self )
	self.sv = {}
	self.sv.saved = self.storage:load()
	if self.sv.saved == nil then
		self.sv.saved = {}
		self.sv.saved.data = self.data
		printf( "Seed: %.0f", self.sv.saved.data.seed )
		self.sv.saved.overworld = sm.world.createWorld( "$SURVIVAL_DATA/Scripts/game/worlds/Overworld.lua", "Overworld", { dev = self.sv.saved.data.dev }, self.sv.saved.data.seed )
	else
		if self.sv.saved.overworld and not sm.exists( self.sv.saved.overworld ) then
			-- Load overworld to create TileStorageKeys
			sm.world.loadWorld( self.sv.saved.overworld )
		end
	end
	self.sv.saved.lootTier = self.sv.saved.lootTier or 1
	g_lootTier = self.sv.saved.lootTier
	self.storage:save( self.sv.saved )

	self.data = nil








	
	self:loadCraftingRecipes()
	g_enableCollisionTumble = true


	g_kinematicManager = KinematicManager()
	g_kinematicManager:sv_onCreate()

	WorldManager.Sv_OnCreate()

	g_respawnManager = RespawnManager()
	g_respawnManager:sv_onCreate( self.sv.saved.overworld )

	g_beaconManager = BeaconManager()
	g_beaconManager:sv_onCreate()

	g_unitManager = UnitManager()
	g_unitManager:sv_onCreate( self.sv.saved.overworld )

	self.sv.questManager = sm.storage.load( STORAGE_CHANNEL_QUESTMANAGER )
	if not self.sv.questManager then
		self.sv.questManager = sm.scriptableObject.createScriptableObject( sm.uuid.new( "83b0cc7e-b164-47b8-a83c-0d33ba5f72ec" ) )
		sm.storage.save( STORAGE_CHANNEL_QUESTMANAGER, self.sv.questManager )
	end

	self.sv.scannerbotManager = sm.scriptableObject.createScriptableObject( sm.uuid.new( "2760488f-98a9-4cad-9d24-0b9fca45f91f" ), nil, self.sv.saved.overworld )

	self.sv.undergroundWorlds = sm.storage.load( STORAGE_CHANNEL_UNDERGROUND_WORLDS )
	if not self.sv.undergroundWorlds then
		self.sv.undergroundWorlds = {}
		sm.storage.save( STORAGE_CHANNEL_UNDERGROUND_WORLDS, self.sv.undergroundWorlds )
	end



	self.sv.time = sm.storage.load( STORAGE_CHANNEL_TIME )
	if self.sv.time then
		print( "Loaded timeData:" )
		print( self.sv.time )
	else
		self.sv.time = {}
		self.sv.time.timeOfDay = GAME_START_TIME -- 06:00
		self.sv.time.timeProgress = true
		sm.storage.save( STORAGE_CHANNEL_TIME, self.sv.time )
	end
	self.sv.gotoLocations = { "marker", "start", "mechanicstation", "hideout", "mainelevator", "excavation", "questfarmer", "questwarehouse", "garage", "scannerbot", "void",
		"underground1", "underground2", "underground3", "underground4", "underground5", "underground6", "underground7", "underground8" }
	self.network:setClientData( { dev = g_survivalDev, gotoLocations = self.sv.gotoLocations }, 1 )
	self:sv_updateClientData()

	self.sv.syncTimer = Timer()
	self.sv.syncTimer:start( 0 )




end

function SurvivalGame.server_onDestroy( self )



end

function SurvivalGame.server_onRefresh( self )
	g_craftingRecipeSets = nil
	self:loadCraftingRecipes()
end

function SurvivalGame.server_onUnload( self )
	TileStorageManager.Sv_Save()
end

function SurvivalGame.client_onCreate( self )
	
	self.cl = {}
	self.cl.time = {}
	self.cl.time.timeOfDay = 0.0
	if sm.isHost then
		-- Host sets time immediately to avoid incorrect time during startup
		self.cl.time.timeOfDay = self.sv.time.timeOfDay
		local todWraped = math.fmod( self.cl.time.timeOfDay, 1 )
		sm.game.setTimeOfDay( todWraped )
	end
	self.cl.time.timeProgress = true
	self.cl.overworld = nil
	self.cl.gotoLocations = {}

	if not sm.isHost then
		self:loadCraftingRecipes()
		g_enableCollisionTumble = true
	end

	WorldManager.Cl_OnCreate()

	if g_respawnManager == nil then
		assert( not sm.isHost )
		g_respawnManager = RespawnManager()
	end
	g_respawnManager:cl_onCreate()

	if g_beaconManager == nil then
		assert( not sm.isHost )
		g_beaconManager = BeaconManager()
	end
	g_beaconManager:cl_onCreate()

	if g_unitManager == nil then
		assert( not sm.isHost )
		g_unitManager = UnitManager()
	end

	g_radioTransmitter = sm.effect.createEffect( "Radio - Transmitter" )
	g_radioTransmitter:setWorldAny()
	g_boomboxTransmitter = sm.effect.createEffect( "Boombox - Transmitter" )
	g_boomboxTransmitter:setWorldAny()

	-- Survival HUD
	g_survivalHud = sm.gui.createSurvivalHudGui()
	assert(g_survivalHud)
	g_survivalHud:setVisible( "StatusPanel", false )
	g_survivalHud:setVisible( "BreathPanel", false )

	-- Compass HUD
	self.cl.compassEnabled = sm.game.getSettingBoolean( "CompassHud" )
	g_compassHud = sm.gui.createCompassHudGui()
	assert(g_compassHud)
	self:cl_compassHudEnable( self.cl.compassEnabled )

	self.cl.renderManager = sm.clientScriptableObject.createScriptableObject( sm.uuid.new( "54563daa-dd25-4f43-9e49-7e58bd59f66a" ) )




end

function SurvivalGame.bindChatCommands( self )
	if sm.isHost then
		sm.game.bindChatCommand( "/kick", { { "string", "player name", false } }, "cl_onChatCommand", "Kick a player from server" )
		sm.game.bindChatCommand( "/ban", { { "string", "player name", false } }, "cl_onChatCommand", "Ban a player from server" )
	end
	



	local addCheats = g_survivalDev

	if addCheats then
		sm.game.bindChatCommand( "/ammo", { { "int", "quantity", true } }, "cl_onChatCommand", "Give ammo (default 100)" )
		sm.game.bindChatCommand( "/spudgun", {}, "cl_onChatCommand", "Give the spudgun" )
		sm.game.bindChatCommand( "/gatling", {}, "cl_onChatCommand", "Give the potato gatling gun" )
		sm.game.bindChatCommand( "/shotgun", {}, "cl_onChatCommand", "Give the fries shotgun" )
		sm.game.bindChatCommand( "/sunshake", {}, "cl_onChatCommand", "Give 1 sunshake" )
		sm.game.bindChatCommand( "/baguette", {}, "cl_onChatCommand", "Give 1 revival baguette" )
		sm.game.bindChatCommand( "/keycard", {}, "cl_onChatCommand", "Give 1 keycard" )
		sm.game.bindChatCommand( "/powercore", {}, "cl_onChatCommand", "Give 1 powercore" )
		sm.game.bindChatCommand( "/components", { { "int", "quantity", true } }, "cl_onChatCommand", "Give <quantity> components (default 10)" )
		sm.game.bindChatCommand( "/glowsticks", { { "int", "quantity", true } }, "cl_onChatCommand", "Give <quantity> components (default 10)" )
		sm.game.bindChatCommand( "/foodplease", {}, "cl_onChatCommand", "Give 5 of each edible type" )
		sm.game.bindChatCommand( "/seedsplease", {}, "cl_onChatCommand", "Give 20 of each seed type and some soil" )
		sm.game.bindChatCommand( "/tumble", { { "bool", "enable", true } }, "cl_onChatCommand", "Set tumble state" )
		sm.game.bindChatCommand( "/god", {}, "cl_onChatCommand", "Mechanic characters will take no damage" )
		sm.game.bindChatCommand( "/limited", {}, "cl_onChatCommand", "Use the limited inventory" )
		sm.game.bindChatCommand( "/unlimited", {}, "cl_onChatCommand", "Use the unlimited inventory" )
		sm.game.bindChatCommand( "/timeofday", { { "number", "timeOfDay", true } }, "cl_onChatCommand", "Sets the time of the day as a fraction (0.5=mid day)" )
		sm.game.bindChatCommand( "/timeprogress", { { "bool", "enabled", true } }, "cl_onChatCommand", "Enables or disables time progress" )
		
		
		local autocomplete = {}
		for k, _ in pairs( g_unitSpawnNames ) do
			autocomplete[#autocomplete+1] = k
		end
		sm.game.bindChatCommand( "/spawn", { { "string", "unitName", true, autocomplete }, { "int", "amount", true } }, "cl_onChatCommand", "Spawn a unit: 'woc', 'tapebot', 'totebot', 'haybot'" )

		local existingKits = { "start", "trashbot", "mechanic", "tutorial", "pipe", "food", "seed" }
		
		sm.game.bindChatCommand( "/starterkit", { { "string", "name", true, existingKits } }, "cl_onChatCommand", "Spawn a starter kit" )
		sm.game.bindChatCommand( "/die", {}, "cl_onChatCommand", "Kill the player" )
		sm.game.bindChatCommand( "/unstuck", {}, "cl_onChatCommand", "Unstuck the player" )
		sm.game.bindChatCommand( "/sethp", { { "number", "hp", false } }, "cl_onChatCommand", "Set player hp value" )
		sm.game.bindChatCommand( "/setbreath", { { "number", "breath", false } }, "cl_onChatCommand", "Set player breath value" )
		sm.game.bindChatCommand( "/aggroall", {}, "cl_onChatCommand", "All hostile units will be made aware of the player's position" )
		
		sm.game.bindChatCommand( "/stopraid", {}, "cl_onChatCommand", "Cancel all incoming raids" )
		sm.game.bindChatCommand( "/disableraids", { { "bool", "enabled", false } }, "cl_onChatCommand", "Disable raids if true" )
		sm.game.bindChatCommand( "/noaggro", { { "bool", "enable", true } }, "cl_onChatCommand", "Toggles the player as a target" )
		sm.game.bindChatCommand( "/exportmultishape", {}, "cl_onChatCommand", "Exports a blueprint shape file" )
		



























































































































	end
end

function SurvivalGame.client_onClientDataUpdate( self, clientData, channel )
	if channel == 2 then
		self.cl.time = clientData.time
	elseif channel == 1 then
		g_survivalDev = clientData.dev
		self.cl.gotoLocations = clientData.gotoLocations
		self:bindChatCommands()
	end
end


function SurvivalGame.loadCraftingRecipes( self )
	local recipeSets = sm.json.open( "$SURVIVAL_DATA/CraftingRecipes/craftbot/craftbot.json" )
	recipeSets.workbench = "$SURVIVAL_DATA/CraftingRecipes/workbench.json"
	recipeSets.portablecrafter = "$SURVIVAL_DATA/CraftingRecipes/portablecrafter.json"
	recipeSets.dispenser = "$SURVIVAL_DATA/CraftingRecipes/dispenser.json"
	recipeSets.cookbot = "$SURVIVAL_DATA/CraftingRecipes/cookbot.json"
	recipeSets.dressbot = "$SURVIVAL_DATA/CraftingRecipes/dressbot.json"
	recipeSets.mininghubDispenser = "$SURVIVAL_DATA/CraftingRecipes/mininghubDispenser.json"
	recipeSets.sawtable = "$SURVIVAL_DATA/CraftingRecipes/sawtable.json"
	LoadCraftingRecipes( recipeSets )
end

function SurvivalGame.server_onFixedUpdate( self, timeStep )
	-- Update time







		if self.sv.time.timeProgress then
			self.sv.time.timeOfDay = self.sv.time.timeOfDay + ( timeStep / DAYCYCLE_TIME )
		end




	if WeatherManager.Get() then
		WeatherManager.Get():sv_setTimeOfDay( self.cl.time.timeOfDay )
	end

	-- Client and save sync
	self.sv.syncTimer:tick()
	if self.sv.syncTimer:done() then
		self.sv.syncTimer:start( SyncInterval )
		sm.storage.save( STORAGE_CHANNEL_TIME, self.sv.time )
		self:sv_updateClientData()
	end

	g_unitManager:sv_onFixedUpdate()

	if g_respawnManager then
		g_respawnManager:sv_onFixedUpdate()
	end




end

function SurvivalGame.sv_updateClientData( self )
	self.network:setClientData( { time = self.sv.time }, 2 )
end

function SurvivalGame.client_onUpdate( self, dt )
	-- Update time







		if self.cl.time.timeProgress then
			self.cl.time.timeOfDay = self.cl.time.timeOfDay + ( dt / DAYCYCLE_TIME )
		end




	if WeatherManager.Get() then
		WeatherManager.Get():cl_setTimeOfDay( self.cl.time.timeOfDay )
	end

	local todWraped = math.fmod( self.cl.time.timeOfDay, 1 )
	sm.game.setTimeOfDay( todWraped )

	-- Update lighting values
	local index = 1
	while index < #DAYCYCLE_LIGHTING_TIMES and todWraped >= DAYCYCLE_LIGHTING_TIMES[index + 1] do
		index = index + 1
	end
	assert( index <= #DAYCYCLE_LIGHTING_TIMES )

	local light = 0.0
	if index < #DAYCYCLE_LIGHTING_TIMES then
		local p = ( todWraped - DAYCYCLE_LIGHTING_TIMES[index] ) / ( DAYCYCLE_LIGHTING_TIMES[index + 1] - DAYCYCLE_LIGHTING_TIMES[index] )
		light = sm.util.lerp( DAYCYCLE_LIGHTING_VALUES[index], DAYCYCLE_LIGHTING_VALUES[index + 1], p )
	else
		light = DAYCYCLE_LIGHTING_VALUES[index]
	end
	
	sm.render.setOutdoorLighting( light )
	-- Capture the authoritative renderer input, independently of player getters.
	local vrTick = sm.game.getCurrentTick()
	if not self.cl.vrLightingTick or vrTick - self.cl.vrLightingTick >= 10 then
		self.cl.vrLightingTick = vrTick
		local ok, err = pcall( sm.json.save, { cycle = light, time = todWraped, tick = vrTick }, "$GAME_DATA/NativeVR/lighting_state.json" )
		if not ok and not self.cl.vrLightingError then sm.log.warning( "SCRAPVR_LIGHTING_WRITE " .. tostring( err ) ); self.cl.vrLightingError = true end
	end




end

function SurvivalGame.client_onFixedUpdate( self, dt )
	
	local compassSetting = sm.game.getSettingBoolean( "CompassHud" )
	if self.cl.compassEnabled ~= compassSetting then
		self.cl.compassEnabled = compassSetting
		self:cl_compassHudEnable( self.cl.compassEnabled )
	end
end

function SurvivalGame.cl_compassHudEnable( self, enable )
	if enable == true then
		g_compassHud:open()
	else
		g_compassHud:close()
	end
end

function SurvivalGame.client_showMessage( self, msg )
	sm.gui.chatMessage( msg )
end

function SurvivalGame.cl_onChatCommand( self, params )
	if params[1] == "/ammo" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = obj_plantables_potato, quantity = ( params[2] or 100 ) } )
	elseif params[1] == "/spudgun" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = tool_spudgun, quantity = 1 } )
	elseif params[1] == "/gatling" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = tool_gatling, quantity = 1 } )
	elseif params[1] == "/shotgun" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = tool_shotgun, quantity = 1 } )
	elseif params[1] == "/sunshake" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = obj_consumable_sunshake, quantity = 1 } )
	elseif params[1] == "/baguette" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = obj_consumable_longsandwich, quantity = 1 } )
	elseif params[1] == "/keycard" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = obj_survivalobject_keycard, quantity = 1 } )
	elseif params[1] == "/powercore" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = obj_survivalobject_powercore, quantity = 1 } )
	elseif params[1] == "/components" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = obj_consumable_component, quantity = ( params[2] or 10 ) } )
	elseif params[1] == "/glowsticks" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = obj_consumable_glowstick, quantity = ( params[2] or 20 ) } )
	elseif params[1] == "/foodplease" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_resource_corn, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_plantables_tomato, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_plantables_carrot, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_plantables_redbeet, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_plantables_banana, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_plantables_blueberry, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_plantables_orange, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_plantables_broccoli, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_plantables_pineapple, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_consumable_milk, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_consumable_pizzaburger, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_consumable_carrotburger, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_consumable_sunshake, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_consumable_tea, quantity = 5 } )
	elseif params[1] == "/seedsplease" then
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_consumable_soilbag, quantity = 5 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_potato, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_tomato, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_carrot, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_redbeet, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_banana, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_blueberry, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_orange, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_broccoli, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_pineapple, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_chili, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_pigmentflower, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_seed_cotton, quantity = 20 } )
		self.network:sendToServer( "sv_giveItem", { player = sm.localPlayer.getPlayer(), item = ITEMS.obj_consumable_soilbag, quantity = 45 } )
	elseif params[1] == "/god" then
		self.network:sendToServer( "sv_switchGodMode" )
	elseif params[1] == "/unlimited" then
		self.network:sendToServer( "sv_setLimitedInventory", false )
	elseif params[1] == "/limited" then
		self.network:sendToServer( "sv_setLimitedInventory", true )
	elseif params[1] == "/timeofday" then
		self.network:sendToServer( "sv_setTimeOfDay", params[2] )
	elseif params[1] == "/timeprogress" then
		self.network:sendToServer( "sv_setTimeProgress", params[2] )
	elseif params[1] == "/die" then
		self.network:sendToServer( "sv_killPlayer", { player = sm.localPlayer.getPlayer() } )
	elseif params[1] == "/unstuck" then
		sm.event.sendToPlayer( sm.localPlayer.getPlayer(), "cl_e_unstuck" )
	elseif params[1] == "/spawn" then
		local rayCastValid, rayCastResult = sm.localPlayer.getRaycast( 100 )
		if rayCastValid then
			local spawnParams = {
				uuid = sm.uuid.getNil(),
				world = sm.localPlayer.getPlayer().character:getWorld(),
				position = rayCastResult.pointWorld,
				yaw = 0.0,
				amount = 1,
				customParams = { tetherPoint = rayCastResult.pointWorld }
			}
			if g_unitSpawnNames[params[2]] then
				spawnParams.uuid = g_unitSpawnNames[params[2]]
			else
				spawnParams.uuid = sm.uuid.new( params[2] )
			end
			if params[3] then
				spawnParams.amount = params[3]
			end
			self.network:sendToServer( "sv_spawnUnit", spawnParams )
		end
	
	
	elseif params[1] == "/noaggro" then
		if type( params[2] ) == "boolean" then
			self.network:sendToServer( "sv_n_switchAggroMode", { aggroMode = not params[2] } )
		else
			self.network:sendToServer( "sv_n_switchAggroMode", { aggroMode = not sm.game.getEnableAggro() } )
		end




























































































































































































































	else
		self.network:sendToServer( "sv_onChatCommand", params )
	end
end

function SurvivalGame.sv_reloadTile( self, params )
	local xMin,xMax,yMin,yMax
	if params.pos then
		xMin,xMax,yMin,yMax = GetTileRanges( params.pos, params.world.id )
	elseif params.cell then
		xMin,xMax,yMin,yMax = GetTileRangesFromCell( params.cell.x, params.cell.y, params.world.id )
	end
	self.network:sendToClients( "cl_reloadTile", { world = params.world, xMin = xMin, xMax = xMax, yMin = yMin, yMax = yMax } )
end

function SurvivalGame.cl_reloadTile( self, params )
	for x = params.xMin, params.xMax do
		for y = params.yMin, params.yMax do
			params.world:reloadCell( x, y )
		end
	end
end

function SurvivalGame.sv_reloadCell( self, params, player )
	self.sv.saved.overworld:loadCell( params.x, params.y, player )
	self.network:sendToClients( "cl_reloadCell", params )
end

function SurvivalGame.sv_loadCell( self, params )
	if not sm.exists( self.sv.saved.overworld ) then
		sm.world.loadWorld( self.sv.saved.overworld )
	end

	if self.handle then
		self.handle:release()
	end

	self.handle = self.sv.saved.overworld:loadCellWithHandle( params.x, params.y, nil )
end

function SurvivalGame.sv_releaseCell( self )
	if self.handle then
		self.handle:release()
		self.handle = nil
	end
end
























































function SurvivalGame.sv_n_chemicaltower( self )
	EffectManager.Cl_PlayNamedCinematic( { name = "cinematic.chemicaltower", callbackData = { world = self.sv.saved.overworld } } )
end

function SurvivalGame.cl_reloadCell( self, params )
	for x = -2, 2 do
		for y = -2, 2 do
			params.world:reloadCell( params.x+x, params.y+y )
		end
	end
end

function SurvivalGame.sv_giveItem( self, params )
	sm.container.beginTransaction()
	sm.container.collect( params.player:getInventory(), params.item, params.quantity, false )
	sm.container.endTransaction()
end

function SurvivalGame.cl_n_onJoined( self, params )
	self.cl.playIntroCinematic = params.newPlayer
end

function SurvivalGame.client_onLoadingScreenLifted( self )
	EffectManager.Cl_OnLoadingScreenLifted()
	TutorialManager.Cl_OnLoadingScreenLifted()

	self.network:sendToServer( "sv_n_loadingScreenLifted" )
	if self.cl.playIntroCinematic and not g_survivalDev then
		EffectManager.Cl_PlayNamedCinematic( { name = "cinematic.survivalstart01", callbackData = { world = self.cl.overworld }, forceCameraData = true } )
	end
end

function SurvivalGame.sv_n_loadingScreenLifted( self, _, player )
	if not g_survivalDev then
		QuestManager.Sv_TryActivateQuest( "quest_tutorial" )
	end
end

function SurvivalGame.client_onLanguageChange( self, newLanguage )
	DialogManager.Cl_OnLanguageChange( newLanguage )
end

function SurvivalGame.client_onUnstuck( self )
	sm.event.sendToPlayer( sm.localPlayer.getPlayer(), "cl_e_unstuck" )
end

function SurvivalGame.sv_switchGodMode( self )
	g_godMode = not g_godMode
	self.network:sendToClients( "client_showMessage", "GODMODE: " .. ( g_godMode and "On" or "Off" ) )
end

function SurvivalGame.sv_n_switchAggroMode( self, params )
	sm.game.setEnableAggro(params.aggroMode )
	self.network:sendToClients( "client_showMessage", "AGGRO: " .. ( params.aggroMode and "On" or "Off" ) )
end

function SurvivalGame.sv_enableRestrictions( self, state )
	sm.game.setEnableRestrictions( state )
	self.network:sendToClients( "client_showMessage", ( state and "Restricted" or "Unrestricted"  ) )
end

function SurvivalGame.sv_setLimitedInventory( self, state )
	sm.game.setLimitedInventory( state )
	self.network:sendToClients( "client_showMessage", ( state and "Limited inventory" or "Unlimited inventory"  ) )
end










































function SurvivalGame.sv_setTimeOfDay( self, timeOfDay )
	if timeOfDay then
		self.sv.time.timeOfDay = math.floor( self.sv.time.timeOfDay ) + sm.util.clamp( timeOfDay, 0.0, 0.9999 )
		self.sv.syncTimer.count = self.sv.syncTimer.ticks -- Force sync
	end
	self.network:sendToClients( "client_showMessage", ( "Time of day set to "..self.sv.time.timeOfDay ) )
end

function SurvivalGame.sv_setTimeProgress( self, timeProgress )
	if timeProgress ~= nil then
		self.sv.time.timeProgress = timeProgress
		self.sv.syncTimer.count = self.sv.syncTimer.ticks -- Force sync
	end
	self.network:sendToClients( "client_showMessage", ( "Time scale set to "..( self.sv.time.timeProgress and "on" or "off ") ) )
end

function SurvivalGame.sv_killPlayer( self, params )
	params.damage = 9999
	params.source = "shock"
	sm.event.sendToPlayer( params.player, "sv_e_receiveDamage", params )
end


















function SurvivalGame.sv_spawnUnit( self, params )
	sm.event.sendToWorld( params.world, "sv_e_spawnUnit", params )
end











function SurvivalGame.sv_spawnHarvestable( self, params )
	sm.event.sendToWorld( params.world, "sv_spawnHarvestable", params )
end

function SurvivalGame.sv_exportCreation( self, params )
	local obj = sm.json.parseJsonString( sm.creation.exportToString( params.body ) )
	sm.json.save( obj, "$SURVIVAL_DATA/LocalBlueprints/"..params.name..".blueprint" )
end

function SurvivalGame.sv_importCreation( self, params )
	sm.creation.importFromFile( params.world, "$SURVIVAL_DATA/LocalBlueprints/"..params.name..".blueprint", params.position )
end

function SurvivalGame.sv_onChatCommand( self, params, player )

	if params[1] == "/kick" then
		if params[2] ~= nil then
			self:sv_kickPlayer( params[2] )
		end
	elseif params[1] == "/ban" then
		if params[2] ~= nil then
			self:sv_banPlayer( params[2] )
		end
	elseif params[1] == "/tumble" then
		if params[2] ~= nil then
			player.character:setTumbling( params[2] )
		else
			player.character:setTumbling( not player.character:isTumbling() )
		end
		if player.character:isTumbling() then
			self.network:sendToClients( "client_showMessage", "Player is tumbling" )
		else
			self.network:sendToClients( "client_showMessage", "Player is not tumbling" )
		end
	elseif params[1] == "/sethp" then
		sm.event.sendToPlayer( player, "sv_e_debug", { hp = params[2] } )

	elseif params[1] == "/setbreath" then
		sm.event.sendToPlayer( player, "sv_e_debug", { breath = params[2] } )
























































































































































































































































































































































































































































































































































































































	else
		params.player = player
		if sm.exists( player.character ) then
			sm.event.sendToWorld( player.character:getWorld(), "sv_e_onChatCommand", params )
		end
	end
end

function SurvivalGame.server_onPlayerJoined( self, player, newPlayer )
	print( player.name, "joined the game" )
	self.sv.gotoLocations = self.sv.gotoLocations or {}
	self.sv.gotoLocations[#self.sv.gotoLocations+1] = string.lower( player.name )
	self.network:setClientData( { dev = g_survivalDev, gotoLocations = self.sv.gotoLocations }, 1 )
	WeatherManager.Sv_PlayerJoined( player )
	if newPlayer then --Player is first time joiners
		local inventory = player:getInventory()
		
		sm.container.beginTransaction()
		
		if g_survivalDev then
			if not sm.game.getLimitedInventory() then
				inventory = player:getHotbar()
			end
			--Hotbar
			sm.container.setItem( inventory, 0, ITEMS.tool_sledgehammer, 1 )
			sm.container.setItem( inventory, 1, ITEMS.tool_lift, 1 )
			sm.container.setItem( inventory, 2, ITEMS.tool_spudgun, 1 )
			sm.container.setItem( inventory, 3, ITEMS.obj_consumable_glowstick, 20 )
			sm.container.setItem( inventory, 9, ITEMS.tool_connect, 1 )

			--Actual inventory
			sm.container.setItem( inventory, 10, ITEMS.tool_paint, 1 )
			sm.container.setItem( inventory, 11, ITEMS.tool_weld, 1 )
		else
			sm.container.setItem( inventory, 0, ITEMS.tool_sledgehammer, 1 )
			sm.container.setItem( inventory, 1, ITEMS.tool_lift, 1 )
		end

		sm.container.endTransaction()

		local spawnPoint = g_survivalDev and SURVIVAL_DEV_SPAWN_POINT or START_AREA_SPAWN_POINT
		if not sm.exists( self.sv.saved.overworld ) then
			sm.world.loadWorld( self.sv.saved.overworld )
		end
		self.sv.saved.overworld:loadCell( math.floor( spawnPoint.x/64 ), math.floor( spawnPoint.y/64 ), player, "sv_createNewPlayer" )
		self.network:sendToClient( player, "cl_n_onJoined", { newPlayer = newPlayer } )
	else
		local inventory = player:getInventory()

		local sledgehammerCount = sm.container.totalQuantity( inventory, ITEMS.tool_sledgehammer )
		if sledgehammerCount == 0 then
			sm.container.beginTransaction()
			sm.container.collect( inventory, ITEMS.tool_sledgehammer, 1 )
			sm.container.endTransaction()
		elseif sledgehammerCount > 1 then
			sm.container.beginTransaction()
			sm.container.spend( inventory, ITEMS.tool_sledgehammer, sledgehammerCount - 1 )
			sm.container.endTransaction()
		end

		local tool_lift_creative = sm.uuid.new( "5cc12f03-275e-4c8e-b013-79fc0f913e1b" )
		local creativeLiftCount = sm.container.totalQuantity( inventory, tool_lift_creative )
		if creativeLiftCount > 0 then
			sm.container.beginTransaction()
			sm.container.spend( inventory, tool_lift_creative, creativeLiftCount )
			sm.container.endTransaction()
		end

		local liftCount = sm.container.totalQuantity( inventory, ITEMS.tool_lift )
		if liftCount == 0 then
			sm.container.beginTransaction()
			sm.container.collect( inventory, ITEMS.tool_lift, 1 )
			sm.container.endTransaction()
		elseif liftCount > 1 then
			sm.container.beginTransaction()
			sm.container.spend( inventory, ITEMS.tool_lift, liftCount - 1 )
			sm.container.endTransaction()
		end
	end

	if player.id > 1 then --Too early for self. Questmanager is not created yet...
		QuestManager.Sv_SendEvent( QuestEvent.PlayerJoined, { player = player } )
	end
end

function SurvivalGame.server_onPlayerLeft( self, player )
	print( player.name, "left the game" )
	for i, name in ipairs( self.sv.gotoLocations ) do
		if name == string.lower( player.name ) then
			self.sv.gotoLocations[i] = nil
			break
		end
	end
	self.network:setClientData( { dev = g_survivalDev, gotoLocations = self.sv.gotoLocations }, 1 )
	if player.id > 1 then
		QuestManager.Sv_SendEvent( QuestEvent.PlayerLeft, { player = player } )
	end
	if player.id ~= sm.player.getHostPlayer().id then
		local carryInventory = player:getCarry()
		local currentCarry = carryInventory:getItem( 0 )
		if currentCarry and currentCarry.uuid ~= sm.uuid.getNil() then
			local character = player:getCharacter()
			if character and sm.exists( character ) then
				local world = character:getWorld()
				local subdivideRatio = 0.25
				local halfShapeSize = sm.item.getShapeSize( currentCarry.uuid ) * 0.5 * subdivideRatio
				local spawnPos = character:getWorldPosition() + sm.vec3.new( 0, 0, halfShapeSize.z + 1 )
				local shapePlacement = {
					worldPosition = spawnPos,
					worldRotation = sm.item.getShapeRotation( currentCarry.uuid ),
				}
				sm.event.sendToWorld( world, "sv_e_dropCarryShape",{
					itemA = currentCarry.uuid,
					characterShape = sm.item.getCharacterShape( currentCarry.uuid ),
					player = player,
					color = player:getCarryColor(),
					shapePlacement = shapePlacement,
					containerA = carryInventory,
					raycastNormal = sm.vec3.new( 0, 0, -1 ),
					aimPosition = spawnPos,
					quantityA = currentCarry.quantity
				},sm.event.types.instant )
			end
		end
	end
end

function SurvivalGame.sv_kickPlayer( self, name )
	local players = sm.player.getAllPlayers()

	for _, player in ipairs( players ) do
		if player:getName() == name then
			sm.game.kickPlayer( player )
		end
	end
end

function SurvivalGame.sv_banPlayer( self, name )
	local players = sm.player.getAllPlayers()

	for _, player in ipairs( players ) do
		if player:getName() == name then
			sm.game.banPlayer( player )
		end
	end
end

function SurvivalGame.sv_e_createUndergroundElevatorDestination( self, params )
	print( "------------------------------------------------" )
	print( "Creating underground elevator destination" )
	print( params )
	print( "------------------------------------------------" )

	assert( params.depth, "Underground elevator destination depth is nil" )
	assert( params.elevatorName, "Underground elevator name is nil" )
	assert( params.connectionTag, "Underground elevator connection tag is nil" )

	if params.depth == 0 then
		if not sm.exists( self.sv.saved.overworld ) then
			sm.world.loadWorld( self.sv.saved.overworld )
		end
		UndergroundElevatorManager.Sv_LoadDestinationWorldCell( self.sv.saved.overworld, params.elevatorName, params.connectionTag )
		return
	end

	if self.sv.undergroundWorlds[params.depth] then
		print( "Underground world already exists, loading..." )
		if not sm.exists( self.sv.undergroundWorlds[params.depth] ) then
			sm.world.loadWorld( self.sv.undergroundWorlds[params.depth] )
		end
	else
		local def = UNDERGROUND_DEFS[params.depth]
		local world = sm.world.createWorld( def.script.file, def.script.class, { depth = params.depth, worldFilePath = def.world }, math.random( 1073741823 ) )
		self.sv.undergroundWorlds[params.depth] = world
		sm.storage.save( STORAGE_CHANNEL_UNDERGROUND_WORLDS, self.sv.undergroundWorlds )
	end
	UndergroundElevatorManager.Sv_LoadDestinationWorldCell( self.sv.undergroundWorlds[params.depth], params.elevatorName, params.connectionTag )
end


function SurvivalGame.sv_createNewPlayer( self, world, x, y, player )
	local params = { player = player, x = x, y = y }
	sm.event.sendToWorld( self.sv.saved.overworld, "sv_spawnNewCharacter", params )
end

function SurvivalGame.sv_spawnEjectedPlayer( self, world, x, y, player )
	local cellPosition = sm.vec3.new( x * 64.0, y * 64.0, 0 )
	local fallbackPosition = cellPosition + sm.vec3.one() * 32.0

	local yaw = 0
	local pitch = 0
	local spawnPosition = nil

	local findEjectNode = function()
		local xMin,xMax,yMin,yMax = GetTileRangesFromCell( x, y, world.id )
		for xit = xMin, xMax, 1 do
			for yit = yMin, yMax, 1 do
				local nodes = sm.cell.getNodesByTag( xit, yit, "WAREHOUSE_EJECT", world )
				if #nodes > 0 then
					local spawnerIndex = ( ( player.id - 1 ) % #nodes ) + 1 -- distribute players over multiple nodes
					local spawnNode = nodes[spawnerIndex]
					local spawnPosition = spawnNode.position + sm.vec3.new( 0, 0, 0.7 )
			
					local spawnDirection = spawnNode.rotation * sm.vec3.new( 0, 0, 1 )
					local spawnYaw = math.atan2( spawnDirection.y, spawnDirection.x ) - math.pi/2
					return spawnPosition, spawnYaw
				end
			end
		end
		return fallbackPosition, yaw
	end
	spawnPosition, yaw = findEjectNode()

	local character = sm.character.createCharacter( player, world, spawnPosition, yaw, pitch )
	player:setCharacter( character )
	character:setTumbling( true )
end

function SurvivalGame.sv_recreatePlayerCharacter( self, world, x, y, player, params )
	local yaw = math.atan2( params.dir.y, params.dir.x ) - math.pi/2
	local pitch = math.asin( params.dir.z )
	local newCharacter
	local pos = params.pos










	if params.world then
		newCharacter = sm.character.createCharacter( player, params.world, pos, yaw, pitch )
	else
		newCharacter = sm.character.createCharacter( player, self.sv.saved.overworld, pos, yaw, pitch )
	end
	player:setCharacter( newCharacter )
	if params.fadeFromBlack then
		sm.event.sendToPlayer( player, "sv_endFadeToBlack", { duration = 2.0, force = true }, sm.event.types.instant )
	end
	print( "Recreate character in new world" )
	print( params )
end











function SurvivalGame.sv_e_recreatePlayerInWorld( self, params )
	local world = params.world
	if not sm.exists( world ) then
		sm.world.loadWorld( world )

	end
	local cellX = math.floor( params.pos.x / 64 )
	local cellY = math.floor( params.pos.y / 64 )
	
	if params.fadeFromBlack then
		sm.event.sendToPlayer( params.player, "sv_endFadeToBlack", { duration = 2.0, force = true }, sm.event.types.instant )
	end

	world:loadCell( cellX, cellY, params.player, "sv_recreatePlayerCharacter", { pos = params.pos, dir = params.dir, world = world } )
end

function SurvivalGame.sv_e_respawn( self, params )
	if params.player.character and sm.exists( params.player.character ) then
		g_respawnManager:sv_requestRespawnCharacter( params.player )
	else
		local spawnPoint = g_survivalDev and SURVIVAL_DEV_SPAWN_POINT or START_AREA_SPAWN_POINT
		if not sm.exists( self.sv.saved.overworld ) then
			sm.world.loadWorld( self.sv.saved.overworld )
		end
		self.sv.saved.overworld:loadCell( math.floor( spawnPoint.x/64 ), math.floor( spawnPoint.y/64 ), params.player, "sv_createNewPlayer" )
	end
end

function SurvivalGame.sv_e_warehouseEject( self, params )
	local warehouse = WarehouseManager.Sv_GetWarehouseFromIndex( params.warehouseIndex )
	local spawnCoords
	if warehouse and warehouse.exits and warehouse.exits[1] then
		spawnCoords = warehouse.exits[1]
	else
		sm.log.error( "Failed to find an exit to eject from, for warehouse: ", params.warehouseIndex )
		local spawnPoint = g_survivalDev and SURVIVAL_DEV_SPAWN_POINT or START_AREA_SPAWN_POINT
		spawnCoords = { x = math.floor( spawnPoint.x / 64.0 ), y = math.floor( spawnPoint.y / 64.0 ) }
	end
	if not sm.exists( self.sv.saved.overworld ) then
		sm.world.loadWorld( self.sv.saved.overworld )
	end
	self.sv.saved.overworld:loadCell( spawnCoords.x, spawnCoords.y, params.player, "sv_spawnEjectedPlayer" )
end

function SurvivalGame.sv_loadedRespawnCell( self, world, x, y, player )
	g_respawnManager:sv_respawnCharacter( player, world )
end

function SurvivalGame.sv_e_onSpawnPlayerCharacter( self, player )
	if player.character and sm.exists( player.character ) then
		g_respawnManager:sv_onSpawnCharacter( player )
		g_beaconManager:sv_onSpawnCharacter( player )
	else
		sm.log.warning("SurvivalGame.sv_e_onSpawnPlayerCharacter for a character that doesn't exist")
	end
end

function SurvivalGame.sv_e_markBag( self, params )
	self.network:sendToClient( params.player, "cl_n_markBag", params.bags )
end

function SurvivalGame.cl_n_markBag( self, params )
	g_respawnManager:cl_markBag( params )
end

function SurvivalGame.sv_e_unmarkBag( self, player )
	self.network:sendToClient( player, "cl_n_unmarkBag" )
end

function SurvivalGame.cl_n_unmarkBag( self )
	g_respawnManager:cl_unmarkBag()
end

function SurvivalGame.sv_e_removeBag( self, params )
	self.network:sendToClient( params.player, "cl_e_removeBag", params.bags )
end

function SurvivalGame.cl_e_removeBag( self, params )
	g_respawnManager:cl_removeBag( params )
end

-- Beacons
function SurvivalGame.sv_e_createBeacon( self, params )
	if sm.exists( params.beacon.world ) then
		sm.event.sendToWorld( params.beacon.world, "sv_e_createBeacon", params )
	else
		sm.log.warning( "SurvivalGame.sv_e_createBeacon in a world that doesn't exist" )
	end
end

function SurvivalGame.sv_e_destroyBeacon( self, params )
	if sm.exists( params.beacon.world ) then
		sm.event.sendToWorld( params.beacon.world, "sv_e_destroyBeacon", params )
	else
		sm.log.warning( "SurvivalGame.sv_e_destroyBeacon in a world that doesn't exist" )
	end
end

function SurvivalGame.sv_e_unloadBeacon( self, params )
	if sm.exists( params.beacon.world ) then
		sm.event.sendToWorld( params.beacon.world, "sv_e_unloadBeacon", params )
	else
		sm.log.warning( "SurvivalGame.sv_e_unloadBeacon in a world that doesn't exist" )
	end
end

function SurvivalGame.cl_e_overworldCreated( self, world )
	self.cl.overworld = world
end









































































































































































function SurvivalGame.sv_e_grantAdditionalRewards( self, rewardList )
	self.network:sendToClients( "cl_n_grantAdditionalRewards", rewardList )
end

function SurvivalGame.sv_e_grantAdditionalRewardsForPlayer( self, args  )
	self.network:sendToClient( args.player, "cl_n_grantAdditionalRewards", args.rewardList )
end

function SurvivalGame.cl_n_grantAdditionalRewards( self, rewardList )
	Cl_GrantAdditionalItems( rewardList )
end
