#pragma once

#include "Includes/Common.slang"
#include "Includes/SceneGlobals.slang"

// SMAA pass 3 (neighborhood blend). No pass-local binding set: LDR color + blend weights are read
// through the bindless table via indices in the push constant (matches FSMAAPushConstants).
struct FPushConstants
{
    float4 RTMetrics;
    float  EdgeThreshold;
    float  DebugMode;
    uint   TexIndex0;   // LDR color
    uint   TexIndex1;   // blend weights
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
    SamplerState LinearClamp = uBindlessSamplers[SAMPLER_LINEAR_CLAMP];

    SMAATex colorTex; colorTex.Tex = uBindlessTex2D[PC.TexIndex0]; colorTex.Smp = LinearClamp;
    SMAATex blendTex; blendTex.Tex = uBindlessTex2D[PC.TexIndex1]; blendTex.Smp = LinearClamp;

    if (PC.DebugMode > 0.5)
    {
        float4 bw = blendTex.Sample(Input.UV);
        return float4(bw.rgb, 1.0);
    }

    float4 offset;
    SMAANeighborhoodBlendingOffsets(Input.UV, offset);

    return SMAANeighborhoodBlendingPS(Input.UV, offset, colorTex, blendTex);
}
