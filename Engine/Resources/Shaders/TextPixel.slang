#pragma once
#include "Includes/TextCommon.slang"

struct FragmentInput
{
    float2 UV                       : TEXCOORD0;
    nointerpolation float4 Color    : COLOR0;
    nointerpolation uint EntityID   : ENTITY_ID;
};

struct FragmentOutput
{
    float4 Color  : SV_Target0;
    uint   Picker : SV_Target1;   // entity id stamped into the Picker buffer where the glyph is opaque
};

// Screen-space distance range at this fragment: scales the per-texel MSDF range into screen pixels so the
// anti-aliased edge stays ~1px wide at any scale or viewing angle (Chlumsky's standard resolve).
float TextScreenPxRange(float2 UV)
{
    const float2 UnitRange     = float2(PC.DistanceRange) / float2(PC.AtlasWidth, PC.AtlasHeight);
    const float2 ScreenTexSize = 1.0 / fwidth(UV);
    return max(0.5 * dot(UnitRange, ScreenTexSize), 1.0);
}

[shader("fragment")]
FragmentOutput FragmentMain(FragmentInput Input)
{
    // MTSDF: rgb = multi-channel SDF (crisp corners, but the median-of-3 dips slightly under magnification at
    // concave junctions), a = true single-channel SDF (no median dip, sign-corrected at bake). Taking the
    // inside-most of the two fills those residual magnification dips while keeping convex corners crisp.
    const float4 MSDF = uBindlessTex2D[PC.AtlasIndex].Sample(uBindlessSamplers[SAMPLER_LINEAR_CLAMP], Input.UV);
    const float  SD = max(TextMedian(MSDF.r, MSDF.g, MSDF.b), MSDF.a);
    const float  Opacity = saturate((SD - 0.5) * TextScreenPxRange(Input.UV) + 0.5);

    if (Opacity <= 0.0)
    {
        discard;
    }

    FragmentOutput Output;
    Output.Color  = float4(Input.Color.rgb, Input.Color.a * Opacity);
    Output.Picker = Input.EntityID;
    return Output;
}
