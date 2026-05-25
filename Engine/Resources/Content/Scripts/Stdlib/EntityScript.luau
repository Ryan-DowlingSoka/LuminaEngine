-- Engine standard library — entity-attached script base.
--
-- Usage:
--   local Script: EntityScript = EntityScript.new()
--   function Script:OnReady() ... end
--   function Script:OnUpdate(DeltaTime) ... end
--   return Script
--
-- The engine expects a plain table with OnAttach / OnReady / OnUpdate /
-- OnDetach keys; new() returns one whose metatable forwards to this module
-- so any method the user doesn't override falls through to the no-op
-- defaults. Field access (`self.X`) and writes (`self.X = v`) that miss on
-- the instance route through the `Exports` subtable so editable properties
-- can be read/written without the `self.Exports.` prefix.
--
-- Per-entity fields injected by the engine before the first lifecycle call:
--   self.Entity    — entt entity id
--   self.World     — owning CWorld
--   self.Transform — STransformComponent (mutable)
--   self.Name      — entity FName
--   self.Exports   — editable properties table (also reachable as self.<key>)
--
-- Engine services live on `self` under underscore-prefixed slots
-- (self._Registry, self._Events, self._Messages, self._Timers, self._Draw,
-- self._Physics). Prefer the helpers below; reach for the underscore
-- services only when an operation isn't covered by a helper.

local EntityScript = {}

-- Shape table: published to autocomplete so the editor can suggest
-- self.<field> for the engine-injected per-entity slots. Add an entry here
-- whenever the engine starts injecting a new one.
EntityScript._Shape = {
    Entity    = "entt entity id",
    World     = "CWorld",
    Transform = "STransformComponent",
    Name      = "FName",
    Exports   = "table",
}

-- ---- Metatable -----------------------------------------------------------
-- __index: instance miss → method on EntityScript → Exports[key] → nil.
-- __newindex: instance miss → write into Exports (so user-declared property
-- writes land where the editor / serializer can see them). Engine-injected
-- slots (Entity, World, Transform, Name, Exports, _*) are populated via
-- rawset on the C++ side, so they bypass __newindex; explicit user writes
-- to those names will still hit __newindex and route to Exports — engine
-- writes win because they happen first and via raw access.

-- Underscore-prefixed keys are private runtime state (caches, handles,
-- engine-injected services). They live as raw fields on the instance and
-- never round-trip through Exports — this keeps internal slots out of
-- the property panel and out of any future serialization pass.

local function IsPrivateKey(Key)
    return type(Key) == "string" and string.sub(Key, 1, 1) == "_"
end

function EntityScript.__index(Self, Key)
    local Method = rawget(EntityScript, Key)
    if Method ~= nil then return Method end
    if IsPrivateKey(Key) then return nil end
    local Exports = rawget(Self, "Exports")
    if Exports ~= nil then return Exports[Key] end
    return nil
end

function EntityScript.__newindex(Self, Key, Value)
    if IsPrivateKey(Key) then
        rawset(Self, Key, Value)
        return
    end
    -- Route to Exports only when the key already exists there — i.e. it
    -- was declared as a property in `Self.Exports = {...}`. This lets
    -- `self.Distance = 9.0` update an exported field without the explicit
    -- `self.Exports.` prefix, while keeping everything else (lifecycle
    -- methods like `function Self:OnUpdate()`, setup-time tables like
    -- `Self.Meta = {...}`, transient runtime state) on the instance where
    -- it belongs. Critical for methods: if they routed to Exports, the
    -- engine's lua_getfield lookup would invoke __index and return the
    -- no-op default from EntityScript before reaching Exports.
    local Exports = rawget(Self, "Exports")
    if Exports ~= nil and Exports[Key] ~= nil then
        Exports[Key] = Value
        return
    end
    rawset(Self, Key, Value)
end

function EntityScript.new()
    return setmetatable({ Exports = {} }, EntityScript)
end

-- ---- Lifecycle -----------------------------------------------------------
-- OnAttach/OnReady/OnDetach run once and fall through here as no-ops when not
-- overridden. They run in a game world for every script, and in the editor only
-- for scripts that define OnEditorUpdate (the editor-active marker).

