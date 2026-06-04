local Script: EntityScript = EntityScript.new()

--@export(ClampMin=0, Units="m/s", Category="Player", Tooltip="Free-fly speed")
Script.Speed = 12.0

--@export(ClampMin=0, Category="Player", Tooltip="Mouse look sensitivity")
Script.LookSensitivity = 0.15

local LabelHeight = 1.8
local LabelSize   = 0.5


function Script:OnReady()
    self.Yaw, self.Pitch = 0.0, 0.0
    self.Captured    = false
    self.CameraSet   = false
    self.LabelEntity = nil
    self.LabelText   = nil
    self.LastConn    = -1
    self.ResendsLeft = 6
    self.ResendTimer = 0.0
end

function Script:EnsureLabel()
    if self.LabelEntity == nil or not World.IsValid(self.LabelEntity) then
        self.LabelEntity = World.CreateEntity("Label")
        World.AddComponent(self.LabelEntity, STextComponent)
        local t = World.GetComponent(self.LabelEntity, STextComponent)
        t:SetBillboard(true)
        t:SetWorldSize(LabelSize)
        if self.LabelText then t:SetText(self.LabelText) end

        -- Attach to the pawn and offset up in the pawn's local space.
        World.AttachEntity(self.LabelEntity, self.Entity)
        World.SetLocalLocation(self.LabelEntity, Vec3(0, LabelHeight, 0))
    end
    return self.LabelEntity
end

--@rpc(multicast, reliable)
function Script:SetLabel(text)
    self.LabelText = text
    World.GetComponent(self:EnsureLabel(), STextComponent):SetText(text)
end


function Script:IsMine()
    return World.Net:GetOwner(self.Entity) == World.Net:GetUniqueId()
end

function Script:OnUpdate(DeltaTime)
    -- The nameplate is parented to the pawn (see EnsureLabel), so it follows via the scene graph -- no
    -- per-frame position copy needed.

    -- Server pushes the label text by ownership; a burst of early re-sends covers the client's async script
    -- attach, and a re-send on connection-count change covers late joiners.
    if World.Net:IsServer() then
        local conn = World.Net:GetConnectionCount()
        if conn ~= self.LastConn then
            self.LastConn    = conn
            self.ResendsLeft = 6
        end
        if self.ResendsLeft > 0 then
            self.ResendTimer = self.ResendTimer + DeltaTime
            if self.ResendTimer >= 0.4 then
                self.ResendTimer = 0.0
                self.ResendsLeft = self.ResendsLeft - 1
                self:SetLabel(World.Net:GetOwner(self.Entity) == 0 and "Server" or "Client")
            end
        end
    end

    -- Everything below is control -- only the owning peer runs it; others see the pawn interpolated.
    if not self:IsMine() then
        return
    end

    if not self.CameraSet then
        Camera.SetActive(self.Entity)
        self.CameraSet = true
    end

    if not World.HasComponent(self.Entity, SInputComponent) then
        World.AddComponent(self.Entity, SInputComponent)
    end
    local input = World.GetComponent(self.Entity, SInputComponent)
    if not input then
        return
    end

    -- Capture on the focus edge, release on blur (so Shift+F1 can hand control back to the editor).
    local active = input:IsInputActive()
    if active and not self.Captured then
        Input.SetMouseMode("Captured")
        self.Captured = true
    elseif not active and self.Captured then
        Input.SetMouseMode("Normal")
        self.Captured = false
    end

    if active then
        self.Yaw   = self.Yaw + input:GetMouseDeltaX() * self.LookSensitivity
        self.Pitch = math.clamp(self.Pitch + input:GetMouseDeltaY() * self.LookSensitivity, -89, 89)
        self.Transform:SetLocalRotationFromEuler(Vec3(self.Pitch, self.Yaw, 0))
    end

    -- Free-fly relative to yaw. IsKeyDown is already gated to the focused world.
    local rad   = math.rad(self.Yaw)
    local fwd   = Vec3(math.sin(rad), 0, math.cos(rad))
    local right = Vec3(math.cos(rad), 0, -math.sin(rad))
    local up    = Vec3(0, 1, 0)

    local move = Vec3(0, 0, 0)
    if input:IsKeyDown("W")     then move = move + fwd   end
    if input:IsKeyDown("S")     then move = move - fwd   end
    if input:IsKeyDown("D")     then move = move + right end
    if input:IsKeyDown("A")     then move = move - right end
    if input:IsKeyDown("Space") then move = move + up    end
    if input:IsKeyDown("Ctrl")  then move = move - up    end

    if move.x ~= 0 or move.y ~= 0 or move.z ~= 0 then
        self:Translate(move * (self.Speed * DeltaTime))
    end
end

return Script
