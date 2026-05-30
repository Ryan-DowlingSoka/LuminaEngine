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
-- The script-facing API is split between flat helpers on `self` (the common,
-- entity-core operations: lifecycle, transform, components, identity, physics
-- verbs) and namespaced facets for the larger domains. The facets read like the
-- engine's global tables but are scoped to THIS entity:
--   self.Tree     — parent / child hierarchy          (self.Tree:GetParent())
--   self.Timers   — delays, intervals, waits          (self.Timers:After(1, fn))
--   self.Events   — pub/sub event bus                 (self.Events:Subscribe("Hit", fn))
--   self.Draw     — debug primitives                  (self.Draw:Sphere(p, r, c))
--   self.Tween    — per-script tween scheduler        (self.Tween:To("Yaw", 90, 0.5))
-- Facets are built lazily on first access and cached, so an entity only pays
-- for the ones it uses. Engine services are injected under private (underscore)
-- slots and live behind these facets — game code never touches them directly.
--
-- Strict boundary: `self` exposes ONLY this entity / script instance. Anything that
-- touches the world or an engine subsystem goes through the global `World` — physics
-- is World.Physics:AddForce(self.Entity, f), audio is World.Audio, etc. There is no
-- self.Physics. (The world-scoped facets above — Tree/Timers/Events/Draw — are being
-- migrated to World as well; see the API redesign.)

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
    Input     = "SInputComponent (after EnableInput)",
    Tree      = "facet: parent / child hierarchy",
    Timers    = "facet: delays, intervals, waits",
    Events    = "facet: pub/sub event bus",
    Draw      = "facet: debug primitives",
    Tween     = "facet: per-script tween scheduler",
}

-- ---- Facets --------------------------------------------------------------
-- A facet is a method table keyed by its self.<Name> slot. The first time a
-- script reads self.<Name>, __index builds a tiny instance bound to the owner
-- ({ Owner = self }) and caches it back onto the instance, so subsequent reads
-- skip __index entirely. Facet methods reach the owner's entity / world and the
-- injected engine services through self.Owner.

local Facets = {}

local function DefineFacet(Name: string)
    local Class = {}
    Class.__index = Class
    Facets[Name] = Class
    return Class
end

-- ---- Metatable -----------------------------------------------------------
-- __index: instance miss → method on EntityScript → facet → Exports[key] → nil.
-- __newindex: instance miss → write into Exports (so user-declared property
-- writes land where the editor / serializer can see them). Engine-injected
-- slots (Entity, World, Transform, Name, Exports, _*) are populated via
-- rawset on the C++ side, so they bypass __newindex; explicit user writes
-- to those names will still hit __newindex and route to Exports — engine
-- writes win because they happen first and via raw access.
--
-- Underscore-prefixed keys are private runtime state (caches, handles,
-- engine-injected services). They live as raw fields on the instance and
-- never round-trip through Exports — this keeps internal slots out of the
-- property panel and out of any future serialization pass.

local function IsPrivateKey(Key)
    return type(Key) == "string" and string.sub(Key, 1, 1) == "_"
end

function EntityScript.__index(Self, Key)
    local Method = rawget(EntityScript, Key)
    if Method ~= nil then return Method end

    -- Lazily materialize and cache a facet on first access.
    local FacetClass = Facets[Key]
    if FacetClass ~= nil then
        local Instance = setmetatable({ Owner = Self }, FacetClass)
        rawset(Self, Key, Instance)
        return Instance
    end

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
--
-- Physics contact callbacks (define any; the engine skips delivery if absent):
--   function MyScript:OnContactBegin(Event) ... end -- solid impact began
--   function MyScript:OnContactEnd(Event)   ... end -- solid contact ended
--   function MyScript:OnOverlapBegin(Event) ... end -- entered a trigger volume
--   function MyScript:OnOverlapEnd(Event)   ... end -- left a trigger volume
-- Event payload: Entity, Other (entt ids); BodyID, OtherBodyID (Jolt ids);
-- Point, Normal; Velocity, OtherVelocity, RelativeVelocity; ImpactSpeed;
-- bIsTrigger (true if the OTHER side was a trigger/sensor).

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

-- ---- Components ----------------------------------------------------------
-- ComponentType is the reflected struct global (e.g. STransformComponent,
-- SStaticMeshComponent). Pass the type itself, not a string. These act on
-- THIS entity; to read a component off another entity id use the global
-- World.GetComponent(entity, Type) / World.HasComponent(entity, Type).

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

