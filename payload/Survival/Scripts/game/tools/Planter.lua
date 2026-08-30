-- Generated from the supported stock build by generate_held_item_payload.py.
dofile"$GAME_DATA/Scripts/game/AnimationUtil.lua"
dofile"$SURVIVAL_DATA/Scripts/util.lua"
dofile"$SURVIVAL_DATA/Scripts/game/survival_harvestable.lua"
dofile"$SURVIVAL_DATA/Scripts/game/survival_items.lua"
Planter = class()

local seedItems =
{
	ITEMS.obj_seed_banana,
	ITEMS.obj_seed_blueberry,
	ITEMS.obj_seed_orange,
	ITEMS.obj_seed_pineapple,
	ITEMS.obj_seed_carrot,
	ITEMS.obj_seed_redbeet,
	ITEMS.obj_seed_tomato,
	ITEMS.obj_seed_broccoli,
	ITEMS.obj_seed_potato,
	ITEMS.obj_seed_cotton,
	ITEMS.obj_seed_pigmentflower,
	ITEMS.obj_seed_chili,
}

local function isSeed(uuid)
	for _, value in pairs(seedItems) do
		if value == uuid then return true end
	end
	return false
end

local renderablesTp = { "$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_tp_carrytool.rend" }
local renderablesFp = { "$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_fp_carrytool.rend" }

sm.tool.preloadRenderables( renderablesTp )
sm.tool.preloadRenderables( renderablesFp )

function Planter.sv_n_updateEffect( self, params )
	self.network:setClientData( { activeUid = params.activeItem } )
end

function Planter.client_onClientDataUpdate( self, clientData )
	if self.tool:isLocal() then
		return
	end
	self.clientActiveItem = clientData.activeUid
end

function Planter.client_onCreate( self )
	self.tpAnimations = createTpAnimations( self.tool, {} )
    if self.tool:isLocal() then
		self.fpAnimations = createFpAnimations( self.tool, {} )
    end
	self.heldEffect = nil
	self.activeItem = sm.uuid.getNil()
end

function Planter.cl_loadAnimations( self )
	self.tpAnimations = createTpAnimations(
		self.tool,
		{
			idle = { "smallitem_idle", { looping = true } },
			equip = { "smallitem_pickup", { nextAnimation = "idle" } },
			unequip = { "smallitem_putdown" }
		}
	)
	local movementAnimations = {

		idle = "smallitem_idle",

		runFwd = "smallitem_run",
		runBwd = "smallitem_runbwd",

		jump = "smallitem_jump",
		jumpUp = "smallitem_jump_up",
		jumpDown = "smallitem_jump_down",

		land = "smallitem_jump_land",
		landFwd = "smallitem_jump_land_fwd",
		landBwd = "item_jump_land_bwd",
		landLeft = "item_jump_land_left",
		landRight = "item_jump_land_right",

		crouchIdle = "item_crouch_idle",
		crouchFwd = "item_crouch_run",
		crouchBwd = "item_crouch_runbwd"
	}

	for name, animation in pairs( movementAnimations ) do
		self.tool:setMovementAnimation( name, animation )
	end

	if self.tool:isLocal() then
		self.fpAnimations = createFpAnimations(
			self.tool,
			{
				idle = { "smallitem_idle", { looping = true } },
				equip = { "smallitem_pickup", { nextAnimation = "idle" } },
				unequip = { "smallitem_putdown" }
			}
		)
	end
	setTpAnimation( self.tpAnimations, "idle", 5.0 )
	self.blendTime = 0.2
end

function Planter.cl_spawnHeldEffect( self )
	local ownerPlayer = self.tool:getOwner()
	local owner = ownerPlayer and ownerPlayer:getCharacter()
	
	if not owner then
		return
	end
	if self.heldEffect then
		self.heldEffect:stop()
		self.heldEffect = nil
	end
	if self.heldEffectFp then
		self.heldEffectFp:stop()
		self.heldEffectFp = nil
	end
	if isSeed(self.activeItem) then
	
		self.heldEffect = sm.effect.createEffect( "CarryTool - Item" )
		self.heldEffectFp = sm.effect.createEffectFirstPerson( "CarryTool - Item" )
		local scale = 0.25
		local effects = { self.heldEffect, self.heldEffectFp }
		for _, effect in pairs(effects) do
			effect:setParameter("uuid", self.activeItem)
			effect:setParameter("Color", sm.shape.getShapeTypeColor(self.activeItem))
			effect:setScale( sm.vec3.new(scale,scale,scale ) )
			effect:setHost(owner,"jnt_right_weapon")
		end

		self.heldEffect:setOffsetPosition( sm.vec3.new( 0.15, 0, 0) )
		self.heldEffect:setOffsetRotation( sm.quat.angleAxis(0,sm.vec3.new( 0, 0, 1)) )
		self.heldEffectFp:setOffsetPosition( sm.vec3.new( 0.15, 0, 0 ) )

		if self.tool:isLocal() and self.tool:isInFirstPersonView() then
			self.heldEffectFp:start( 0 )
		else
			self.heldEffect:start( 0 )
		end
	end
