-- Entity script. Attach it to an entity via its Script component.
local EntityScript = require("Stdlib/EntityScript")
local Script: EntityScript = EntityScript.new()

-- Editable properties: declare them as top-level fields and annotate with
-- --@export(...) to expose them in the entity's Script panel. The annotation
-- args drive the editor widget, just like a C++ PROPERTY: ClampMin/ClampMax,
-- Delta, Units, Category, Tooltip, Color, NoDrag, ReadOnly, AssetType. Read/write
-- anywhere as self.<Name>.
--@export(ClampMin = 0, Units = "m/s")
Script.Speed = 5.0

-- A string + AssetType becomes an asset picker; the value is the asset's path.
-- AssetType is a loose-asset kind (luau/rml/rcss) or a reflected class (e.g. "StaticMesh").
-- --@export(AssetType = "luau")
-- Script.OnHitScript = ""

-- Runs once when the entity spawns. Cache components, set up state here.
function Script:OnReady()
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