-- ---- Input opt-in --------------------------------------------------------
-- Player input is component-gated: only an entity with SInputComponent can
-- read keys, actions, or mouse delta. Call self:EnableInput() once (typically
-- in OnReady) to add the component and cache it as self.Input, then query
-- via the component:
--   self.Input:IsActionDown("Jump")
--   self.Input:GetActionAxis("MoveX")
--   self.Input:GetMouseDeltaX()
--   local Id = self.Input:BindAction("Fire", function() ... end)
--   self.Input:UnbindAction(Id)
-- Call self:DisableInput() to remove the component (and drop bound callbacks).
-- For a temporary mute that keeps bindings, set self.Input.bEnabled = false.
-- The global `Input.*` table only carries window-scoped knobs (SetMouseMode,
-- SetMode, GetMode); every gameplay query lives on the component.
--
-- EnableInput is idempotent: when the component already exists it just
-- refreshes the self.Input cache.

function EntityScript:EnableInput()
    local C = self:GetComponent(SInputComponent)
    if C == nil then
        C = self:AddComponent(SInputComponent)
    end
    self.Input = C
    return C
end

function EntityScript:DisableInput()
    self.Input = nil
    if self:HasComponent(SInputComponent) then
        self:RemoveComponent(SInputComponent)
    end
end

-- ---- Transform shorthand -------------------------------------------------
-- Forwarders for the most common STransformComponent calls so simple scripts
-- don't need `self.Transform:Foo(...)` boilerplate. Anything not covered here
-- is still reachable through self.Transform directly.

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

-- ---- Camera --------------------------------------------------------------
-- Make this entity's camera the active view. BlendTime > 0 eases from the
-- current camera over that many seconds; Ease defaults to EaseInOut. Use the
-- Camera.Ease.* constants (Linear, EaseIn, EaseOut, EaseInOut).
function EntityScript:SetActiveCamera(BlendTime, Ease)
    Camera.SetActive(self.Entity, BlendTime or 0.0, Ease or Camera.Ease.EaseInOut)
end

-- Point this entity's follow / spring-arm rig at a target entity. No-op if the
-- entity has neither rig component.
function EntityScript:SetCameraTarget(Target)
    local Follow = self:GetComponent(SCameraFollowComponent)
    if Follow then
        Follow:SetTarget(Target)
        return
    end
    local Arm = self:GetComponent(SSpringArmComponent)
    if Arm then
        Arm:SetTarget(Target)
    end
end

-- Physics lives on World, not self: World.Physics:AddForce(self.Entity, Force),
-- World.Physics:SetLinearVelocity(self.Entity, Velocity), World.Physics:GetBodyPosition(
-- self.Entity), etc. Pass self.Entity as the target. There is intentionally no self.Physics.

-- ==========================================================================
-- Facet: self.Tree
-- ==========================================================================
-- Parent/child traversal over the entity relationship graph. Single-entity
-- getters return nil instead of a null id when there's no parent, so they read
-- cleanly in `if`/`and` chains; list getters return arrays of entity ids
-- (possibly empty) ready for ipairs.

local Tree = DefineFacet("Tree")

function Tree:HasParent()
    -- valid(entt::null) is false, so this doubles as the "has a parent" test.
    local R = self.Owner._Registry
    return R:Valid(R:GetParent(self.Owner.Entity))
end

-- Immediate parent entity id, or nil if this entity is a root.
function Tree:GetParent()
    local R = self.Owner._Registry
    local Parent = R:GetParent(self.Owner.Entity)
    if not R:Valid(Parent) then return nil end
    return Parent
end

-- The parent's script table, or nil when there's no parent or it has no script.
function Tree:GetParentScript()
    local Parent = self:GetParent()
    if Parent == nil then return nil end
    return self.Owner._Registry:GetScriptTable(Parent)
end

-- Topmost ancestor in the parent chain; returns this entity's own id when it
-- has no parent.
function Tree:GetRoot()
    return self.Owner._Registry:GetRoot(self.Owner.Entity)
end

-- Immediate children (array of entity ids; empty when none).
function Tree:GetChildren()
    return self.Owner._Registry:GetChildren(self.Owner.Entity)
end

-- Ancestors from immediate parent up to the root, nearest first.
function Tree:GetAncestors()
    return self.Owner._Registry:GetAncestors(self.Owner.Entity)
