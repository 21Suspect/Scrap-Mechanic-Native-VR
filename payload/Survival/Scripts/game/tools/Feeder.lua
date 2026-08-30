-- Generated from the supported stock build by generate_held_item_payload.py.

dofile "$GAME_DATA/Scripts/game/AnimationUtil.lua"
dofile "$SURVIVAL_DATA/Scripts/util.lua"
dofile "$SURVIVAL_DATA/Scripts/game/survival_shapes.lua"

Feeder = class()

local renderables =   {"$SURVIVAL_DATA/Character/Char_Tools/Char_longsandwich/char_longsandwich.rend" }
local renderablesTp = {"$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_tp_longsandwich.rend", "$SURVIVAL_DATA/Character/Char_Tools/Char_longsandwich/char_longsandwich_tp.rend"}
local renderablesFp = {"$SURVIVAL_DATA/Character/Char_Male/Animations/char_male_fp_longsandwich.rend", "$SURVIVAL_DATA/Character/Char_Tools/Char_longsandwich/char_longsandwich_fp.rend"}

local currentRenderablesTp = {}
local currentRenderablesFp = {}

sm.tool.preloadRenderables( renderables )
sm.tool.preloadRenderables( renderablesTp )
sm.tool.preloadRenderables( renderablesFp )


function Feeder.client_onCreate( self )
	self:cl_init()
end

function Feeder.client_onRefresh( self )
	self:cl_init()
end

function Feeder.cl_init( self )
	if self.tool:isEquipped() then
		self:cl_loadAnimations()
	end
end

function Feeder.cl_loadAnimations( self )

	self.tpAnimations = createTpAnimations(
			self.tool,
			{
				idle = { "longsandwich_idle", { looping = true } },
				use = { "longsandwich_use", { nextAnimation = "idle" } },
				sprint = { "longsandwich_sprint" },
				pickup = { "longsandwich_pickup", { nextAnimation = "idle" } },
				putdown = { "longsandwich_putdown" }
			
			}
		)
		local movementAnimations = {

			idle = "longsandwich_idle",
			
			runFwd = "longsandwich_run_fwd",
			runBwd = "longsandwich_run_bwd",

			sprint = "longsandwich_sprint",
			sprintLeft = "longsandwich_sprint_left",
			sprintRight = "longsandwich_sprint_right",
			
			jump = "longsandwich_jump_start",
			jumpUp = "longsandwich_jump_up",
			jumpDown = "longsandwich_jump_down",
			
			land = "longsandwich_land",
			landFwd = "longsandwich_jump_land_fwd",
			landBwd = "longsandwich_jump_land_bwd",
			landLeft = "longsandwich_jump_land_left",
			landRight = "longsandwich_jump_land_right",

			crouchIdle = "longsandwich_crouch_idle",
			crouchFwd = "longsandwich_crouch_fwd",
			crouchBwd = "longsandwich_crouch_bwd"
		}
		
		for name, animation in pairs( movementAnimations ) do
			self.tool:setMovementAnimation( name, animation )
		end
		
		if self.tool:isLocal() then
			self.fpAnimations = createFpAnimations(
				self.tool,
				{
					idle = { "longsandwich_idle", { looping = true } },
					
					sprintInto = { "longsandwich_sprint_into", { nextAnimation = "sprintIdle",  blendNext = 0.2 } },
					sprintIdle = { "longsandwich_sprint_idle", { looping = true } },
					sprintExit = { "longsandwich_sprint_exit", { nextAnimation = "idle",  blendNext = 0 } },
					
					use = { "longsandwich_use", { nextAnimation = "idle" } },
					
					equip = { "longsandwich_pickup", { nextAnimation = "idle" } },
					unequip = { "longsandwich_putdown" }
				}
			)
		end
		setTpAnimation( self.tpAnimations, "idle", 5.0 )
		self.blendTime = 0.2

end

