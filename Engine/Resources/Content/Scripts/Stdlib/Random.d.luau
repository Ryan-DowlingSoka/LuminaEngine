-- Random — convenience wrappers over Lua's math.random for game code.
--
-- math.random is global state shared between every script (and the engine).
-- Random.Stream(seed) returns an isolated generator if you need
-- determinism for a specific system without disturbing the global one.

local Random = {}

-- Float in [Min, Max].
function Random.Range(Min: number, Max: number): number
    return Min + (Max - Min) * math.random()
end

-- Integer in [Min, Max] inclusive.
function Random.RangeInt(Min: number, Max: number): number
    return math.random(Min, Max)
end

-- True with the given probability in [0, 1].
function Random.Chance(P: number): boolean
    return math.random() < P
end

-- Pick a random element from an array-style table. Returns nil for empty.
function Random.Pick(T)
    local N = #T
    if N == 0 then return nil end
    return T[math.random(1, N)]
end

-- Weighted pick. Items is array of {Value = X, Weight = N}; ties resolve
-- by order. Returns the picked Value, or nil for empty / zero total weight.
function Random.PickWeighted(Items)
    local Total = 0.0
    for _, Entry in ipairs(Items) do
        Total += (Entry.Weight or 0.0)
    end
    if Total <= 0.0 then return nil end

    local Roll = math.random() * Total
    local Acc  = 0.0
    for _, Entry in ipairs(Items) do
        Acc += (Entry.Weight or 0.0)
        if Roll <= Acc then return Entry.Value end
    end
    return Items[#Items].Value
end

-- Direction on the unit circle (XZ plane), then optional radius.
function Random.OnCircle(Radius: number?): vector
    local A = math.random() * 2.0 * math.pi
    local R = Radius or 1.0
    return vector.create(math.cos(A) * R, 0.0, math.sin(A) * R)
end

-- Uniform point inside a disc of given radius (XZ plane).
function Random.InDisc(Radius: number): vector
    local A = math.random() * 2.0 * math.pi
    local R = Radius * math.sqrt(math.random())
    return vector.create(math.cos(A) * R, 0.0, math.sin(A) * R)
end

-- Direction on the unit sphere. Uses Marsaglia's method to avoid the
-- pole bias of naive lat/lon sampling.
function Random.OnSphere(): vector
    local U = 2.0 * math.random() - 1.0
    local T = 2.0 * math.pi * math.random()
    local R = math.sqrt(math.max(0.0, 1.0 - U * U))
    return vector.create(math.cos(T) * R, U, math.sin(T) * R)
end

-- Uniform point inside a sphere of given radius. Uses cube-root scale on
-- the radius so density stays uniform per unit volume, not per shell.
function Random.InSphere(Radius: number): vector
    local Dir = Random.OnSphere()
    local R   = Radius * math.pow(math.random(), 1.0 / 3.0)
    return Dir * R
end

-- Random point inside an axis-aligned box (Min/Max are vectors).
function Random.InBox(Min: vector, Max: vector): vector
    return vector.create(
        Min.x + (Max.x - Min.x) * math.random(),
        Min.y + (Max.y - Min.y) * math.random(),
        Min.z + (Max.z - Min.z) * math.random())
end

-- Random unit rotation. Uses three Euler angles drawn uniformly; not
-- perfectly uniform over SO(3) (true uniformity needs Shoemake's quat
-- formula) but cheap and good enough for spawn jitter.
function Random.Rotation()
    return Math.QuatFromEuler(vector.create(
        math.random() * 360.0 - 180.0,
        math.random() * 360.0 - 180.0,
        math.random() * 360.0 - 180.0))
end

-- Reseed the global generator. Use sparingly — most game systems share
-- the global stream, and reseeding mid-run can cause visible artefacts
-- (terrain regen, AI re-rolls). Prefer Random.Stream for isolated runs.
function Random.Seed(Seed: number)
    math.randomseed(Seed)
end

-- Independent stream backed by a Lua closure: { Range, RangeInt, Chance,
-- Pick }. Useful when you want a per-system / per-save generator that
-- doesn't perturb the global one. Uses a 32-bit LCG; not cryptographic.
function Random.Stream(Seed: number)
    local State = (Seed or 1) % 2147483648
    if State == 0 then State = 1 end

    local function Next()
        State = (State * 1103515245 + 12345) % 2147483648
        return State / 2147483648
    end

    return {
        Next     = Next,
        Range    = function(Min, Max) return Min + (Max - Min) * Next() end,
        RangeInt = function(Min, Max) return Min + math.floor(Next() * (Max - Min + 1)) end,
        Chance   = function(P)        return Next() < P end,
        Pick     = function(T)
            local N = #T
            if N == 0 then return nil end
            return T[1 + math.floor(Next() * N)]
        end,
    }
end

return Random
