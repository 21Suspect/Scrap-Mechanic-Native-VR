dofile( "$GAME_DATA/Scripts/game/BasePlayer.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/NativeVRConfig.lua" )
dofile( "$SURVIVAL_DATA/Scripts/game/Chapter2VR.lua" )

CreativePlayer = class( BasePlayer )

local UnstuckPopupGui = {}
UnstuckPopupGui.root = sm.json.open( "$GAME_DATA/Gui/JsonGuis/PopUp_YN.gui" )
UnstuckPopupGui.index = IndexWidgets( UnstuckPopupGui.root )
UnstuckPopupGui.index["Title"].Caption = "#{MENU_YN_TITLE_ARE_YOU_SURE}"
UnstuckPopupGui.index["Message"].Caption = "#{MENU_YN_MESSAGE_UNSTUCK_CREATIVE}"
UnstuckPopupGui.index["Yes"].onClick = "cl_e_unstuckYes"
UnstuckPopupGui.index["No"].onClick = "cl_e_unstuckNo"

function CreativePlayer.server_onCreate( self )
	BasePlayer.server_onCreate( self )
	Chapter2VR.serverCreate( self )
end

function CreativePlayer.server_onRefresh( self )
	BasePlayer.server_onRefresh( self )
	Chapter2VR.serverCreate( self )
end

function CreativePlayer.sv_n_vrHandPhysics( self, params, player )
	Chapter2VR.serverReceive( self, params, player )
end

function CreativePlayer.client_onCreate( self )
	BasePlayer.client_onCreate( self )
	Chapter2VR.clientCreate( self )
end

function CreativePlayer.client_onRefresh( self )
	BasePlayer.client_onRefresh( self )
	Chapter2VR.clientCreate( self )
end

function CreativePlayer.client_onDestroy( self )
	Chapter2VR.clientDestroy( self )
end

-- Scrap Mechanic 1.0 registers player callbacks from the concrete class.
-- Keep an explicit update callback here instead of relying on the inherited
-- BasePlayer callback to redispatch into cl_localPlayerUpdate.
function CreativePlayer.client_onUpdate( self, dt )
	if self.player == sm.localPlayer.getPlayer() then
		self:cl_localPlayerUpdate( dt )
	end
end

function CreativePlayer.cl_localPlayerUpdate( self, dt )
	BasePlayer.cl_localPlayerUpdate( self, dt )
	Chapter2VR.clientUpdate( self, dt )
end

function CreativePlayer.server_onFixedUpdate( self, dt )
	BasePlayer.server_onFixedUpdate( self, dt )
	Chapter2VR.serverUpdate( self )
end

function CreativePlayer.sv_n_unstuck( self )
	local character = self.player:getCharacter()
	if not character then
		return
	end
	local params = { player = self.player, x = 16, y = 16 }
	sm.event.sendToWorld( character:getWorld(), "sv_e_spawnNewCharacter", params )
end

function CreativePlayer.cl_e_unstuck( self )
	self.cl.unstuckPopUp = sm.jsonGui.createGui( { isInteractive = true, needsCursor = true } )
	self.cl.unstuckPopUp:render( UnstuckPopupGui.root )
end

function CreativePlayer.cl_e_unstuckYes( self )
	self.network:sendToServer( "sv_n_unstuck" )
	if self.cl.unstuckPopUp then
		self.cl.unstuckPopUp:close()
		self.cl.unstuckPopUp = nil
	end
end

function CreativePlayer.cl_e_unstuckNo( self )
	if self.cl.unstuckPopUp then
		self.cl.unstuckPopUp:close()
		self.cl.unstuckPopUp = nil
	end
end
