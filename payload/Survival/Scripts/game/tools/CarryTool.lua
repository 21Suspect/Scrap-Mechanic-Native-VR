-- Generated from the supported stock build by generate_held_item_payload.py.
dofile"$GAME_DATA/Scripts/game/AnimationUtil.lua"
dofile"$SURVIVAL_DATA/Scripts/util.lua"
dofile"$SURVIVAL_DATA/Scripts/game/survival_shapes.lua"
dofile"$SURVIVAL_DATA/Scripts/game/survival_constants.lua"
dofile( "$SURVIVAL_DATA/Scripts/game/managers/DialogManager.lua" )
local config = dofile"$GAME_DATA/Tools/carry_config.carryconfig"
CarryTool = class( nil )
local GyroCoreSlotUuid = sm.uuid.new( "a5093336-af02-42c4-8afd-196f22a0917f" )
local GyroCoreUuid = sm.uuid.new( "5f5b2a0d-b629-4bea-93ca-e1f45ccac62b" )
local PowerActivatorUuid = sm.uuid.new( "895f5d04-c29a-46fd-bff5-742fc310940b" )

-- Carrying one of these items plays a named (one-off) bond builder. DialogManager enforces that each
-- only fires once, so the carry tool does not track it.
local CarryBondBuilderItems = {
	[tostring(ITEMS.obj_jewel_02)] = "carry_pickup1",
	[tostring(ITEMS.obj_jewel_03)] = "carry_pickup2",
}

local ContainerInsertTargets = {
	[tostring(ITEMS.obj_craftbot_refinery)]           = { slot = 1, accepts = g_resourceCollectorHarvests },
	[tostring(ITEMS.obj_craftbot_resourcecontainer)]  = { slot = 0, accepts = g_resourceCollectorHarvests },
	[tostring(ITEMS.obj_interactive_gravelcollector)] = { slot = 0, accepts = g_gravelCollectorHarvests },
	[tostring(ITEMS.obj_interactive_mineralcrusher)]  = { slot = 1, accepts = g_gravelCollectorHarvests },
}

local GenericInsertTargets = {
	[tostring(GyroCoreSlotUuid)]                          = { GyroCoreUuid },
	[tostring(ITEMS.obj_interactive_plasmadrill_lvl1)]     = { ITEMS.obj_resource_drillcasingmixedt1, ITEMS.obj_resource_drillcasingmixedt3, ITEMS.obj_resource_drillcasingmixedt4, ITEMS.obj_resource_drillcasingmixedrich },
	[tostring(ITEMS.obj_interactive_plasmadrill_lvl2)]     = { ITEMS.obj_resource_drillcasingmixedt1, ITEMS.obj_resource_drillcasingmixedt3, ITEMS.obj_resource_drillcasingmixedt4, ITEMS.obj_resource_drillcasingmixedrich	 },
	[tostring(ITEMS.obj_interactive_plasmadrill_lvl3)]     = { ITEMS.obj_resource_drillcasingmixedt1, ITEMS.obj_resource_drillcasingmixedt3, ITEMS.obj_resource_drillcasingmixedt4, ITEMS.obj_resource_drillcasingmixedrich	 },
}

local HarvestableInsertTargets = {
	[tostring(PowerActivatorUuid)] = ITEMS.obj_powerstation_battery,
}

local TossConfig = {
    duration = 0.5,
    amplitude = 0.5,
}

local blocksEffect = false
local function getTossHeight( elapsed, duration, amplitude )
    local t = elapsed / duration
    if t >= 1.0 then return 0 end
    return amplitude * -math.sin( 2 * math.pi * t ) * ( 1 - t )
end

local function axisToTarget(shapeAxis, targetAxis)
    if shapeAxis == targetAxis then
        return sm.quat.identity()
    end
    local rotations = {
        x = { y = {  90, sm.vec3.new(0, 0, 1) }, z = { -90, sm.vec3.new(0, 1, 0) } },
        y = { x = { -90, sm.vec3.new(0, 0, 1) }, z = {  90, sm.vec3.new(1, 0, 0) } },
        z = { x = {  90, sm.vec3.new(0, 1, 0) }, y = { -90, sm.vec3.new(1, 0, 0) } },
    }
    local r = rotations[shapeAxis][targetAxis]
    return sm.quat.angleAxis(math.rad(r[1]), r[2])
end

local function findAxis(size, selector)
    local entries = {
        { name = "z", value = math.abs(size.z) },
        { name = "y", value = math.abs(size.y) },
        { name = "x", value = math.abs(size.x) },
    }
    table.sort(entries, function(a, b) return a.value > b.value end) 
    if selector == "longest" then
        return entries[1].name
    elseif selector == "medium" then
        return entries[2].name
    else -- "shortest"
        return entries[3].name
    end
end

local OrientationStrategies = {
    longest = function(size, primaryAxis)
        local axis = findAxis(size, "longest")
        return axisToTarget(axis, primaryAxis)
    end,
    medium = function(size, primaryAxis)
        local axis = findAxis(size, "medium")
        return axisToTarget(axis, primaryAxis)
    end,
    smallest = function(size, primaryAxis)
        local axis = findAxis(size, "shortest")
        return axisToTarget(axis, primaryAxis)
    end,
    fixed = function(size, primaryAxis, fixedRotation)
        return fixedRotation or sm.quat.identity()
    end,
}

local AxisVectors = {
    x = sm.vec3.new(1, 0, 0),
    y = sm.vec3.new(0, 1, 0),
    z = sm.vec3.new(0, 0, 1),
}

local RemainingAxes = {
    x = { "y", "z" },
    y = { "x", "z" },
    z = { "x", "y" },
}

local function getAxisValue(vec, axisName)
    if axisName == "x" then return math.abs(vec.x)
    elseif axisName == "y" then return math.abs(vec.y)
    else return math.abs(vec.z)
    end
end

