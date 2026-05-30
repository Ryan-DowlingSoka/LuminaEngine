#pragma once
#include "Includes/SceneGlobals.slang"

// Picker-only companion to WidgetPixel: runs pre-tone-map and writes the widget's entity id where opaque,
// so world-space widgets stay click-selectable even though their color is drawn post-tone-map.

struct FragmentInput
{
    float2 UV                           : TEXCOORD0;
    float4 Tint                         : TINT;
    nointerpolation uint TextureIndex   : TEXTURE_INDEX;
    nointerpolation uint EntityID       : ENTITY_ID;
};

[shader("fragment")]
uint FragmentMain(FragmentInput Input) : SV_Target0
{
    const float Alpha = SampleBindless2D(Input.TextureIndex, SAMPLER_LINEAR_CLAMP, Input.UV).a;
    if (Alpha < 0.01)
    {
        discard;
    }
    return Input.EntityID;
}
