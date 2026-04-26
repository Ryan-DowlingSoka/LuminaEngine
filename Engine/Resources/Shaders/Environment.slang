#pragma once
#include "Includes/SceneGlobals.slang"

// Sky background. The C++ EnvironmentPass writes one FEnvironmentParams per
// frame; this shader switches on Misc.x to pick a path:
//
//   SolidColor   - flat fill, no sun rendering. Fastest.
//   Gradient     - vertical zenith/horizon/ground gradient + procedural sun.
//   Dynamic      - Rayleigh + Mie atmospheric scattering with a time-of-day
//                  sun color curve. Most expensive.
//
// The sun direction comes from the directional light (uLightData.SunDirection),
// not from the env params, so day/night cycles still drive the sun position
// regardless of which mode is active.

// Must match the GSkyMode_* constants in EnvironmentRenderTypes.h.
#define SKY_MODE_SOLID    0u
#define SKY_MODE_GRADIENT 1u
#define SKY_MODE_DYNAMIC  2u

struct FEnvironmentParams
{
    float4 SolidSkyColor;     // rgb=color, w unused
    float4 ZenithColor;       // rgb=color, w=horizonExponent
    float4 HorizonColor;      // rgb=color, w unused
    float4 GroundColor;       // rgb=color, w unused
    float4 SunTint;           // rgb=tint, w=sunIntensity
    float4 Misc;              // x=skyMode (asfloat(uint)), y=sunDiscScale,
                              // z=skyExposure, w=mieAnisotropy
};

[[vk::binding(0, 2)]] ConstantBuffer<FEnvironmentParams> uEnvironment;

// Sun's true angular diameter, ~half a degree. The component-side
// SunDiscScale stretches the rendered disc and glow without affecting
// lighting math.
static const float kSunBaseAngle = radians(0.53);

// Atmospheric scattering scale. Tuned with Earth-like Rayleigh + Mie
// constants; stays fixed because tweaking these changes the sky's hue
// fundamentally rather than just brightening it.
static const float3 kRayleighCoeff   = float3(5.8e-6, 13.5e-6, 33.1e-6);
static const float  kMieCoeff        = 2.1e-5;
static const float  kAtmosphereR     = 6420e2;
static const float  kPlanetR         = 6360e2;
static const float  kRayleighHeight  = 8e2;
static const float  kMieHeight       = 1.2e2;

struct FragmentInput  { float2 UV : TEXCOORD0; };
struct FragmentOutput { float4 Color : SV_Target0; };

// ============================================================================
//  Helpers
// ============================================================================

float3 GetViewRay(float2 UV)
{
    float2 NDC      = UV * 2.0 - 1.0;
    float4 RayClip  = float4(NDC, 1.0, 1.0);
    float4 RayView  = mul(GetInverseCameraProjection(), RayClip);
    RayView.xyz    /= RayView.w;
    float3 RayWorld = mul(GetInverseCameraView(), float4(RayView.xyz, 0.0)).xyz;
    return normalize(RayWorld);
}

float RayleighPhase(float CosTheta)
{
    static const float Factor = 3.0 / (16.0 * 3.14159);
    return Factor * (1.0 + CosTheta * CosTheta);
}

float MiePhase(float CosTheta, float G)
{
    static const float Factor = 1.0 / (4.0 * 3.14159);
    float G2    = G * G;
    float Denom = 1.0 + G2 - 2.0 * G * CosTheta;
    return Factor * (1.0 - G2) / (Denom * sqrt(Denom));
}

float2 RaySphereIntersect(float3 RayOrigin, float3 RayDir, float Radius)
{
    float B            = dot(RayOrigin, RayDir);
    float C            = dot(RayOrigin, RayOrigin) - Radius * Radius;
    float Discriminant = B * B - C;

    if (Discriminant < 0.0)
        return float2(-1.0, -1.0);

    float SqrtDisc = sqrt(Discriminant);
    return float2(-B - SqrtDisc, -B + SqrtDisc);
}

// ============================================================================
//  Sun
// ============================================================================

// Time-of-day sun color: night -> twilight -> sunrise -> day. The intensity
// term is multiplied in at the end so the artist's SunIntensity scales the
// whole curve uniformly.
float3 SunTimeOfDayColor(float SunHeight)
{
    float3 NightColor    = float3(0.10, 0.15, 0.30) * 0.04;
    float3 TwilightColor = float3(1.00, 0.40, 0.15) * 0.60;
    float3 SunriseColor  = float3(1.00, 0.70, 0.30) * 0.90;
    float3 DayColor      = float3(1.00, 0.95, 0.85) * 1.00;

    if (SunHeight < -0.3)  return NightColor;
    if (SunHeight < -0.05) return lerp(NightColor,    TwilightColor, smoothstep(-0.3,  -0.05, SunHeight));
    if (SunHeight <  0.05) return lerp(TwilightColor, SunriseColor,  smoothstep(-0.05,  0.05, SunHeight));
    if (SunHeight <  0.40) return lerp(SunriseColor,  DayColor,      smoothstep( 0.05,  0.40, SunHeight));
    return DayColor;
}

