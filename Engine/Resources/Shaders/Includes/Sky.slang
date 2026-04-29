#pragma once

// ============================================================================
//  Sky evaluator
//
//  Pure functions: same code path runs for the fullscreen sky (Environment.slang)
//  and for the IBL cubemap capture (SkyCubeCapture.slang). No global bindings;
//  callers pass FEnvironmentParams + sun direction + a world-space ray.
//
//  Coordinate system: ray is world-space, +Y up. SunDir points FROM surface
//  TO sun, so SunDir.y is the sun's elevation in [-1, 1].
// ============================================================================

// Must match the GSkyMode_* constants in EnvironmentRenderTypes.h.
#define SKY_MODE_SOLID    0u
#define SKY_MODE_GRADIENT 1u
#define SKY_MODE_DYNAMIC  2u
// HDRI sky is sampled outside Sky.slang (it needs a SamplerCube binding,
// which Sky.slang's pure-function design avoids). EvaluateSky() returns
// black for this mode; the env shader handles HDRI as an early-out
// before calling EvaluateSky.
#define SKY_MODE_HDRI     3u

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

// Sun's true angular diameter, ~half a degree. SunDiscScale stretches the
// rendered disc and glow without affecting lighting math.
static const float kSunBaseAngle = radians(0.53);

// Earth-like atmospheric scattering constants. Tuned to give a recognizable
// Rayleigh + Mie sky; tweaking them changes hue fundamentally rather than
// just brightening, so they stay fixed and the artist scales via SkyExposure.
static const float3 kRayleighCoeff   = float3(5.8e-6, 13.5e-6, 33.1e-6);
static const float  kMieCoeff        = 2.1e-5;
static const float  kAtmosphereR     = 6420e2;
static const float  kPlanetR         = 6360e2;
static const float  kRayleighHeight  = 8e2;
static const float  kMieHeight       = 1.2e2;

// ============================================================================
//  Helpers
// ============================================================================

float Sky_RayleighPhase(float CosTheta)
{
    static const float Factor = 3.0 / (16.0 * 3.14159);
    return Factor * (1.0 + CosTheta * CosTheta);
}

float Sky_MiePhase(float CosTheta, float G)
{
    static const float Factor = 1.0 / (4.0 * 3.14159);
    float G2    = G * G;
    float Denom = 1.0 + G2 - 2.0 * G * CosTheta;
    return Factor * (1.0 - G2) / (Denom * sqrt(Denom));
}

float2 Sky_RaySphereIntersect(float3 RayOrigin, float3 RayDir, float Radius)
{
    float B            = dot(RayOrigin, RayDir);
    float C            = dot(RayOrigin, RayOrigin) - Radius * Radius;
    float Discriminant = B * B - C;

    if (Discriminant < 0.0)
        return float2(-1.0, -1.0);

    float SqrtDisc = sqrt(Discriminant);
    return float2(-B - SqrtDisc, -B + SqrtDisc);
}

// Time-of-day sun color: night -> twilight -> sunrise -> day. The intensity
// term is multiplied in at the end so the artist's SunIntensity scales the
// whole curve uniformly.
float3 Sky_SunTimeOfDayColor(float SunHeight)
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

