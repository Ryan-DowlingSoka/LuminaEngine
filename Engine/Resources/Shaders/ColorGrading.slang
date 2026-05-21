#pragma once
#include "Includes/Common.slang"

// =============================================================================
//  Color Grading + Tone Mapping
// =============================================================================
//
//  Single fused post-process pass. Reads the linear HDR scene color, runs
//  parametric color grading (white balance, CDL, contrast, saturation,
//  color filter), tone maps via the selected operator (AGX by default),
//  applies an optional tinted vignette in display space, then gamma-encodes
//  for the SRGBA8_UNORM swapchain.
//
//  Constants block lives in a UBO at (set 2, binding 2) -- mirrors
//  Lumina::FColorGradingConstants exactly (144 bytes, std140 packing).
//  Must be a buffer rather than push constants because AMD RDNA's
//  maxPushConstantsSize is 128 B.
// =============================================================================

[[vk::binding(0, 2)]] Sampler2D uHDRSceneColor;
[[vk::binding(1, 2)]] Sampler2D uBloom;

#define TONEMAPPER_NONE       0u
#define TONEMAPPER_ACES       1u
#define TONEMAPPER_AGX        2u
#define TONEMAPPER_AGX_PUNCHY 3u
#define TONEMAPPER_AGX_GOLDEN 4u

struct FColorGradingConstants
{
    // 16 B
    float Exposure;        // linear multiplier built from EV stops on CPU side
    float Contrast;        // 1.0 == identity, around mid-grey 0.18
    float Saturation;      // 1.0 == identity (Rec.709 luma)
    float Gamma;           // 1.0 == identity (display-space)

    // 16 B
    float WhiteTemp;       // -1..1, cool->warm
    float WhiteTint;       // -1..1, green->magenta
    float VignetteIntensity;
    float VignetteSmoothness;

    // 16 B
    float VignetteRoundness;
    uint  TonemapMode;
    float Time;
    float BloomIntensity;  // 0 == bloom disabled (uBloom not sampled).

    // 80 B
    float4 ColorFilter;    // .rgb tint, .a intensity (0..2)
    float4 Shadows;        // .rgb lift color, .a film grain intensity
    float4 Midtones;       // .rgb gamma color, .a film grain cell size (px)
    float4 Highlights;     // .rgb gain color, .a film grain shadow response
    float4 VignetteColor;  // .rgb tint, .a unused

    // 16 B
    float4 BloomTint;          // .rgb tint, .a chromatic aberration intensity

    // 16 B
    float AutoExposureKey;     // middle-grey key luminance; <= 0 disables auto-exposure
    float AutoExposureMinMul;  // clamp on the auto-exposure multiplier (2^MinEV)
    float AutoExposureMaxMul;  // clamp on the auto-exposure multiplier (2^MaxEV)
    float _PadAE;
};

[[vk::binding(2, 2)]] ConstantBuffer<FColorGradingConstants> PC;
// Persistent 1x1 adapted scene luminance written by AutoExposure.slang.
[[vk::binding(3, 2)]] Sampler2D uAdaptedLuminance;

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

// -----------------------------------------------------------------------------
//  White balance via LMS chromatic adaptation. Maps the user [-1, 1] knobs
//  onto a CIECAM02-style scale so neutrals stay neutral after the shift.
// -----------------------------------------------------------------------------
float3 WhiteBalance(float3 X, float Temperature, float Tint)
{
    // Range scaling -- matches the feel of the corresponding HDRP / Unreal
    // sliders: 0.05 is "subtle", 1.0 is "obvious but not destructive".
    float t1 = Temperature * 0.05;
    float t2 = Tint        * 0.05;

    // Reference white balance points, pre-baked in LMS.
    float x = 0.31271 - t1 * (t1 < 0 ? 0.1 : 0.05);
    float y = 0.32902 + (x - 0.31271) * (-3.0) + t2 * 0.05;

    float3 w1 = float3(0.949237, 1.03542, 1.08728);
    float Y  = 1.0;
    float X_ = Y * x / y;
    float Z  = Y * (1.0 - x - y) / y;

    float L =  0.7328 * X_ + 0.4296 * Y - 0.1624 * Z;
    float M = -0.7036 * X_ + 1.6975 * Y + 0.0061 * Z;
    float S =  0.0030 * X_ + 0.0136 * Y + 0.9834 * Z;
    float3 w2 = float3(L, M, S);

    float3 Balance = w1 / w2;

    static const float3x3 LIN_2_LMS = float3x3(
        3.90405e-1, 5.49941e-1, 8.92632e-3,
        7.08416e-2, 9.63172e-1, 1.35775e-3,
        2.31082e-2, 1.28021e-1, 9.36245e-1);

    static const float3x3 LMS_2_LIN = float3x3(
         2.85847,  -1.62879, -2.48910e-2,
        -2.10182e-1, 1.15820,  3.24281e-4,
        -4.18120e-2,-1.18169e-1, 1.06867);

    float3 lms = mul(LIN_2_LMS, X);
    lms       *= Balance;
    return mul(LMS_2_LIN, lms);
}