end

-- Every descendant in the subtree (depth-first), excluding this entity.
function Tree:GetDescendants()
    return self.Owner._Registry:GetDescendants(self.Owner.Entity)
end

-- True when Ancestor sits somewhere above this entity in the parent chain.
function Tree:IsChildOf(Ancestor)
    return self.Owner._Registry:IsDescendantOf(self.Owner.Entity, Ancestor)
end

-- True when this entity sits somewhere above Other in its parent chain.
function Tree:IsAncestorOf(Other)
    return self.Owner._Registry:IsDescendantOf(Other, self.Owner.Entity)
end

function Tree:GetChildCount()
    return self.Owner._Registry:GetChildCount(self.Owner.Entity)
end

-- Reparent under NewParent, or pass nil to detach to the world root. The
-- entity's world transform is preserved across the move.
function Tree:SetParent(NewParent)
    local R = self.Owner._Registry
    if NewParent == nil then
        R:Detach(self.Owner.Entity)
    else
        R:Reparent(self.Owner.Entity, NewParent)
    end
end

-- Reparent Child under this entity (the inverse of SetParent).
function Tree:AddChild(Child)
    self.Owner._Registry:Reparent(Child, self.Owner.Entity)
end

-- First direct child named Name, or nil. Scoped to this entity's children, so
-- it's unambiguous where a global World.FindByName would not be.
function Tree:FindChild(Name: string)
    local R = self.Owner._Registry
    local Child = R:FindChild(self.Owner.Entity, Name)
    if not R:Valid(Child) then return nil end
    return Child
end

-- As FindChild but searches the entire subtree (depth-first), or nil.
function Tree:FindDescendant(Name: string)
    local R = self.Owner._Registry
    local Found = R:FindDescendant(self.Owner.Entity, Name)
    if not R:Valid(Found) then return nil end
    return Found
end

-- Other children of this entity's parent (array; empty when parentless).
function Tree:GetSiblings()
    return self.Owner._Registry:GetSiblings(self.Owner.Entity)
end

-- Zero-based position among the parent's children (0 when parentless).
function Tree:GetSiblingIndex()
    return self.Owner._Registry:GetSiblingIndex(self.Owner.Entity)
end

-- Next/previous entry in the parent's child list, or nil at the ends.
function Tree:GetNextSibling()
    local R = self.Owner._Registry
    local Sibling = R:GetNextSibling(self.Owner.Entity)
    if not R:Valid(Sibling) then return nil end
    return Sibling
end

function Tree:GetPrevSibling()
    local R = self.Owner._Registry
    local Sibling = R:GetPrevSibling(self.Owner.Entity)
    if not R:Valid(Sibling) then return nil end
    return Sibling
end

-- ==========================================================================
-- Facet: self.Timers
-- ==========================================================================
-- After/Every are scoped to this entity, so timers stop when the entity is
-- destroyed. Returned handle can be passed to Cancel. Wait yields the current
-- coroutine for N seconds — lifecycle methods (OnReady/OnUpdate/OnContact*)
-- run as direct pcalls, NOT coroutines, so calling Wait from one errors; use
-- After instead, or invoke from a coroutine you spawned yourself.

local Timers = DefineFacet("Timers")

function Timers:Wait(Seconds: number)
    self.Owner._Timers:Wait(Seconds)
end

function Timers:After(Seconds: number, Callback)
    return self.Owner._Timers:Delay(Seconds, Callback)
end

function Timers:Every(Seconds: number, Callback)
    return self.Owner._Timers:SetEntityTimer(self.Owner.Entity, Seconds, Callback)
end

function Timers:Cancel(Handle)
    self.Owner._Timers:ClearTimer(Handle)
end

-- ==========================================================================
-- Facet: self.Events
-- ==========================================================================
-- Subscribe is automatically scoped to this entity, so subscriptions are
-- cleaned up when the entity is destroyed. Use Dispatch to broadcast.

local Events = DefineFacet("Events")

function Events:Subscribe(EventName: string, Handler)
    self.Owner._Events:SubscribeEntity(self.Owner.Entity, EventName, Handler)
end

function Events:Dispatch(EventName: string, Payload)
    self.Owner._Events:Dispatch(EventName, Payload)
end

function Events:DispatchDeferred(EventName: string, Payload)
    self.Owner._Events:DispatchDeferred(EventName, Payload)
