#pragma once

// =============================================================================
//  Volumetric Lighting -- Depth-Aware Bilateral Upsample
// =============================================================================
//
//  Reads the half-res scattering buffer produced by VolumetricLighting.slang
//  and additively blends it into HDR at full resolution. The four nearest
//  half-res taps are weighted by how closely their full-res depths match the
//  destination pixel's depth, so silhouettes stay sharp instead of bleeding
//  fog across edges.
//
//  Joint bilateral upsample (Kopf 2007 / Sloan 2008) with full-res depth as
//  the guidance image. The half-res march itself sampled depth at exactly the
//  tap positions used here, so depth-at-tap and the value-at-tap line up.
//
//  Push constants:
//    HalfTexelSize   1 / half-res resolution (UV step between adjacent taps).
//    DepthSigma      Inverse of the view-Z falloff -- precomputed on the CPU
//                    so the shader does one multiply instead of a divide.
// =============================================================================

[[vk::binding(0, 0)]] Sampler2D uHalfRes;     // R11G11B10F volumetric scatter.
[[vk::binding(1, 0)]] Sampler2D uSceneDepth;  // Full-res scene depth (point-clamp sampler).

struct FPushConstants
{
    float2 HalfTexelSize;
    float  DepthSigma;
    float  _Pad0;
};

[[vk::push_constant]] FPushConstants PC;

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

[shader("fragment")]
float4 FragmentMain(VSOutput Input) : SV_Target0
{
    float2 UV = Input.UV;

    // Reverse-Z: (1 - z) gives a monotonic distance proxy so the bilateral
    // weight reacts to scene-space discontinuities rather than the non-linear
    // depth slope. We only need the *ratio* between differences, not absolute
    // view-Z, so this is enough.
    float DstDepth = 1.0 - uSceneDepth.SampleLevel(UV, 0.0).r;

    // Sample positions snap to the centers of the four half-res taps a
    // bilinear filter would pick. HalfTexelSize is in UV-space at half-res.
    float2 P00 = UV + PC.HalfTexelSize * float2(-0.5, -0.5);
    float2 P10 = UV + PC.HalfTexelSize * float2( 0.5, -0.5);
    float2 P01 = UV + PC.HalfTexelSize * float2(-0.5,  0.5);
    float2 P11 = UV + PC.HalfTexelSize * float2( 0.5,  0.5);

    float3 C00 = uHalfRes.SampleLevel(P00, 0.0).rgb;
    float3 C10 = uHalfRes.SampleLevel(P10, 0.0).rgb;
    float3 C01 = uHalfRes.SampleLevel(P01, 0.0).rgb;
    float3 C11 = uHalfRes.SampleLevel(P11, 0.0).rgb;

    // Depth at the same tap positions. Point-clamp sampler -> the tap with
    // the smaller area in screen space resolves to a single full-res texel,
    // which is what we want as the "half-res depth".
    float D00 = 1.0 - uSceneDepth.SampleLevel(P00, 0.0).r;
    float D10 = 1.0 - uSceneDepth.SampleLevel(P10, 0.0).r;
    float D01 = 1.0 - uSceneDepth.SampleLevel(P01, 0.0).r;
    float D11 = 1.0 - uSceneDepth.SampleLevel(P11, 0.0).r;

    float2 F = frac(UV * (1.0 / PC.HalfTexelSize) - 0.5);
    float W00 = (1.0 - F.x) * (1.0 - F.y);
    float W10 = (      F.x) * (1.0 - F.y);
    float W01 = (1.0 - F.x) * (      F.y);
    float W11 = (      F.x) * (      F.y);

    float E00 = exp(-PC.DepthSigma * (D00 - DstDepth) * (D00 - DstDepth));
    float E10 = exp(-PC.DepthSigma * (D10 - DstDepth) * (D10 - DstDepth));
    float E01 = exp(-PC.DepthSigma * (D01 - DstDepth) * (D01 - DstDepth));
    float E11 = exp(-PC.DepthSigma * (D11 - DstDepth) * (D11 - DstDepth));

    W00 *= E00;
    W10 *= E10;
    W01 *= E01;
    W11 *= E11;

    float WSum = W00 + W10 + W01 + W11;

    // Fallback: if every neighbor disagrees wildly with dst depth, collapse
    // to the closest single tap so we never divide by ~0.
    float3 Result;
    if (WSum < 1e-4)
    {
        float MaxW = max(max(W00, W10), max(W01, W11));
        if (MaxW == W00)      Result = C00;
        else if (MaxW == W10) Result = C10;
        else if (MaxW == W01) Result = C01;
        else                  Result = C11;
    }
    else
    {
        Result = (C00 * W00 + C10 * W10 + C01 * W01 + C11 * W11) / WSum;
    }

    return float4(Result, 1.0);
}
