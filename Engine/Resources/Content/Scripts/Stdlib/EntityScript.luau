-- Engine standard library — entity-attached script base.
--
-- Usage:
--   local Script = EntityScript.new()
--   function Script:OnReady() ... end
--   function Script:Update(DeltaTime) ... end
--   return Script
--
-- The engine still expects a plain table with OnAttach / OnReady / Update /
-- OnDetach keys; new() returns one whose metatable points back here so any
-- method the user doesn't override falls through to the no-op defaults below.
--
-- Per-entity fields injected by the engine before the first lifecycle call:
--   self.Entity    — entt entity id
--   self.World     — owning CWorld
--   self.Transform — STransformComponent (mutable)
--   self.Name      — entity FName
--   self.Exports   — editable properties table
--
-- Engine-internal services live as underscore-prefixed fields on `self`
-- (self._Registry, self._Events, self._Messages, self._Timers, self._Draw,
-- self._Physics). They have to be on `self` rather than as script globals:
-- the helper methods below are loaded into the main thread's globals at
-- engine init, so their closure environment doesn't see per-script globals,
-- but `self` is passed in directly on every call. The leading `_` keeps
-- them out of autocomplete; users go through self:GetComponent etc.

-- `EntityScript = EntityScript or {}` preserves the table's identity across
-- hot-reloads. Already-instantiated scripts hold this table as their
-- metatable — mutating it in place lets them see redefined methods without
-- being respawned.
EntityScript = EntityScript or {}
EntityScript.__index = EntityScript

-- Per-entity instance shape. Each entry: field name → human-readable type
-- string. The engine writes the real values on attach; this table exists
-- purely so the editor's symbol harvester can publish the field set into
-- autocomplete without anyone hardcoding the list. Add a field here when
-- the engine starts injecting a new one.
EntityScript._Shape = {
    Entity    = "entt entity id",
    World     = "CWorld",
    Transform = "STransformComponent",
    Name      = "FName",
    Exports   = "table",
}

function EntityScript.new()
    return setmetatable({
        Exports = {},
    }, EntityScript)
end

-- Lifecycle defaults. Override what you need; unspecified hooks fall through
-- here as no-ops so the engine's per-key dispatch never sees a nil function.
function EntityScript:OnAttach() end
function EntityScript:OnReady() end
function EntityScript:OnUpdate(DeltaTime: number) end
function EntityScript:OnDetach() end

-- ---- Components ----------------------------------------------------------
-- ComponentType is the reflected struct global (e.g. STransformComponent,
-- SStaticMeshComponent). Pass the type itself, not a string:
--     local Mesh = self:GetComponent(SStaticMeshComponent)

function EntityScript:GetComponent(ComponentType)
    return self._Registry:Get(self.Entity, ComponentType)
end

function EntityScript:HasComponent(ComponentType)
    return self._Registry:Has(self.Entity, ComponentType)
end

function EntityScript:AddComponent(ComponentType)
    return self._Registry:Emplace(self.Entity, ComponentType)
end

function EntityScript:RemoveComponent(ComponentType)
    return self._Registry:Remove(self.Entity, ComponentType)
end

-- ---- Events --------------------------------------------------------------

function EntityScript:Subscribe(EventName: string, Handler)
    self._Events:SubscribeEntity(self.Entity, EventName, Handler)
end

function EntityScript:Dispatch(EventName: string, Payload)
    self._Events:Dispatch(EventName, Payload)
end

function EntityScript:DispatchDeferred(EventName: string, Payload)
    self._Events:DispatchDeferred(EventName, Payload)
end

-- ---- Direct messages -----------------------------------------------------

function EntityScript:Send(Target, EventName: string, Payload)
    self._Messages:Send(Target, EventName, Payload)
end

function EntityScript:SendDeferred(Target, EventName: string, Payload)
    self._Messages:SendDeferred(Target, EventName, Payload)
end

-- ---- Timers --------------------------------------------------------------

function EntityScript:After(Seconds: number, Callback)
    return self._Timers:Delay(Seconds, Callback)
end

function EntityScript:Every(Seconds: number, Callback)
    return self._Timers:SetEntityTimer(self.Entity, Seconds, Callback)
end

function EntityScript:CancelTimer(Handle)
    self._Timers:ClearTimer(Handle)
end

-- ---- Spawning / lifetime -------------------------------------------------

function EntityScript:Spawn(PrefabPath: string)
    return self.World:SpawnPrefab(PrefabPath)
end

function EntityScript:Destroy()
    self.World:DestroyEntity(self.Entity)
end

-- ---- Transform shorthand -------------------------------------------------
-- These mirror the most common STransformComponent calls so users don't have
-- to write `self.Transform:Foo(...)` for every nudge. Anything not covered
-- here is still reachable through self.Transform directly.

function EntityScript:Translate(Delta)
    return self.Transform:Translate(Delta)
end

function EntityScript:AddRotationFromEuler(EulerDegrees)
    return self.Transform:AddRotationFromEuler(EulerDegrees)
end

function EntityScript:GetForward()  return self.Transform:GetForward()  end
function EntityScript:GetRight()    return self.Transform:GetRight()    end
function EntityScript:GetUp()       return self.Transform:GetUp()       end

function EntityScript:GetLocation() return self.Transform:GetWorldLocation() end
function EntityScript:GetRotation() return self.Transform:GetWorldRotation() end
function EntityScript:GetScale()    return self.Transform:GetWorldScale()    end

-- ---- Debug draw ----------------------------------------------------------

function EntityScript:DrawSphere(Center, Radius, Color, Segments, Thickness, DepthTest, Duration)
    self._Draw:DrawSphere(Center, Radius, Color, Segments or 16, Thickness or 1.0, DepthTest ~= false, Duration or 0.0)
end

-- ---- Physics -------------------------------------------------------------
-- Thin wrappers around IPhysicsScene that fill in self.Entity. Users who
-- need cross-entity physics work can still go through self.World.

function EntityScript:ApplyImpulse(Impulse)
    self._Physics:ApplyImpulse(self.Entity, Impulse)
end

function EntityScript:ApplyForce(Force)
    self._Physics:ApplyForce(self.Entity, Force)
end

function EntityScript:SetVelocity(Velocity)
    self._Physics:SetVelocity(self.Entity, Velocity)
end

function EntityScript:SetAngularVelocity(AngularVelocity)
    self._Physics:SetAngularVelocity(self.Entity, AngularVelocity)
end
