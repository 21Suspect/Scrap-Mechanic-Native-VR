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
    if size == nil then
        return nil, nil, nil 
    end
    local defaultRotation = sm.item.getShapeRotation(uid)
    local orientationStrategy, facingStrategy, primaryAxis = getStrategies( config, activeConfigTransform )
    local shapeRotation = computeShapeRotation( size, defaultRotation, orientationStrategy, facingStrategy, primaryAxis, config.fixedRotation, isFPView )

    local character = self.tool:getOwner().character
    local fnGetBonePos = ( self.tool:isLocal() and isFPView) and sm.localPlayer.getFpBonePos or function(bone) return character:getTpBonePos(bone) end

    local primaryBonePos = fnGetBonePos( config.transform.primaryBone )
    local secondaryBonePos = fnGetBonePos( config.transform.secondaryBone )
    if not primaryBonePos or not secondaryBonePos then
        return nil, nil, nil
    end
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
	local override = ItemOverrides[tostring(uuid)]

	if sm.item.isMultiShape(uuid) and not override then
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
        append(set.tp, config.animation.extraTpAnims)
    end

    if config.animation.movementOverrides then
        append(set.movement, config.animation.movementOverrides)
    end

    if config.animation.fpOverrides then
        append(set.fp, config.animation.fpOverrides)
    end

    return set
end

function Sv_DropCarry( params )
	if params.characterShape then
		local offset = params.raycastNormal * params.characterShape.placementSphereRadius
		local spawnPosition = params.aimPosition + offset
		local collision =  params.checkCollision and sm.physics.sphereHasContact( spawnPosition, params.characterShape.placementSphereRadius )

		if not collision then
			if sm.container.beginTransaction() then
				sm.container.spendFromSlot( params.containerA, 0, params.itemA, params.quantityA, true )
				if sm.container.endTransaction() then
					local p = { deathTick = sm.game.getCurrentTick() + DaysInTicks( 30 ), isCattle = true }
                    local spawnHeading = math.random() * 2 * math.pi
                    if params.shapePlacement then
                        local spawnDirection = params.shapePlacement.worldRotation * sm.vec3.new( 0, 0, 1 )
                        spawnHeading = math.atan2( spawnDirection.y, spawnDirection.x ) - math.pi / 2
                    end
                    sm.unit.createUnit( sm.uuid.new( params.characterShape.characterUuid ), spawnPosition, spawnHeading, p )
				end
			end
		end
	elseif params.shapePlacement and sm.container.beginTransaction() then
            -- need valid placement
            local shapeOffset = sm.item.getShapeOffset( params.itemA )
            local shapeRotation = sm.item.getShapeRotation( params.itemA )
			sm.container.spendFromSlot( params.containerA, 0, params.itemA, params.quantityA, true )
			if sm.container.endTransaction() then
				local shape = nil
                local spawnWorldPosition = params.shapePlacement.worldPosition - params.shapePlacement.worldRotation * shapeOffset
				if sm.item.isMultiShape( params.itemA ) then
					shape = sm.creation.buildMultiShape( params.itemA,
					spawnWorldPosition, params.shapePlacement.worldRotation * sm.quat.inverse( shapeRotation ), -- Global transform
						sm.vec3.new(0,0,0), shapeRotation ) -- Local transform
				else
                    shape = sm.shape.createPart( params.itemA, spawnWorldPosition, params.shapePlacement.worldRotation, true, true )
				end
				assert( shape )
				shape:setColor( params.color )

				if shape.interactable and shape.interactable:getType() == "scripted" then
					shape.interactable:setCarryData( params.player:getCarryData() )
				end
				params.player:setCarryData( nil )
				if params.containerA:getSize() > 1 then -- Additional items in carry
					local interactable = shape:getInteractable()
					local container = interactable and ( interactable:getContainer( 0 ) or interactable:addContainer( 0, params.containerA:getSize() - 1, params.containerA:getMaxStackSize() ) )
                    assert( container )
                    sm.container.beginTransaction()
                    for slot = 2, params.containerA:getSize() do
                        local item = params.containerA:getItem( slot - 1 )
                        sm.container.setItem( params.containerA, slot - 1, sm.uuid.getNil(), 0 )
                        sm.container.setItem( container, slot - 2, item.uuid, item.quantity, item.instance )
                    end
                    if not sm.container.endTransaction() then
                        sm.log.error("Failed to move carry items!")
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