local FacingStrategies = {
    along = function(orientedSize, primaryAxis, isFPView)
        local remaining = RemainingAxes[primaryAxis]
        local a, b = remaining[1], remaining[2]
        local va = getAxisValue(orientedSize, a)
        local vb = getAxisValue(orientedSize, b)
        local longerAxis = va >= vb and a or b
        if longerAxis ~= "y" and longerAxis ~= primaryAxis then
            if isFPView then
                if longerAxis == "x" then
                    return sm.quat.angleAxis(math.rad(-90), AxisVectors[primaryAxis])
                end
            else
                if longerAxis == "x" then
                    return sm.quat.angleAxis(math.rad(90), AxisVectors[primaryAxis])
                end
            end
        end
        return sm.quat.identity()
    end,

    across = function(orientedSize, primaryAxis, isFPView)
        local remaining = RemainingAxes[primaryAxis]
        local a, b = remaining[1], remaining[2]
        local va = getAxisValue(orientedSize, a)
        local vb = getAxisValue(orientedSize, b)
        local longerAxis = va >= vb and a or b

        if longerAxis ~= "x" and longerAxis ~= primaryAxis then
            if isFPView then
                if longerAxis == "y" then
                    return sm.quat.angleAxis(math.rad(90), AxisVectors[primaryAxis])
                end
            else
                if longerAxis == "y" then
                    return sm.quat.angleAxis(math.rad(-90), AxisVectors[primaryAxis])
                end
            end
        end
        return sm.quat.identity()
    end,
}

local function computeShapeRotation(size, defaultRotation, orientationStrategy, facingStrategy, primaryAxis, fixedRotation, isFPView)
    defaultRotation = defaultRotation or sm.quat.identity()
    primaryAxis = primaryAxis or "z"

    local rotatedSize = defaultRotation * size
    local normalizedSize = sm.vec3.new(math.abs(rotatedSize.x), math.abs(rotatedSize.y), math.abs(rotatedSize.z))

    local primaryFn = OrientationStrategies[orientationStrategy]
    local primaryRotation = primaryFn and primaryFn(normalizedSize, primaryAxis, fixedRotation) or sm.quat.identity()

    local shapeRotation = primaryRotation

    if facingStrategy then
        local facingFn = FacingStrategies[facingStrategy]
        if facingFn then
            local orientedSize = primaryRotation * normalizedSize
            shapeRotation = facingFn(orientedSize, primaryAxis, isFPView) * primaryRotation
        end
    end

    return shapeRotation * defaultRotation, normalizedSize
end

local function loadViewTransform( data )
    if data == nil then return end
    data.rotation = sm.quat.fromEuler( data.rotation )
    local bc = data.boneCorrection
    local angle = bc and bc.angle or 0
    local axis = bc and bc.axis or sm.vec3.new(0, 0, 1)
    data.boneCorrection = sm.quat.angleAxis( math.rad(angle), axis )
    if data.extentOffsetScalar == nil then
        data.extentOffsetScalar = sm.vec3.new(0, 0, 0)
    end
end

local function getStrategies( config, viewTransform )
    local orientation = (viewTransform and viewTransform.orientationStrategy) or config.orientationStrategy
    local facing      = (viewTransform and viewTransform.facingStrategy)      or config.facingStrategy
    local primary     = (viewTransform and viewTransform.primaryAxis)         or config.primaryAxis or "z"
    return orientation, facing, primary
end

local function loadCarryConfigJson(  )
    local json = DeepCopy(config)
     
    local carryType = {}
    for typeName, entry in pairs( json.carryTypes ) do
        carryType[typeName] = typeName
        loadViewTransform( entry.transform.tp )
        loadViewTransform( entry.transform.fp )
    end

    local itemOverrides = {}
    for _, entry in ipairs( json.itemOverrides ) do
        if entry.type == "" then
            entry.type = nil
        end
        if entry.overrideTransform then
            loadViewTransform( entry.overrideTransform.tp )
            loadViewTransform( entry.overrideTransform.fp )
        end
        itemOverrides[tostring(entry.uuid)] = entry
        entry.uuid = nil
    end

    return carryType, json.carryTypes, itemOverrides, json.carryTypePriority
end

local CarryType, CarryConfig, ItemOverrides, CarryTypePriority = loadCarryConfigJson()

function CarryTool.cl_getCarryTransform( self, uid )
    if uid == nil or uid == sm.uuid.getNil() then 
        return nil,nil,nil
    end
    local carryType = self.cl.carryType
    if carryType == nil then 
        return nil,nil,nil
    end
    local override = ItemOverrides[tostring(uid)]
 
    local config = CarryConfig[carryType]
    if config == nil then 
        return nil,nil,nil
    end
    
    local isFPView = self.tool:isInFirstPersonView()
    local activeConfigTransform = isFPView and config.transform.fp or config.transform.tp
    if carryType == CarryType.heavyTool then
        return activeConfigTransform.scale, activeConfigTransform.gripOffset, activeConfigTransform.rotation
    end
    if override and override.overrideTransform then
        activeConfigTransform = override.overrideTransform and (isFPView and override.overrideTransform.fp or override.overrideTransform.tp) 
        if activeConfigTransform then
            return activeConfigTransform.scale, activeConfigTransform.gripOffset, activeConfigTransform.rotation
        else
            activeConfigTransform = isFPView and config.transform.fp or config.transform.tp
        end
    end
    
    local size = sm.item.getShapeSize(uid)
    if size == nil then return nil, nil, nil end
    local defaultRotation = sm.item.getShapeRotation(uid)
    local orientationStrategy, facingStrategy, primaryAxis = getStrategies( config, activeConfigTransform )
    local shapeRotation = computeShapeRotation( size, defaultRotation, orientationStrategy, facingStrategy, primaryAxis, config.fixedRotation, isFPView )

    local character = self.tool:getOwner().character
    local isLocal = self.tool:isLocal()
    local fnGetBonePos = (isLocal and isFPView) and sm.localPlayer.getFpBonePos or function(bone) return character:getTpBonePos(bone) end

    local primaryBonePos = fnGetBonePos( config.transform.primaryBone )
    local secondaryBonePos = fnGetBonePos( config.transform.secondaryBone )
    
    local handSpan = (primaryBonePos - secondaryBonePos):length() 
    
    



    local scale = activeConfigTransform.scale

    local subdivideRatio = 0.25
    local shapeExtent = size * subdivideRatio
    
    local shapeRot =   shapeRotation * activeConfigTransform.rotation 
    local rotatedExtent = shapeRot * shapeExtent    
    local rotatedHalfExtent = sm.vec3.new(math.abs(rotatedExtent.x), math.abs(rotatedExtent.y), math.abs(rotatedExtent.z)) * 0.5

    






     
    local extentScalar = activeConfigTransform.extentOffsetScalar

    local idealOffset = handSpan  

    local widthOffset = idealOffset * extentScalar.x 
    local heightOffset = rotatedHalfExtent.z * extentScalar.z
    local depthOffset = rotatedHalfExtent.y * extentScalar.y

    local boneCorrection = activeConfigTransform.boneCorrection
    local rotation = boneCorrection * shapeRot 

    local boneOffset
    if isFPView then
        boneOffset = sm.vec3.new(depthOffset, widthOffset ,heightOffset)
    else
        boneOffset = sm.vec3.new(widthOffset, depthOffset, heightOffset)
    end
    
    local offset = boneCorrection * (boneOffset  + activeConfigTransform.gripOffset)
    
    return scale, offset , rotation