// -----------------------------------------------------------------------------
//  Contrast / saturation around perceptual midpoints. Contrast pivots on
//  18% grey (the standard photographer's reference). Saturation pivots on
//  Rec.709 luma so neutrals don't drift.
// -----------------------------------------------------------------------------
float3 ApplyContrast(float3 X, float Contrast)
{
    static const float3 MidGrey = float3(0.18);
    return (X - MidGrey) * Contrast + MidGrey;
}

float3 ApplySaturation(float3 X, float Saturation)
{
    float Luma = dot(X, float3(0.2126, 0.7152, 0.0722));
    return lerp(float3(Luma), X, Saturation);
}

// -----------------------------------------------------------------------------
//  Lift / Gamma / Gain (CDL-ish). Operates per channel:
//    Lift  -- additive offset weighted by (1 - X), so it pulls shadows.
//    Gamma -- power curve, weights midtones the most.
//    Gain  -- multiplier weighted by X, so it lifts highlights.
//
//  Each color knob is centered on 1.0 (identity). Subtracting 1 gives the
//  signed offset; we then weight by where on the curve the channel sits.
// -----------------------------------------------------------------------------
float3 LiftGammaGain(float3 X, float3 Shadows, float3 Midtones, float3 Highlights)
{
    float3 Lift = Shadows  - 1.0;     // signed
    float3 Gain = Highlights;         // identity at 1.0
    float3 Mid  = max(Midtones, 0.001);

    X = X * Gain + Lift * (1.0 - X);
    X = max(X, 0.0);
    X = pow(X, 1.0 / Mid);            // higher Midtone color -> brighter mids
    return X;
}

// -----------------------------------------------------------------------------
//  Tone mappers
// -----------------------------------------------------------------------------

float3 Tonemap_ACES(float3 X)
{
    static const float A = 2.51;
    static const float B = 0.03;
    static const float C = 2.43;
    static const float D = 0.59;
    static const float E = 0.14;
    return saturate((X * (A * X + B)) / (X * (C * X + D) + E));
}

// AGX -- bwrensch's polynomial fit of Troy Sobotka's AGX. Operates on
// linear scene-referred values, returns display-referred [0, 1].
//
// References:
//   https://github.com/sobotka/AgX
//   https://iolite-engine.com/blog_posts/minimal_agx_implementation
float3 AGX_Polynomial(float3 X)
{
    float3 X2 = X  * X;
    float3 X4 = X2 * X2;
    return + 15.5     * X4 * X2
           - 40.14    * X4 * X
           + 31.96    * X4
           - 6.868    * X2 * X
           + 0.4298   * X2
           + 0.1191   * X
           - 0.00232;
}

float3 Tonemap_AGX_Base(float3 X)
{
    static const float3x3 AGX_IN = float3x3(
        0.842479062253094, 0.0784335999999992, 0.0792237451477643,
        0.0423282422610123, 0.878468636469772, 0.0791661274605434,
        0.0423756549057051, 0.0784336000000000, 0.879142973793104);

    static const float MIN_EV = -12.47393;
    static const float MAX_EV =  4.026069;

    X = max(X, 0.0);
    X = mul(AGX_IN, X);
    X = clamp(log2(X), MIN_EV, MAX_EV);
    X = (X - MIN_EV) / (MAX_EV - MIN_EV);
    return AGX_Polynomial(X);
}

