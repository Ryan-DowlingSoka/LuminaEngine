#pragma once
#include "Includes/SceneGlobals.slang"

struct FragmentInput
{
    float2 UV                           : TEXCOORD0;
    float4 Tint                         : TINT;
    nointerpolation uint TextureIndex   : TEXTURE_INDEX;
    nointerpolation uint EntityID       : ENTITY_ID;
};

// Drawn AFTER tone mapping, straight onto the final (display-referred) target -- so the
// RmlUi RT's sRGB colors are written as-is and match the screen-space UI. No tone map,
// no gamma conversion.
[shader("fragment")]
float4 FragmentMain(FragmentInput Input) : SV_Target0
{
    float4 Texel = SampleBindless2D(Input.TextureIndex, SAMPLER_LINEAR_CLAMP, Input.UV) * Input.Tint;

    if (Texel.a < 0.01)
    {
        discard;
    }

    return Texel;
}
