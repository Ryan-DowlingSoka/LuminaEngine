#pragma once

// FXAA 3.11 operating on tonemapped LDR color.
// Uses SampleLevel(UV, 0) to avoid implicit-derivative issues in fullscreen passes.

[[vk::binding(0, 2)]] Sampler2D uLDRSceneColor;

struct FPushConstants
{
    float2 InverseScreenSize;
    float  SubpixelBlending;
    float  EdgeThreshold;
    float  EdgeThresholdMin;
    float  BlurStrength;        // Extra edge-aware blur applied on top of FXAA
    float  DebugMode;           // 0 = off, 1 = magenta edges, 2 = show FXAA delta, 3 = force strong blur
    float  _Pad2;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

static float3 SampleRGB(float2 UV)
{
    return uLDRSceneColor.SampleLevel(UV, 0.0).rgb;
}

static float RGBToLuma(float3 Color)
{
    return sqrt(dot(Color, float3(0.299, 0.587, 0.114)));
}

static const int FXAA_SEARCH_STEPS = 16;

[shader("fragment")]
float4 FragmentMain(VSOutput Input, uniform FPushConstants PC) : SV_Target0
{
    float2 UV     = Input.UV;
    float2 InvRes = PC.InverseScreenSize;

    float3 ColorCenter = SampleRGB(UV);
    float  LumaCenter  = RGBToLuma(ColorCenter);

    float LumaDown  = RGBToLuma(SampleRGB(UV + float2( 0.0,  InvRes.y)));
    float LumaUp    = RGBToLuma(SampleRGB(UV + float2( 0.0, -InvRes.y)));
    float LumaLeft  = RGBToLuma(SampleRGB(UV + float2(-InvRes.x, 0.0)));
    float LumaRight = RGBToLuma(SampleRGB(UV + float2( InvRes.x, 0.0)));

    float LumaMin   = min(LumaCenter, min(min(LumaDown, LumaUp), min(LumaLeft, LumaRight)));
    float LumaMax   = max(LumaCenter, max(max(LumaDown, LumaUp), max(LumaLeft, LumaRight)));
    float LumaRange = LumaMax - LumaMin;

    bool bEdge = LumaRange >= max(PC.EdgeThresholdMin, LumaMax * PC.EdgeThreshold);

    // Debug mode 1: paint detected edges magenta. If nothing turns magenta, the
    // pass isn't running (or the input is flat/uninitialized).
    if (PC.DebugMode > 0.5 && PC.DebugMode < 1.5)
    {
        return bEdge ? float4(1.0, 0.0, 1.0, 1.0) : float4(ColorCenter, 1.0);
    }

    // Debug mode 3: force a strong 3x3 box blur everywhere (ignoring edge test).
    if (PC.DebugMode > 2.5 && PC.DebugMode < 3.5)
    {
        float3 Sum = ColorCenter;
        Sum += SampleRGB(UV + float2( 0.0,  InvRes.y));
        Sum += SampleRGB(UV + float2( 0.0, -InvRes.y));
        Sum += SampleRGB(UV + float2(-InvRes.x, 0.0));
        Sum += SampleRGB(UV + float2( InvRes.x, 0.0));
        Sum += SampleRGB(UV + float2(-InvRes.x,  InvRes.y));
        Sum += SampleRGB(UV + float2( InvRes.x,  InvRes.y));
        Sum += SampleRGB(UV + float2(-InvRes.x, -InvRes.y));
        Sum += SampleRGB(UV + float2( InvRes.x, -InvRes.y));
        return float4(Sum / 9.0, 1.0);
    }

    if (!bEdge)
    {
        return float4(ColorCenter, 1.0);
    }

    float LumaDownLeft  = RGBToLuma(SampleRGB(UV + float2(-InvRes.x,  InvRes.y)));
    float LumaUpRight   = RGBToLuma(SampleRGB(UV + float2( InvRes.x, -InvRes.y)));
    float LumaUpLeft    = RGBToLuma(SampleRGB(UV + float2(-InvRes.x, -InvRes.y)));
    float LumaDownRight = RGBToLuma(SampleRGB(UV + float2( InvRes.x,  InvRes.y)));

    float LumaDownUp       = LumaDown + LumaUp;
    float LumaLeftRight    = LumaLeft + LumaRight;
    float LumaLeftCorners  = LumaDownLeft + LumaUpLeft;
    float LumaDownCorners  = LumaDownLeft + LumaDownRight;
    float LumaRightCorners = LumaDownRight + LumaUpRight;
    float LumaUpCorners    = LumaUpRight + LumaUpLeft;

    float EdgeHorizontal =
        abs(-2.0 * LumaLeft   + LumaLeftCorners)  +
        abs(-2.0 * LumaCenter + LumaDownUp) * 2.0 +
        abs(-2.0 * LumaRight  + LumaRightCorners);

    float EdgeVertical =
        abs(-2.0 * LumaUp     + LumaUpCorners)       +
        abs(-2.0 * LumaCenter + LumaLeftRight) * 2.0 +
        abs(-2.0 * LumaDown   + LumaDownCorners);

    bool bIsHorizontal = EdgeHorizontal >= EdgeVertical;

    float Luma1 = bIsHorizontal ? LumaDown : LumaLeft;
    float Luma2 = bIsHorizontal ? LumaUp   : LumaRight;

    float Gradient1 = Luma1 - LumaCenter;
    float Gradient2 = Luma2 - LumaCenter;
    bool  bIs1Steepest   = abs(Gradient1) >= abs(Gradient2);
    float GradientScaled = 0.25 * max(abs(Gradient1), abs(Gradient2));

    float StepLength = bIsHorizontal ? InvRes.y : InvRes.x;
    float LumaLocalAverage;

    if (bIs1Steepest)
    {
        StepLength       = -StepLength;
        LumaLocalAverage = 0.5 * (Luma1 + LumaCenter);
    }
    else
    {
        LumaLocalAverage = 0.5 * (Luma2 + LumaCenter);
    }

    float2 CurrentUV = UV;
    if (bIsHorizontal) CurrentUV.y += StepLength * 0.5;
    else               CurrentUV.x += StepLength * 0.5;

    float2 Offset = bIsHorizontal ? float2(InvRes.x, 0.0) : float2(0.0, InvRes.y);

    float2 UV1 = CurrentUV - Offset;
    float2 UV2 = CurrentUV + Offset;

    float LumaEnd1 = RGBToLuma(SampleRGB(UV1)) - LumaLocalAverage;
    float LumaEnd2 = RGBToLuma(SampleRGB(UV2)) - LumaLocalAverage;

    bool bReached1    = abs(LumaEnd1) >= GradientScaled;
    bool bReached2    = abs(LumaEnd2) >= GradientScaled;
    bool bReachedBoth = bReached1 && bReached2;

    if (!bReached1) UV1 -= Offset;
    if (!bReached2) UV2 += Offset;

    for (int Step = 0; Step < FXAA_SEARCH_STEPS; ++Step)
    {
        if (bReachedBoth) break;

        if (!bReached1)
        {
            LumaEnd1  = RGBToLuma(SampleRGB(UV1)) - LumaLocalAverage;
            bReached1 = abs(LumaEnd1) >= GradientScaled;
        }
        if (!bReached2)
        {
            LumaEnd2  = RGBToLuma(SampleRGB(UV2)) - LumaLocalAverage;
            bReached2 = abs(LumaEnd2) >= GradientScaled;
        }

        float StepScale = (Step < 5) ? 1.5 : ((Step < 11) ? 2.0 : 4.0);

        if (!bReached1) UV1 -= Offset * StepScale;
        if (!bReached2) UV2 += Offset * StepScale;

        bReachedBoth = bReached1 && bReached2;
    }

    float Distance1 = bIsHorizontal ? (UV.x  - UV1.x) : (UV.y  - UV1.y);
    float Distance2 = bIsHorizontal ? (UV2.x - UV.x)  : (UV2.y - UV.y);

    bool  bIsDirection1 = Distance1 < Distance2;
    float DistanceFinal = min(Distance1, Distance2);
    float EdgeThickness = Distance1 + Distance2;
    float PixelOffset   = -DistanceFinal / EdgeThickness + 0.5;

    bool  bIsLumaCenterSmaller = LumaCenter < LumaLocalAverage;
    bool  bCorrectVariation    = ((bIsDirection1 ? LumaEnd1 : LumaEnd2) < 0.0) != bIsLumaCenterSmaller;
    float FinalOffset          = bCorrectVariation ? PixelOffset : 0.0;

    float LumaAverage         = (1.0 / 12.0) * (2.0 * (LumaDownUp + LumaLeftRight) + LumaLeftCorners + LumaRightCorners);
    float SubPixelOffset1     = clamp(abs(LumaAverage - LumaCenter) / LumaRange, 0.0, 1.0);
    float SubPixelOffset2     = (-2.0 * SubPixelOffset1 + 3.0) * SubPixelOffset1 * SubPixelOffset1;
    float SubPixelOffsetFinal = SubPixelOffset2 * SubPixelOffset2 * PC.SubpixelBlending;

    FinalOffset = max(FinalOffset, SubPixelOffsetFinal);

    float2 FinalUV = UV;
    if (bIsHorizontal) FinalUV.y += FinalOffset * StepLength;
    else               FinalUV.x += FinalOffset * StepLength;

    float3 FinalColor = SampleRGB(FinalUV);

    // Extra edge-aware blur. FXAA alone only blurs across a ~1 texel range which
    // can't fully resolve thick close-up aliased edges. On detected edges, take
    // two wider taps perpendicular to the edge and blend them in proportional to
    // BlurStrength. This widens the effective smoothing kernel.
    if (PC.BlurStrength > 0.001)
    {
        float2 WideOffset = bIsHorizontal ? float2(0.0, StepLength) : float2(StepLength, 0.0);
        float3 Wide1 = SampleRGB(UV + WideOffset * (1.0 + PC.BlurStrength));
        float3 Wide2 = SampleRGB(UV - WideOffset * (0.5 + PC.BlurStrength * 0.5));
        float3 Blurred = (FinalColor + Wide1 + Wide2) * (1.0 / 3.0);

        float BlendFactor = saturate(PC.BlurStrength);
        FinalColor = lerp(FinalColor, Blurred, BlendFactor);
    }

    // Debug mode 2: show the delta between center and FXAA output, amplified.
    if (PC.DebugMode > 1.5 && PC.DebugMode < 2.5)
    {
        float3 Delta = abs(FinalColor - ColorCenter) * 10.0;
        return float4(Delta, 1.0);
    }

    return float4(FinalColor, 1.0);
}
