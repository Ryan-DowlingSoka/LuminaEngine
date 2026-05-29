local Random = require("Stdlib/Random")

local Script: EntityScript = EntityScript.new()

Script.Exports = {
    -- Name of the entity to clone (must exist in the world; should NOT have this script).
    Template  = "Cube",
    Count     = 10000,
    Radius    = 500.0,
    Speed     = 5.0,
    Center    = vector.zero,
}

local Entities = {}

function Script:OnReady()
    local Template = World.FindByName(self.Template)
    if not World.IsValid(Template) then
        return
    end

	World.BeginPhysicsBatch()
    for _ = 1, self.Count do
        local New = World.Duplicate(Template)
        World.SetLocation(New, Random.InSphere(self.Radius))
        table.insert(Entities, New)
    end
    World.EndPhysicsBatch()
end

function Script:OnUpdate(DeltaTime: number)
    World.MoveToward(Entities, self.Center, self.Speed * DeltaTime)
end

return Script
