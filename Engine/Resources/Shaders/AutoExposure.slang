#pragma once
#include "Includes/Common.slang"
#include "Includes/SceneGlobals.slang"

// =============================================================================
//  Auto Exposure (eye adaptation)
// =============================================================================
//
//  One workgroup meters the HDR scene color and writes a single adapted
//  luminance into a persistent 1x1 R32F image. Each thread samples one cell
//  of a 16x16 grid over the frame, the group log-averages those samples
//  (geometric mean = standard key luminance), then thread 0 eases the stored
//  value toward the new target so exposure changes feel like an eye adapting
//  rather than snapping. ColorGrading.slang reads the result and turns it into
//  an exposure multiplier.
//
//  Resources are bindless (HDR SRV + adapted-luminance UAV via push-constant
//  indices), so there is no pass-local binding set -- mirrors BloomDownsampleSPD.
// =============================================================================

#define TILE_DIM     16
#define THREAD_COUNT (TILE_DIM * TILE_DIM)

struct FPushConstants
{
    uint  HDRIndex;         // bindless SRV index of HDR scene color
    uint  AdaptUAV;         // bindless UAV index of the 1x1 adapted-luminance image
    float DeltaTime;        // seconds since last frame, for adaptation rate
    float AdaptationSpeed;  // eye-adaptation rate (per second)
};

[[vk::push_constant]] FPushConstants PC;

groupshared float gLogLum[THREAD_COUNT];

[shader("compute")]
[numthreads(TILE_DIM, TILE_DIM, 1)]
void ComputeMain(uint3 LocalID : SV_GroupThreadID)
{
    uint Local = LocalID.y * TILE_DIM + LocalID.x;

    // Sample the cell center of a TILE_DIM x TILE_DIM grid over the whole frame.
    float2 UV = (float2(LocalID.xy) + 0.5) / float(TILE_DIM);
    float3 Color = uBindlessTex2D[PC.HDRIndex].SampleLevel(
        uBindlessSamplers[SAMPLER_LINEAR_CLAMP], UV, 0.0).rgb;

    float Luma = max(dot(Color, float3(0.2126, 0.7152, 0.0722)), 1e-5);
    gLogLum[Local] = log(Luma);

    GroupMemoryBarrierWithGroupSync();

    // Parallel sum reduction over the 256 log-luminance samples.
    [unroll]
    for (uint s = THREAD_COUNT / 2; s > 0; s >>= 1)
    {
        if (Local < s)
        {
            gLogLum[Local] += gLogLum[Local + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (Local == 0)
    {
        float TargetLum = exp(gLogLum[0] / float(THREAD_COUNT));

        float PrevLum = uBindlessRWTex2D[PC.AdaptUAV][int2(0, 0)].r;
        // Seed the feedback the first time we run (the 1x1 image is created
        // uncleared, so its initial contents are undefined). Reject anything
        // outside a plausible luminance range -- this also catches NaN/Inf,
        // since every comparison against NaN is false -- and snap straight to
        // the metered target instead of easing from garbage.
        if (!(PrevLum > 1e-6 && PrevLum < 1e6))
        {
            PrevLum = TargetLum;
        }

        // Exponential approach, framerate independent.
        float Blend     = 1.0 - exp(-PC.DeltaTime * PC.AdaptationSpeed);
        float AdaptedLum = PrevLum + (TargetLum - PrevLum) * Blend;

        uBindlessRWTex2D[PC.AdaptUAV][int2(0, 0)] = float4(AdaptedLum, 0.0, 0.0, 0.0);
    }
}
