-- Example Lua ECS system. Assign it to a world via the World Editor's Systems panel.
--
-- A system is a stateless module that returns a descriptor table:
--   * Stage    - which update stage to tick in ("FrameStart" | "PrePhysics" |
--                "DuringPhysics" | "PostPhysics" | "FrameEnd" | "Paused").
--   * Priority - order within the stage ("Highest" | "High" | "Medium" | "Low").
--                Optional; defaults to "Medium". (For multiple stages use a
--                Stages = { PrePhysics = "High", FrameEnd = "Low" } map instead.)
--   * Name     - display name in the editor. Optional; defaults to the file stem.
--
-- Hooks all receive `ctx`, the system context (mirrors the C++ FSystemContext):
--   ctx:DeltaTime() / ctx:Time()
--   ctx:Each({Comp, ...}, fn)   ctx:Get/Has/Emplace/Remove(entity, Comp)
--   ctx:Create() / ctx:Destroy(entity)   ctx:View(...) / ctx:Registry()
-- Components are the reflected struct globals (e.g. STransformComponent).
-- World.<Subsystem> (World.Physics / World.Net / World.Debug) resolve globally.

local System = {}

System.Name     = "Example Spin"
System.Stage    = "PrePhysics"
System.Priority = "Medium"

-- Optional one-shot setup when the system is added to the world.
function System.Startup(ctx)
    Console.Log("[ExampleSpinSystem] started")
end

-- Spins every entity that has a transform around the up (Y) axis.
function System.Update(ctx, dt)
    local YawPerSecond = 90.0 -- degrees
    local Spin = Vec3(0.0, YawPerSecond * dt, 0.0)

    ctx:Each({ STransformComponent }, function(entity)
        local T = ctx:Get(entity, STransformComponent)
        if T then
            T:AddRotationFromEuler(Spin)
        end
    end)
end

function System.Teardown(ctx)
    Console.Log("[ExampleSpinSystem] stopped")
end

return System