end

local function fitsWithin(value,range) 
	return value >= range.min and value <= range.max
end

local function getCarryType(uuid)
	if uuid == sm.uuid.getNil() then return nil end
	local override = ItemOverrides[tostring(uuid)]

	local isMultiShape = sm.item.isMultiShape(uuid)
	if isMultiShape and not override then
		return CarryType.heavyTool
	end
	if override and override.type then
		return override.type
	end

	local size = sm.item.getShapeSize(uuid)
	if size == nil then return nil end

	local baselineSize = sm.vec3.new(math.abs(size.x), math.abs(size.y), math.abs(size.z))
	local isEqual = math.abs(baselineSize.x - baselineSize.y) < FLT_EPSILON and math.abs(baselineSize.y - baselineSize.z) < FLT_EPSILON

	local bestType = nil

	for _, type in ipairs(CarryTypePriority) do
		local config = CarryConfig[type]
		if config.requireEqualSides and not isEqual then
			goto continue
		end
		if config.size then
			local defaultRotation = sm.item.getShapeRotation(uuid)
			local orientationStrategy, facingStrategy, primaryAxis = getStrategies( config, config.transform.tp )
			local shapeRotation = computeShapeRotation( size, defaultRotation, orientationStrategy, facingStrategy, primaryAxis, config.fixedRotation, false )
			local configRotation = config.transform.tp.rotation
			local shapeRot = shapeRotation * configRotation

			local rotatedSize = shapeRot * size
			local remaining = RemainingAxes[primaryAxis]
			local height = math.floor(math.max(getAxisValue(rotatedSize, primaryAxis), 1) + 0.5)
			local footprint = math.floor(math.max(getAxisValue(rotatedSize, remaining[1]), getAxisValue(rotatedSize, remaining[2])) + 0.5)

			if fitsWithin(footprint, config.size.footprint) and fitsWithin(height, config.size.height) then
                bestType = type
                break
			end
		end
        ::continue::
	end

	return bestType or CarryType.heavyTool
end

CarryTool.emptyTpRenderables = {}
CarryTool.emptyFpRenderables = {}



function CarryTool.server_onCreate( self )
	self.sv = {}
    local activeItemUuid = sm.uuid.getNil()
    local player = self.tool:getOwner()
    local carryContainer = player:getCarry()
    activeItemUuid = carryContainer:getItem( 0 ).uuid
    local carryColor = player:getCarryColor()
	self.network:setClientData( { activeUid = activeItemUuid, activeColor = carryColor } )
end

function CarryTool.server_onDestroy( self )
	if self.sv.radiationTrigger and sm.exists( self.sv.radiationTrigger ) then
		self.sv.radiationTrigger:destroy()
		self.sv.radiationTrigger = nil
	end
	self.sv.radiationCarryingPlayer = nil
end

function CarryTool.server_onFixedUpdate( self )
	if self.sv.radiationCarryingPlayer then
		sm.event.sendToPlayer( self.sv.radiationCarryingPlayer, "sv_e_onStayRadiation" )
	end
end

function CarryTool.sv_n_updateCarryRenderables( self, param, player ) 
	local carryContainer = player:getCarry()
	if carryContainer then
		local radiationRadius = 0
		for _,uid in ipairs( sm.container.itemUuid( carryContainer ) ) do
			if RADIOACTIVE_CARRY_ITEMS[tostring( uid )] then
				radiationRadius = RADIOACTIVE_CARRY_ITEMS[tostring( uid )].radius
			end
		end
		if radiationRadius > 0 then
			sm.event.sendToPlayer( player, "sv_e_onEnterRadiation" )
			self.sv.radiationTrigger = sm.areaTrigger.createAttachedSphere( player.character, radiationRadius, nil, nil, sm.areaTrigger.filter.character )
			self.sv.radiationCarryingPlayer = player
		elseif self.sv.radiationTrigger then
			self.sv.radiationTrigger:destroy()
			self.sv.radiationTrigger = nil
			self.sv.radiationCarryingPlayer = nil
		end
	end
	local carryBondBuilder = param.activeItem and CarryBondBuilderItems[tostring( param.activeItem )]
	if carryBondBuilder then
		DialogManager.Sv_PlayNamedBondBuilder( carryBondBuilder )
	end
	self.network:setClientData( { activeUid = param.activeItem, activeColor = param.activeColor } )
end

function CarryTool.sv_onTriggerEnter( self, _, results )
	FireTriggerEventForPlayers( results, "sv_e_onEnterRadiation" )
end

function CarryTool.sv_onTriggerStay( self, _, results )
	FireTriggerEventForPlayers( results, "sv_e_onStayRadiation" )
end

