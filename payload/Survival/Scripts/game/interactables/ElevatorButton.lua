ElevatorButton = class()
ElevatorButton.maxParentCount = 0
ElevatorButton.maxChildCount = 1
ElevatorButton.connectionInput = sm.interactable.connectionType.none
ElevatorButton.connectionOutput = sm.interactable.connectionType.logic
ElevatorButton.poseWeightCount = 2

function ElevatorButton.server_onCreate( self )
	self.sv = {}
	self.sv.goingUp = false
	self.sv.saved = self.storage:load()

	if self.sv.saved == nil or ( self.params and self.params ~= self.sv.saved ) then
		self.sv.saved = self.params or { goingUp = true }
		if self.tags then
			self.sv.saved.tags = self.tags
			if isAnyOf( "FLOOR_1", self.tags ) then
				self.sv.saved.goingUp = true
			elseif isAnyOf( "FLOOR_2", self.tags ) then
				self.sv.saved.goingUp = false
			end
		end
		self.storage:save( self.sv.saved )
	end
end

function ElevatorButton.server_onUnload( self )
end

function ElevatorButton.sv_e_shutOff( self )
	self.network:sendToClients( "cl_n_deactivate" )
end

function ElevatorButton.client_onCreate( self )
	self.cl = {}
	self.cl.effect = sm.effect.createEffect( "Elevator Button", self.interactable )
end

function ElevatorButton.client_onInteract( self, character, state )
	if state == true then
		self.network:sendToServer( "sv_n_push" )
	end
end

function ElevatorButton.server_onProjectile( self, hitPos, hitTime, hitVelocity, _, attacker, damage, userData, hitNormal, projectileUuid )
	if type( attacker ) == "Player" and damage > 0 then
		self:sv_n_push( nil, attacker )
	end
end

-- Chapter 2 VR direct-touch entry point. Keep the stock quest and
-- all-players-present checks in sv_n_push instead of duplicating them.
function ElevatorButton.sv_e_vrInteract( self, params )
	self:sv_n_push( nil, params and params.player or nil )
end

function ElevatorButton.sv_n_push( self, _, player )
	-- Block all use during the scrapyard bossfight
	-- Check if a quest requires all players present (or blocks use entirely)
	local requireAllPlayers = false
	if self.sv.saved.tags then
		for _, tag in ipairs( { "QUESTWAREHOUSE_PLAYERS_PRESENT" , "SCRAPYARD_PLAYERS_PRESENT" } ) do
			if isAnyOf( tag, self.sv.saved.tags ) then
				local quest = QuestManager.Sv_GetQuest( "quest_clear_warehouse" )
				if quest and (quest.publicData == nil or quest.publicData.bossfight ~= true) then
					requireAllPlayers = true
				end
				if not requireAllPlayers then
					quest = QuestManager.Sv_GetQuest( "quest_scrapyard" )
					if quest then
						if quest.publicData and quest.publicData.bossfight == true then
							return
						end
						requireAllPlayers = true
					end
				end
				break
			end
		end
	end


	if requireAllPlayers then
		-- Query box contacts from child elevator's publicData
		for _, child in ipairs( self.interactable:getChildren() ) do
			local pd = child:getPublicData()
			if pd and pd.portalHalfSize then
				local contacts = sm.physics.getBoxContacts( pd.portalPosition, pd.portalRotation, pd.portalHalfSize )
				if contacts.characters and #contacts.characters > 0 then
					local presentPlayers = 0
					for _, character in ipairs( contacts.characters ) do
						if character:isPlayer() then
							presentPlayers = presentPlayers + 1
						end
					end
					if presentPlayers > 0 and presentPlayers ~= #sm.player.getAllPlayers() then
						if player then
							self.network:sendToClient( player, "cl_n_alertPlayersPresent" )
						end
						return
					end
				end
				break
			end
		end
	end

	self.network:sendToClients( "cl_push", self.sv.saved.goingUp )
	self.interactable.active = true
end

function ElevatorButton.cl_n_alertPlayersPresent()
	NotificationManager.Cl_AddGenericNotification( "#{REQUIRES_ALL_PLAYERS_PRESENT}" )
end

function ElevatorButton.cl_n_deactivate( self )
	self.interactable:setPoseWeight( 1, 0.0 )
	self.interactable:setPoseWeight( 0, 0.0 )
	self.cl.effect:stop()
end

function ElevatorButton.cl_push( self, goingUp )
	if goingUp then
		self.interactable:setPoseWeight( 0, 1.0 ) -- Up
	else
		self.interactable:setPoseWeight( 1, 1.0 ) -- Down
	end
	self.cl.effect:start()
end
