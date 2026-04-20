#pragma once
#include "Includes/Common.slang"

[[vk::binding(0, 0)]] Sampler2D uAccum;
[[vk::binding(1, 0)]] Sampler2D uRevealage;

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

    float Revealage = uRevealage.Load(int3(Coords, 0)).r;

    if(IsApproximatelyEqual(Revealage, 1.0, 1e-6))
    {
        discard;
    }

    float4 Accum = uAccum.Load(int3(Coords, 0));

    if(isinf(max3(abs(Accum.rgb))))
    {
        Accum.rgb = float3(Accum.a);
    }

    float3 FinalColor = Accum.rgb / max(Accum.a, 1e-6);

    FSOutput Output;
    Output.Color = float4(FinalColor, 1.0 - Revealage);
    return Output;
}