local function buildAnimSet(config)
    local prefix = config.animation.prefix
    local set = {
        tp = {
            idle    = { prefix .. "_idle", { looping = true } },
            pickup  = { prefix .. "_pickup", { nextAnimation = "idle" } },
            putdown = { prefix .. "_putdown" },
        },
        movement = {
            idle       = prefix .. "_idle",
            runFwd     = prefix .. "_run",
            runBwd     = prefix .. "_runbwd",
            jump       = prefix .. "_jump",
            jumpUp     = prefix .. "_jump_up",
            jumpDown   = prefix .. "_jump_down",
            land       = prefix .. "_jump_land",
            landFwd    = prefix .. "_jump_land_fwd",
            landBwd    = prefix .. "_jump_land_bwd",
            landLeft   = prefix .. "_jump_land_left",
            landRight  = prefix .. "_jump_land_right",
            crouchIdle = prefix .. "_crouch_idle",
            crouchFwd  = prefix .. "_crouch_run",
            crouchBwd  = prefix .. "_crouch_runbwd",
        },
        fp = {
            idle       = { prefix .. "_idle", { looping = true } },
            equip      = { prefix .. "_pickup", { nextAnimation = "idle" } },
            unequip    = { prefix .. "_putdown" },
        },
    }

    -- Extra tp anims (e.g. bucket sizebend)
    if config.animation.extraTpAnims then
        for k, v in pairs(config.animation.extraTpAnims) do
            set.tp[k] = v
        end
    end

    if config.animation.movementOverrides then
        for k, v in pairs(config.animation.movementOverrides) do
            set.movement[k] = v
        end
    end

    if config.animation.fpOverrides then
        for k, v in pairs(config.animation.fpOverrides) do
            set.fp[k] = v
        end
    end

    return set
end

function CarryTool.client_onCreate( self )
	self.cl = {}
	self.cl.carryItemData = {}
	self.cl.tpAnimations = createTpAnimations( self.tool, {} )
	self.cl.fpAnimations = createFpAnimations( self.tool, {} )
    
	self.cl.activeItemUuid = sm.uuid.getNil()
	self.cl_preloadRenderables()
	
end

function CarryTool.cl_preloadRenderables(self)
    for key,config in pairs(CarryConfig) do
        if config.animation.animObject then 
            sm.tool.preloadRenderables( config.animation.animObject)
        end

        sm.tool.preloadRenderables(config.animation.tp)
        sm.tool.preloadRenderables(config.animation.fp)
    end
end

function CarryTool.client_onRefresh( self )
    CarryType, CarryConfig, ItemOverrides, CarryTypePriority = loadCarryConfigJson()
	self:cl_updateActiveCarry()
	self:cl_preloadRenderables()
end

