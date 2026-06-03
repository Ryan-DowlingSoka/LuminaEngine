-- Networked free-fly player controller. Attach to a single pre-placed entity in the level (exists on the
-- server + every client). The server spawns one player per peer; each peer flies the player it owns.
--
-- All mutable state lives on `self` (per-entity): the engine shares one Lua VM across every world, so a
-- module-level `local` would be shared between the server's and the client's controller — which would make
-- them fight over the mouse capture and share look direction.
local EntityScript = require("Stdlib/EntityScript")
local Script: EntityScript = EntityScript.new()

--@export(ClampMin=0, Units="m/s", Category="Player", Tooltip="Free-fly speed")
Script.Speed = 12.0

--@export(ClampMin=0, Category="Player", Tooltip="Mouse look sensitivity")
Script.LookSensitivity = 0.15

--@export(Slider, ClampMin=0, ClampMax=1, Gradient, Glow, Category="Movement")
Script.Thing = 0.5

-- Server-authored spawn: CreateEntity + components + ownership; the net layer replicates the whole entity.
local function SpawnPlayer(owner, x, z)
    local p = World.CreateEntity("Player")
    World.AddComponent(p, SStaticMeshComponent)
    World.GetComponent(p, SStaticMeshComponent):SetStaticMesh(Engine.GetCubeMesh())
    World.AddComponent(p, SCameraComponent)
    World.AddComponent(p, SNetworkComponent)
    World.SetLocation(p, Vec3(x, 3, z))
    World.Net:SetOwner(p, owner)
    return p
end

function Script:OnReady()
    -- This controller entity reads input for its world; add the input component if the level didn't.
    if not World.HasComponent(self.Entity, SInputComponent) then
        World.AddComponent(self.Entity, SInputComponent)
    end

    self.Yaw        = 0.0
    self.Pitch      = 0.0
    self.Captured   = false   -- whether WE hold the mouse (capture on focus edge, release on blur)
    self.CameraSet  = false
    self.HostPlayer = nil     -- server: the host's own player entity
    self.SpawnedFor = {}      -- server: connections we've spawned a player for
end

function Script:OnUpdate(DeltaTime)
    if World.Net:IsStandalone() then
        return
    end

    -- Server spawns a player for itself (owner 0) and for each newly-connected client.
    if World.Net:IsServer() then
        if not self.HostPlayer then
            self.HostPlayer = SpawnPlayer(0, 0, 0)
        end
        for i = 0, World.Net:GetConnectionCount() - 1 do
            local conn = World.Net:GetConnectionAt(i)
            if not self.SpawnedFor[conn] then
                self.SpawnedFor[conn] = true
                SpawnPlayer(conn, (i + 1) * 4, 0)
            end
        end
    end

    -- The player THIS peer controls: the host's own player on the server, my AutonomousProxy on a client.
    local me
    if World.Net:IsServer() then
        me = self.HostPlayer
    else
        me = World.Net:GetLocallyOwnedEntity()
    end
    if not me or not World.IsValid(me) then
        return
    end

    -- Each window shows its own player's view.
    if not self.CameraSet then
        Camera.SetActive(me)
        self.CameraSet = true
    end

    local input = World.GetComponent(self.Entity, SInputComponent)
    if not input then
        return
    end

    -- Mouse-look + movement only drive when this window is focused (so both PIE worlds don't move at once).
    -- Capture on the focus edge and release on blur -- never re-capture every frame, or Shift+F1 can't win.
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
        World.GetComponent(me, STransformComponent):SetLocalRotationFromEuler(Vec3(self.Pitch, self.Yaw, 0))
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
        World.Translate(me, move * (self.Speed * DeltaTime))
    end
end

return Script
