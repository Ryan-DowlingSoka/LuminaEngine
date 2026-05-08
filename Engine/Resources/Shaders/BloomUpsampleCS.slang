#pragma once
#include "Includes/Common.slang"

// =============================================================================
//  Bloom Upsample (Compute)
// =============================================================================
//
//  3x3 tent filter (Jimenez 2014) read from the smaller mip, additively
//  accumulated into the larger mip in-shader. One dispatch per upsample step;
//  4 dispatches total for a 5-mip chain. No render pass setup, no fullscreen
//  quad, cached binding sets in the pipeline cache -- effectively zero CPU
//  overhead per step.
//
//  The destination mip already holds the downsample chain's contribution from
//  the SPD pass; this shader reads that, adds the tent-filtered contribution
//  from the smaller mip, and writes the sum back. The previous fragment-shader
//  version got the "additive" behavior from a hardware blend; in compute we
//  do it explicitly.
//
//  Push constants:
//    SrcTexelSize    1 / source mip resolution (tap offsets multiply by this).
//    Radius          tent footprint scale; 1.0 == reference Jimenez.
//    DstSize         destination mip resolution (for bounds).
// =============================================================================

#define TILE_WIDTH  8
#define TILE_HEIGHT 8

[[vk::binding(0)]] Sampler2D            uSrc;     // smaller mip, sampled bilinearly
[[vk::binding(1)]] RWTexture2D<float3>  uDst;     // larger mip, read+modify+write

struct FPushConstants
{
    float2 SrcTexelSize;
    float  Radius;
    float  _Pad0;

    uint2  DstSize;
    uint2  _Pad1;
};

[shader("compute")]
[numthreads(TILE_WIDTH, TILE_HEIGHT, 1)]
void ComputeMain(
    uint3 DispatchID : SV_DispatchThreadID,
    uniform FPushConstants PC)
{
    const int2 DstP = int2(DispatchID.xy);
    if (any(uint2(DstP) >= PC.DstSize))
    {
        return;
    }

    // Destination mip UV: each dest pixel center maps to the same UV on the
    // smaller source thanks to the 1:2 ratio.
    const float2 UV = (float2(DstP) + 0.5) / float2(PC.DstSize);
    const float2 T  = PC.SrcTexelSize * PC.Radius;

    // 3x3 tent. Bilinear samples make this 9 taps without manual corner
    // fetches; the source filter mode is linear-clamp.
    float3 a = uSrc.SampleLevel(UV + T * float2(-1.0, -1.0), 0.0).rgb;
    float3 b = uSrc.SampleLevel(UV + T * float2( 0.0, -1.0), 0.0).rgb;
    float3 c = uSrc.SampleLevel(UV + T * float2( 1.0, -1.0), 0.0).rgb;

    float3 d = uSrc.SampleLevel(UV + T * float2(-1.0,  0.0), 0.0).rgb;
    float3 e = uSrc.SampleLevel(UV + T * float2( 0.0,  0.0), 0.0).rgb;
    float3 f = uSrc.SampleLevel(UV + T * float2( 1.0,  0.0), 0.0).rgb;

    float3 g = uSrc.SampleLevel(UV + T * float2(-1.0,  1.0), 0.0).rgb;
    float3 h = uSrc.SampleLevel(UV + T * float2( 0.0,  1.0), 0.0).rgb;
    float3 i = uSrc.SampleLevel(UV + T * float2( 1.0,  1.0), 0.0).rgb;

    float3 Tent =  e * 4.0;
    Tent       += (b + d + f + h) * 2.0;
    Tent       += (a + c + g + i);
    Tent       *= (1.0 / 16.0);

    // Additive: dest already has the downsample contribution from SPD plus any
    // accumulated upsample from a deeper-mip step.
    uDst[DstP] = uDst[DstP] + Tent;
}