function EntityScript:OnAttach() end
function EntityScript:OnReady() end
function EntityScript:OnDetach() end

-- ---- Ticks ---------------------------------------------------------------
-- These have NO no-op fallback on purpose: define one and the engine tags the
-- entity and ticks it, leave it out and the engine never iterates it (zero
-- per-frame cost). Don't add no-op versions to this base table.
--
--   function MyScript:OnUpdate(DeltaTime) ... end
--     Per-frame tick in play mode (game / simulation worlds).
--
--   function MyScript:OnFixedUpdate(FixedDelta) ... end
--     Runs at the physics fixed rate (Meta.PhysicsHz), 0..MaxPhysicsSteps
--     times per frame, before the physics step. Apply forces / impulses
--     here so they integrate this step. Play mode only.
--
--   function MyScript:OnEditorUpdate(DeltaTime) ... end
--     Per-frame tick while editing. Defining it is what makes a script run in
--     the editor at all (its OnAttach/OnReady/OnDetach then fire there too) --
--     the editor-mode counterpart to OnUpdate. Use for tool-style preview /
--     gizmo logic.

-- ---- Identity ------------------------------------------------------------

function EntityScript:IsValid()
    return self._Registry:Valid(self.Entity)
end

function EntityScript:Destroy()
    self.World:DestroyEntity(self.Entity)
end

function EntityScript:Clone()
    return self._Registry:Duplicate(self.Entity)
end

function EntityScript:Spawn(PrefabPath: string)
    return self.World:SpawnPrefab(PrefabPath)
end

-- ---- Components ----------------------------------------------------------
-- ComponentType is the reflected struct global (e.g. STransformComponent,
-- SStaticMeshComponent). Pass the type itself, not a string.

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

-- Same as the Self-targeted helpers but for an arbitrary entity. Use these
-- when you've cached another entity's id and want to read its components
-- without grabbing the registry directly.

function EntityScript:GetComponentOf(Entity, ComponentType)
    return self._Registry:Get(Entity, ComponentType)
end

function EntityScript:HasComponentOf(Entity, ComponentType)
    return self._Registry:Has(Entity, ComponentType)
end

-- ---- Cross-script access -------------------------------------------------
-- Returns the Lua table of another entity's script, or nil if the entity
-- has no script component. Use colon syntax to invoke methods on it:
--     local Other = self:GetScriptTable(TargetEntity)
--     if Other then Other:DoThing(arg) end

function EntityScript:GetScriptTable(Entity)
    return self._Registry:GetScriptTable(Entity)
end

-- ---- Hierarchy -----------------------------------------------------------
-- Parent/child traversal over the entity relationship graph. Single-entity
-- getters return nil instead of a null id when there's no parent, so they
-- read cleanly in `if`/`and` chains; list getters return arrays of entity
-- ids (possibly empty) ready for ipairs.

function EntityScript:HasParent()
    -- valid(entt::null) is false, so this doubles as the "has a parent" test.
    return self._Registry:Valid(self._Registry:GetParent(self.Entity))
end

-- Immediate parent entity id, or nil if this entity is a root.
function EntityScript:GetParent()
    local Parent = self._Registry:GetParent(self.Entity)
    if not self._Registry:Valid(Parent) then return nil end
    return Parent
end

-- The parent's script table, or nil when there's no parent or it has no script.
-- Use colon syntax on the result to call its methods: GetParentScript():Foo().
function EntityScript:GetParentScript()
    local Parent = self:GetParent()
    if Parent == nil then return nil end
    return self._Registry:GetScriptTable(Parent)
end

-- Topmost ancestor in the parent chain; returns this entity's own id when it
-- has no parent.
function EntityScript:GetRoot()
    return self._Registry:GetRoot(self.Entity)
end

-- Immediate children (array of entity ids; empty when none).
function EntityScript:GetChildren()
    return self._Registry:GetChildren(self.Entity)
end

-- Ancestors from immediate parent up to the root, nearest first.
function EntityScript:GetAncestors()
    return self._Registry:GetAncestors(self.Entity)
