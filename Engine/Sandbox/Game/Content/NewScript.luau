local Random = require("Stdlib/Random")

local Script: EntityScript = EntityScript.new()

Script.Exports = {
    Speed    = 5.0,
}

function Script:OnReady()
	local NewEntity = self:Clone()
    ECS.SetEntityLocation(self._Registry, NewEntity, Random.InSphere(500.0))
end

function Script:OnUpdate(DeltaTime: number)
    local Pos = self:GetLocation()
    if Math.Length3(Pos) < 0.01 then return end

    local Dir = Math.Normalize3(-Pos)
    self:Translate(Dir * (self.Speed * DeltaTime))
end

return Script
