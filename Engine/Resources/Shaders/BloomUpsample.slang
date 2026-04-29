#pragma once

// =============================================================================
//  Bloom Upsample
// =============================================================================
//
//  3x3 tent filter (Jimenez 2014) reading from the smaller mip and additively
//  blended into the larger one. Tent weights are 1/16 ... 4/16 in the standard
//  binomial pattern. Combined with the COD-AW downsample, the chain produces
//  the smooth, halo-free bloom every modern renderer ships with.
//
//  The host pass enables additive blending (One, One); the shader simply
//  outputs the filtered scattered contribution and the framebuffer ROP adds
//  it to the destination mip already populated by the downsample chain.
//
//  Push constants:
//    SrcTexelSize   1 / source mip resolution.
//    Radius         scales the sample offset; 1.0 == reference Jimenez tent.
//                   Larger == softer, wider halo (and more cross-mip bleed).
// =============================================================================

[[vk::binding(0, 0)]] Sampler2D uSource;

struct FPushConstants
{
    float2 SrcTexelSize;
    float  Radius;
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
    float2 T  = PC.SrcTexelSize * PC.Radius;

    // 3x3 tent. Bilinear samples make this 9 taps without manual fetching
    // of corner pixels.
    float3 a = uSource.SampleLevel(UV + T * float2(-1.0, -1.0), 0.0).rgb;
    float3 b = uSource.SampleLevel(UV + T * float2( 0.0, -1.0), 0.0).rgb;
    float3 c = uSource.SampleLevel(UV + T * float2( 1.0, -1.0), 0.0).rgb;

    float3 d = uSource.SampleLevel(UV + T * float2(-1.0,  0.0), 0.0).rgb;
    float3 e = uSource.SampleLevel(UV + T * float2( 0.0,  0.0), 0.0).rgb;
    float3 f = uSource.SampleLevel(UV + T * float2( 1.0,  0.0), 0.0).rgb;

    float3 g = uSource.SampleLevel(UV + T * float2(-1.0,  1.0), 0.0).rgb;
    float3 h = uSource.SampleLevel(UV + T * float2( 0.0,  1.0), 0.0).rgb;
    float3 i = uSource.SampleLevel(UV + T * float2( 1.0,  1.0), 0.0).rgb;

    // Binomial 1 2 1 / 2 4 2 / 1 2 1, normalized to sum 16.
    float3 Result =  e * 4.0;
    Result       += (b + d + f + h) * 2.0;
    Result       += (a + c + g + i);
    Result       *= (1.0 / 16.0);

    return float4(Result, 1.0);
}