end

function Planter.client_onEquippedUpdate( self, primaryState, secondaryState )
	if Chapter2VR then
		if Chapter2VR.markAdapter then Chapter2VR.markAdapter( "planter" ) end
		if Chapter2VR.primaryState then primaryState = Chapter2VR.primaryState( self, primaryState ) end
	end

	local plantable = sm.item.getPlantable( self.activeItem )
	local playerOwner = self.tool:getOwner()
	local ownerCharacter = playerOwner:getCharacter()

	self.isOverrideAnimationActive = ownerCharacter:hasActiveOverrideAnimation()
	if self.isOverrideAnimationActive then
		if self.heldEffectFp and self.heldEffectFp:isPlaying() then
			self.heldEffectFp:stopImmediate()
		end
		if self.heldEffect and self.heldEffect:isPlaying() then
			self.heldEffect:stopImmediate()
		end
	end

	if self.tool:isLocal() and self.equipped and self.heldEffect and self.heldEffectFp and not (self.isOverrideAnimationActive) then
		if self.tool:isInFirstPersonView() then
			if self.heldEffect:isPlaying() then
				self.heldEffect:stopImmediate()
			end
			if not self.heldEffectFp:isPlaying() then
				self.heldEffectFp:start()
			end
		else
			if self.heldEffectFp:isPlaying() then
				self.heldEffectFp:stopImmediate()
			end
			if not self.heldEffect:isPlaying() then
				self.heldEffect:start()
			end
		end
	end

	-- Detect soil
	local soil = nil
	local success, result
	if Chapter2VR and Chapter2VR.actionRaycast then
		success, result = Chapter2VR.actionRaycast( 7.5, self.tool:getOwner():getCharacter() )
	else
		success, result = sm.localPlayer.getLatestRaycast()
	end
	if result.type == "harvestable" then
		local harvestable = result:getHarvestable()
		if harvestable:getUuid() == hvs_soil then
			soil = harvestable
			local keyBindingText = sm.gui.getKeyBinding( "Create", true )
			sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_PLANT}" )
			if not self.raidManagerShowing then
				if plantable then
					RaidManager.Cl_ShowPlantingOutcome( plantable.harvestable )
					self.raidManagerShowing = true
				end
			end
		elseif self.raidManagerShowing then
			RaidManager.Cl_HidePlantingOutcome()
			self.raidManagerShowing = false
		end
	elseif result.type == "body" then
		if self.raidManagerShowing then
			RaidManager.Cl_HidePlantingOutcome()
			self.raidManagerShowing = false
		end
		local growbed = result:getShape()
		if growbed.uuid == ITEMS.obj_interactive_growbed_soil then
			soil = growbed
			local keyBindingText = sm.gui.getKeyBinding( "Create", true )

			if plantable and plantable.allowInGrowbed == false then 
				sm.gui.setInteractionText( "", "", "#{INTERACTION_CANNOT_PLANT_IN_GROWBED}" )
			else 
				sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_PLANT}" )
			end
		end
	else
		if self.raidManagerShowing then
			RaidManager.Cl_HidePlantingOutcome()
			self.raidManagerShowing = false
		end
	end


	-- Plant harvestable
	if soil then
		if primaryState == sm.tool.interactState.start then
			if plantable then
				local params = {
					targetSoil = soil,
					plantableUuid = self.activeItem,
					playerInventory = sm.localPlayer.getInventory(),
					slot = sm.localPlayer.getSelectedHotbarSlot()
				}
				self.network:sendToServer( "sv_n_plant", params )
			end
		end
	end
	return soil ~= nil, false
end

