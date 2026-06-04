-- Server-authoritative mover. Attached by the server (RpcTest) to a replicated cube; its ScriptPath
-- replicates, so clients attach it too, but the IsServer guard makes it inert there. The cube therefore
-- moves only on the server, and clients see the motion purely through movement replication + interpolation.
local EntityScript = require("Stdlib/EntityScript")
local Script: EntityScript = EntityScript.new()

local Radius = 4.0
local Speed  = 1.5 -- radians/sec

function Script:OnReady()
    self.Time   = math.random() * 6.2831853 -- random phase so cubes don't move in lockstep
    self.Origin = self.Transform:GetLocation()
end

function Script:OnUpdate(DeltaTime)
    -- Authority only. The server owns this cube; clients receive its transform over the wire.
    if not World.Net:IsServer() then
        return
    end

    self.Time = self.Time + DeltaTime * Speed
    local x = self.Origin.x + math.sin(self.Time) * Radius
    local z = self.Origin.z + math.cos(self.Time) * Radius
    self.Transform:SetLocation(Vec3(x, self.Origin.y, z))
end

return Script