function CarryTool.sv_n_onPickup( self, param, player ) 
	local carryBondBuilder = param.activeItem and CarryBondBuilderItems[tostring( param.activeItem )]
	if carryBondBuilder then
		DialogManager.Sv_PlayNamedBondBuilder( carryBondBuilder )
	end
end

function CarryTool.client_onCreate( self )
	self.cl = {}
	self.cl.carryItemData = {}
	self.cl.tpAnimations = createTpAnimations( self.tool, {} )
    if self.tool:isLocal() then
	    self.cl.fpAnimations = createFpAnimations( self.tool, {} )
    end
	self.cl.activeItemUuid = sm.uuid.getNil()
	self.cl_preloadRenderables()
end

function CarryTool.cl_preloadRenderables(self)
    for _,config in pairs(CarryConfig) do
        if config.animation.animObject then 
            sm.tool.preloadRenderables( config.animation.animObject)
        end
        sm.tool.preloadRenderables(config.animation.tp)
        sm.tool.preloadRenderables(config.animation.fp)
    end
end

function CarryTool.client_onRefresh( self )
    CarryType, CarryConfig, ItemOverrides, CarryTypePriority = loadCarryConfigJson()
	self:cl_preloadRenderables()
end

function CarryTool.client_onUpdate( self, dt )
    if not self.tool:isEquipped() then
        return 
    end
    local player = self.tool:getOwner()
    local character = player.character
    if not character then
        return
    end

    local isSwimming = character:isSwimming()
    local hasTpSwimIdle = self.tool:hasTpAnimation( "swimIdle" )
    if isSwimming and not self.cl.wasSwimming and not hasTpSwimIdle then
        if self.cl.carryEffectTp and self.cl.carryEffectTp:isPlaying() then
            self.cl.carryEffectTp:stopImmediate()
        end
        if self.cl.hostedCarryEffectTp and self.cl.hostedCarryEffectTp:isPlaying() then
            self.cl.hostedCarryEffectTp:stopImmediate()
        end
    end
    self.cl.wasSwimming = isSwimming

    if character:hasActiveOverrideAnimation() then
        return 
    end
    if self.cl.effectStartTimer then
        self.cl.effectStartTimer = self.cl.effectStartTimer - dt
    end

    local isFP = self.tool:isInFirstPersonView()
    local tpSwimmingWithNoSwimIdle = not isFP and isSwimming and not hasTpSwimIdle
    if self.cl.blocksEffect == nil then
        local desiredEffect = isFP and self.cl.carryEffectFp or self.cl.carryEffectTp
        if desiredEffect and not desiredEffect:isPlaying() then
            local otherEffect = isFP and self.cl.carryEffectTp or self.cl.carryEffectFp
            local desiredHostedEffect = isFP and self.cl.hostedCarryEffectFp or self.cl.hostedCarryEffectTp
            local otherHostedEffect = isFP and self.cl.hostedCarryEffectTp or self.cl.hostedCarryEffectFp
            local swapEffect = otherEffect and otherEffect:isPlaying()
            if swapEffect then
                otherEffect:stopImmediate()
            end
            if otherHostedEffect and otherHostedEffect:isPlaying() then
                otherHostedEffect:stopImmediate()
            end

            if not tpSwimmingWithNoSwimIdle and self.cl.effectStartTimer and self.cl.effectStartTimer < 0 then
                local scale, pos, rot = self:cl_getCarryTransform( self.cl.activeItemUuid )
                if scale and pos and rot then
                    desiredEffect:setScale( scale )
                    desiredEffect:setOffsetPosition( pos )
                    desiredEffect:setOffsetRotation( rot )
                    desiredEffect:start()
                    self.cl.carryEffect = desiredEffect
                    if desiredHostedEffect then
                        desiredHostedEffect:setScale( scale )
                        desiredHostedEffect:setOffsetPosition( pos )
                        desiredHostedEffect:setOffsetRotation( rot )
                        desiredHostedEffect:start()
                    end
                end
            end
        end
    end
    
    if self.tool:isLocal() then
        updateFpAnimations( self.cl.fpAnimations, self.tool:isEquipped(), dt )
    end
    updateTpAnimations( self.cl.tpAnimations, self.tool:isEquipped(), dt )

    -- Bucket carry sizebend controls the positioning of the hands in the horizontal axis.
    if not tpSwimmingWithNoSwimIdle and self.cl.carryType and self.cl.carryType == CarryType.bucket then
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
    if self.cl.tossTimer then
        -- Large carry Toss animation 
        self.cl.tossTimer = self.cl.tossTimer + dt
        if not self.tool:isInFirstPersonView() and self.cl.tossTimer then
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