end

-- Every descendant in the subtree (depth-first), excluding this entity.
function EntityScript:GetDescendants()
    return self._Registry:GetDescendants(self.Entity)
end

-- True when Ancestor sits somewhere above this entity in the parent chain.
function EntityScript:IsChildOf(Ancestor)
    return self._Registry:IsDescendantOf(self.Entity, Ancestor)
end

function EntityScript:GetChildCount()
    return self._Registry:GetChildCount(self.Entity)
end

-- Reparent under NewParent, or pass nil to detach to the world root. The
-- entity's world transform is preserved across the move.
function EntityScript:SetParent(NewParent)
    if NewParent == nil then
        self._Registry:Detach(self.Entity)
    else
        self._Registry:Reparent(self.Entity, NewParent)
    end
end

-- Reparent Child under this entity (the inverse of SetParent).
function EntityScript:AddChild(Child)
    self._Registry:Reparent(Child, self.Entity)
end

-- True when this entity sits somewhere above Other in its parent chain.
function EntityScript:IsAncestorOf(Other)
    return self._Registry:IsDescendantOf(Other, self.Entity)
end

-- First direct child named Name, or nil. Scoped to this entity's children, so
-- it's unambiguous where a global World.FindByName would not be.
function EntityScript:FindChild(Name: string)
    local Child = self._Registry:FindChild(self.Entity, Name)
    if not self._Registry:Valid(Child) then return nil end
    return Child
end

-- As FindChild but searches the entire subtree (depth-first), or nil.
function EntityScript:FindDescendant(Name: string)
    local Found = self._Registry:FindDescendant(self.Entity, Name)
    if not self._Registry:Valid(Found) then return nil end
    return Found
end

-- Other children of this entity's parent (array; empty when parentless).
function EntityScript:GetSiblings()
    return self._Registry:GetSiblings(self.Entity)
end

-- Zero-based position among the parent's children (0 when parentless).
function EntityScript:GetSiblingIndex()
    return self._Registry:GetSiblingIndex(self.Entity)
end

-- Next/previous entry in the parent's child list, or nil at the ends.
function EntityScript:GetNextSibling()
    local Sibling = self._Registry:GetNextSibling(self.Entity)
    if not self._Registry:Valid(Sibling) then return nil end
    return Sibling
end

function EntityScript:GetPrevSibling()
    local Sibling = self._Registry:GetPrevSibling(self.Entity)
    if not self._Registry:Valid(Sibling) then return nil end
    return Sibling
end

-- ---- Events --------------------------------------------------------------
-- Subscribe is automatically scoped to this entity, so subscriptions are
-- cleaned up when the entity is destroyed. Use Dispatch to broadcast.

function EntityScript:Subscribe(EventName: string, Handler)
    self._Events:SubscribeEntity(self.Entity, EventName, Handler)
end

function EntityScript:Dispatch(EventName: string, Payload)
    self._Events:Dispatch(EventName, Payload)
end

function EntityScript:DispatchDeferred(EventName: string, Payload)
    self._Events:DispatchDeferred(EventName, Payload)
end

-- ---- Physics callbacks ---------------------------------------------------
-- Define any of these on your script and the physics layer will call them
-- when contacts begin/end on this entity's body. Nothing to subscribe to:
-- the engine caches the function ref at attach time and skips delivery if
-- you didn't define one.
--
--   function MyScript:OnContactBegin(Event) ... end -- solid impact began
--   function MyScript:OnContactEnd(Event)   ... end -- solid contact ended
--   function MyScript:OnOverlapBegin(Event) ... end -- entered a trigger volume
--   function MyScript:OnOverlapEnd(Event)   ... end -- left a trigger volume
--
-- Event payload:
--   Entity, Other        — entt entity ids (self / other)
--   BodyID, OtherBodyID  — Jolt body ids
--   Point                — world-space contact point
--   Normal               — contact normal pointing away from self toward other
--   Velocity             — self linear velocity at contact time (m/s)
--   OtherVelocity        — other body linear velocity (m/s)
--   RelativeVelocity     — Other - Self (m/s)
--   ImpactSpeed          — |relative velocity along normal| (m/s)
--   bIsTrigger           — true if the OTHER side was a trigger/sensor

