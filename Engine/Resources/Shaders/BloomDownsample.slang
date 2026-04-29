#pragma once

// =============================================================================
//  Bloom Downsample
// =============================================================================
//
//  COD Advanced Warfare 13-tap downsample (Sledgehammer, Jorge Jimenez 2014).
//  Each output texel reads 13 bilinearly-filtered taps from the source mip
//  arranged in a 3x3 box of 2x2 sub-quads plus the 4 outer corners; the
//  partial sums are then weighted to produce a downsample with built-in
//  anti-flicker behavior (no single firefly survives the average).
//
//  Mip 0 (the prefilter pass that reads HDR) additionally:
//    - applies a soft-knee threshold so dim-but-non-bright pixels can fade
//      in instead of popping in,
//    - replaces the 13-tap weighted average with a Karis-luma-weighted
//      average (per-quad inverse-luma weighting); this is the canonical
//      fix for sub-pixel highlights blowing up the entire bloom chain.
//
//  Push constants:
//    SrcTexelSize    1 / source mip resolution.
//    Threshold       knee curve x.
//    KneeCurve       (Threshold - Knee, 2*Knee, 0.25/Knee). Precomputed on
//                    CPU so the shader is just a few muls + one max.
//    bIsPrefilter    1 only for mip 0; cheaper passes skip the threshold
//                    branch and the Karis weighting entirely.
// =============================================================================

[[vk::binding(0, 0)]] Sampler2D uSource;

struct FPushConstants
{
    float2 SrcTexelSize;
    float  Threshold;       // knee.x = max(threshold - knee, 0)
    uint   bIsPrefilter;

    float3 KneeCurve;       // x = threshold - knee, y = 2*knee, z = 0.25 / max(knee, 1e-4)
    float  _Pad0;
};

[[vk::push_constant]] FPushConstants PC;

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

// Soft-knee quadratic threshold from Jimenez's "Next Generation Post
// Processing in Call of Duty: Advanced Warfare". Builds a smooth ramp on
// [threshold - knee, threshold + knee] so we don't get popping pixels.
float3 QuadraticThreshold(float3 Color, float Threshold, float3 Curve)
{
    float Br   = max(max(Color.r, Color.g), Color.b);
    float Rq   = clamp(Br - Curve.x, 0.0, Curve.y);
    Rq         = (Rq * Rq) * Curve.z;
    float Mul  = max(Rq, Br - Threshold) / max(Br, 1e-4);
    return Color * Mul;
}

// Karis luma weight. Operates on per-tap groups so a single
// fireball-bright tap is divided down before it averages with its
// neighbors -- the standard fix for "stable" bloom.
float KarisAverage(float3 Color)
{
    float Luma = dot(Color, float3(0.2126, 0.7152, 0.0722)) * 0.25;
    return 1.0 / (1.0 + Luma);
}

[shader("fragment")]
float4 FragmentMain(VSOutput Input) : SV_Target0
{
    float2 UV = Input.UV;
    float2 T  = PC.SrcTexelSize;

    // 13 bilinear taps, arranged as five 2x2 quads (a..e) for the inner
    // contribution plus four 1x1 outer corners (f..i). Read straight off
    // the linear-clamp sampler so each tap is already a 4-pixel average.
    float3 a = uSource.SampleLevel(UV + T * float2(-2.0, -2.0), 0.0).rgb;
    float3 b = uSource.SampleLevel(UV + T * float2( 0.0, -2.0), 0.0).rgb;
    float3 c = uSource.SampleLevel(UV + T * float2( 2.0, -2.0), 0.0).rgb;

    float3 d = uSource.SampleLevel(UV + T * float2(-2.0,  0.0), 0.0).rgb;
    float3 e = uSource.SampleLevel(UV + T * float2( 0.0,  0.0), 0.0).rgb;
    float3 f = uSource.SampleLevel(UV + T * float2( 2.0,  0.0), 0.0).rgb;

    float3 g = uSource.SampleLevel(UV + T * float2(-2.0,  2.0), 0.0).rgb;
    float3 h = uSource.SampleLevel(UV + T * float2( 0.0,  2.0), 0.0).rgb;
    float3 i = uSource.SampleLevel(UV + T * float2( 2.0,  2.0), 0.0).rgb;

    float3 j = uSource.SampleLevel(UV + T * float2(-1.0, -1.0), 0.0).rgb;
    float3 k = uSource.SampleLevel(UV + T * float2( 1.0, -1.0), 0.0).rgb;
    float3 l = uSource.SampleLevel(UV + T * float2(-1.0,  1.0), 0.0).rgb;
    float3 m = uSource.SampleLevel(UV + T * float2( 1.0,  1.0), 0.0).rgb;

    float3 Result;
    if (PC.bIsPrefilter != 0u)
    {
        // Per-quad Karis weighting. Grouping the taps into the 5 inner
        // 2x2 quads first matches Jimenez's reference implementation.
        float3 Q0 = (j + k + l + m) * 0.25;
        float3 Q1 = (a + b + d + e) * 0.25;
        float3 Q2 = (b + c + e + f) * 0.25;
        float3 Q3 = (d + e + g + h) * 0.25;
        float3 Q4 = (e + f + h + i) * 0.25;

        float W0 = KarisAverage(Q0) * 0.5;
        float W1 = KarisAverage(Q1) * 0.125;
        float W2 = KarisAverage(Q2) * 0.125;
        float W3 = KarisAverage(Q3) * 0.125;
        float W4 = KarisAverage(Q4) * 0.125;

        float WSum = W0 + W1 + W2 + W3 + W4;
        Result = (Q0 * W0 + Q1 * W1 + Q2 * W2 + Q3 * W3 + Q4 * W4) / max(WSum, 1e-4);

        Result = QuadraticThreshold(max(Result, 0.0), PC.Threshold, PC.KneeCurve);
    }
    else
    {
        // Standard COD-AW weights.
        Result  = e * 0.125;
        Result += (a + c + g + i) * 0.03125;
        Result += (b + d + f + h) * 0.0625;
        Result += (j + k + l + m) * 0.125;
    }

    return float4(Result, 1.0);
}
