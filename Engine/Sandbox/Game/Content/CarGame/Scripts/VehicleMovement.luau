VehicleMovement = EntityScript.new()


local SpringK = 800000.0
local Damping = 15600.0
local RestLength = 3.35

function VehicleMovement:OnUpdate(DeltaTime)

	local BodyID: number = self:GetBodyID()
	
	local Location = self.Transform:GetLocation()
	local ForwardAxis: number = Input.GetActionAxis("MoveY")
	local RightAxis: number = Input.GetActionAxis("MoveX")
	
	local Up: vector = self.Transform:GetUp()
	local Forward: vector = self.Transform:GetForward()
	local Right: vector = self.Transform:GetRight()
	
	local LinearVelocity = self._Physics:GetLinearVelocity(BodyID)

	local ForwardSpeed = vector.dot(Forward, LinearVelocity)
	local SideSpeed = vector.dot(Right, LinearVelocity)
	
	local HalfLength = 7.5
	local HalfWidth  = 3.0
	local HalfHeight = 2.0

	local Points =
	{
    	Location + Forward * HalfLength + Right * HalfWidth + Up * HalfHeight,
    	Location + Forward * HalfLength + Right * HalfWidth - Up * HalfHeight,
    	Location + Forward * HalfLength - Right * HalfWidth + Up * HalfHeight,
    	Location + Forward * HalfLength - Right * HalfWidth - Up * HalfHeight,

    	Location - Forward * HalfLength + Right * HalfWidth + Up * HalfHeight,
    	Location - Forward * HalfLength + Right * HalfWidth - Up * HalfHeight,
    	Location - Forward * HalfLength - Right * HalfWidth + Up * HalfHeight,
    	Location - Forward * HalfLength - Right * HalfWidth - Up * HalfHeight,
	}
	
	for _, Origin in ipairs(Points) do
	
		local RaySettings = SRayCastSettings.new()
		RaySettings.DebugDuration = 0.2
		RaySettings.bDrawDebug = false
		RaySettings.Start = Origin
		RaySettings.End = Origin - Up * 10.0
		RaySettings.LayerMask = ECollisionProfiles.Static
		RaySettings:AddIgnoredBody(BodyID)
		
		local Hit = self.World:CastRay(RaySettings)
		
		if Hit then
			local Distance = Hit.Distance
			if Distance < RestLength then
				local Compression = RestLength - Distance
				
				local Velocity = self._Physics:GetVelocityAtPoint(BodyID, Origin)
				
				local VelocityAlongNormal = vector.dot(Velocity, Up)
				
				local ForceMag = (Compression * SpringK) - (VelocityAlongNormal * Damping)
				
				local ImpulseEvent: SAddImpulseAtPositionEvent = SAddImpulseAtPositionEvent.new()
				ImpulseEvent.BodyID = BodyID
				ImpulseEvent.Impulse = Up * ForceMag
				ImpulseEvent.Position = Origin
				self._Physics:AddImpulseAtPosition(ImpulseEvent)
			end
		end
	end
	
	
	local EngineImpulse = SImpulseEvent.new()
	EngineImpulse.BodyID = BodyID
	EngineImpulse.Impulse = Forward * (ForwardAxis * 150000.0)

	self._Physics:ApplyImpulse(EngineImpulse)

	local Velocity = self._Physics:GetLinearVelocity(BodyID)

	local Speed = vector.magnitude(Velocity)

	if Speed > 0.001 then
    	local AlignedVelocity = Forward * vector.dot(Velocity, Forward)

    	local Grip = 12.0
    	local Alpha = math.clamp(Grip * DeltaTime, 0.0, 1.0)

    	local NewVelocity = vector.lerp(Velocity, AlignedVelocity, Alpha)

    	self._Physics:SetLinearVelocity(BodyID, NewVelocity)
	end

	-- STEERING (scale with actual forward speed)
	local SpeedFactor = math.clamp(math.abs(ForwardSpeed) / 20.0, 0.0, 1.0)
	SpeedFactor = SpeedFactor * SpeedFactor

	local SteerStrength = 250000.0

	local Torque = SAngularImpulseEvent.new()
	Torque.BodyID = BodyID
	Torque.AngularImpulse = Up * (RightAxis * SteerStrength * SpeedFactor)

	self._Physics:ApplyAngularImpulse(Torque)
	

end


return VehicleMovement