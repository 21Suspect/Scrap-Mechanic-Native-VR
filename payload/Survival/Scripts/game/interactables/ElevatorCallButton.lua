ElevatorCallButton = class()
ElevatorCallButton.maxParentCount = 0
ElevatorCallButton.maxChildCount = 1
ElevatorCallButton.connectionInput = sm.interactable.connectionType.none
ElevatorCallButton.connectionOutput = sm.interactable.connectionType.logic
ElevatorCallButton.poseWeightCount = 1
ElevatorCallButton.resetStateOnInteract = false

local CallButtonState = { off = 1, destB = 2, destA = 3, goingB = 4, goingA = 5 }

function ElevatorCallButton.server_onCreate( self )
	self.sv = {}
	self.sv.state = CallButtonState.off

	self.interactable:setPublicData( { destination = "", ticksToDestination = 0 })
end

function ElevatorCallButton.server_onFixedUpdate( self )
	local publicData = self.interactable:getPublicData()
	local dest = publicData.destination
	local ticks = publicData.ticksToDestination
	local elevatorHome = publicData.elevatorHome

	local state = CallButtonState.off
	if dest == "b" then
		if ticks > 0 then
			state = CallButtonState.goingB
		else
			state = CallButtonState.destB
		end
	elseif dest == "a" then
		if ticks > 0 then
			state = CallButtonState.goingA
		else
			state = CallButtonState.destA
		end
	end

	if state ~= self.sv.state then
		self.network:setClientData( { state = state, elevatorHome = elevatorHome } )
		self.sv.state = state
	end

end

function ElevatorCallButton.sv_push( self, state )
	self.interactable:setActive( true )
end

function ElevatorCallButton.client_onCreate( self )
	self.cl = {}
	self.cl.held = false
	self.cl.pressed = false
	self.cl.state = CallButtonState.off
	self.cl.loopingIndex = 4
	self.cl.elevatorMoving = sm.effect.createEffect( "Elevator - MoveOutside", self.interactable )
end

function ElevatorCallButton.client_onInteract( self, character, state )
	self.network:sendToServer( "sv_push", state )
	self.cl.held = state
end

function ElevatorCallButton.server_onProjectile( self, hitPos, hitTime, hitVelocity, _, attacker, damage, userData, hitNormal, projectileUuid )
	self:sv_push( true )
end

function ElevatorCallButton.sv_e_vrInteract( self )
	self:sv_push( true )
end

function ElevatorCallButton.client_onFixedUpdate( self )
	if self.cl.held or self.cl.pressed then
		self.interactable:setPoseWeight( 0, 1.0 ) -- Down
	else
		self.interactable:setPoseWeight( 0, 0.0 ) -- Up
	end
	self.cl.pressed = false
end

local LoopingSpeed = 4.0

function ElevatorCallButton.client_onUpdate( self, dt )

	--print( self.cl.state  )
	if self.cl.state == CallButtonState.off then
		self.interactable:setUvFrameIndex( 0 )
	elseif self.cl.state == CallButtonState.destB then
		self.interactable:setUvFrameIndex( 1 )
	elseif self.cl.state == CallButtonState.destA then
		self.cl.elevatorMoving:stop()
		self.interactable:setUvFrameIndex( 12 )
	elseif self.cl.state == CallButtonState.goingA then
		self.cl.loopingIndex = self.cl.loopingIndex + dt * LoopingSpeed
		if self.cl.loopingIndex > 7 then
			self.cl.loopingIndex = 4
		end
		self.interactable:setUvFrameIndex( math.floor( self.cl.loopingIndex ) )
	elseif self.cl.state == CallButtonState.goingB then
		self.cl.loopingIndex = self.cl.loopingIndex + dt * LoopingSpeed
		if self.cl.loopingIndex > 11 then
			self.cl.loopingIndex = 8
		end
		self.interactable:setUvFrameIndex( math.floor( self.cl.loopingIndex ) )
	end
end

function ElevatorCallButton.client_onClientDataUpdate( self, clientData )
	if clientData.state == CallButtonState.goingB then
		self.cl.loopingIndex = 8
		
		if clientData.elevatorHome == "a" then
			sm.effect.playHostedEffect( "Elevator - Closedoor", self.interactable )
		end

		self.cl.elevatorMoving:start()
	
	elseif clientData.state == CallButtonState.goingA then
		self.cl.loopingIndex = 4
		
		if clientData.elevatorHome == "b" then
			sm.effect.playHostedEffect( "Elevator - Closedoor", self.interactable )
		end

		self.cl.elevatorMoving:start()

	elseif self.cl.state == CallButtonState.goingA and clientData.state == CallButtonState.destA then
		self.cl.elevatorMoving:stop()
		
		if clientData.elevatorHome == "a" then
			sm.effect.playHostedEffect( "Elevator - Opendoor", self.interactable )
		end

	elseif self.cl.state == CallButtonState.goingB and clientData.state == CallButtonState.destB then
		self.cl.elevatorMoving:stop()
		
		if clientData.elevatorHome == "b" then
			sm.effect.playHostedEffect( "Elevator - Opendoor", self.interactable )
		end
	end

	self.cl.state = clientData.state
end