// "Look" stage applied in the AGX log space after the polynomial sigmoid
// but before EOTF. Slope, power and offset are per-channel CDL-style.
float3 AGX_Look(float3 X, float3 Slope, float3 Power, float Saturation)
{
    float Luma = dot(X, float3(0.2126, 0.7152, 0.0722));
    X = pow(X * Slope, Power);
    return Luma + Saturation * (X - Luma);
}

float3 Tonemap_AGX(float3 X)
{
    return Tonemap_AGX_Base(X);
}

float3 Tonemap_AGX_Punchy(float3 X)
{
    X = Tonemap_AGX_Base(X);
    return AGX_Look(X, float3(1.0), float3(1.35), 1.4);
}

float3 Tonemap_AGX_Golden(float3 X)
{
    X = Tonemap_AGX_Base(X);
    return AGX_Look(X, float3(1.0, 0.9, 0.5), float3(0.8), 0.8);
}

float3 ApplyTonemap(float3 X, uint Mode)
{
    switch (Mode)
    {
        default:
        case TONEMAPPER_NONE:       return saturate(X);
        case TONEMAPPER_ACES:       return Tonemap_ACES(X);
        case TONEMAPPER_AGX:        return Tonemap_AGX(X);
        case TONEMAPPER_AGX_PUNCHY: return Tonemap_AGX_Punchy(X);
        case TONEMAPPER_AGX_GOLDEN: return Tonemap_AGX_Golden(X);
    }
}

// -----------------------------------------------------------------------------
//  Vignette (display space). Roundness = 0 follows the viewport aspect,
//  Roundness = 1 is a circle. Smoothness controls the falloff width.
// -----------------------------------------------------------------------------
float3 ApplyVignette(float3 X, float2 UV, float Intensity, float Roundness, float Smoothness, float3 Tint)
{
    if (Intensity <= 0.0) return X;

    float2 Center = UV - 0.5;
    // Aspect-correct unless Roundness==1.
    Center.x *= lerp(2.0, 1.0, Roundness);
    float Dist = length(Center);

    // Inner radius shrinks with intensity so the falloff stays controllable.
    float Inner = lerp(0.7, 0.0, Intensity);
    float Outer = Inner + max(Smoothness, 0.001);
    float V     = 1.0 - smoothstep(Inner, Outer, Dist);

    return lerp(Tint, X, V);
}

// -----------------------------------------------------------------------------
//  Film grain. Animated per-pixel hash decorrelated by Time so the grain
//  shimmers between frames like real film silver halide. `Size` quantises
//  pixel coords into cells so the grain reads as a coarser stock at higher
//  values. `Response` biases the effect toward shadows (1 == grain only in
//  dark regions, 0 == uniform across luminance). Applied multiplicatively
//  in display space after tone mapping but before sRGB encode.
// -----------------------------------------------------------------------------
float FilmGrainHash(float2 P, float Time)
{
    P += Time * float2(47.0, 17.0);
    float3 P3 = frac(float3(P.xyx) * float3(0.1031, 0.1030, 0.0973));
    P3 += dot(P3, P3.yzx + 33.33);
    return frac((P3.x + P3.y) * P3.z);
}

float3 ApplyFilmGrain(float3 X, float2 PixelPos, float Time, float Intensity, float Size, float Response)
{
    if (Intensity <= 0.0) return X;

    float2 Cell  = floor(PixelPos / max(Size, 1.0));
    float  Noise = FilmGrainHash(Cell, Time) - 0.5;        // signed [-0.5, 0.5]

    // Brighter pixels get less grain when Response > 0 (matches the
    // shoulder behaviour of real film stocks).
    float Luma   = dot(X, float3(0.2126, 0.7152, 0.0722));
    float Falloff = lerp(1.0, 1.0 - smoothstep(0.0, 0.85, Luma), saturate(Response));

    return X * (1.0 + Noise * Intensity * Falloff);
}

// =============================================================================
//  Main
// =============================================================================

