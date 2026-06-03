local EntityScript = require("Stdlib/EntityScript")
local Script: EntityScript = EntityScript.new()

-- Server-authored spawn: CreateEntity + components + ownership + pawn script; the net layer replicates it.
local function SpawnPlayer(owner, x, z)
    local p = World.CreateEntity("Player")
    World.AddComponent(p, SStaticMeshComponent):SetStaticMesh(Engine.GetCubeMesh())
    World.AddComponent(p, SCameraComponent)
    local NetworkComp: SNetworkComponent = World.AddComponent(p, SNetworkComponent)
    NetworkComp.NetUpdateFrequency = 120
    
    World.SetLocation(p, Vec3(x, 3, z))
    World.Net:SetOwner(p, owner)
    World.AddScript(p, "/Game/Scripts/Player.luau")
    return p
end

function Script:OnReady()
    self.HostPlayer = nil   -- the host's own pawn (owner 0)
    self.SpawnedFor = {}    -- connections we've already spawned a pawn for
end

function Script:OnUpdate(DeltaTime)
    -- Authority only. (If the level didn't mark this server-only, this guard still makes it inert on clients.)
    if not World.Net:IsServer() then
        return
    end

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

return Script
