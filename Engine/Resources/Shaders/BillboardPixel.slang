#pragma once
#include "Includes/SceneGlobals.slang"

struct FragmentInput
{
    float2 UV                           : TEXCOORD0;
    float3 ColorTint                    : COLORTINT;
    nointerpolation uint TextureIndex   : TEXTURE_INDEX;
    nointerpolation uint EntityID       : ENTITY_ID;
};

struct FragmentOutput
{
    float4 Color  : SV_Target0;
    uint   Picker : SV_Target1;
};


[shader("fragment")]
FragmentOutput FragmentMain(FragmentInput Input)
{
    float4 Texture = uGlobalTextures[Input.TextureIndex].Sample(Input.UV);

    if (Texture.a == 0.0)
    {
        discard;
    }

    FragmentOutput Output;
    Output.Color  = Texture * float4(Input.ColorTint, 1.0f);
    Output.Picker = Input.EntityID;
    return Output;
}