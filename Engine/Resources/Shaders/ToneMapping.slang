#pragma once
#include "Includes/SceneGlobals.slang"

struct FPushConstants
{
    float Exposure;
    float Time;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
}

float3 ACESFilm(float3 X)
{
    static const float A = 2.51;
    static const float B = 0.03;
    static const float C = 2.43;
    static const float D = 0.59;
    static const float E = 0.14;
    return clamp((X * (A * X + B)) / (X * (C * X + D) + E), 0.0, 1.0);
}

float3 Uncharted2Tonemap(float3 X)
{
    static const float A = 0.15;
    static const float B = 0.50;
    static const float C = 0.10;
    static const float D = 0.20;
    static const float E = 0.02;
    static const float F = 0.30;
    return ((X * (A * X + C * B) + D * E) / (X * (A * X + B) + D * F)) - E / F;
}

// ============================================================================
//  Main
// ============================================================================

[shader("fragment")]
float4 FragmentMain(VSOutput Input, uniform FPushConstants PC) : SV_Target0
{
    float3 HDRColor = uHDRSceneColor.Sample(Input.UV).rgb;

    HDRColor *= PC.Exposure;

    float3 Tonemapped = ACESFilm(HDRColor);

    // Gamma correction
    static const float INV_GAMMA = 1.0 / 2.2;
    Tonemapped = pow(Tonemapped, float3(INV_GAMMA));

    // Dither
    Tonemapped += ScreenSpaceDither(Input.Position.xy, PC.Time);

    return float4(Tonemapped, 1.0);
}