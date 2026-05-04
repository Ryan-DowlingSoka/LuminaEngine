local EntityScript = require("Stdlib/EntityScript")
local CameraScript = EntityScript.new()

CameraScript.Meta = {
	UpdateStage = EUpdateStage.PostPhysics
}

CameraScript.Exports = {
	EntityToOrbit    = -1,
	Distance         = 8.0,
	YawSensitivity   = 0.25,
	PitchSensitivity = 0.25,
	-- Pitch is the camera's elevation angle around the target.
	-- Positive = above target looking down, negative = below.
	MinPitch         = -1.0,
	MaxPitch         =  80.0,
	InitialPitch     =  15.0,
	FollowSmoothing  = 12.0,
}

function CameraScript:OnReady()
	self.Yaw = 0.0
	self.Pitch = self.Exports.InitialPitch
	self.TargetTransform = nil
	self.SmoothLocation = nil
	Input.SetMouseMode("Captured")
end

local function ResolveTarget(self)
	if self.TargetTransform then return self.TargetTransform end
	local Id = self.Exports.EntityToOrbit
	if not Id or Id < 0 then return nil end
	self.TargetTransform = self._Registry:Get(Id, STransformComponent)
	return self.TargetTransform
end

function CameraScript:OnUpdate(DeltaTime)
	local Target = ResolveTarget(self)
	if not Target then return end

	self.Yaw   = self.Yaw   + Input.GetMouseDeltaX() * self.Exports.YawSensitivity
	self.Pitch = math.clamp(
		self.Pitch + Input.GetMouseDeltaY() * self.Exports.PitchSensitivity,
		self.Exports.MinPitch,
		self.Exports.MaxPitch)

	local YawRad   = math.rad(self.Yaw)
	local PitchRad = math.rad(self.Pitch)
	local CosP     = math.cos(PitchRad)
	local SinP     = math.sin(PitchRad)
	local SinY     = math.sin(YawRad)
	local CosY     = math.cos(YawRad)

	-- Spherical orbit. Pitch is the elevation angle of the camera relative to
	-- the target's horizon: positive lifts the camera up. The horizontal axis
	-- uses (-SinY, -CosY) so yaw=0 sits the camera behind the target along
	-- the engine's +Z forward.
	local TargetLoc = Target:GetWorldLocation()
	local Distance  = self.Exports.Distance
	local Desired   = TargetLoc + vector.create(
		-SinY * CosP * Distance,
		 SinP        * Distance,
		-CosY * CosP * Distance)

	if self.SmoothLocation == nil or self.Exports.FollowSmoothing <= 0.0 then
		self.SmoothLocation = Desired
	else
		local Alpha = math.clamp(self.Exports.FollowSmoothing * DeltaTime, 0.0, 1.0)
		self.SmoothLocation = self.SmoothLocation + (Desired - self.SmoothLocation) * Alpha
	end

	self.Transform:SetLocation(self.SmoothLocation)

	local ToTarget = TargetLoc - self.SmoothLocation
	local Len = vector.magnitude(ToTarget)
	if Len > 1e-4 then
		local Dir = ToTarget / Len
		local LookYaw   = math.deg(math.atan2(Dir.x, Dir.z))
		local LookPitch = -math.deg(math.asin(math.clamp(Dir.y, -1.0, 1.0)))
		self.Transform:SetRotationFromEuler(vector.create(LookPitch, LookYaw, 0.0))
	end
end

return CameraScript