// Procedural sun disc + glow.
float3 Sky_RenderSun(float3 RayDir, float3 SunDir, float3 SunColor, float SunDiscScale)
{
    float SunDot     = dot(RayDir, SunDir);
    float DiscAngle  = kSunBaseAngle * max(SunDiscScale, 0.0);

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
//  Modes
// ============================================================================

float3 Sky_Solid(FEnvironmentParams Env)
{
    return Env.SolidSkyColor.rgb;
}

float3 Sky_Gradient(FEnvironmentParams Env, float3 SunDir, float3 RayDir)
{
    float Y = RayDir.y;
    float HorizonExp = max(Env.ZenithColor.w, 0.05);

    float3 Color;
    if (Y >= 0.0)
    {
        float T = pow(saturate(Y), HorizonExp);
        Color   = lerp(Env.HorizonColor.rgb, Env.ZenithColor.rgb, T);
    }
    else
    {
        float T = smoothstep(0.0, -0.4, Y);
        Color   = lerp(Env.HorizonColor.rgb, Env.GroundColor.rgb, T);
    }

    float3 SunColor = Env.SunTint.rgb * Env.SunTint.w;
    Color += Sky_RenderSun(RayDir, SunDir, SunColor, Env.Misc.y);
    return Color;
}

// Mode-specific scattering integration helpers (Dynamic only).
float2 Sky_ComputeOpticalDepth(float3 RayOrigin, float3 RayDir, float RayLength)
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

float3 Sky_ComputeAtmosphericScattering(float3 RayOrigin, float3 RayDir, float3 SunDir, float3 SunColor, float MieG)
{
    float2 AtmosphereIntersect = Sky_RaySphereIntersect(RayOrigin, RayDir, kAtmosphereR);
    if (AtmosphereIntersect.y < 0.0)
        return float3(0.0, 0.0, 0.0);

    float RayStart = max(AtmosphereIntersect.x, 0.0);
    float RayEnd   = AtmosphereIntersect.y;

    // Trim against the planet so rays through the atmosphere don't add light
    // from the far side when looking at the ground.
    float2 GroundIntersect = Sky_RaySphereIntersect(RayOrigin, RayDir, kPlanetR);
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

    [[unroll]]
    for (int i = 0; i < PrimarySteps; i++)
    {
        float3 SamplePoint   = RayOrigin + RayDir * (RayStart + (float(i) + 0.5) * PrimaryStepSize);
        float  PrimaryHeight = length(SamplePoint) - kPlanetR;

        if (PrimaryHeight < 0.0) break;

        float PrimaryRayleighDensity = exp(-PrimaryHeight / kRayleighHeight);
        float PrimaryMieDensity      = exp(-PrimaryHeight / kMieHeight);

        PrimaryOpticalDepth += float2(PrimaryRayleighDensity, PrimaryMieDensity) * PrimaryStepSize;

        float2 LightIntersect    = Sky_RaySphereIntersect(SamplePoint, SunDir, kAtmosphereR);
        float2 LightOpticalDepth = Sky_ComputeOpticalDepth(SamplePoint, SunDir, LightIntersect.y);
        float2 TotalOpticalDepth = PrimaryOpticalDepth + LightOpticalDepth;

        float3 RayleighTransmittance = exp(-kRayleighCoeff * TotalOpticalDepth.x);
        float3 MieTransmittance      = exp(-float3(kMieCoeff, kMieCoeff, kMieCoeff) * TotalOpticalDepth.y * 1.1);

        TotalRayleigh += PrimaryRayleighDensity * RayleighTransmittance * PrimaryStepSize;
        TotalMie      += PrimaryMieDensity      * MieTransmittance      * PrimaryStepSize;
    }

    float  CosTheta      = dot(RayDir, SunDir);
    float3 RayleighColor = kRayleighCoeff * Sky_RayleighPhase(CosTheta) * TotalRayleigh;
    float3 MieColor      = float3(kMieCoeff * Sky_MiePhase(CosTheta, MieG)) * TotalMie;

    return SunColor * (RayleighColor + MieColor);
}

float3 Sky_Dynamic(FEnvironmentParams Env, float3 SunDir, float3 RayDir)
{
    float  SunHeight  = SunDir.y;
    float3 SunColor   = Sky_SunTimeOfDayColor(SunHeight) * Env.SunTint.rgb * Env.SunTint.w;
    float  MieG       = clamp(Env.Misc.w, -0.99, 0.99);

    // Atmosphere coords: planet-surface origin, fixed in world so the sky
    // doesn't shift as the player moves.
    float3 AtmoCamera = float3(0.0, kPlanetR + 50.0, 0.0);

    float3 Color = Sky_ComputeAtmosphericScattering(AtmoCamera, RayDir, SunDir, SunColor, MieG);
    Color += Sky_RenderSun(RayDir, SunDir, SunColor, Env.Misc.y);

    float Exposure = Env.Misc.z;
    Color = 1.0 - exp(-Color * Exposure);
    Color = lerp(Color, Color * Color * (3.0 - 2.0 * Color), 0.1);
    return Color;
}

// ============================================================================
//  Entry
// ============================================================================

float3 EvaluateSky(FEnvironmentParams Env, float3 SunDir, float3 RayDir)
{
    uint Mode = asuint(Env.Misc.x);

    if (Mode == SKY_MODE_SOLID)
    {
        return Sky_Solid(Env);
    }
    if (Mode == SKY_MODE_GRADIENT)
    {
        return Sky_Gradient(Env, SunDir, RayDir);
    }
    return Sky_Dynamic(Env, SunDir, RayDir);
}

// Cubemap face -> world-space ray direction. Convention matches Vulkan/D3D
// cubemap sampling so the captured cube can be sampled later with a normal
// `texCube.SampleLevel(R)` call where R is a world-space direction.
//
//   Face 0 = +X, 1 = -X, 2 = +Y, 3 = -Y, 4 = +Z, 5 = -Z
//
// (u, v) is the face-local pixel coord remapped to [-1, 1].
float3 CubeFaceDirection(uint Face, float2 UV)
{
    float u = UV.x * 2.0 - 1.0;
    float v = UV.y * 2.0 - 1.0;

    float3 Dir;
    if (Face == 0u)      Dir = float3( 1.0, -v,  -u  );
    else if (Face == 1u) Dir = float3(-1.0, -v,   u  );
    else if (Face == 2u) Dir = float3( u,    1.0, v  );
    else if (Face == 3u) Dir = float3( u,   -1.0,-v  );
    else if (Face == 4u) Dir = float3( u,   -v,   1.0);
    else                 Dir = float3(-u,   -v,  -1.0);

    return normalize(Dir);
}