function CarryTool.client_onEquip( self, animate )
	if self.tool:isLocal() then
		self.tool:setBlockSprint( true )
	end
    local player = self.tool:getOwner()
    local activeItemUuid = player:getCarry():getItem( 0 ).uuid
    local activeColor = sm.localPlayer.getCarryColor() 
    self.cl.carryType = getCarryType( activeItemUuid )
    
    local config = CarryConfig[self.cl.carryType]
    local rendSet = config.animation
    local animSet = buildAnimSet(config)
    --Load renderables
    local currentRenderablesTp = shallowcopy(rendSet.tp)
    if rendSet.animObject then
        concat(currentRenderablesTp, rendSet.animObject)
    end
    self.tool:setTpRenderables( currentRenderablesTp )
    if self.tool:isLocal() then
        local currentRenderablesFp = shallowcopy(rendSet.fp)
        if rendSet.animObject then 
            concat(currentRenderablesFp, rendSet.animObject)
        end
        self.tool:setFpRenderables( currentRenderablesFp )
        self.cl.fpAnimations = createFpAnimations( self.tool, animSet.fp )
        setFpAnimation( self.cl.fpAnimations, "equip", 0.2 )
    end

	self.cl.tpAnimations = createTpAnimations( self.tool, animSet.tp )
	for name, animation in pairs( animSet.movement ) do
		self.tool:setMovementAnimation( name, animation )
	end
    setTpAnimation( self.cl.tpAnimations, "pickup", 0.0001 )
   
    self.cl.tpAnimations.blendSpeed = 0.2
    self.blendTime = 0.2

    -- Create effects
    local itemOverride = ItemOverrides[tostring( activeItemUuid )]
    self.cl.blocksEffect = itemOverride and itemOverride.blocksEffect
    if not self.cl.blocksEffect then
        self:cl_createCarryEffects( activeItemUuid,activeColor )
        self.cl.effectStartTimer = ( config and config.effectStartTimer ) or 0.15
        self.cl.tossTimer = self.cl.carryType == "large" and 0 or nil
    end
	
    -- Play pickup sound effects
	if config and config.pickupEffects and ( config.effectStartTimer == nil or config.effectStartTimer == 0 ) then
		for _, value in ipairs( config.pickupEffects ) do
			if value.uuid == nil or value.uuid == activeItemUuid or value.uuid == sm.uuid.getNil() then
				sm.effect.playEffect( value.effectName, player.character.worldPosition )
			end
		end
	end

    self.cl.activeItemUuid = activeItemUuid
    self.cl.activeColor = activeColor
    self.network:sendToServer( "sv_n_onPickup", { activeItem = self.cl.activeItemUuid, activeColor = self.cl.activeColor } )
end