function Feeder.client_onUpdate( self, dt )
	
	-- First person animation	
	local isSprinting =  self.tool:isSprinting() 
	local isCrouching =  self.tool:isCrouching() 

	if self.tool:isLocal() then
		if self.equipped then
			if isSprinting and self.fpAnimations.currentAnimation ~= "sprintInto" and self.fpAnimations.currentAnimation ~= "sprintIdle" then
				swapFpAnimation( self.fpAnimations, "sprintExit", "sprintInto", 0.0 )
			elseif not self.tool:isSprinting() and ( self.fpAnimations.currentAnimation == "sprintIdle" or self.fpAnimations.currentAnimation == "sprintInto" ) then
				swapFpAnimation( self.fpAnimations, "sprintInto", "sprintExit", 0.0 )
			end
		end
		updateFpAnimations( self.fpAnimations, self.equipped, dt )
	end
	
	if not self.equipped then
		if self.wantEquipped then
			self.wantEquipped = false
			self.equipped = true
		end
		return
	end	
	
	local crouchWeight = self.tool:isCrouching() and 1.0 or 0.0
	local normalWeight = 1.0 - crouchWeight 
	local totalWeight = 0.0
	
	for name, animation in pairs( self.tpAnimations.animations ) do
		animation.time = animation.time + dt

		if name == self.tpAnimations.currentAnimation then
			animation.weight = math.min( animation.weight + ( self.tpAnimations.blendSpeed * dt ), 1.0 )
			
			if animation.looping == true then
				if animation.time >= animation.info.duration then
					animation.time = animation.time - animation.info.duration
				end
			end
			if animation.time >= animation.info.duration - self.blendTime and not animation.looping then
				if ( name == "use" ) then
					setTpAnimation( self.tpAnimations, "idle", 10.0 )
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

function Feeder.client_onEquip( self, animate )
	if animate then
		sm.effect.playEffect( "audio:event:/char/consumtion/food_pickup", self.tool:getPosition() )
	end
	
	self.wantEquipped = true
	
	currentRenderablesTp = {}
	currentRenderablesFp = {}
	
	for k,v in pairs( renderablesTp ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( renderablesFp ) do currentRenderablesFp[#currentRenderablesFp+1] = v end
	for k,v in pairs( renderables ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( renderables ) do currentRenderablesFp[#currentRenderablesFp+1] = v end
	
	self.tool:setTpRenderables( currentRenderablesTp )
	if self.tool:isLocal() then
		self.tool:setFpRenderables( currentRenderablesFp )
	end
	
	self:cl_loadAnimations()
	
	setTpAnimation( self.tpAnimations, "pickup", 0.0001 )
	if self.tool:isLocal() then
		swapFpAnimation( self.fpAnimations, "unequip", "equip", 0.2 )
	end
end

function Feeder.client_onUnequip( self, animate )
	self.wantEquipped = false
	self.equipped = false
	if sm.exists( self.tool ) then
		if animate then
			sm.effect.playEffect( "audio:event:/char/consumtion/food_putdown", self.tool:getPosition() )
		end
		setTpAnimation( self.tpAnimations, "putdown" )
		if self.tool:isLocal() and self.fpAnimations.currentAnimation ~= "unequip" then
			swapFpAnimation( self.fpAnimations, "equip", "unequip", 0.2 )
		end
	end
end

function Feeder.client_onEquippedUpdate( self, primaryState, secondaryState )
	if Chapter2VR then
		if Chapter2VR.markAdapter then Chapter2VR.markAdapter( "feeder" ) end
		if Chapter2VR.primaryState then primaryState = Chapter2VR.primaryState( self, primaryState ) end
	end
	-- Detect player
	local targetPlayer = nil
	local targetUnit = nil --HACK
	local success, result
	if Chapter2VR and Chapter2VR.actionRaycast then
		success, result = Chapter2VR.actionRaycast( 7.5, self.tool:getOwner():getCharacter() )
	else
		success, result = sm.localPlayer.getLatestRaycast()
	end
	if result.type == "character" then
		local character = result:getCharacter()
		targetPlayer = character:getPlayer()
		targetUnit = character:getUnit()
		if ( targetPlayer or targetUnit ) and character:isDowned() == true then
			local keyBindingText =  sm.gui.getKeyBinding( "Create", true )
			sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_FEED}" )
			
			-- Feed item
			if targetPlayer then
				if primaryState == sm.tool.interactState.start then
					local activeItem = sm.localPlayer.getActiveItem()
					local params = { targetPlayer = targetPlayer, foodUuid = activeItem, playerInventory = sm.localPlayer.getInventory() }
					self.network:sendToServer( "sv_n_feed", params )
				end
			end
			if targetUnit then
				if primaryState == sm.tool.interactState.start then
					local activeItem = sm.localPlayer.getActiveItem()
					local params = { targetUnit = targetUnit, foodUuid = activeItem, playerInventory = sm.localPlayer.getInventory() }
					self.network:sendToServer( "sv_n_feed", params )
				end
			end
		end
	end
	
	return targetPlayer ~= nil, false
	
end

function Feeder.sv_n_feed( self, params )
	if params.targetPlayer then
		sm.event.sendToPlayer( params.targetPlayer, "sv_e_feed", params )
	end
	if params.targetUnit then
		sm.event.sendToUnit( params.targetUnit, "sv_e_feed", params )
	end
end