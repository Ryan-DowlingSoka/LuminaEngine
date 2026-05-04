local EntityScript = require("Stdlib/EntityScript")
local VehicleMovement = EntityScript.new()

-- Tunables -----------------------------------------------------------------
-- Stiffness/damping are forces (N), applied via ApplyForceAt so Jolt
-- integrates them across the fixed step. The previous formulation called
-- ApplyImpulseAt with these same values, which treated N as N·s and
-- effectively multiplied them by PhysicsHz — stable at rest but unstable at
-- speed when compression varied. New numbers ≈ old × 60Hz to preserve feel.
local SuspensionStiffness = 180000.0 -- spring force per metre of compression
local SuspensionDamping   = 12000.0  -- damper force per m/s along spring
local MaxSuspensionForce  = 200000.0 -- cap so a noisy ray hit can't punt the body
local RestLength          = 0.50
local RayExtra            = 0.00       -- extra ray length past rest, for slopes

local EngineAccel         = 22.0      -- m/s² at full throttle
local ReverseFactor       = 0.45      -- reverse is weaker than forward
local BrakeAccel          = 36.0      -- m/s² when throttle opposes motion
local CoastDrag           = 0.9       -- 1/s exponential decay when no input
local MaxSpeed            = 55.0      -- m/s

local SteerYawRateMax     = 2.6       -- max yaw rate (rad/s)
local SteerSpeedFalloff   = 30.0      -- speed at which steering authority caps
local SteerResponse       = 7.0       -- yaw blend rate toward target

local ThrottleAttack      = 10.0      -- pedal-down response (1/s)
local ThrottleRelease     = 3.0       -- pedal-up response (1/s) — slower for realism

local LateralGrip         = 4.0       -- side-velocity decay (1/s)
local DriftLateralGrip    = 0.6       -- with handbrake — lets the rear slide

-- Wheel layout in body frame ----------------------------------------------
local HalfLength = 1.0
local HalfWidth  = 1.0
local WheelDrop  = -0.1

local DebugWheels = false   -- draw the wheel rays

-- --------------------------------------------------------------------------

local function CastWheel(self, BodyID, Origin, Up)
    local Settings = SRayCastSettings.new()
    Settings.Start = Origin
    Settings.End = Origin - Up * (RestLength + RayExtra)
    Settings.LayerMask = ECollisionProfiles.Static
    Settings.bDrawDebug = DebugWheels
    Settings.DebugDuration = 0.0
    Settings:AddIgnoredBody(BodyID)
    return self.World:CastRay(Settings)
end

function VehicleMovement:OnUpdate(DeltaTime)
    local BodyID   = self:GetBodyID()
    local Rotation = self._Physics:GetBodyRotation(BodyID)
    local Loc      = self._Physics:GetBodyPosition(BodyID)
    local Forward  = Rotation:RotateVector(vector.create(0.0, 0.0, 1.0))
    local Right    = Rotation:RotateVector(vector.create(1.0, 0.0, 0.0))
    local Up       = Rotation:RotateVector(vector.create(0.0, 1.0, 0.0))

    local RawThrottle = Input.GetActionAxis("MoveY")
    local Steer       = Input.GetActionAxis("MoveX")
    local Handbrake   = Input.IsActionDown("Jump")
    local Boost	      = Input.IsActionDown("Sprint")

    self.SmoothThrottle = self.SmoothThrottle or 0.0
    local Rate = (math.abs(RawThrottle) > math.abs(self.SmoothThrottle)) and ThrottleAttack or ThrottleRelease
    local TAlpha = math.clamp(Rate * DeltaTime, 0.0, 1.0)
    self.SmoothThrottle = self.SmoothThrottle + (RawThrottle - self.SmoothThrottle) * TAlpha
    local Throttle = self.SmoothThrottle

    local Wheels = {
        Loc + Forward *  HalfLength + Right *  HalfWidth + Up * WheelDrop,
        Loc + Forward *  HalfLength - Right *  HalfWidth + Up * WheelDrop,
        Loc - Forward *  HalfLength + Right *  HalfWidth + Up * WheelDrop,
        Loc - Forward *  HalfLength - Right *  HalfWidth + Up * WheelDrop,
    }

    local NumGrounded = 0
    for _, Origin in ipairs(Wheels) do
        local Hit = CastWheel(self, BodyID, Origin, Up)
        if Hit and Hit.Distance < RestLength then
            local Compression = RestLength - Hit.Distance
            local SpringSpeed = vector.dot(self:GetVelocityAtPoint(Origin), Up)
            local Force = SuspensionStiffness * Compression - SuspensionDamping * SpringSpeed
            if Force > 0.0 then
                Force = math.min(Force, MaxSuspensionForce)
                self:ApplyForceAt(Up * Force, Origin)
                NumGrounded = NumGrounded + 1
            end
        end
    end

    local Grounded = NumGrounded > 0
    
    local Velocity     = self:GetLinearVelocity()
    local ForwardSpeed = vector.dot(Velocity, Forward)

    if Grounded then
        if math.abs(Throttle) > 0.01 then
            local Accel
            if Throttle * ForwardSpeed < 0.0 and math.abs(ForwardSpeed) > 1.0 then
                Accel = math.sign(Throttle) * BrakeAccel
            elseif Throttle > 0.0 then
                Accel = Throttle * EngineAccel
            else
                Accel = Throttle * EngineAccel * ReverseFactor
            end
            if math.abs(ForwardSpeed) < MaxSpeed or Throttle * ForwardSpeed < 0.0 then
            	
            	if Boost then
                	Velocity = Velocity + Forward * (Accel * DeltaTime) * 2
                else
                    Velocity = Velocity + Forward * (Accel * DeltaTime)
                end
            end
        else
            Velocity = Velocity - Forward * (ForwardSpeed * CoastDrag * DeltaTime)
        end

        local SideSpeed = vector.dot(Velocity, Right)
        local Grip      = Handbrake and DriftLateralGrip or LateralGrip
        local Alpha     = math.clamp(Grip * DeltaTime, 0.0, 1.0)
        Velocity = Velocity - Right * (SideSpeed * Alpha)
        self:SetVelocity(Velocity)
    end

    if Grounded then
        local SpeedFactor = math.clamp(math.abs(ForwardSpeed) / SteerSpeedFalloff, 0.0, 1.0)
        local YawTarget   = Steer * SteerYawRateMax * (0.4 + 0.6 * SpeedFactor)
        if ForwardSpeed < -0.5 then YawTarget = -YawTarget end

        local AngVel = self:GetAngularVelocity()
        local YawNow = vector.dot(AngVel, Up)
        local Blend  = math.clamp(SteerResponse * DeltaTime, 0.0, 1.0)
        local YawNew = YawNow + (YawTarget - YawNow) * Blend
        self:SetAngularVelocity(AngVel - Up * YawNow + Up * YawNew)
    end
end

return VehicleMovement
