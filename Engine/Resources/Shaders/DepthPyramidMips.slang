#pragma once
#include "Includes/Common.slang"

struct FPushConstants
{
    float2 DstSize; // Destination mip dimensions in texels.
};

// ============================================================================
//  Bindings
// ============================================================================

[[vk::binding(0)]] Sampler2D          SrcMip;
[[vk::binding(1)]] RWTexture2D<float> DstMip;

// ============================================================================
//  Main
// ============================================================================

[shader("compute")]
[numthreads(32, 32, 1)]
void ComputeMain(uint3 GlobalID : SV_DispatchThreadID, uniform FPushConstants PC)
{
    uint2 Pos = GlobalID.xy;
    if (any(Pos >= uint2(PC.DstSize)))
        return;

    // Sample the center of the destination texel in [0,1] UV space. For a
    // mip > 0 pass the bound sampler is a MIN-reduction sampler, so the 2x2
    // bilinear tap collapses four source texels into their minimum (= the
    // farthest depth in reverse-Z). For mip 0 the source is the same size
    // as the destination and this falls back to a point fetch.
    float2 UV    = (float2(Pos) + 0.5) / PC.DstSize;
    float  Depth = SrcMip.SampleLevel(UV, 0.0).x;

    DstMip[int2(Pos)] = Depth;
}