function Planter.client_onUpdate( self, dt )
	local crouchWeight = self.tool:isCrouching() and 1.0 or 0.0
	local normalWeight = 1.0 - crouchWeight
	local totalWeight = 0.0

	for name, animation in pairs( self.tpAnimations.animations ) do
		animation.time = animation.time + dt
		if name == self.tpAnimations.currentAnimation then
			animation.weight = math.min( animation.weight + (self.tpAnimations.blendSpeed * dt), 1.0 )
			if animation.time >= animation.info.duration - self.blendTime then
				if name == "equip" then
					setTpAnimation( self.tpAnimations, "idle", 0.001 )
				elseif animation.nextAnimation ~= "" then
					setTpAnimation( self.tpAnimations, animation.nextAnimation, 0.001 )
				end
			end
		else
			animation.weight = math.max( animation.weight - (self.tpAnimations.blendSpeed * dt), 0.0 )
		end

		totalWeight = totalWeight + animation.weight
	end

	if self.tool:isLocal() then
		updateFpAnimations( self.fpAnimations, self.equipped, dt )
	end

	if self.equipped then
		local currentActiveItem
		if self.tool:isLocal() then
			currentActiveItem = sm.localPlayer.getActiveItem()
		else
			currentActiveItem = self.clientActiveItem
		end

		if self.activeItem ~= currentActiveItem then
			self.activeItem = currentActiveItem

			if self.tool:isLocal() then
				self.network:sendToServer( "sv_n_updateEffect", { activeItem = self.activeItem } )
				setFpAnimation( self.fpAnimations, "equip", 0.25 )
				self.raidManagerShowing = false
			end

			setTpAnimation( self.tpAnimations, "equip", 0.25 )

			self:cl_spawnHeldEffect()

		end
	elseif self.heldEffect or self.heldEffectFp then
		if self.heldEffect then
			self.heldEffect:stop()
			self.heldEffect = nil
		end
		if self.heldEffectFp then
			self.heldEffectFp:stop()
			self.heldEffectFp = nil
		end
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
		if animation.looping == true then
			if animation.time >= animation.info.duration then
				animation.time = animation.time - animation.info.duration
			end
		end
	end

end

function Planter.client_onEquip( self )
	self.equipped = true
	
	self.raidManagerShowing = false
	self:cl_loadAndUpdateAnimation()
	if self.tool:isLocal() then
		setFpAnimation( self.fpAnimations, "equip", 0.25 )
	end
end

function Planter.cl_loadAndUpdateAnimation( self )
		
		self:cl_updatePlanterRenderables()
		self:cl_loadAnimations()
		if self.activeItem == nil or self.activeItem == sm.uuid.getNil() then
			setTpAnimation( self.tpAnimations, "unequip" )
			if self.tool:isLocal() and self.fpAnimations.currentAnimation ~= "unequip" then
				swapFpAnimation( self.fpAnimations, "equip", "unequip", 0.2 )
			end 	
		else
			setTpAnimation( self.tpAnimations, "equip", 0.0001 )
			if self.tool:isLocal() then
				swapFpAnimation( self.fpAnimations, "unequip", "equip", 0.2 )
		end
	end
end


function Planter.client_onUnequip( self )
	self.equipped = false
	self.raidManagerShowing = false
	self.activeItem = sm.uuid.getNil()

	-- self.wantEquipped = false
	self.equipped = false

	RaidManager.Cl_HidePlantingOutcome()
	self:cl_loadAndUpdateAnimation()

	if self.heldEffect then
		self.heldEffect:stop()
		self.heldEffect = nil
	end
	if self.heldEffectFp then
		self.heldEffectFp:stop()
		self.heldEffectFp = nil
	end
end

function Planter.client_onRefresh( self )
	self:cl_loadAndUpdateAnimation()
	self:cl_spawnHeldEffect()
end

function Planter.sv_n_plant( self, params )
	if type( params.targetSoil ) == "Harvestable" then
		sm.event.sendToHarvestable( params.targetSoil, "sv_e_plant", params )
	elseif type( params.targetSoil ) == "Shape" then
		sm.event.sendToInteractable( params.targetSoil.interactable, "sv_e_plant", params )
	end
end

function Planter.cl_updatePlanterRenderables( self )
	self.tool:setTpRenderables( renderablesTp )
	if self.tool:isLocal() then
		self.tool:setFpRenderables( renderablesFp )
	end
end
