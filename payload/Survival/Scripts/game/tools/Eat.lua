-- Generated from the supported stock build by generate_held_item_payload.py.
dofile "$GAME_DATA/Scripts/game/AnimationUtil.lua"
dofile "$SURVIVAL_DATA/Scripts/util.lua"
dofile "$SURVIVAL_DATA/Scripts/game/survival_shapes.lua"
dofile "$SURVIVAL_DATA/Scripts/game/survival_items.lua"

Eat = class()
Eat.emptyTpRenderables = {}
Eat.emptyFpRenderables = {}

local SunshakeRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_sunshake.rend" }
local CarrotBurgerRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_carrotburger/char_carrotburger.rend" }
local PizzaBurgerRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_pizzaburger/char_pizzaburger.rend" }
local CarrotRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_carrot.rend" }
local OrangeRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_orange.rend" }
local RedbeetRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_redbeet.rend" }
local BroccoliRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_brococoli.rend" }
local BananaRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_banana.rend" }
local BlueberryRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_blueberry.rend" }
local TomatoRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_tomato.rend" }
local PineappleRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_pineapple.rend" }
local MilkRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_milk.rend" }
local CornRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_corn.rend" }
local TeaRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_tea.rend" }
local ChiliRenderables = { "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_chili.rend" }

local RenderablesEattoolTp = { "$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_tp_eattool.rend", "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_tp.rend" }
local RenderablesEattoolFp = { "$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_fp_eattool.rend", "$SURVIVAL_DATA/Character/Char_Tools/Char_eattool/char_eattool_fp.rend" }

sm.tool.preloadRenderables( BlueberryRenderables )
sm.tool.preloadRenderables( OrangeRenderables )
sm.tool.preloadRenderables( MilkRenderables )
sm.tool.preloadRenderables( BroccoliRenderables )
sm.tool.preloadRenderables( TomatoRenderables )
sm.tool.preloadRenderables( BananaRenderables )
sm.tool.preloadRenderables( CarrotBurgerRenderables )
sm.tool.preloadRenderables( SunshakeRenderables )
sm.tool.preloadRenderables( CarrotRenderables )
sm.tool.preloadRenderables( PizzaBurgerRenderables )
sm.tool.preloadRenderables( PineappleRenderables )
sm.tool.preloadRenderables( CornRenderables )
sm.tool.preloadRenderables( RedbeetRenderables )
sm.tool.preloadRenderables( TeaRenderables )
sm.tool.preloadRenderables( RenderablesEattoolTp )
sm.tool.preloadRenderables( RenderablesEattoolFp )
sm.tool.preloadRenderables( ChiliRenderables )

local FoodUuidToRenderable =
{
	[tostring( obj_consumable_sunshake )] = SunshakeRenderables,
	[tostring( obj_consumable_milk )] = MilkRenderables,
	[tostring( obj_consumable_carrotburger )] = CarrotBurgerRenderables,
	[tostring( obj_consumable_pizzaburger )] = PizzaBurgerRenderables,
	[tostring( obj_plantables_banana )] = BananaRenderables,
	[tostring( obj_plantables_blueberry )] = BlueberryRenderables,
	[tostring( obj_plantables_orange )] = OrangeRenderables,
	[tostring( obj_plantables_pineapple )] = PineappleRenderables,
	[tostring( obj_plantables_carrot )] = CarrotRenderables,
	[tostring( obj_plantables_redbeet )] = RedbeetRenderables,
	[tostring( obj_plantables_tomato )] = TomatoRenderables,
	[tostring( obj_plantables_broccoli )] = BroccoliRenderables,
	[tostring( obj_resource_corn )] = CornRenderables,
	[tostring( obj_consumable_tea )] = TeaRenderables,
	[tostring( obj_plantables_chili)] = ChiliRenderables
}

local Drinks = { obj_consumable_sunshake, obj_consumable_milk, obj_consumable_tea }