function CarryTool.cl_tryTumbleDrop( self, character, playerCarry, carryUuid, characterShape, playerCarryColor )
	if not sm.localPlayer.getPlayer().character:isTumbling() or not playerCarry then
		return
	end

	local config = CarryConfig[self.cl.carryType]
	local bone = config.transform.primaryBone
	local shapeOffset = sm.item.getShapeSize(self.cl.activeItemUuid) * 0.5 * 0.25
	local dropHeightOffset = 0.25
	local bonePos = bone and character:getTpBonePos( bone ) or character:getWorldPosition()
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
	-- The native VR ray hook feeds the current action pose into the engine's
	-- latest player ray.  Keep using the stock construction-placement helper
	-- for VR as well so carried bodies (including multi-shape/large items) get
	-- the same grid snapping and collision validation as desktop placement.
	local worldPosition, worldRotation = sm.localPlayer.getConstructionPlacement()
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

function CarryTool.cl_createEffect( self, activeUid,activeColor ,isFirstPerson, existingEffect )
    local config = CarryConfig[self.cl.carryType]
    if config == nil then return nil end

    local owner = self.tool:getOwner():getCharacter()
    local uuid = self.cl.carryType == CarryType.heavyTool and sm.uuid.new("e0fcf485-d0e7-4279-b42d-6a9b4347f616") or activeUid

    local effect = existingEffect
    if effect then
        effect:stop()
    else
        local createEffect = isFirstPerson and sm.effect.createEffectFirstPerson or sm.effect.createEffect
        effect = createEffect( "CarryTool - Item" )
        effect:setHost(owner, config.transform.primaryBone)
    end

    effect:setParameter("uuid", uuid)
    effect:setParameter("color", activeColor)
    return effect
end

function CarryTool.cl_createCarryEffects( self, activeUid, activeColor )
    -- Mirrors effect played on placed part, hosted on carried item's bone.
    local featureData = sm.item.getFeatureData( activeUid )
    if featureData and featureData.data and featureData.data.hostedEffectOnCarry and featureData.data.hostedEffect and featureData.data.hostedEffect ~= "" then
        if self.cl.hostedCarryEffectTp then
            self.cl.hostedCarryEffectTp:stopImmediate()
            self.cl.hostedCarryEffectTp = nil
        end
        if self.cl.hostedCarryEffectFp then
            self.cl.hostedCarryEffectFp:stopImmediate()
            self.cl.hostedCarryEffectFp = nil
        end

        local owner = self.tool:getOwner():getCharacter()
        local primaryBone = CarryConfig[self.cl.carryType].transform.primaryBone
        self.cl.hostedCarryEffectTp = sm.effect.createEffect( featureData.data.hostedEffect, owner, primaryBone )
        if self.tool:isLocal() then
            self.cl.hostedCarryEffectFp = sm.effect.createEffectFirstPerson( featureData.data.hostedEffect )
            self.cl.hostedCarryEffectFp:setHost( owner, primaryBone )
        end
    end

    self.cl.carryEffectTp = self:cl_createEffect( activeUid,activeColor ,false, self.cl.carryEffectTp)
    if self.tool:isLocal() then
        self.cl.carryEffectFp = self:cl_createEffect( activeUid,activeColor ,true, self.cl.carryEffectFp)
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
            if sm.exists(self.cl.carryEffectTp) then
            self.cl.carryEffectTp:stop()
            self.cl.carryEffectTp = nil
        end
        if sm.exists(self.cl.carryEffectFp) then
            self.cl.carryEffectFp:stop()
            self.cl.carryEffectFp = nil
        end
        self.cl.carryEffect = nil

        if sm.exists(self.cl.hostedCarryEffectTp) then
            self.cl.hostedCarryEffectTp:stopImmediate()
            self.cl.hostedCarryEffectTp = nil
        end
        if sm.exists(self.cl.hostedCarryEffectFp) then
            self.cl.hostedCarryEffectFp:stopImmediate()
            self.cl.hostedCarryEffectFp = nil
        end
		if self.tool:isLocal() then
			self.tool:setBlockSprint( false )
		end
	end
	
    self.cl.effectStartTimer = nil
    self.cl.tossTimer = nil
	self.cl.wantEquipped = nil
	self.cl.equipped = nil
end

