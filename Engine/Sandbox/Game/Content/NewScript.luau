local Random = require("Stdlib/Random")

local Script: EntityScript = EntityScript.new()

Script.Exports = {
    -- Name of the entity to clone (must exist in the world; should NOT have this script).
    Template  = "Cube",
    Count     = 10000,
    Radius    = 500.0,
    Speed     = 5.0,
}

local Entities = {}

function Script:OnReady()
    local Template = World.FindByName(self.Template)
    if not World.IsValid(Template) then
        return
    end

    for _ = 1, self.Count do
        local New = World.Duplicate(Template)
        World.SetLocation(New, Random.InSphere(self.Radius))
        table.insert(Entities, New)
    end
end

function Script:OnUpdate(DeltaTime: number)
    local Step = self.Speed * DeltaTime

    for _, E in ipairs(Entities) do
        local Pos = World.GetLocation(E)
        if Math.Length3(Pos) >= 0.01 then
            World.Translate(E, Math.Normalize3(-Pos) * Step)
        end
    end
end

return Script
