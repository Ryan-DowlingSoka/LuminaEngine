#pragma once
#include "Includes/SceneGlobals.slang"

// ============================================================================
//  Push Constants
// ============================================================================

struct FPushConstants
{
    uint LightIndex;
    int  ShadowDataIndex;
};

// ============================================================================
//  Input
// ============================================================================

struct PSInput
{
    float3 WorldPos : TEXCOORD0;
    float3 LightPos : TEXCOORD1;
    float4 Position : SV_Position;
};

// ============================================================================
//  Main
// ============================================================================

[shader("fragment")]
float FragmentMain(PSInput Input, uniform FPushConstants PC) : SV_Depth
{
    FLight Light    = GetLightAt(PC.LightIndex);
    float  Distance = length(Input.WorldPos - Input.LightPos);
    return Distance / Light.Radius;
}