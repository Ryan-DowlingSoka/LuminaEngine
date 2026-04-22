#pragma once

struct FPushConstants
{
    float4 RTMetrics;
    float  EdgeThreshold;
    float  DebugMode;
    float  _Pad0;
    float  _Pad1;
};

[[vk::push_constant]] ConstantBuffer<FPushConstants> PC;

#define SMAA_RT_METRICS (PC.RTMetrics)
#include "Includes/SMAA.slang"

[[vk::binding(0, 2)]] Sampler2D uColorTex;
[[vk::binding(1, 2)]] Sampler2D uBlendTex;

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

[shader("fragment")]
float4 FragmentMain(VSOutput Input) : SV_Target0
{
    if (PC.DebugMode > 0.5)
    {
        float4 bw = uBlendTex.Sample(Input.UV);
        return float4(bw.rgb, 1.0);
    }

    float4 offset;
    SMAANeighborhoodBlendingOffsets(Input.UV, offset);

    return SMAANeighborhoodBlendingPS(Input.UV, offset, uColorTex, uBlendTex);
}
