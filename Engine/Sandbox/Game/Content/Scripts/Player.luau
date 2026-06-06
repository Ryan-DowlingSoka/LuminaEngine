local Script: EntityScript = EntityScript.new()

--@export(ClampMin = 0, Units = "m/s", Category = "Player")
Script.MoveSpeed = 6.0

--@export(ClampMin = 0, Category = "Player")
Script.LookSensitivity = 0.15

function Script:OnReady()
    self.Yaw   = 0.0
    self.Pitch = 0.0

    self:EnableInput()              -- let this entity read keyboard and mouse
    Camera.SetActive(self.Entity)   -- look through this entity's camera
    Input.SetMouseMode("Captured")  -- hide and lock the cursor for mouse-look
end

function Script:OnUpdate(DeltaTime: number)
    -- Look: the mouse turns us left and right (yaw) and up and down (pitch).
    self.Yaw   = self.Yaw + self.Input:GetMouseDeltaX() * self.LookSensitivity
    self.Pitch = math.clamp(self.Pitch + self.Input:GetMouseDeltaY() * self.LookSensitivity, -89, 89)
    self.Transform:SetLocalRotationFromEuler(Vec3(self.Pitch, self.Yaw, 0))

    -- Move: WASD along the direction we are facing.
    local Move = Vec3(0, 0, 0)
    if self.Input:IsKeyDown("W") then Move = Move + self.Transform:GetForward() end
    if self.Input:IsKeyDown("S") then Move = Move - self.Transform:GetForward() end
    if self.Input:IsKeyDown("D") then Move = Move + self.Transform:GetRight() end
    if self.Input:IsKeyDown("A") then Move = Move - self.Transform:GetRight() end
   
    
    Move *= Vec3(1, 0, 1)

    self.Transform:Translate(Move * (self.MoveSpeed * DeltaTime))
end

return Script