-- ---- Direct messages -----------------------------------------------------
-- Send a directed message to a specific entity's `On<Name>` handler.

function EntityScript:Send(Target, MessageName: string, Payload)
    self._Messages:Send(Target, MessageName, Payload)
end

function EntityScript:SendDeferred(Target, MessageName: string, Payload)
    self._Messages:SendDeferred(Target, MessageName, Payload)
end

-- ---- Timers --------------------------------------------------------------
-- After/Every are scoped to this entity, so timers stop when the entity
-- is destroyed. Returned handle can be passed to CancelTimer.
-- Wait yields the current coroutine for N seconds — only valid in lifecycle
-- methods or coroutines spawned via the engine; you can't Wait from a raw
-- callback (e.g. an Input.OnActionPressed handler) because there's no
-- coroutine to yield.

function EntityScript:Wait(Seconds: number)
    self._Timers:Wait(Seconds)
end

function EntityScript:After(Seconds: number, Callback)
    return self._Timers:Delay(Seconds, Callback)
end

function EntityScript:Every(Seconds: number, Callback)
    return self._Timers:SetEntityTimer(self.Entity, Seconds, Callback)
end

function EntityScript:CancelTimer(Handle)
    self._Timers:ClearTimer(Handle)
end

-- ---- Transform shorthand -------------------------------------------------
-- Forwarders for the most common STransformComponent calls so simple
-- scripts don't need `self.Transform:Foo(...)` boilerplate. Anything not
-- covered here is still reachable through self.Transform directly.

function EntityScript:Translate(Delta)            return self.Transform:Translate(Delta)            end
function EntityScript:AddRotationFromEuler(Euler) return self.Transform:AddRotationFromEuler(Euler) end

function EntityScript:GetForward()  return self.Transform:GetForward()  end
function EntityScript:GetRight()    return self.Transform:GetRight()    end
function EntityScript:GetUp()       return self.Transform:GetUp()       end

function EntityScript:GetLocation() return self.Transform:GetWorldLocation() end
function EntityScript:GetRotation() return self.Transform:GetWorldRotation() end
function EntityScript:GetScale()    return self.Transform:GetWorldScale()    end

function EntityScript:SetLocation(Location)        self.Transform:SetLocation(Location)               end
function EntityScript:SetRotation(Rotation)        self.Transform:SetRotation(Rotation)               end
function EntityScript:SetRotationFromEuler(Euler)  self.Transform:SetRotationFromEuler(Euler)         end

-- ---- Tweens --------------------------------------------------------------
-- Per-script tween scheduler. Lazily allocated on first use; advanced via
-- a chained zero-second timer that auto-stops when the queue empties.
-- TweenTo(field, value, duration, easing) is the common shorthand;
-- TweenRun lets you supply an arbitrary setter for compound writes
-- (e.g. self.Transform:SetLocation(v)).
--
-- Hot-reloading the script clears all running tweens implicitly because
-- the scheduler lives on `self`, which gets rebuilt with the new instance.

local function EnsureTweenScheduler(Self)
    if Self._TweenScheduler ~= nil then return Self._TweenScheduler end

    local TweenLib = require("Stdlib/Tween")
    local Sched    = TweenLib.New()
    Self._TweenScheduler = Sched

    local Timers = Self._Timers
    local function Tick()
        Sched:Step(Time.DeltaTime())
        if Sched:HasActive() then
            Timers:Delay(0.0, Tick)
        else
            Self._TweenSchedulerRunning = false
        end
    end

    Self._TweenSchedulerStart = function()
        if Self._TweenSchedulerRunning then return end
        Self._TweenSchedulerRunning = true
        Timers:Delay(0.0, Tick)
    end

    return Sched
end

function EntityScript:TweenTo(Field: string, ToValue, Duration: number, Easing, OnComplete)
    local Sched = EnsureTweenScheduler(self)
    local Handle = Sched:To(self, Field, ToValue, Duration, Easing, OnComplete)
    self._TweenSchedulerStart()
    return Handle