function Eat.client_onCreate( self )
	self.tpAnimations = createTpAnimations( self.tool, {} )
	if self.tool:isLocal() then
		self.fpAnimations = createFpAnimations( self.tool, {} )
	end

	self.activeItem = sm.uuid.getNil()
	if self.tool:isLocal() then
		self.wasOnGround = true
		self.eatProgress = 0
		self.eatTime = 2.1
		self.munchEffectFp = sm.effect.createEffect( "Eat - MunchFP" )
	else
		self.desiredActiveItem = sm.uuid.getNil()
	end

	self.eating = false
	self.munchEffectTp = sm.effect.createEffect( "Eat - Munch" )
	self.drinkEffectTp = sm.effect.createEffect( "Eat - Drink" )
	self.munchEffectAudio = sm.effect.createEffect( "Eat - MunchSound" )
	self.drinkEffectAudio = sm.effect.createEffect( "Eat - DrinkSound" )
	self.drinkCanEffectAudio = sm.effect.createEffect( "Eat - DrinkCanSound" )
end

function Eat.client_onRefresh( self )
	self:cl_updateActiveFood()
end

function Eat.client_onClientDataUpdate( self, clientData )
	if not self.tool:isLocal() then
		self.desiredActiveItem = clientData.activeUid
	end
end

function Eat.cl_loadAnimations( self )

	self.tpAnimations = createTpAnimations(
			self.tool,
			{
				idle = { "Idle" },
				eat = { "Eat" },
				drink = { "Drink" },
				sprint = { "Sprint_fwd" },
				pickup = { "Pickup", { nextAnimation = "idle" } },
				putdown = { "Putdown" }
			
			}
		)
		local movementAnimations = {

			idle = "Idle",
			
			runFwd = "Run_fwd",
			runBwd = "Run_bwd",
			
			sprint = "Sprint_fwd",
			sprintLeft = "Sprint_left",
			sprintRight = "Sprint_right",
			
			jump = "Jump",
			jumpUp = "Jump_up",
			jumpDown = "Jump_down",

			land = "Jump_land",
			landFwd = "Jump_land_fwd",
			landBwd = "Jump_land_bwd",
			landLeft = "Jump_land_left",
			landRight = "Jump_land_right",

			crouchIdle = "Crouch_idle",
			crouchFwd = "Crouch_fwd",
			crouchBwd = "Crouch_bwd"
		}
		
		for name, animation in pairs( movementAnimations ) do
			self.tool:setMovementAnimation( name, animation )
		end
		
		if self.tool:isLocal() then
			self.fpAnimations = createFpAnimations(
				self.tool,
				{
					idle = { "Idle", { looping = true } },
					
					eat = { "Eat" },
					drink = { "Drink" },
					
					sprintInto = { "Sprint_into", { nextAnimation = "sprintIdle",  blendNext = 0.2 } },
					sprintIdle = { "Sprint_idle", { looping = true } },
					sprintExit = { "Sprint_exit", { nextAnimation = "idle",  blendNext = 0 } },
					
					jump = { "Jump", { nextAnimation = "idle" } },
					land = { "Jump_land", { nextAnimation = "idle" } },
					
					equip = { "Pickup", { nextAnimation = "idle" } },
					unequip = { "Putdown" }
				}
			)
		end
		setTpAnimation( self.tpAnimations, "idle", 5.0 )
		self.blendTime = 0.2
		
end


