-- Player controller + third-person orbit camera.
--
-- Movement is fed into the character controller; the mouse drives the active
-- camera entity's rotation so its spring arm orbits this player. We keep the
-- controller's yaw aligned with the camera heading so movement is
-- camera-relative, and the character turns to face where it moves.
local PlayerControllerScript: EntityScript = EntityScript.new()

PlayerControllerScript.Exports = {
    Sensitivity = 0.12,   -- degrees of look per pixel of mouse movement
    MinPitch    = -55.0,  -- look-up limit (negative = up)
    MaxPitch    =  70.0,  -- look-down limit (positive = down)
    ZoomSpeed   =  1.0,   -- meters of boom per scroll notch
    MinDistance =  1.5,
    MaxDistance = 12.0,
}

local function WrapDegrees(D: number): number
    while D > 180.0 do D -= 360.0 end
    while D < -180.0 do D += 360.0 end
    return D
end

-- Resolve the active camera (its spring arm is what orbits us). Cached after
-- the first successful lookup; returns false until a camera exists.
local function EnsureCamera(self): boolean
    if self._CamXform ~= nil then return true end

    local E = Camera.GetActive()
    if not self._Registry:Valid(E) then return false end

    self._CamXform = self:GetComponentOf(E, STransformComponent)
    if self._CamXform == nil then return false end

    self._CamArm = self:GetComponentOf(E, SSpringArmComponent)
    if self._CamArm then
        self._CamArm.bUseControlRotation = true
        if not self._Registry:Valid(self._CamArm.Target) then
            self._CamArm:SetTarget(self.Entity)
        end
    end

    -- Seed the pitch accumulator from the authored rotation (AddPitch(+) tilts
    -- down, so forward.y = -sin(pitch)).
    local Fwd = self._CamXform:GetForward()
    self._Pitch = Math.Degrees(Math.Asin(Math.Clamp(-Fwd.y, -1.0, 1.0)))
    return true
end

function PlayerControllerScript:OnReady()
    self._PC = self:GetComponent(SCharacterControllerComponent)
    
    local Test = self:GetComponent(TestType)
    print(Test.Health)

    local Move = self:GetComponent(SCharacterMovementComponent)
    if Move then
        Move.bOrientRotationToMovement = true
        Move.bUseControllerRotation    = false
    end

    Input.SetMouseMode("Captured")

    -- TEST: render-target painting. Create a Render Target asset at /Game/BloodMap
    -- (content browser -> New Asset -> Render Target) and sample it in a material to
    -- see the splats. Nil-safe if the asset doesn't exist yet.
    self._BloodMap  = Engine.LoadObject("/Game/BloodMap")
    self._PaintTime = 0.0
end

function PlayerControllerScript:OnUpdate(DeltaTime: number)
    -- TEST: sweep a red splat across the render target each frame to confirm writes land.
    if self._BloodMap then
        self._PaintTime += DeltaTime
        local U = (self._PaintTime * 0.13) % 1.0
        local V = (self._PaintTime * 0.07) % 1.0
        RenderTarget.Paint(self._BloodMap, U, V, 0.04, 0.8, 0.0, 0.0, 1.0)
    end

    if self._PC == nil then return end

    if EnsureCamera(self) then
        local Xform = self._CamXform

        -- Yaw orbits freely; pitch is clamped against the accumulated angle
        -- (AddPitch's own clamp only limits the per-frame delta, not the total).
        local MouseX = Input.GetMouseDeltaX() * self.Sensitivity
        local MouseY = Input.GetMouseDeltaY() * self.Sensitivity
        Xform:AddYaw(MouseX)
        local NewPitch = Math.Clamp(self._Pitch + MouseY, self.MinPitch, self.MaxPitch)
        Xform:AddPitch(NewPitch - self._Pitch, -360.0, 360.0)
        self._Pitch = NewPitch

        -- Scroll wheel zooms the boom in/out.
        if self._CamArm then
            local Scroll = Input.GetMouseZ()
            if Scroll ~= 0.0 then
                self._CamArm.TargetArmLength = Math.Clamp(self._CamArm.TargetArmLength - Scroll * self.ZoomSpeed, self.MinDistance, self.MaxDistance)
            end
        end

        -- Align our movement frame with the camera heading so W is "away from us".
        local Fwd = Xform:GetForward()
        local Yaw = Math.Degrees(Math.Atan2(Fwd.x, Fwd.z))
        self._PC:AddYaw(WrapDegrees(Yaw - self._PC.LookInput.x))
    end

    -- MoveInput.x = strafe (right), MoveInput.z = forward; the movement system
    -- rotates both by the controller yaw we keep in sync above.
    local X = Input.GetActionAxis("MoveX")
    local Z = Input.GetActionAxis("MoveY")
    self._PC:AddMovementInput(vector.create(X, 0.0, Z))
end

return PlayerControllerScript
