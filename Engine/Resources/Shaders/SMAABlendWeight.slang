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

[[vk::binding(0, 2)]] Sampler2D uEdgesTex;
[[vk::binding(1, 2)]] Sampler2D uAreaTex;
[[vk::binding(2, 2)]] Sampler2D uSearchTex;

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

[shader("fragment")]
float4 FragmentMain(VSOutput Input) : SV_Target0
{
    float2 pixcoord;
    float4 offset[3];
    SMAABlendingWeightCalculationOffsets(Input.UV, pixcoord, offset);

    return SMAABlendingWeightCalculationPS(
        Input.UV,
        pixcoord,
        offset,
        uEdgesTex,
        uAreaTex,
        uSearchTex,
        float4(0.0, 0.0, 0.0, 0.0));
}
