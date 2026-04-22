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

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

[shader("fragment")]
float4 FragmentMain(VSOutput Input) : SV_Target0
{
    float4 offset[3];
    SMAAEdgeDetectionOffsets(Input.UV, offset);

    float2 edges = SMAALumaEdgeDetectionPS(Input.UV, offset, uColorTex, PC.EdgeThreshold);
    return float4(edges, 0.0, 1.0);
}