// Procedural sun disc + glow. Used by both Gradient and Dynamic modes; the
// dynamic mode adds atmospheric scattering on top, the gradient mode just
// composites this against the sky gradient.
//
// SunDirection points FROM surface TO sun, so dot(RayDir, SunDir) approaches
// 1 when the ray heads at the sun, and SunDir.y is the sun's elevation.
float3 RenderSun(float3 RayDir, float3 SunColor, float SunDiscScale)
{
    float3 SunDir = GetSunDirection();
    float  SunDot = dot(RayDir, SunDir);

    float DiscAngle = kSunBaseAngle * max(SunDiscScale, 0.0);

    float CosDiscOuter = cos(DiscAngle * 1.4);
    float CosDiscInner = cos(DiscAngle * 0.5);
    float CosGlowOuter = cos(DiscAngle * 1.8);
    float CosGlowInner = cos(DiscAngle * 1.1);

    float SunDisc  = smoothstep(CosDiscOuter, CosDiscInner, SunDot);
    float GlowBase = smoothstep(CosGlowOuter, CosGlowInner, SunDot);
    float SunGlow  = GlowBase * GlowBase * GlowBase;

    // Sun fades out as it dips below the horizon to avoid a hard cutoff.
    float SunFade = smoothstep(0.0, 0.15, SunDir.y);

    return SunColor * (SunDisc * 0.8 + SunGlow * 0.3) * SunFade;
}

// ============================================================================
//  Mode: Solid
// ============================================================================

float3 SkySolid()
{
    return uEnvironment.SolidSkyColor.rgb;
}

// ============================================================================
//  Mode: Gradient
// ============================================================================

float3 SkyGradient(float3 RayDir)
{
    float Y = RayDir.y;
    float HorizonExp = max(uEnvironment.ZenithColor.w, 0.05);

    float3 Color;
    if (Y >= 0.0)
    {
        // Above the horizon: horizon -> zenith on a power curve. Smaller
        // exponents push more zenith color down the dome (a "thick" sky);
        // larger exponents keep a thin horizon band.
        float T = pow(saturate(Y), HorizonExp);
        Color   = lerp(uEnvironment.HorizonColor.rgb, uEnvironment.ZenithColor.rgb, T);
    }
    else
    {
        // Below the horizon: horizon -> ground. The smoothstep gives a soft
        // transition rather than a hard line at Y=0.
        float T = smoothstep(0.0, -0.4, Y);
        Color   = lerp(uEnvironment.HorizonColor.rgb, uEnvironment.GroundColor.rgb, T);
    }

    // Composite a procedural sun. Color is the artist's tint times an
    // intensity term -- gradient mode skips the time-of-day curve so the
    // sun stays a consistent color regardless of where the directional
    // light points (matches the "stylized" feel of the gradient sky).
    float3 SunColor = uEnvironment.SunTint.rgb * uEnvironment.SunTint.w;
    Color += RenderSun(RayDir, SunColor, uEnvironment.Misc.y);

    return Color;
}

// ============================================================================
//  Mode: Dynamic (atmospheric scattering)
// ============================================================================

float2 ComputeOpticalDepth(float3 RayOrigin, float3 RayDir, float RayLength)
{
    const int Steps    = 8;
    float     StepSize = RayLength / float(Steps);
    float2    OpticalDepth = float2(0.0, 0.0);

    [[unroll]]
    for (int i = 0; i < Steps; i++)
    {
        float3 SamplePoint = RayOrigin + RayDir * (float(i) + 0.5) * StepSize;
        float  Height      = length(SamplePoint) - kPlanetR;

        if (Height < 0.0) break;

        OpticalDepth += float2(exp(-Height / kRayleighHeight), exp(-Height / kMieHeight)) * StepSize;
    }

    return OpticalDepth;
}

