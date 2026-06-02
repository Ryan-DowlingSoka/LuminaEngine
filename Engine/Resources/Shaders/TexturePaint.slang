#pragma once
#include "Includes/SceneGlobals.slang"

// Stamps a soft radial brush into a bindless UAV; the CPU dispatches only over the brush's pixel
// rect [RectMin, RectMax). An optional grayscale brush mask (white = full coverage) modulates falloff.

struct FPushConstants
{
    uint    TargetIndex;    // bindless RWTexture2D<float4> slot to write
    int     BrushIndex;     // bindless Texture2D slot of a grayscale brush mask; < 0 = solid radial
    uint2   TargetSize;     // full image size in pixels
    uint2   RectMin;        // dispatch covers pixels [RectMin, RectMax)
    uint2   RectMax;
    float2  CenterPx;       // brush center, in pixels
    float   RadiusPx;       // brush radius, in pixels
    float   Strength;       // 0..1 opacity at the brush center
    float   Hardness;       // edge falloff exponent; > 1 sharpens the edge
    // RGBA as scalars so the CPU struct's offsets match under any block layout.
    float   ColorR;
    float   ColorG;
    float   ColorB;
    float   ColorA;
};

#define PC (*(FPushConstants*)gPC.PassAddr)

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 DTid : SV_DispatchThreadID)
{
    uint2 Px = PC.RectMin + DTid.xy;
    if (Px.x >= PC.RectMax.x || Px.y >= PC.RectMax.y)   return;
    if (Px.x >= PC.TargetSize.x || Px.y >= PC.TargetSize.y) return;

    float2 Center = float2(Px) + 0.5f;
    float  Dist   = length(Center - PC.CenterPx);
    if (Dist > PC.RadiusPx)
    {
        return;
    }

    float Falloff = saturate(1.0f - Dist / max(PC.RadiusPx, 1e-4f));
    float Mask    = pow(Falloff, max(PC.Hardness, 1e-3f));

    if (PC.BrushIndex >= 0)
    {
        // Map the brush rect onto [0,1]^2 and sample the mask's red channel as coverage.
        float2 BrushUV = (Center - (PC.CenterPx - PC.RadiusPx)) / (2.0f * PC.RadiusPx);
        Mask *= uBindlessTex2D[PC.BrushIndex].SampleLevel(uBindlessSamplers[SAMPLER_LINEAR_CLAMP], BrushUV, 0.0f).r;
    }

    float  Alpha = saturate(Mask * PC.Strength);
    float4 Color = float4(PC.ColorR, PC.ColorG, PC.ColorB, PC.ColorA);
    float4 Dst   = uBindlessRWTex2D[PC.TargetIndex][Px];
    uBindlessRWTex2D[PC.TargetIndex][Px] = lerp(Dst, Color, Alpha);
}
