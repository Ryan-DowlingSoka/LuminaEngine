#pragma once
#include "Includes/Common.slang"
#include "Includes/SceneGlobals.slang"

// OIT resolve. No pass-local set: the WBOIT accum / revealage targets are read through the bindless
// table (texel fetch, no sampler) via indices in the push constant.
struct FPushConstants
{
    uint AccumIndex;
    uint RevealageIndex;
    uint _Pad0;
    uint _Pad1;
};
#define PC (*(FPushConstants*)gPC.PassAddr)

struct FSInput
{
    float4 Coord : SV_Position;
}

struct FSOutput
{
    float4 Color : SV_Target0;
};

[shader("fragment")]
FSOutput FragmentMain(FSInput Input)
{
    int2 Coords = int2(Input.Coord.xy);

    float Revealage = uBindlessTex2D[PC.RevealageIndex].Load(int3(Coords, 0)).r;

    if(IsApproximatelyEqual(Revealage, 1.0, 1e-6))
    {
        discard;
    }

    float4 Accum = uBindlessTex2D[PC.AccumIndex].Load(int3(Coords, 0));

    if(isinf(max3(abs(Accum.rgb))))
    {
        Accum.rgb = float3(Accum.a);
    }

    float3 FinalColor = Accum.rgb / max(Accum.a, 1e-6);

    FSOutput Output;
    Output.Color = float4(FinalColor, 1.0 - Revealage);
    return Output;
}