end

function EntityScript:TweenRun(From, To, Duration: number, Easing, Setter, OnComplete)
    local Sched = EnsureTweenScheduler(self)
    local Handle = Sched:Run(From, To, Duration, Easing, Setter, OnComplete)
    self._TweenSchedulerStart()
    return Handle
end

function EntityScript:CancelTween(Handle)
    if self._TweenScheduler then self._TweenScheduler:Cancel(Handle) end
end

function EntityScript:CancelAllTweens()
    if self._TweenScheduler then self._TweenScheduler:CancelAll() end
end

-- ---- Debug draw ----------------------------------------------------------
-- Each call forwards directly to the world DrawInterface. Trailing
-- arguments are optional and default to sane values; pass `false` for
-- DepthTest to keep lines visible through geometry.

-- Draw* bindings are registered via SetFunction(..., Instance) on the C++
-- side, which routes the instance through a closure upvalue rather than
-- argument 1. That means the calls below MUST use dot syntax — colon syntax
-- would push self._Draw as Lua arg #1 and shift every C++ parameter by one,
-- which produces "argument #N: number expected, got <wrong type>" errors at
-- runtime when the bool DepthTest slot lands where Duration is expected.

function EntityScript:DrawLine(Start, End, Color, Thickness, DepthTest, Duration)
    self._Draw.DrawLine(Start, End, Color, Thickness or 1.0, DepthTest ~= false, Duration or 0.0)
end

function EntityScript:DrawBox(Center, HalfExtents, Rotation, Color, Thickness, DepthTest, Duration)
    self._Draw.DrawBox(Center, HalfExtents, Rotation, Color, Thickness or 1.0, DepthTest ~= false, Duration or 0.0)
end

function EntityScript:DrawSphere(Center, Radius, Color, Segments, Thickness, DepthTest, Duration)
    self._Draw.DrawSphere(Center, Radius, Color, Segments or 16, Thickness or 1.0, DepthTest ~= false, Duration or 0.0)
end

function EntityScript:DrawCapsule(Start, End, Radius, Color, Segments, Thickness, DepthTest, Duration)
    self._Draw.DrawCapsule(Start, End, Radius, Color, Segments or 16, Thickness or 1.0, DepthTest ~= false, Duration or 0.0)
end

function EntityScript:DrawCone(Apex, Direction, AngleRadians, Length, Color, Segments, Stacks, Thickness, DepthTest, Duration)
    self._Draw.DrawCone(Apex, Direction, AngleRadians, Length, Color, Segments or 16, Stacks or 4, Thickness or 1.0, DepthTest ~= false, Duration or 0.0)
end

function EntityScript:DrawArrow(Start, Direction, Length, Color, Thickness, DepthTest, Duration, HeadSize)
    self._Draw.DrawArrow(Start, Direction, Length, Color, Thickness or 1.0, DepthTest ~= false, Duration or 0.0, HeadSize or 0.2)
end

-- ---- Physics -------------------------------------------------------------
-- Thin wrappers around IPhysicsScene that build the event struct and fill
-- in this entity's body id, so scripts don't repeat the SImpulseEvent etc.
-- boilerplate. Cross-entity physics work still goes through self._Physics.

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
-- script runs in PrePhysics and needs to compute force application points
-- or ray origins relative to the body — the cached transform component
-- holds the lagged interpolated render value, not what physics will
-- integrate from this step.

function EntityScript:GetBodyPosition()
    return self._Physics:GetBodyPosition(self:GetBodyID())
end

function EntityScript:GetBodyRotation()
    return self._Physics:GetBodyRotation(self:GetBodyID())
end

function EntityScript:GetBodyForward()
    return self:GetBodyRotation():RotateVector(vector.create(0.0, 0.0, 1.0))
end

function EntityScript:GetBodyRight()
    return self:GetBodyRotation():RotateVector(vector.create(1.0, 0.0, 0.0))
end

function EntityScript:GetBodyUp()
    return self:GetBodyRotation():RotateVector(vector.create(0.0, 1.0, 0.0))
end

return EntityScript
