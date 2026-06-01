-- Entity script. Attach it to an entity via its Script component.
local EntityScript = require("Stdlib/EntityScript")
local Script: EntityScript = EntityScript.new()

-- Editable properties: appear in the entity's Script panel.
-- Read/write them anywhere as self.<Name> (no .Exports prefix).
Script.Exports = {
    Speed = 5.0,
}

local Entities = {}

-- Runs once when the entity spawns. Cache components, set up state here.
function Script:OnReady()

	for i = 1, 5000 do
		local New = World.Duplicate(6)
		local New2 = World.Duplicate(1048585)
		table.insert(Entities, New)
		World.SetLocation(New, Random.InSphere(500))
		table.insert(Entities, New2)
		World.SetLocation(New2, Random.InSphere(500))
	end

end

-- Per-frame tick in play mode. Defining this is what makes the engine
-- tick this entity -- leave it out and it is never iterated.
function Script:OnUpdate(DeltaTime)
end

-- ---------------------------------------------------------------------
-- The hooks below only run if you define them. Uncomment what you need.
-- ---------------------------------------------------------------------

-- Runs once, just before OnReady.
-- function Script:OnAttach() end

-- Fixed-rate tick before the physics step -- apply forces/impulses here.
-- function Script:OnFixedUpdate(FixedDelta) end

-- Per-frame tick while editing; defining it makes the script run in the editor.
-- function Script:OnEditorUpdate(DeltaTime) end

-- Runs once when the entity is destroyed. Release anything you grabbed.
-- function Script:OnDetach() end

-- Solid physics impact began / ended (Event: Other, Point, Normal, ImpactSpeed...).
-- function Script:OnContactBegin(Event) end
-- function Script:OnContactEnd(Event) end

-- Entered / left a trigger volume (a collider with bIsTrigger = true).
-- function Script:OnOverlapBegin(Event) end
-- function Script:OnOverlapEnd(Event) end

return Script