function Eat.client_onUpdate( self, dt )
	-- First person animation
	local isSprinting =  self.tool:isSprinting()
	local isCrouching =  self.tool:isCrouching()
	local isOnGround =  self.tool:isOnGround()
	
	if self.tool:isLocal() then
		if self.equipped and self.eating == false then
			if isSprinting and self.fpAnimations.currentAnimation ~= "sprintInto" and self.fpAnimations.currentAnimation ~= "sprintIdle" then
				swapFpAnimation( self.fpAnimations, "sprintExit", "sprintInto", 0.0 )
			elseif not self.tool:isSprinting() and ( self.fpAnimations.currentAnimation == "sprintIdle" or self.fpAnimations.currentAnimation == "sprintInto" ) then
				swapFpAnimation( self.fpAnimations, "sprintInto", "sprintExit", 0.0 )
			end
			if not isOnGround and self.wasOnGround and self.fpAnimations.currentAnimation ~= "jump" then
				swapFpAnimation( self.fpAnimations, "land", "jump", 0.02 )
			elseif isOnGround and not self.wasOnGround and self.fpAnimations.currentAnimation ~= "land" then
				swapFpAnimation( self.fpAnimations, "jump", "land", 0.02 )
			end
		end
		updateFpAnimations( self.fpAnimations, self.equipped, dt )

		self.wasOnGround = isOnGround
	end

	-- Update the equipped item for the clients that do not own the tool
	if not self.tool:isLocal() and self.activeItem ~= self.desiredActiveItem and self.tool:isEquipped() then
		self.activeItem = self.desiredActiveItem
		self:cl_updateActiveFood()
	end

	if not self.equipped then
		if self.wantEquipped then
			self.wantEquipped = false
			self.equipped = true
		end
		return
	end
	
	if self.tool:isLocal() then
		local activeItem = sm.localPlayer.getActiveItem()
		if self.activeItem ~= activeItem then
			if sm.item.getEdible( activeItem ) then
				-- Simulate a new equip
				self.activeItem = activeItem
				self:cl_updateEatRenderables()
				self:cl_loadAnimations()
				self.network:sendToServer( "sv_n_updateEatRenderables", self.activeItem )
				self:stopEat()
				self.network:sendToServer( "sv_n_stopEat" )
			end
		end
	end
	
	-- Eat progress
	local character = self.tool:getOwner().character
	local firstPerson = false
	if self.tool:isLocal() then
		local activeItem = sm.localPlayer.getActiveItem()
		self.tool:setBlockSprint( self.eating and not isAnyOf( activeItem, Drinks ) )
		if self.eating then
			self.eatProgress = self.eatProgress + dt
			if self.eatProgress >= self.eatTime then
				self:stopEat()
				self.network:sendToServer( "sv_n_stopEat", { itemId = activeItem, selectedSlot = sm.localPlayer.getSelectedHotbarSlot() } )
			end
			firstPerson = self.tool:isInFirstPersonView()
			if character then
				local characterRotation = sm.vec3.getRotation( sm.vec3.new( 0, 1, 0 ), character.direction )
				self.munchEffectFp:setPosition( self.tool:getFpBonePos( "jnt_right_weapon" ) )
				self.munchEffectFp:setRotation( characterRotation )
			end
			sm.gui.setProgressFraction( self.eatProgress / self.eatTime )
		else
			self.eatProgress = 0
		end
	end
	-- Eat effects
	if self.eating and character then
		self.munchEffectTp:setPosition( self.tool:getTpBonePos( "jnt_head" ) )
		self.munchEffectTp:setRotation( sm.vec3.getRotation( sm.vec3.new( 0, 1, 0 ), self.tool:getTpBoneDir( "jnt_head" ) ) )
		self.drinkEffectTp:setPosition( self.tool:getTpBonePos( "jnt_head" ) )
		self.drinkEffectTp:setRotation( sm.vec3.getRotation( sm.vec3.new( 0, 1, 0 ), self.tool:getTpBoneDir( "jnt_head" ) ) )
		self.munchEffectAudio:setPosition( self.tool:getTpBonePos( "jnt_head" ) )
		self.drinkEffectAudio:setPosition( self.tool:getTpBonePos( "jnt_head" ) )
		self.drinkCanEffectAudio:setPosition( self.tool:getTpBonePos( "jnt_head" ) )
		if firstPerson then
			-- Enable first-person effects
			if not isAnyOf( self.activeItem, Drinks ) then
				if not self.munchEffectFp:isPlaying() then
					self.munchEffectFp:start()
				end
			end
			-- Disable third-person effects
			if self.drinkEffectTp:isPlaying() then
				self.drinkEffectTp:stop()
			end
			if self.munchEffectTp:isPlaying() then
				self.munchEffectTp:stop()
			end
		else
			-- Enable third-person effects ( drink or munch )
			if isAnyOf( self.activeItem, Drinks ) then
				if not self.drinkEffectTp:isPlaying() then
					self.drinkEffectTp:start()
				end
				if self.munchEffectTp:isPlaying() then
					self.munchEffectTp:stop()
				end
			else
				if not self.munchEffectTp:isPlaying() then
					self.munchEffectTp:start()
				end
				if self.drinkEffectTp:isPlaying() then
					self.drinkEffectTp:stop()
				end
			end
			-- Disable first-person effects
			if self.tool:isLocal() then
				if self.munchEffectFp:isPlaying() then
					self.munchEffectFp:stop()
				end
			end
		end
		
	else
		-- Disable all effects
		if self.munchEffectTp:isPlaying() then
			self.munchEffectTp:stop()
		end
		if self.drinkEffectTp:isPlaying() then
			self.drinkEffectTp:stop()
		end
		if self.tool:isLocal() then
			if self.munchEffectFp:isPlaying() then
				self.munchEffectFp:stop()
			end
		end
	end
	
	local crouchWeight = self.tool:isCrouching() and 1.0 or 0.0
	local normalWeight = 1.0 - crouchWeight 
	local totalWeight = 0.0
	
	for name, animation in pairs( self.tpAnimations.animations ) do
		animation.time = animation.time + dt
	
		if name == self.tpAnimations.currentAnimation then
			animation.weight = math.min( animation.weight + ( self.tpAnimations.blendSpeed * dt ), 1.0 )
			
			if animation.time >= animation.info.duration - self.blendTime then
				if ( name == "eat" ) then
					setTpAnimation( self.tpAnimations, "pickup",  10.05 )
				elseif name == "drink" then
						setTpAnimation( self.tpAnimations, "pickup", 10.05 )
				elseif name == "pickup" then
					setTpAnimation( self.tpAnimations, "idle", 0.001 )
				elseif animation.nextAnimation ~= "" then
					setTpAnimation( self.tpAnimations, animation.nextAnimation, 0.001 )
				end 
				
			end
		else
			animation.weight = math.max( animation.weight - ( self.tpAnimations.blendSpeed * dt ), 0.0 )
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

