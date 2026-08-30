-- Generated from the supported stock build by generate_held_item_payload.py.

dofile "$SURVIVAL_DATA/Scripts/game/survival_shapes.lua"

KeyTool = class()

function KeyTool.client_onEquippedUpdate( self, primaryState, secondaryState )
	if Chapter2VR then
		if Chapter2VR.markAdapter then Chapter2VR.markAdapter( "key" ) end
		if Chapter2VR.primaryState then primaryState = Chapter2VR.primaryState( self, primaryState ) end
	end

	local success, result
	if Chapter2VR and Chapter2VR.actionRaycast then
		success, result = Chapter2VR.actionRaycast( 7.5, self.tool:getOwner():getCharacter() )
	else
		success, result = sm.localPlayer.getLatestRaycast()
	end
	if success and result.type == "body" then
		local targetShape = result:getShape()
		if isAnyOf( targetShape:getShapeUuid(), { obj_survivalobject_powercoresocket, obj_survivalobject_cardreader, obj_survivalobject_keylock_growlab } ) then
			if primaryState == sm.tool.interactState.start then
				local params = { targetShape = targetShape, keyId = sm.localPlayer.getActiveItem() }
				self.network:sendToServer( "sv_n_use", params )
			end
			return true, false
		end
	end

	return false, false

end

function KeyTool.client_onEquip( self ) end

function KeyTool.client_onUnequip( self ) end

function KeyTool.sv_n_use( self, params, player )
	if params.targetShape.interactable then
		params.player = player
		sm.event.sendToInteractable( params.targetShape.interactable, "sv_e_unlock", params )
	end
end
