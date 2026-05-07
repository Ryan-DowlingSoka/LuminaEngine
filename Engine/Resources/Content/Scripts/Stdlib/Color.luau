-- Color — vec4 constants and helpers. Channel order is (R, G, B, A) and
-- values are in [0, 1] (linear). Use FromBytes if your input is 0..255.
--
-- Engine APIs that take "Color" expect a vec4. Every function here
-- returns a `vector` so the result drops straight into _Draw and friends.

local Color = {}

-- ---- Construction --------------------------------------------------------

function Color.RGB(R: number, G: number, B: number): vector
    return vector.create(R, G, B, 1.0)
end

function Color.RGBA(R: number, G: number, B: number, A: number): vector
    return vector.create(R, G, B, A)
end

-- 0..255 sRGB-style bytes → linear [0,1]. Doesn't apply gamma, just scales.
function Color.FromBytes(R: number, G: number, B: number, A: number?): vector
    return vector.create(R / 255.0, G / 255.0, B / 255.0, (A or 255) / 255.0)
end

-- HSV inputs in (H: degrees [0,360), S/V: [0,1]). Pass A in [0,1].
function Color.HSV(H: number, S: number, V: number, A: number?): vector
    local C = V * S
    local Hp = (H % 360.0) / 60.0
    local X = C * (1.0 - math.abs(Hp % 2.0 - 1.0))
    local R, G, B = 0.0, 0.0, 0.0
    if     Hp < 1.0 then R, G, B = C, X, 0
    elseif Hp < 2.0 then R, G, B = X, C, 0
    elseif Hp < 3.0 then R, G, B = 0, C, X
    elseif Hp < 4.0 then R, G, B = 0, X, C
    elseif Hp < 5.0 then R, G, B = X, 0, C
    else                 R, G, B = C, 0, X
    end
    local M = V - C
    return vector.create(R + M, G + M, B + M, A or 1.0)
end

-- ---- Helpers -------------------------------------------------------------

function Color.Lerp(A: vector, B: vector, T: number): vector
    return A + (B - A) * T
end

function Color.WithAlpha(C: vector, A: number): vector
    return vector.create(C.x, C.y, C.z, A)
end

-- ---- Constants -----------------------------------------------------------
-- Standard CSS-style palette plus a few engine staples. All opaque.

Color.White       = vector.create(1.0, 1.0, 1.0, 1.0)
Color.Black       = vector.create(0.0, 0.0, 0.0, 1.0)
Color.Transparent = vector.create(0.0, 0.0, 0.0, 0.0)
Color.Red         = vector.create(1.0, 0.0, 0.0, 1.0)
Color.Green       = vector.create(0.0, 1.0, 0.0, 1.0)
Color.Blue        = vector.create(0.0, 0.0, 1.0, 1.0)
Color.Yellow      = vector.create(1.0, 1.0, 0.0, 1.0)
Color.Cyan        = vector.create(0.0, 1.0, 1.0, 1.0)
Color.Magenta     = vector.create(1.0, 0.0, 1.0, 1.0)
Color.Orange      = vector.create(1.0, 0.5, 0.0, 1.0)
Color.Purple      = vector.create(0.5, 0.0, 0.5, 1.0)
Color.Pink        = vector.create(1.0, 0.4, 0.7, 1.0)
Color.Gray        = vector.create(0.5, 0.5, 0.5, 1.0)
Color.LightGray   = vector.create(0.75, 0.75, 0.75, 1.0)
Color.DarkGray    = vector.create(0.25, 0.25, 0.25, 1.0)
Color.Brown       = vector.create(0.55, 0.27, 0.07, 1.0)

return Color