function Eat.client_onToggle( self )
	return false
end

function Eat.client_onEquip( self, animate )
	if self.tool:isLocal() then
		if animate then
			local audioEvent = isAnyOf( self.activeItem, Drinks ) and "audio:event:/char/consumtion/drink_pickup" or "audio:event:/char/consumtion/food_pickup"
			sm.effect.playEffect( audioEvent, self.tool:getPosition() )
		end

		self.activeItem = sm.localPlayer.getActiveItem()
		self:cl_updateActiveFood()
		self.network:sendToServer( "sv_n_updateEatRenderables", self.activeItem )
	else
		if not animate then
			-- reload renderable
			self.activeItem = sm.uuid.getNil()
		end
	end

	self.wantEquipped = true
end

function Eat.sv_n_updateEatRenderables( self, activeUid, player )
	self.network:setClientData( { activeUid = activeUid } )
end

function Eat.cl_updateActiveFood( self )
	self:cl_updateEatRenderables()
	self:cl_loadAnimations()
	if self.activeItem == nil or self.activeItem == sm.uuid.getNil() then
		setTpAnimation( self.tpAnimations, "putdown" )
		if self.tool:isLocal() and self.fpAnimations.currentAnimation ~= "unequip" then
			swapFpAnimation( self.fpAnimations, "equip", "unequip", 0.2 )
		end
	else
		setTpAnimation( self.tpAnimations, "pickup", 0.0001 )
		if self.tool:isLocal() then
			swapFpAnimation( self.fpAnimations, "unequip", "equip", 0.2 )
		end
	end
end

