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

local SpawnInterval = 0.1   -- seconds between spawn waves
local SpawnPerWave  = 4     -- cubes added each wave
local MaxCubes      = 5000  -- soft cap so the session stays recoverable; raise to push harder
local GridCols      = 16
local Spacing       = 3.0

local function SpawnMovingCube(index: number)
    local col = index % GridCols
    local row = math.floor(index / GridCols)
    local x   = (col - GridCols / 2) * Spacing
    local z   = 8 + row * Spacing

    local c = World.CreateEntity("MovingCube")
    World.AddComponent(c, SStaticMeshComponent):SetStaticMesh(Engine.GetCubeMesh())
    local NetworkComp: SNetworkComponent = World.AddComponent(c, SNetworkComponent)
    NetworkComp.bReplicatesMovement = true
    NetworkComp.NetUpdateFrequency  = 30
    World.SetLocation(c, Vec3(x, 3, z))
    World.AddScript(c, "/Game/Scripts/CubeMover.luau")
    return c
end

function Script:OnReady()
    self.HostPlayer = nil   -- the host's own pawn (owner 0)
    self.SpawnedFor = {}    -- connections we've already spawned a pawn for
    self.CubeCount  = 0     -- total cubes spawned so far (also the next grid index)
    self.SpawnTimer = 0.0
end

function Script:OnUpdate(DeltaTime)
    -- Authority only. (If the level didn't mark this server-only, this guard still makes it inert on clients.)
    if not World.Net:IsServer() then
        return
    end

    if not self.HostPlayer then
        self.HostPlayer = SpawnPlayer(0, 0, 0)
    end

    -- One networked actor that demonstrates script-side replicated properties (--@replicated).
    if not self.RepDemo then
        self.RepDemo = World.CreateEntity("RepStateActor")
        World.AddComponent(self.RepDemo, SStaticMeshComponent):SetStaticMesh(Engine.GetCubeMesh())
        local nc: SNetworkComponent = World.AddComponent(self.RepDemo, SNetworkComponent)
        nc.bAlwaysRelevant = true -- relevant to every client without AOI proximity, for easy testing
        World.SetLocation(self.RepDemo, Vec3(0, 6, 0))
        World.AddScript(self.RepDemo, "/Game/Scripts/ReplicatedStateTest.luau")
    end

    -- Keep adding cubes on an interval to stress movement replication, until the soft cap.
    if self.CubeCount < MaxCubes then
        self.SpawnTimer = self.SpawnTimer + DeltaTime
        if self.SpawnTimer >= SpawnInterval then
            self.SpawnTimer = self.SpawnTimer - SpawnInterval
            for _ = 1, SpawnPerWave do
                SpawnMovingCube(self.CubeCount)
                self.CubeCount = self.CubeCount + 1
            end
        end
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
