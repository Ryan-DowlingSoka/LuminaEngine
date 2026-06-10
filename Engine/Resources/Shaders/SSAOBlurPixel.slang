#pragma once
#include "Includes/SceneGlobals.slang"

// Box blur of the raw SSAO factor. SSAO is rotated per-pixel by a 4x4 noise tile, which leaves a
// high-frequency 4x4 pattern in the output; a matching 4x4 box average removes it. Single channel.

struct FragmentInput
{
    float2 UV : TEXCOORD0;
};

struct FragmentOutput
{
    float4 Color : SV_Target0;
};

struct FData
{
    uint AOIndex;   // bindless SRV of the raw SSAO texture
};

[shader("fragment")]
FragmentOutput FragmentMain(FragmentInput Input)
{
    FragmentOutput Output;

    FData* Data = GetPassArgs<FData>();

    float2 TexelSize = 1.0 / float2(Scene().ScreenSize.xy);

    // Symmetric 4x4 neighbourhood (offsets -2..1) matching the noise tile size.
    float Result = 0.0;
    for (int x = -2; x < 2; ++x)
    {
        for (int y = -2; y < 2; ++y)
        {
            float2 Offset = float2(x, y) * TexelSize;
            Result += SampleTexture2D(Data.AOIndex, SAMPLER_POINT_CLAMP, Input.UV + Offset).r;
        }
    }

    Result /= 16.0;
    Output.Color = float4(Result, Result, Result, 1.0);
    return Output;
}