end

-- ==========================================================================
-- Facet: self.Draw
-- ==========================================================================
-- Each call forwards directly to the world DrawInterface. Trailing arguments
-- are optional and default to sane values; pass `false` for DepthTest to keep
-- lines visible through geometry.
--
-- The underlying Draw* bindings are registered via SetFunction(..., Instance)
-- on the C++ side, which routes the instance through a closure upvalue rather
-- than argument 1, so the calls below MUST use dot syntax — colon syntax would
-- push the draw interface as arg #1 and shift every C++ parameter by one.

local Draw = DefineFacet("Draw")

function Draw:Line(Start, End, Color, Thickness, DepthTest, Duration)
    self.Owner._Draw.DrawLine(Start, End, Color, Thickness or 1.0, DepthTest ~= false, Duration or 0.0)
end

function Draw:Box(Center, HalfExtents, Rotation, Color, Thickness, DepthTest, Duration)
    self.Owner._Draw.DrawBox(Center, HalfExtents, Rotation, Color, Thickness or 1.0, DepthTest ~= false, Duration or 0.0)
end

function Draw:Sphere(Center, Radius, Color, Segments, Thickness, DepthTest, Duration)
    self.Owner._Draw.DrawSphere(Center, Radius, Color, Segments or 16, Thickness or 1.0, DepthTest ~= false, Duration or 0.0)
end

function Draw:Capsule(Start, End, Radius, Color, Segments, Thickness, DepthTest, Duration)
    self.Owner._Draw.DrawCapsule(Start, End, Radius, Color, Segments or 16, Thickness or 1.0, DepthTest ~= false, Duration or 0.0)
end

function Draw:Cone(Apex, Direction, AngleRadians, Length, Color, Segments, Stacks, Thickness, DepthTest, Duration)
    self.Owner._Draw.DrawCone(Apex, Direction, AngleRadians, Length, Color, Segments or 16, Stacks or 4, Thickness or 1.0, DepthTest ~= false, Duration or 0.0)
end

function Draw:Arrow(Start, Direction, Length, Color, Thickness, DepthTest, Duration, HeadSize)
    self.Owner._Draw.DrawArrow(Start, Direction, Length, Color, Thickness or 1.0, DepthTest ~= false, Duration or 0.0, HeadSize or 0.2)
end

-- ==========================================================================
-- Facet: self.Tween
-- ==========================================================================
-- Per-script tween scheduler. Lazily allocated on first use; advanced via a
-- chained zero-second timer that auto-stops when the queue empties.
--   self.Tween:To(field, value, duration, easing, onComplete)
--     Tween a field on the script instance (e.g. an exported number) toward a
--     value. Reads the current value as the start.
--   self.Tween:Run(from, to, duration, easing, setter, onComplete)
--     Arbitrary setter for compound writes (e.g. self.Transform:SetLocation(v)).
-- Scheduler state lives on the facet, which is rebuilt with the instance on
-- hot-reload, so running tweens clear implicitly when the script reloads.

local Tween = DefineFacet("Tween")

local function EnsureScheduler(self)
    if self.Scheduler ~= nil then return self.Scheduler end

    local TweenLib = require("Stdlib/Tween")
    local Sched    = TweenLib.New()
    self.Scheduler = Sched

    local Timers = self.Owner._Timers
    local function Tick()
        Sched:Step(Time.DeltaTime())
        if Sched:HasActive() then
            Timers:Delay(0.0, Tick)
        else
            self.Running = false
        end
    end

    self.Start = function()
        if self.Running then return end
        self.Running = true
        Timers:Delay(0.0, Tick)
    end

    return Sched
end

function Tween:To(Field: string, ToValue, Duration: number, Easing, OnComplete)
    local Sched = EnsureScheduler(self)
    local Handle = Sched:To(self.Owner, Field, ToValue, Duration, Easing, OnComplete)
    self.Start()
    return Handle
end

function Tween:Run(From, To, Duration: number, Easing, Setter, OnComplete)
    local Sched = EnsureScheduler(self)
    local Handle = Sched:Run(From, To, Duration, Easing, Setter, OnComplete)
    self.Start()
    return Handle
end

function Tween:Cancel(Handle)
    if self.Scheduler then self.Scheduler:Cancel(Handle) end
end

function Tween:CancelAll()
    if self.Scheduler then self.Scheduler:CancelAll() end
end

return EntityScript
