ElevatorButton = class()
ElevatorButton.maxParentCount = 0
ElevatorButton.maxChildCount = 1
ElevatorButton.connectionInput = sm.interactable.connectionType.none
ElevatorButton.connectionOutput = sm.interactable.connectionType.logic
ElevatorButton.poseWeightCount = 1

function ElevatorButton.server_onCreate( self )
	self.sv = {}
	self.sv.goingUp = false
end

function ElevatorButton.client_onCreate( self )
	self.cl = {}
	self.cl.goingUp = false
	self.cl.effect = sm.effect.createEffect( "Elevator Button", self.interactable )
end

function ElevatorButton.client_onInteract( self, character, state )
	if state == true then
		self.network:sendToServer( "sv_push", not self.cl.goingUp )
	end
end

function ElevatorButton.server_onProjectile( self, hitPos, hitTime, hitVelocity, _, attacker, damage, userData, hitNormal, projectileUuid )
	self:sv_push( not self.sv.goingUp )
end

function ElevatorButton.sv_e_vrInteract( self )
	self:sv_push( not self.sv.goingUp )
end

function ElevatorButton.sv_push( self, goingUp )
	if goingUp ~= self.sv.goingUp then
		self.sv.goingUp = goingUp
		self.network:sendToClients( "cl_push", self.sv.goingUp )
		self.interactable.active = true
	end
end

function ElevatorButton.cl_push( self, goingUp )
	self.cl.goingUp = goingUp
	if goingUp then
		self.interactable:setPoseWeight( 0, 1.0 ) -- Up
	else
		self.interactable:setPoseWeight( 0, 0.0 ) -- Down
	end
	self.cl.effect:start()
end