function CarryTool.cl_updateCarryRenderables( self, activeUid )
    
	if activeUid == sm.uuid.getNil() then return end
    local strUuid = tostring(activeUid)
	local carryRenderables = nil
	local animationRenderablesTp = {}
	local animationRenderablesFp = {}

	if RADIOACTIVE_CARRY_ITEMS[strUuid] then
		if not self.radioactiveEffect then
			self.radioactiveEffect = sm.effect.createEffect( "Reactor - Uranium_oreball_glow", self.tool:getOwner().character, nil, sm.effect.axis.all )
			self.radioactiveEffect:setOffsetPosition( sm.vec3.new( 0, 0, self.tool:getOwner().character:getHeight() ) )
			self.radioactiveEffect:start()
		end
	else
		if self.radioactiveEffect then
			self.radioactiveEffect:stop()
			self.radioactiveEffect = nil
		end
	end

	local rendSet = CarryConfig[self.cl.carryType].animation
	if rendSet == nil then 
		return 
	end

	local override = ItemOverrides[strUuid]
	if override and override.type then
		rendSet = CarryConfig[override.type].animation
	end

    animationRenderablesTp = rendSet.tp
    animationRenderablesFp = rendSet.fp
    if rendSet.animObject ~= nil then
        carryRenderables = rendSet.animObject
    end

	local currentRenderablesTp = {}
	local currentRenderablesFp = {}

	for k,v in pairs( animationRenderablesTp ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
	for k,v in pairs( animationRenderablesFp ) do currentRenderablesFp[#currentRenderablesFp+1] = v end

	self.emptyTpRenderables = shallowcopy( animationRenderablesTp )
	self.emptyFpRenderables = shallowcopy( animationRenderablesFp )
	if carryRenderables then
		for k,v in pairs( carryRenderables ) do currentRenderablesTp[#currentRenderablesTp+1] = v end
		for k,v in pairs( carryRenderables ) do currentRenderablesFp[#currentRenderablesFp+1] = v end
	end

    local shapeColor = sm.shape.getShapeTypeColor( activeUid )
	self.tool:setTpRenderables( currentRenderablesTp )
	self.tool:setTpColor( shapeColor )
	if self.tool:isLocal() then
		self.tool:setFpRenderables( currentRenderablesFp )
		self.tool:setFpColor( shapeColor )
	end
end

function CarryTool.client_onClientDataUpdate( self, clientData )
	if not self.tool:isLocal() then
		self.cl.activeItemUuid = clientData.activeUid
		self.cl.activeColor = clientData.activeColor
        self:cl_updateActiveCarry()
	end
end

function CarryTool.cl_loadAnimations( self, activeUid )
	if activeUid == nil or activeUid == sm.uuid.getNil() then
		return
	end
	local carry = self.cl.carryType
	
	if carry == nil then
		return
	end

	local config = CarryConfig[carry]
	local animSet = buildAnimSet(config)

	self.cl.tpAnimations = createTpAnimations( self.tool, animSet.tp )
    
	for name, animation in pairs( animSet.movement ) do
		self.tool:setMovementAnimation( name, animation )
	end
	
	self.cl.fpAnimations = createFpAnimations( self.tool, animSet.fp )
	setTpAnimation( self.cl.tpAnimations, "idle", 5.0 )
    self.cl.tpAnimations.blendSpeed = 0.2
    self.blendTime = 0.2
end

function CarryTool.client_onUpdate( self, dt )
    if self.cl and type(self.cl.effectStartTimer) == "number"   then
        self.cl.effectStartTimer = self.cl.effectStartTimer - dt 
    end
	if self.tool:isLocal() then
		updateFpAnimations( self.cl.fpAnimations, self.equipped, dt )
    end
    if self.cl.blocksEffect == nil and self.cl.activeItemUuid and self.cl.activeItemUuid ~= sm.uuid.getNil() then
        local isFP = self.tool:isInFirstPersonView()
        local desiredEffect = isFP and self.cl.carryEffectFp or self.cl.carryEffectTp
        local otherEffect = isFP and self.cl.carryEffectTp or self.cl.carryEffectFp
        local swapEffect = otherEffect and otherEffect:isPlaying()
        if swapEffect then
            otherEffect:stopImmediate()
        end

        if desiredEffect  then
            self.cl.carryEffect = desiredEffect
            local scale, pos, rot = self:cl_getCarryTransform( self.cl.activeItemUuid )
            if scale and pos and rot then
                desiredEffect:setScale( scale )
                desiredEffect:setOffsetPosition( pos )
                desiredEffect:setOffsetRotation( rot )
                if not blocksEffect and not desiredEffect:isPlaying() and self.cl.effectStartTimer and self.cl.effectStartTimer < 0 then
                    desiredEffect:start()
                    if CarryConfig[self.cl.carryType] and CarryConfig[self.cl.carryType].pickupEffects and not swapEffect then
                        for _, value in ipairs(CarryConfig[self.cl.carryType].pickupEffects) do
                            if value.uuid == self.cl.activeItemUuid or value.uuid == sm.uuid.getNil() then
                                sm.effect.playEffect( value.effectName, self.tool:getOwner().character.worldPosition )
                            end
                        end
                    end
                end
            end
        end
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
    local activeOverrideAnimation = self.tool:getOwner():getCharacter():hasActiveOverrideAnimation()

    if activeOverrideAnimation then
        return 
    end

    for name, animation in pairs( self.cl.tpAnimations.animations ) do
        animation.time = animation.time + dt

        if name == self.cl.tpAnimations.currentAnimation then
			animation.weight = math.min( animation.weight + ( self.cl.tpAnimations.blendSpeed * dt ), 1.0 )

            if animation.time >= animation.info.duration - self.blendTime then
				if ( name == "use" or name == "useempty" ) then
                    setTpAnimation( self.cl.tpAnimations, "idle", 10.0 )
                elseif name == "pickup" then
                    setTpAnimation( self.cl.tpAnimations, "idle", 0.001 )
                elseif animation.nextAnimation ~= "" then
                    setTpAnimation( self.cl.tpAnimations, animation.nextAnimation, 0.001 )
                end

            end
        else
			animation.weight = math.max( animation.weight - ( self.cl.tpAnimations.blendSpeed * dt ), 0.0 )
        end

        totalWeight = totalWeight + animation.weight
    end

    totalWeight = totalWeight == 0 and 1.0 or totalWeight
    for name, animation in pairs( self.cl.tpAnimations.animations ) do

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

    -- Bucket carry sizebend controls the positioning of the hands in the horizontal axis.
    if self.cl.activeItemUuid and self.cl.activeItemUuid ~= sm.uuid.getNil() then
        if self.cl.carryType and self.cl.carryType == CarryType.bucket then
            local config = CarryConfig[CarryType.bucket]
            local size = sm.item.getShapeSize(self.cl.activeItemUuid)
            if size == nil then return nil end

            local isFPView = self.tool:isInFirstPersonView()
            local activeConfigTransform = isFPView and config.transform.fp or config.transform.tp

            local defaultRotation = sm.item.getShapeRotation(self.cl.activeItemUuid)
            local orientationStrategy, facingStrategy, primaryAxis = getStrategies( config, activeConfigTransform )
            local shapeRotation = computeShapeRotation( size, defaultRotation, orientationStrategy, facingStrategy, primaryAxis, config.fixedRotation, isFPView )

            local subdivideRatio = 0.25
            local shapeExtent = size * subdivideRatio 

            local rotatedExtent = shapeRotation * activeConfigTransform.rotation * shapeExtent
            local scaleRatio = activeConfigTransform.scale.y / 0.25
            local footprint = math.floor(math.abs(rotatedExtent.z) / subdivideRatio * scaleRatio + 0.5)

            local function getStep(w)
                local s = config.sizeBend[math.floor(w)]
                return s or config.sizeBendDefault or 0.5
            end
            self.tool:updateAnimation( "bucket_sizebend", getStep(footprint), 1 )
            if self.tool:isLocal() then
                self.tool:updateFpAnimation( "bucket_sizebend", getStep(footprint), 1, false )
            end
        end
    end
    
    if self.cl.tossTimer then
    -- Large carry Toss animation 
    self.cl.tossTimer = self.cl.tossTimer + dt
    if not self.tool:isInFirstPersonView() and self.cl.tossTimer and self.cl.carryType == "large" then
        local tossHeight = getTossHeight( self.cl.tossTimer, TossConfig.duration, TossConfig.amplitude )
        if self.cl.tossTimer >= TossConfig.duration then
            tossHeight = 0
            self.cl.tossTimer = nil
        end
        if self.cl.carryEffect then
            local _, offset, _ = self:cl_getCarryTransform( self.cl.activeItemUuid )
            if offset then
                offset = offset + sm.vec3.new( 0, 0, tossHeight )
                self.cl.carryEffect:setOffsetPosition( offset )
            end
        end
    end
    end
end

function CarryTool.client_onToggle( self )
	return false
end

function CarryTool.client_onEquip( self, animate )
    self.cl.activeItemUuid = sm.uuid.getNil()
	if self.tool:isLocal() then
		self.tool:setBlockSprint( true )
		local carryContainer = sm.localPlayer.getCarry()
		self.cl.activeItemUuid = carryContainer:getItem( 0 ).uuid
		self.cl.activeColor = sm.localPlayer.getCarryColor()
		
		self:cl_updateActiveCarry()
		self.network:sendToServer( "sv_n_updateCarryRenderables", { activeItem = self.cl.activeItemUuid, activeColor = self.cl.activeColor } )

        if CarryConfig[self.cl.carryType] and CarryConfig[self.cl.carryType].pickupEffects and CarryConfig[self.cl.carryType].effectStartTimer == nil or CarryConfig[self.cl.carryType].effectStartTimer == 0   then
            for _, value in ipairs(CarryConfig[self.cl.carryType].pickupEffects) do
                if value.uuid == self.cl.activeItemUuid or value.uuid == sm.uuid.getNil() then
                    sm.effect.playEffect( value.effectName, self.tool:getOwner().character.worldPosition )
                end
            end
        end
	end
	self.cl.effectStartTimer = CarryConfig[self.cl.carryType] and CarryConfig[self.cl.carryType].effectStartTimer or 0.5
	self.wantEquipped = true
end



function CarryTool.cl_tryTumbleDrop( self, character, playerCarry, carryUuid, characterShape, playerCarryColor )
	if not sm.localPlayer.getPlayer().character:isTumbling() or not playerCarry then
		return
	end

	local config = CarryConfig[self.cl.carryType]
	if not config then
		return
	end

	local bone = config.transform.primaryBone
	local shapeOffset = sm.item.getShapeSize(self.cl.activeItemUuid) * 0.5 * 0.25
	local dropHeightOffset = 0.25
	local bonePos = character:getTpBonePos( bone )
	local dropPos = bonePos + sm.vec3.new(0, 0, shapeOffset.z + dropHeightOffset)

	local hit, result = sm.physics.raycast( bonePos, dropPos )
	if hit then
		dropPos = result.pointWorld - sm.vec3.new( 0, 0, shapeOffset.z )
	end

	local boneRot = character:getTpBoneRot( bone )
	local params = {
		containerA = playerCarry, itemA = carryUuid, quantityA = 1,
		aimPosition = dropPos,
		camUp = sm.camera.getUp(), camRight = sm.camera.getRight(), camDirection = sm.camera.getDirection(),
		raycastNormal = sm.camera.getUp(), color = playerCarryColor
	}
	if characterShape then
		params.characterShape = characterShape
		params.checkCollision = true
	end
	params.shapePlacement = {}
	params.shapePlacement.worldPosition = dropPos
	params.shapePlacement.worldRotation = boneRot
	self.network:sendToServer( "sv_n_dropCarry", params )
end

local function showInsertInteraction()
	local keyBindingText = sm.gui.getKeyBinding( "Create", true )
	sm.gui.setInteractionText( "", keyBindingText, "#{INTERACTION_INSERT}" )
end

function CarryTool.cl_tryInsert( self, character, primaryState, playerCarry, carryUuid, playerCarryColor )
	local aimRange = 7.5
	local vrPose, vrActive = nil, false
	if Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end
	local success, result
	if vrActive then
		success, result = sm.physics.raycast( vrPose.position, vrPose.position + vrPose.direction * aimRange, character )
	else
		success, result = sm.localPlayer.getRaycast( aimRange )
	end

	if result.type == "body" then
		local shape = result:getShape()
		local shapeUuid = shape:getShapeUuid()


		local containerTarget = ContainerInsertTargets[tostring(shapeUuid)]
		if containerTarget then
			local shapeContainer = shape.interactable:getContainer( containerTarget.slot )
			if isAnyOf( carryUuid, containerTarget.accepts ) and sm.container.canCollect( shapeContainer, carryUuid, 1 ) then
				showInsertInteraction()
				if primaryState == sm.tool.interactState.start then
					local fromPosition = character:getTpBonePos( CarryConfig[self.cl.carryType].transform.primaryBone  )
					local fromRotation = character:getTpBoneRot( CarryConfig[self.cl.carryType].transform.primaryBone  )
					local params = { containerA = playerCarry, itemA = carryUuid, quantityA = 1, targetShape = shape, fromPosition = fromPosition, fromRotation = fromRotation, color = playerCarryColor }
					self.network:sendToServer( "sv_n_sendItem", params )
				end
				return true
			end
			return false
		end

		
		local genericAccepts = GenericInsertTargets[tostring(shapeUuid)]
		if genericAccepts then
			if isAnyOf( carryUuid, genericAccepts ) then
				showInsertInteraction()
				if primaryState == sm.tool.interactState.start then
					local fromPosition = character:getTpBonePos( CarryConfig[self.cl.carryType].transform.primaryBone )
					local fromRotation = character:getTpBoneRot( CarryConfig[self.cl.carryType].transform.primaryBone )
					local params = { containerA = playerCarry, itemA = carryUuid, quantityA = 1, targetShape = shape, fromPosition = fromPosition, fromRotation = fromRotation, color = playerCarryColor }
					self.network:sendToServer( "sv_n_sendItem", params )
				end
				return true
			end
			return false
		end

	elseif result.type == "harvestable" then
		local harvestable = result:getHarvestable()
		local requiredCarry = HarvestableInsertTargets[tostring(harvestable.uuid)]
		if requiredCarry and carryUuid == requiredCarry then
			showInsertInteraction()
			if primaryState == sm.tool.interactState.start then
				local fromPosition = character:getTpBonePos( CarryConfig[self.cl.carryType].transform.primaryBone  )
				local fromRotation = character:getTpBoneRot( CarryConfig[self.cl.carryType].transform.primaryBone  )
				local params = { containerA = playerCarry, itemA = carryUuid, quantityA = 1, targetHarvestable = harvestable, fromPosition = fromPosition, fromRotation = fromRotation }
				self.network:sendToServer( "sv_n_sendItemToHarvestable", params )
			end
			return true
		end
	end

	return false
end

function CarryTool.cl_tryDrop( self, primaryState, secondaryState, playerCarry, carryUuid, characterShape, playerCarryColor )
	local vrPose, vrActive = nil, false
	if Chapter2VR and Chapter2VR.actionPose then vrPose, vrActive = Chapter2VR.actionPose() end
	if not (( primaryState == sm.tool.interactState.start and ( characterShape or vrActive ) ) or secondaryState == sm.tool.interactState.start) then
		return false
	end

	local dropRange = 7.5
	local rayStart = vrActive and vrPose.position or sm.localPlayer.getRaycastStart()
	local rayDirection = vrActive and vrPose.direction:normalize() or sm.localPlayer.getDirection()
	local success, result
	if vrActive then
		success, result = sm.physics.raycast( rayStart, rayStart + rayDirection * dropRange, self.tool:getOwner().character )
	else
		success, result = sm.localPlayer.getRaycast( dropRange )
	end

	local fraction = success and result.fraction or 1
	local aimPosition = rayStart + rayDirection * dropRange * fraction
	local params = {
		containerA = playerCarry, itemA = carryUuid, quantityA = 1,
		aimPosition = aimPosition,
		camUp = vrActive and vrPose.up or sm.camera.getUp(),
		camRight = vrActive and vrPose.right or sm.camera.getRight(),
		camDirection = rayDirection,
		raycastNormal = result.normalWorld, color = playerCarryColor
	}
	if characterShape then
		params.characterShape = characterShape
		if primaryState == sm.tool.interactState.start then
			params.checkCollision = true
		end
	end
	local worldPosition, worldRotation = nil, nil
	if not vrActive then worldPosition, worldRotation = sm.localPlayer.getConstructionPlacement() end
	if worldPosition ~= nil and worldRotation ~= nil then
		params.shapePlacement = {}
		params.shapePlacement.worldPosition = worldPosition
		params.shapePlacement.worldRotation = worldRotation
	end
    
	self.network:sendToServer( "sv_n_dropCarry", params )
	return true
end

function CarryTool.client_onEquippedUpdate( self, primaryState, secondaryState )
	if Chapter2VR then
		if Chapter2VR.markAdapter then Chapter2VR.markAdapter( "carry" ) end
		if Chapter2VR.primaryState then primaryState = Chapter2VR.primaryState( self, primaryState ) end
	end
	local playerCarry = sm.localPlayer.getCarry()
	local playerCarryColor = sm.localPlayer.getCarryColor()
	local carryUuid = sm.container.itemUuid( playerCarry )[1]
	local characterShape = sm.item.getCharacterShape( carryUuid )
	local character = self.tool:getOwner().character

	









	self:cl_tryTumbleDrop( character, playerCarry, carryUuid, characterShape, playerCarryColor )

	local _, vrActive = nil, false
	if Chapter2VR and Chapter2VR.actionPose then _, vrActive = Chapter2VR.actionPose() end
    if primaryState == sm.tool.interactState.start and ( characterShape or vrActive ) or secondaryState == sm.tool.interactState.start then
        local consumeInput = self:cl_tryDrop( primaryState, secondaryState, playerCarry, carryUuid, characterShape, playerCarryColor )
        return consumeInput, consumeInput
    elseif self:cl_tryInsert( character, primaryState, playerCarry, carryUuid, playerCarryColor ) then
        return true, true
    end

	return false, false
end

function CarryTool.cl_updateActiveCarry( self )
	if self.cl.activeItemUuid == nil then
        self:cl_stopAndDestroyEffects()
		return
	end
    self.cl.carryType = getCarryType( self.cl.activeItemUuid )
	self:cl_updateCarryRenderables( self.cl.activeItemUuid )
	self:cl_loadAnimations( self.cl.activeItemUuid )
    self.cl.blocksEffect = nil
    local strUuid = tostring( self.cl.activeItemUuid )
    if (ItemOverrides[strUuid]  ~= nil ) then
       local itemOverride = ItemOverrides[strUuid]
       self.cl.blocksEffect = itemOverride and itemOverride.blocksEffect
    end

    if self.cl.blocksEffect == nil or self.cl.blocksEffect == false  then 
        self:cl_updateCarryEffect( self.cl.activeItemUuid )
    else
        self:cl_stopAndDestroyEffects()
    end

	if self.cl.activeItemUuid == nil or self.cl.activeItemUuid == sm.uuid.getNil() then
		setTpAnimation( self.cl.tpAnimations, "putdown" )
		if self.tool:isLocal() and self.cl.fpAnimations.currentAnimation ~= "unequip" then
			swapFpAnimation( self.cl.fpAnimations, "equip", "unequip", 0.2 )
		end
	else
		self.cl.tossTimer = 0
		setTpAnimation( self.cl.tpAnimations, "pickup", 0.0001 )
		if self.tool:isLocal() then
			swapFpAnimation( self.cl.fpAnimations, "unequip", "equip", 0.2 )
		end
	end
end

function CarryTool.cl_createEffect( self, activeUid, isFirstPerson, existingEffect )

    if activeUid == nil or activeUid == sm.uuid.getNil() then return nil end

    if  self.cl.carryType == nil  then return nil end

    local config = CarryConfig[self.cl.carryType]
    if config == nil then return nil end

	local owner = self.tool:getOwner():getCharacter()
    local  uuid = self.cl.carryType == CarryType.heavyTool and sm.uuid.new("e0fcf485-d0e7-4279-b42d-6a9b4347f616") or activeUid

    local effect = existingEffect
    if effect then
        effect:stop()
    else
        local createEffect = isFirstPerson and sm.effect.createEffectFirstPerson or sm.effect.createEffect
        effect = createEffect( "CarryTool - Item" )
        
        effect:setHost(owner, config.transform.primaryBone)
    end

    effect:setParameter("uuid", uuid)
    effect:setParameter("color", self.cl.activeColor)

    return effect
end

function CarryTool.cl_stopAndDestroyEffects( self )
    if self.cl.carryEffectTp then
        self.cl.carryEffectTp:stop()
        self.cl.carryEffectTp = nil
    end
    if self.cl.carryEffectFp then
        self.cl.carryEffectFp:stop()
        self.cl.carryEffectFp = nil
    end
    self.cl.carryEffect = nil

    if self.cl.hostedCarryEffect then
        self.cl.hostedCarryEffect:stopImmediate()
        self.cl.hostedCarryEffect = nil
    end
end

function CarryTool.cl_updateCarryEffect( self, activeUid )
    if RADIOACTIVE_CARRY_ITEMS[tostring(activeUid)] then
        if not self.radioactiveEffect then
            self.radioactiveEffect = sm.effect.createEffect("Reactor - Uranium_oreball_glow", self.tool:getOwner().character, nil, sm.effect.axis.all)
            self.radioactiveEffect:setOffsetPosition(sm.vec3.new(0, 0, self.tool:getOwner().character:getHeight()))
            self.radioactiveEffect:start(0)
        end
    else
        if self.radioactiveEffect then
            self.radioactiveEffect:stop()
            self.radioactiveEffect = nil
        end
    end
    
    if self.cl.carryType == nil  then
        return
    end

    -- Mirrors effect played on placed part, hosted on carried item's bone.
    local featureData = sm.item.getFeatureData( activeUid )
    if featureData and featureData.data and featureData.data.hostedEffectOnCarry and featureData.data.hostedEffect and featureData.data.hostedEffect ~= "" then
        if self.cl.hostedCarryEffect then
            self.cl.hostedCarryEffect:stopImmediate()
            self.cl.hostedCarryEffect = nil
        end

        local owner = self.tool:getOwner():getCharacter()
        self.cl.hostedCarryEffect = sm.effect.createEffect( featureData.data.hostedEffect, owner, CarryConfig[self.cl.carryType].transform.primaryBone )
        self.cl.hostedCarryEffect:start()
    end

    self.cl.carryEffectTp = self:cl_createEffect(activeUid, false, self.cl.carryEffectTp)
    if self.tool:isLocal() then
        self.cl.carryEffectFp = self:cl_createEffect(activeUid, true, self.cl.carryEffectFp)
    end

    if self.tool:isInFirstPersonView() then
        self.cl.carryEffect = self.cl.carryEffectFp
    else
        self.cl.carryEffect = self.cl.carryEffectTp
    end
end

function CarryTool.client_onUnequip( self )
	self.cl.activeItemUuid = nil
	if sm.exists( self.tool ) then
		self:cl_updateActiveCarry()
		if self.tool:isLocal() then
			self.tool:setBlockSprint( false )
			self.network:sendToServer( "sv_n_updateCarryRenderables", { activeItem = self.cl.activeItemUuid, activeColor = self.cl.activeColor } )
		end
	end

	if self.radioactiveEffect then
		self.radioactiveEffect:stopImmediate()
		self.radioactiveEffect = nil
	end
    self.cl.effectStartTimer = nil
    self.cl.tossTimer = nil
	self.wantEquipped = false
	self.equipped = false
end

function Sv_DropCarry( params )
	if params.characterShape then
		local offset = params.raycastNormal * params.characterShape.placementSphereRadius
		local spawnPosition = params.aimPosition + offset
		local collision = false
		if params.checkCollision then
			collision = sm.physics.sphereHasContact( spawnPosition, params.characterShape.placementSphereRadius )
		end
		if not collision then
			if sm.container.beginTransaction() then
				sm.container.spendFromSlot( params.containerA, 0, params.itemA, params.quantityA, true )
				if sm.container.endTransaction() then
					local p = { deathTick = sm.game.getCurrentTick() + DaysInTicks( 30 ), isCattle = true }
					sm.unit.createUnit( sm.uuid.new( params.characterShape.characterUuid ), spawnPosition, math.random() * 2 * math.pi, p )
				end
			end
		end
	else
		local shapeOffset = sm.item.getShapeOffset( params.itemA )
		local shapeRotation = sm.item.getShapeRotation( params.itemA )
		-- need valid placement
		if params.shapePlacement and sm.container.beginTransaction() then
			sm.container.spendFromSlot( params.containerA, 0, params.itemA, params.quantityA, true )
			if sm.container.endTransaction() then
				local shape = nil
				if sm.item.isMultiShape( params.itemA ) then
					local spawnWorldPosition = params.shapePlacement.worldPosition - params.shapePlacement.worldRotation * shapeOffset
					shape = sm.creation.buildMultiShape( params.itemA,
						spawnWorldPosition, params.shapePlacement.worldRotation * sm.quat.inverse( shapeRotation ), -- Global transform
						sm.vec3.new(0,0,0), shapeRotation )										   -- Local transform
				else
					if params.shapePlacement then
						local spawnWorldPosition = params.shapePlacement.worldPosition - params.shapePlacement.worldRotation * shapeOffset
						shape = sm.shape.createPart( params.itemA, spawnWorldPosition, params.shapePlacement.worldRotation, true, true )
					end
				end
				assert( shape )
				shape:setColor( params.color )

				if shape.interactable and shape.interactable:getType() == "scripted" then
					shape.interactable:setCarryData( params.player:getCarryData() )
				end
				params.player:setCarryData( nil )
				if params.containerA:getSize() > 1 then -- Additional items in carry
					local interactable = shape:getInteractable()
					local container = nil
					if interactable then
						container = interactable:getContainer( 0 )
						if container == nil then
							container = interactable:addContainer( 0, params.containerA:getSize() - 1, params.containerA:getMaxStackSize() )
						end
					end
					sm.container.beginTransaction()
					for slot = 2, params.containerA:getSize() do
						local item = params.containerA:getItem( slot - 1 )
						--sm.container.spendFromSlot( params.containerA, slot - 1, item.uuid, item.quantity, true )
						--sm.container.collectToSlot( container, slot - 2, item.uuid, item.quantity, true )
						sm.container.setItem( params.containerA, slot - 1, sm.uuid.getNil(), 0 )
						if container then
							sm.container.setItem( container, slot - 2, item.uuid, item.quantity, item.instance )
						end
					end
					if not sm.container.endTransaction() then
						sm.log.error("Failed to move carry items!")
					end
				end
			end
		end
	end
end
function CarryTool.sv_n_dropCarry( self, params, player )
    params.player = player
	Sv_DropCarry( params )
end

function CarryTool.sv_n_sendItem( self, params, player )
	params.player = player
	sm.event.sendToInteractable( params.targetShape.interactable, "sv_e_receiveItem", params )
end

function CarryTool.sv_n_sendItemToHarvestable( self, params, player )
	params.player = player
	sm.event.sendToHarvestable( params.targetHarvestable, "sv_e_receiveItem", params )
end