float3 ComputeAtmosphericScattering(float3 RayOrigin, float3 RayDir, float3 SunColor, float MieG)
{
    float2 AtmosphereIntersect = RaySphereIntersect(RayOrigin, RayDir, kAtmosphereR);
    if (AtmosphereIntersect.y < 0.0)
        return float3(0.0, 0.0, 0.0);

    float RayStart = max(AtmosphereIntersect.x, 0.0);
    float RayEnd   = AtmosphereIntersect.y;

    // Trim the integration if the ray hits the planet -- prevents adding
    // light from the far side of the atmosphere when looking at the ground.
    float2 GroundIntersect = RaySphereIntersect(RayOrigin, RayDir, kPlanetR);
    if (GroundIntersect.x > 0.0)
        RayEnd = min(RayEnd, GroundIntersect.x);

    float RayLength = RayEnd - RayStart;
    if (RayLength <= 0.0)
        return float3(0.0, 0.0, 0.0);

    const int PrimarySteps    = 12;
    float     PrimaryStepSize = RayLength / float(PrimarySteps);

    float3 TotalRayleigh       = float3(0.0, 0.0, 0.0);
    float3 TotalMie            = float3(0.0, 0.0, 0.0);
    float2 PrimaryOpticalDepth = float2(0.0, 0.0);

    // SunDirection points FROM surface TO sun, so it's also the direction
    // we walk along to integrate the sun's incoming-light optical depth.
    float3 SunDir = GetSunDirection();

    [[unroll]]
    for (int i = 0; i < PrimarySteps; i++)
    {
        float3 SamplePoint   = RayOrigin + RayDir * (RayStart + (float(i) + 0.5) * PrimaryStepSize);
        float  PrimaryHeight = length(SamplePoint) - kPlanetR;

        if (PrimaryHeight < 0.0) break;

        float PrimaryRayleighDensity = exp(-PrimaryHeight / kRayleighHeight);
        float PrimaryMieDensity      = exp(-PrimaryHeight / kMieHeight);

        PrimaryOpticalDepth += float2(PrimaryRayleighDensity, PrimaryMieDensity) * PrimaryStepSize;

        float2 LightIntersect    = RaySphereIntersect(SamplePoint, SunDir, kAtmosphereR);
        float2 LightOpticalDepth = ComputeOpticalDepth(SamplePoint, SunDir, LightIntersect.y);
        float2 TotalOpticalDepth = PrimaryOpticalDepth + LightOpticalDepth;

        float3 RayleighTransmittance = exp(-kRayleighCoeff * TotalOpticalDepth.x);
        float3 MieTransmittance      = exp(-float3(kMieCoeff, kMieCoeff, kMieCoeff) * TotalOpticalDepth.y * 1.1);

        TotalRayleigh += PrimaryRayleighDensity * RayleighTransmittance * PrimaryStepSize;
        TotalMie      += PrimaryMieDensity      * MieTransmittance      * PrimaryStepSize;
    }

    float  CosTheta      = dot(RayDir, SunDir);
    float3 RayleighColor = kRayleighCoeff * RayleighPhase(CosTheta) * TotalRayleigh;
    float3 MieColor      = float3(kMieCoeff * MiePhase(CosTheta, MieG)) * TotalMie;

    return SunColor * (RayleighColor + MieColor);
}

float3 SkyDynamic(float3 RayDir)
{
    // Sun height drives both the time-of-day color curve and the sun fade.
    // SunDirection points FROM surface TO sun, so positive Y is "sun is up".
    float  SunHeight  = GetSunDirection().y;
    float3 SunColor   = SunTimeOfDayColor(SunHeight)
                      * uEnvironment.SunTint.rgb
                      * uEnvironment.SunTint.w;
    float  MieG       = clamp(uEnvironment.Misc.w, -0.99, 0.99);

    // Camera position in atmosphere coords: on the planet surface, plus a
    // small offset. Scattering integration is independent of world camera
    // position so the sky doesn't shift as the player moves.
    float3 AtmoCamera = float3(0.0, kPlanetR + 50.0, 0.0);

    float3 Color = ComputeAtmosphericScattering(AtmoCamera, RayDir, SunColor, MieG);
    Color += RenderSun(RayDir, SunColor, uEnvironment.Misc.y);

    // Per-component exposure + soft tone curve. Keeping the smoothstep
    // contrast nudge from the original shader so silhouettes stay readable.
    float Exposure = uEnvironment.Misc.z;
    Color = 1.0 - exp(-Color * Exposure);
    Color = lerp(Color, Color * Color * (3.0 - 2.0 * Color), 0.1);

    return Color;
}

// ============================================================================
//  Main
// ============================================================================

[shader("fragment")]
FragmentOutput FragmentMain(FragmentInput Input)
{
    float3 RayDir = GetViewRay(Input.UV);
    uint   Mode   = asuint(uEnvironment.Misc.x);

    float3 Color;
    if (Mode == SKY_MODE_SOLID)
    {
        Color = SkySolid();
    }
    else if (Mode == SKY_MODE_GRADIENT)
    {
        Color = SkyGradient(RayDir);
    }
    else
    {
        Color = SkyDynamic(RayDir);
    }

    FragmentOutput Output;
    Output.Color = float4(Color, 1.0);
    return Output;
}
