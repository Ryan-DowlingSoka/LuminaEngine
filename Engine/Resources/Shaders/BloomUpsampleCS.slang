#pragma once
#include "Includes/Common.slang"
#include "Includes/SceneGlobals.slang"

// =============================================================================
//  Bloom Upsample (Compute)
// =============================================================================
//
//  3x3 tent filter (Jimenez 2014) read from the smaller mip, additively
//  accumulated into the larger mip in-shader. One dispatch per upsample step;
//  4 dispatches total for a 5-mip chain. Resources are bindless: source via
//  uBindlessTex2D[SrcIndex] sampled at SrcMip, destination via
//  uBindlessRWTex2D[DstUAV] which is read+modify+write (relies on
//  shaderStorageImageReadWithoutFormat).
// =============================================================================

#define TILE_WIDTH  8
#define TILE_HEIGHT 8

struct FPushConstants
{
    float2 SrcTexelSize;
    float  Radius;
    uint   SrcIndex;        // bindless SRV: source image (all mips)

    uint2  DstSize;
    uint   DstUAV;          // bindless RW: destination mip
    float  SrcMip;          // mip level to sample on the source SRV
};

[[vk::push_constant]] FPushConstants PC;

[shader("compute")]
[numthreads(TILE_WIDTH, TILE_HEIGHT, 1)]
void ComputeMain(uint3 DispatchID : SV_DispatchThreadID)
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
    SamplerState Smp = uBindlessSamplers[SAMPLER_LINEAR_CLAMP];
    Texture2D    Src = uBindlessTex2D[PC.SrcIndex];

    float3 a = Src.SampleLevel(Smp, UV + T * float2(-1.0, -1.0), PC.SrcMip).rgb;
    float3 b = Src.SampleLevel(Smp, UV + T * float2( 0.0, -1.0), PC.SrcMip).rgb;
    float3 c = Src.SampleLevel(Smp, UV + T * float2( 1.0, -1.0), PC.SrcMip).rgb;

    float3 d = Src.SampleLevel(Smp, UV + T * float2(-1.0,  0.0), PC.SrcMip).rgb;
    float3 e = Src.SampleLevel(Smp, UV + T * float2( 0.0,  0.0), PC.SrcMip).rgb;
    float3 f = Src.SampleLevel(Smp, UV + T * float2( 1.0,  0.0), PC.SrcMip).rgb;

    float3 g = Src.SampleLevel(Smp, UV + T * float2(-1.0,  1.0), PC.SrcMip).rgb;
    float3 h = Src.SampleLevel(Smp, UV + T * float2( 0.0,  1.0), PC.SrcMip).rgb;
    float3 i = Src.SampleLevel(Smp, UV + T * float2( 1.0,  1.0), PC.SrcMip).rgb;

    float3 Tent =  e * 4.0;
    Tent       += (b + d + f + h) * 2.0;
    Tent       += (a + c + g + i);
    Tent       *= (1.0 / 16.0);

    // Additive: dest already has the downsample contribution from SPD plus any
    // accumulated upsample from a deeper-mip step.
    float3 Prev = uBindlessRWTex2D[PC.DstUAV][DstP].rgb;
    uBindlessRWTex2D[PC.DstUAV][DstP] = float4(Prev + Tent, 0);
}
