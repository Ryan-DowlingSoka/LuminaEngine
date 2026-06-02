#pragma once

#include "Includes/Common.slang"
#include "Includes/SceneGlobals.slang"

// SMAA pass 1 (edge detection). No pass-local binding set: the input color is read through the
// bindless table via an index in the push constant (matches FSMAAPushConstants on the C++ side).
struct FPushConstants
{
    float4 RTMetrics;
    float  EdgeThreshold;
    float  DebugMode;
    uint   TexIndex0;   // LDR color
    uint   TexIndex1;
    uint   TexIndex2;
    uint   _Pad0;
    uint   _Pad1;
    uint   _Pad2;
};

#define PC (*(FPushConstants*)gPC.PassAddr)

#define SMAA_RT_METRICS (PC.RTMetrics)
#include "Includes/SMAA.slang"

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

[shader("fragment")]
float4 FragmentMain(VSOutput Input) : SV_Target0
{
    SMAATex colorTex;
    colorTex.Tex = uBindlessTex2D[PC.TexIndex0];
    colorTex.Smp = uBindlessSamplers[SAMPLER_POINT_CLAMP];

    float4 offset[3];
    SMAAEdgeDetectionOffsets(Input.UV, offset);

    float2 edges = SMAALumaEdgeDetectionPS(Input.UV, offset, colorTex, PC.EdgeThreshold);
    return float4(edges, 0.0, 1.0);
}
