-- Engine standard library — entity-attached script base.
--
-- Usage:
--   local EntityScript = require("Stdlib/EntityScript")
--   local Script = EntityScript.new()
--   function Script:OnReady() ... end
--   function Script:OnUpdate(DeltaTime) ... end
--   return Script
--
-- The engine still expects a plain table with OnAttach / OnReady / OnUpdate /
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
-- self._Physics). They have to be on `self` rather than module globals:
-- helper methods are shared across every requiring script, so `self` is the
-- only place per-entity context can live. The leading `_` keeps them out of
-- autocomplete; users go through self:GetComponent etc.

local EntityScript = {}
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
-- Thin wrappers around IPhysicsScene that build the event struct and fill
-- in this entity's body id, so scripts don't repeat the SImpulseEvent etc.
-- boilerplate. Cross-entity work still goes through self._Physics directly.

function EntityScript:GetBodyID()
    return self._Physics:GetEntityBodyID(self.Entity)
end

function EntityScript:ApplyImpulse(Impulse)
    local E = SImpulseEvent.new()
    E.BodyID = self:GetBodyID()
    E.Impulse = Impulse
    self._Physics:ApplyImpulse(E)
end

function EntityScript:ApplyForce(Force)
    local E = SForceEvent.new()
    E.BodyID = self:GetBodyID()
    E.Force = Force
    self._Physics:ApplyForce(E)
end

function EntityScript:ApplyTorque(Torque)
    local E = STorqueEvent.new()
    E.BodyID = self:GetBodyID()
    E.Torque = Torque
    self._Physics:ApplyTorque(E)
end

function EntityScript:ApplyAngularImpulse(AngularImpulse)
    local E = SAngularImpulseEvent.new()
    E.BodyID = self:GetBodyID()
    E.AngularImpulse = AngularImpulse
    self._Physics:ApplyAngularImpulse(E)
end

function EntityScript:ApplyImpulseAt(Impulse, Position)
    local E = SAddImpulseAtPositionEvent.new()
    E.BodyID = self:GetBodyID()
    E.Impulse = Impulse
    E.Position = Position
    self._Physics:AddImpulseAtPosition(E)
end

function EntityScript:ApplyForceAt(Force, Position)
    local E = SAddForceAtPositionEvent.new()
    E.BodyID = self:GetBodyID()
    E.Force = Force
    E.Position = Position
    self._Physics:AddForceAtPosition(E)
end

function EntityScript:SetVelocity(Velocity)
    local E = SSetVelocityEvent.new()
    E.BodyID = self:GetBodyID()
    E.Velocity = Velocity
    self._Physics:SetVelocity(E)
end

function EntityScript:SetAngularVelocity(AngularVelocity)
    local E = SSetAngularVelocityEvent.new()
    E.BodyID = self:GetBodyID()
    E.AngularVelocity = AngularVelocity
    self._Physics:SetAngularVelocity(E)
end

function EntityScript:GetLinearVelocity()
    return self._Physics:GetLinearVelocity(self:GetBodyID())
end

function EntityScript:GetAngularVelocity()
    return self._Physics:GetAngularVelocity(self:GetBodyID())
end

function EntityScript:GetVelocityAtPoint(Point)
    return self._Physics:GetVelocityAtPoint(self:GetBodyID(), Point)
end

-- Body's actual current pose. Use these instead of self.Transform when the
-- script runs in PrePhysics and needs to compute force application points or
-- ray origins relative to the body -- the cached transform component holds
-- the lagged interpolated render value, not what physics will integrate from.
function EntityScript:GetBodyPosition()
    return self._Physics:GetBodyPosition(self:GetBodyID())
end

function EntityScript:GetBodyRotation()
    return self._Physics:GetBodyRotation(self:GetBodyID())
end

function EntityScript:GetBodyForward()
    return self._Physics:GetBodyRotation(self:GetBodyID()):RotateVector(vector.create(0.0, 0.0, 1.0))
end

function EntityScript:GetBodyRight()
    return self._Physics:GetBodyRotation(self:GetBodyID()):RotateVector(vector.create(1.0, 0.0, 0.0))
end

function EntityScript:GetBodyUp()
    return self._Physics:GetBodyRotation(self:GetBodyID()):RotateVector(vector.create(0.0, 1.0, 0.0))
end

return EntityScript