function Eat.cl_updateEatRenderables( self )
	
	local eatRenderables = {}
	local animationRenderablesTp = RenderablesEattoolTp
	local animationRenderablesFp = RenderablesEattoolFp
	
	if FoodUuidToRenderable[tostring( self.activeItem )] then
		eatRenderables = FoodUuidToRenderable[tostring( self.activeItem )]
	else
		animationRenderablesTp = self.emptyTpRenderables
		animationRenderablesFp = self.emptyFpRenderables
		self.emptyTpRenderables = {}
		self.emptyFpRenderables = {}
	end

	local currentRenderablesTp = {}
	local currentRenderablesFp = {}
	
	for k,v in pairs( animationRenderablesTp ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( animationRenderablesFp ) do currentRenderablesFp[#currentRenderablesFp+1] = v end

	self.emptyTpRenderables = shallowcopy( animationRenderablesTp )
	self.emptyFpRenderables = shallowcopy( animationRenderablesFp )

	for k,v in pairs( eatRenderables ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( eatRenderables ) do currentRenderablesFp[#currentRenderablesFp+1] = v end
	
	self.tool:setTpRenderables( currentRenderablesTp )
	if self.tool:isLocal() then
		self.tool:setFpRenderables( currentRenderablesFp )
	end
end

function Eat.client_onUnequip( self, animate )
	if self.tool:isLocal() then
		if animate then
			if isAnyOf( self.activeItem, Drinks )  then
				sm.effect.playEffect( "audio:event:/char/consumtion/drink_putdown", self.tool:getPosition() )
			else
				sm.effect.playEffect( "audio:event:/char/consumtion/food_putdown", self.tool:getPosition() )
			end
		end
	end

	self.eating = false
	self.activeItem = sm.uuid.getNil()
	if sm.exists( self.tool ) then
		self:cl_updateActiveFood()
		if self.tool:isLocal() then
			self.eatProgress = 0
			self.network:sendToServer( "sv_n_updateEatRenderables", self.activeItem )
		end
	end

	self.wantEquipped = false
	self.equipped = false
end

-- Start
function Eat.sv_n_startEat( self, params )
	self.network:sendToClients( "cl_n_startEat" )
end

function Eat.cl_n_startEat( self )
	if not self.tool:isLocal() and self.tool:isEquipped() then
		self:startEat()
	end
end

function Eat.startEat( self )
	self.eating = true
	if self.tool:isLocal() then
		self.eatProgress = 0
		if isAnyOf( self.activeItem, Drinks )  then
			setFpAnimation( self.fpAnimations, "drink", 0.25 )
			self.eatTime = 0.7
		else
			setFpAnimation( self.fpAnimations, "eat", 0.25 )
			self.eatTime = 2.1
		end
	end

	if isAnyOf( self.activeItem, Drinks )  then
		if obj_consumable_sunshake == self.activeItem then
			if not self.drinkCanEffectAudio:isPlaying() then
				self.drinkCanEffectAudio:start()
			end
		else
			if not self.drinkEffectAudio:isPlaying() then
				self.drinkEffectAudio:start()
			end
		end
		setTpAnimation( self.tpAnimations, "drink", 10.0 )
	else
		if not self.munchEffectAudio:isPlaying() then
			self.munchEffectAudio:start()
		end
		setTpAnimation( self.tpAnimations, "eat", 10.0 )
	end
end

-- Stop
function Eat.sv_n_stopEat( self, params, player )
	if params then
		local edible = sm.item.getEdible( params.itemId )
		assert( edible )
		sm.container.beginTransaction()
		sm.container.spendFromSlot( player:getInventory(), params.selectedSlot, params.itemId, 1, true )
		if sm.container.endTransaction() then
			sm.event.sendToPlayer( self.tool:getOwner(), "sv_e_eat", edible )
			local torsoUuid = player:getCustomizationUuid(3)
			if params.itemId == ITEMS.obj_consumable_pizzaburger and torsoUuid == CUSTOMIZATIONS.outfit_torso_pizzaburger_tshirt then
				sm.achievement.seti( "c2cfaf12-413d-4e77-819e-c7b6985c85bf", 1, { player:getId() } )
			end
		end
	end
	self.network:sendToClients( "cl_n_stopEat", params )
end

function Eat.cl_n_stopEat( self, params )
	if self.tool:isLocal() then
		if params and params.itemId then
			self.activeItem = sm.localPlayer.getActiveItem()
			self:cl_updateEatRenderables()
		end
	else
		self:stopEat()
	end
end

function Eat.stopEat( self )
	self.eating = false
	if self.tool:isLocal() then
		self.eatProgress = 0
		setFpAnimation( self.fpAnimations, "idle", 0.25 )
	end
	setTpAnimation( self.tpAnimations, "idle", 10.0 )
	
	if self.drinkEffectAudio:isPlaying() then
		self.drinkEffectAudio:stop()
	end

	if self.drinkCanEffectAudio:isPlaying() then
		self.drinkCanEffectAudio:stop()
	end

	if self.munchEffectAudio:isPlaying() then
		self.munchEffectAudio:stop()
	end
end

-- Interact
function Eat.client_onEquippedUpdate( self, primaryState, secondaryState, forceBuildActive )
	if Chapter2VR then
		if Chapter2VR.markAdapter then Chapter2VR.markAdapter( "food" ) end
		if Chapter2VR.primaryState then primaryState = Chapter2VR.primaryState( self, primaryState ) end
	end
	if primaryState == sm.tool.interactState.start and not forceBuildActive then
		if sm.container.canSpend( sm.localPlayer.getInventory(), self.activeItem, 1 ) then
			self:startEat()
			self.network:sendToServer( "sv_n_startEat" )
		end
	elseif primaryState == sm.tool.interactState.stop then
		self:stopEat()
		self.network:sendToServer( "sv_n_stopEat" )
	end

	if forceBuildActive and not self.eating then
		return false, false
	else
		sm.gui.setInteractionText( "", sm.gui.getKeyBinding( "Create", true ), "#{INTERACTION_EAT}" )
		if not self.eating then
			UpdateForceBuildText()
		end
	end
	return true, false
	
end