[shader("fragment")]
float4 FragmentMain(VSOutput Input) : SV_Target0
{
    // Radial chromatic aberration. Offset scales with distance from the
    // screen center so the middle of the image stays clean. Applied to the
    // HDR fetch so the lens artefact is correctly graded and tone-mapped
    // alongside everything else, not stamped on top in display space.
    float3 Color;
    float  CA = PC.BloomTint.a;
    if (CA > 0.0)
    {
        float2 Center = Input.UV - 0.5;
        float2 Offset = Center * CA;
        Color.r = uHDRSceneColor.Sample(Input.UV + Offset).r;
        Color.g = uHDRSceneColor.Sample(Input.UV).g;
        Color.b = uHDRSceneColor.Sample(Input.UV - Offset).b;
    }
    else
    {
        Color = uHDRSceneColor.Sample(Input.UV).rgb;
    }

    // ---- Bloom composite (linear HDR space, before grading) ----
    if (PC.BloomIntensity > 0.0)
    {
        float3 Bloom = uBloom.Sample(Input.UV).rgb;
        Color += Bloom * PC.BloomTint.rgb * PC.BloomIntensity;
    }

    // ---- Linear HDR grading ----
    // Auto-exposure: scale toward the middle-grey key based on the adapted
    // scene luminance, clamped to the user EV range. Key <= 0 means the
    // feature is off and the manual PC.Exposure stands alone.
    float AEScale = 1.0;
    if (PC.AutoExposureKey > 0.0)
    {
        float AdaptedLum = uAdaptedLuminance.SampleLevel(float2(0.5, 0.5), 0.0).r;
        AEScale = clamp(PC.AutoExposureKey / max(AdaptedLum, 1e-4),
                        PC.AutoExposureMinMul, PC.AutoExposureMaxMul);
    }
    Color *= PC.Exposure * AEScale;

    if (PC.WhiteTemp != 0.0 || PC.WhiteTint != 0.0)
    {
        Color = WhiteBalance(Color, PC.WhiteTemp, PC.WhiteTint);
    }

    Color = LiftGammaGain(Color, PC.Shadows.rgb, PC.Midtones.rgb, PC.Highlights.rgb);

    // Color filter, lerp by intensity so users can crank picker
    // saturation independently of strength.
    float3 Filtered = Color * PC.ColorFilter.rgb;
    Color = lerp(Color, Filtered, saturate(PC.ColorFilter.a));
    if (PC.ColorFilter.a > 1.0)
    {
        // Allow >1 to push past full filter for stylized passes.
        Color = lerp(Color, Filtered, PC.ColorFilter.a - 1.0);
    }

    Color = ApplyContrast(Color, PC.Contrast);
    Color = max(Color, 0.0);
    Color = ApplySaturation(Color, PC.Saturation);
    Color = max(Color, 0.0);

    // ---- Tone map ----
    Color = ApplyTonemap(Color, PC.TonemapMode);

    // ---- Display-space ----
    Color = ApplyVignette(Color, Input.UV, PC.VignetteIntensity, PC.VignetteRoundness, PC.VignetteSmoothness, PC.VignetteColor.rgb);

    // Display gamma (post-tonemap). Gamma == 1 is identity for the
    // operators above (which already output display-referred), so this
    // is purely a designer knob -- the 1/2.2 sRGB encode is applied
    // separately below to match the legacy ToneMapping.slang behaviour.
    if (PC.Gamma != 1.0)
    {
        Color = pow(max(Color, 0.0), float3(1.0 / PC.Gamma));
    }

    // ---- Film grain (display space, pre-OETF) ----
    // Knobs are packed into the .a slots of the LGG triplet to keep the
    // push-constant block at 144 B. See FColorGradingPushConstants.
    Color = ApplyFilmGrain(Color, Input.Position.xy, PC.Time,
                           PC.Shadows.a, PC.Midtones.a, PC.Highlights.a);
    Color = max(Color, 0.0);

    // sRGB encode (the swapchain target is _UNORM, not _SRGB, so we
    // bake the OETF here -- same as the legacy pass).
    static const float INV_GAMMA = 1.0 / 2.2;
    Color = pow(max(Color, 0.0), float3(INV_GAMMA));

    Color += ScreenSpaceDither(Input.Position.xy, PC.Time);

    return float4(Color, 1.0);
}
