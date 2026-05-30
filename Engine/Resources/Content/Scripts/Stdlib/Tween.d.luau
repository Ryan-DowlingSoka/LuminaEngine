-- Tween — interpolation primitives. Pure Lua; the entity-script base
-- (EntityScript) wraps this with per-script scheduling so user code can
-- write `self:TweenTo(...)` without managing a timer chain.
--
-- Standalone usage:
--   local Tween = require("Stdlib/Tween")
--   local Sched = Tween.New()
--   Sched:To(self, "Yaw", 90.0, 0.5, Tween.OutCubic)
--   -- Drive every frame:
--   Sched:Step(Time.DeltaTime())

local Tween = {}

-- ---- Easing functions ----------------------------------------------------
-- t in [0, 1] → eased value in [0, 1].

Tween.Linear     = function(t) return t end

Tween.InQuad     = function(t) return t * t end
Tween.OutQuad    = function(t) return 1.0 - (1.0 - t) * (1.0 - t) end
Tween.InOutQuad  = function(t)
    if t < 0.5 then return 2.0 * t * t end
    return 1.0 - (-2.0 * t + 2.0) ^ 2 * 0.5
end

Tween.InCubic    = function(t) return t * t * t end
Tween.OutCubic   = function(t) local f = 1.0 - t; return 1.0 - f * f * f end
Tween.InOutCubic = function(t)
    if t < 0.5 then return 4.0 * t * t * t end
    return 1.0 - (-2.0 * t + 2.0) ^ 3 * 0.5
end

Tween.InQuart    = function(t) return t * t * t * t end
Tween.OutQuart   = function(t) local f = 1.0 - t; return 1.0 - f * f * f * f end

Tween.InSine     = function(t) return 1.0 - math.cos(t * math.pi * 0.5) end
Tween.OutSine    = function(t) return math.sin(t * math.pi * 0.5) end
Tween.InOutSine  = function(t) return -(math.cos(math.pi * t) - 1.0) * 0.5 end

Tween.InExpo     = function(t) return t == 0 and 0 or 2.0 ^ (10.0 * (t - 1.0)) end
Tween.OutExpo    = function(t) return t == 1 and 1 or 1.0 - 2.0 ^ (-10.0 * t) end

-- "Out back" overshoot — useful for snappy UI bounces.
Tween.OutBack = function(t)
    local C1 = 1.70158
    local C3 = C1 + 1.0
    local f  = t - 1.0
    return 1.0 + C3 * f * f * f + C1 * f * f
end

-- "Out elastic" damped sine; oscillates as it lands.
Tween.OutElastic = function(t)
    if t == 0 or t == 1 then return t end
    local C4 = (2.0 * math.pi) / 3.0
    return 2.0 ^ (-10.0 * t) * math.sin((t * 10.0 - 0.75) * C4) + 1.0
end

-- ---- Lerp dispatch -------------------------------------------------------
-- Handles number, vector (Luau built-in), and userdata with :Lerp /
-- :SLerp (the engine Quat binding has both).

local function Lerp(A, B, T)
    if type(A) == "number" then
        return A + (B - A) * T
    elseif typeof(A) == "vector" then
        return A + (B - A) * T
    elseif type(A) == "userdata" then
        if A.SLerp then return A:SLerp(B, T) end
        if A.Lerp  then return A:Lerp(B, T) end
    end
    return A + (B - A) * T
end

Tween.Lerp = Lerp

-- ---- Scheduler -----------------------------------------------------------
-- A scheduler owns a set of running tweens. Step(dt) advances them all
-- and clears any that finished, firing OnComplete callbacks.

local Scheduler = {}
Scheduler.__index = Scheduler

function Tween.New()
    return setmetatable({
        Active     = {},
        NextHandle = 0,
    }, Scheduler)
end

function Scheduler:Run(From, To, Duration: number, Easing, Setter, OnComplete)
    self.NextHandle += 1
    local Handle = self.NextHandle
    self.Active[Handle] = {
        From       = From,
        To         = To,
        Duration   = math.max(1e-4, Duration),
        Elapsed    = 0.0,
        Easing     = Easing or Tween.Linear,
        Setter     = Setter,
        OnComplete = OnComplete,
    }
    return Handle
end

function Scheduler:To(Target, Field: string, ToValue, Duration: number, Easing, OnComplete)
    local From = Target[Field]
    return self:Run(From, ToValue, Duration, Easing, function(v)
        Target[Field] = v
    end, OnComplete)
end

function Scheduler:Cancel(Handle)
    self.Active[Handle] = nil
end

function Scheduler:CancelAll()
    self.Active = {}
end

function Scheduler:HasActive(): boolean
    return next(self.Active) ~= nil
end

function Scheduler:Step(DeltaTime: number)
    local Done
    for Handle, T in pairs(self.Active) do
        T.Elapsed += DeltaTime
        local U = math.clamp(T.Elapsed / T.Duration, 0.0, 1.0)
        T.Setter(Lerp(T.From, T.To, T.Easing(U)))
        if U >= 1.0 then
            if Done == nil then Done = {} end
            table.insert(Done, Handle)
        end
    end
    if Done then
        for _, Handle in ipairs(Done) do
            local T = self.Active[Handle]
            self.Active[Handle] = nil
            if T and T.OnComplete then T.OnComplete() end
        end
    end
end

return Tween
