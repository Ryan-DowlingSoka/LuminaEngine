#pragma once
#include "Includes/SceneGlobals.slang"

// ============================================================================
//  Constants
// ============================================================================

static const float  uSunSize         = radians(0.53);
static const float3 uSkyZenith       = float3(0.05, 0.1, 0.4);
static const float3 uSkyHorizon      = float3(0.6, 0.8, 1.0);
static const float3 uGroundColor     = float3(0.72627, 0.7555, 0.9908);

static const float3 rayleighCoeff    = float3(5.8e-6, 13.5e-6, 33.1e-6);
static const float  mieCoeff         = 2.1e-5;
static const float  mieG             = 0.76;

static const float  atmosphereRadius = 6420e2;
static const float  planetRadius     = 6360e2;
static const float  rayleighHeight   = 8e2;
static const float  mieHeight        = 1.2e2;

// ============================================================================
//  IO
// ============================================================================

struct FragmentInput
{
    float2 UV       : TEXCOORD0;
};

struct FragmentOutput
{
    float4 Color    : SV_Target0;
};

// ============================================================================
//  Sun Color
// ============================================================================

float3 CalculateSunColor()
{
    float SunHeight = GetSunDirection().y;

    float3 NightColor    = float3(0.1, 0.15, 0.3)   * 0.8;
    float3 TwilightColor = float3(1.0, 0.4, 0.15)   * 12.0;
    float3 SunriseColor  = float3(1.0, 0.7, 0.3)    * 18.0;
    float3 DayColor      = float3(1.0, 0.95, 0.85)  * 20.0;

    float3 SunColor;
    if (SunHeight < -0.3)
    {
        SunColor = NightColor;
    }
    else if (SunHeight < -0.05)
    {
        SunColor = lerp(NightColor, TwilightColor, smoothstep(-0.3, -0.05, SunHeight));
    }
    else if (SunHeight < 0.05)
    {
        SunColor = lerp(TwilightColor, SunriseColor, smoothstep(-0.05, 0.05, SunHeight));
    }
    else if (SunHeight < 0.4)
    {
        SunColor = lerp(SunriseColor, DayColor, smoothstep(0.05, 0.4, SunHeight));
    }
    else
    {
        SunColor = DayColor;
    }

    return SunColor;
}

// ============================================================================
//  Atmosphere Helpers
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

float2 ComputeOpticalDepth(float3 RayOrigin, float3 RayDir, float RayLength)
{
    const int Steps    = 8;
    float     StepSize = RayLength / float(Steps);
    float2    OpticalDepth = float2(0.0, 0.0);
	
	[[unroll]]
    for (int i = 0; i < Steps; i++)
    {
        float3 SamplePoint = RayOrigin + RayDir * (float(i) + 0.5) * StepSize;
        float  Height      = length(SamplePoint) - planetRadius;

        if (Height < 0.0) break;

        OpticalDepth += float2(exp(-Height / rayleighHeight), exp(-Height / mieHeight)) * StepSize;
    }

    return OpticalDepth;
}

float3 ComputeAtmosphericScattering(float3 RayOrigin, float3 RayDir, float3 SunColor)
{
    float2 AtmosphereIntersect = RaySphereIntersect(RayOrigin, RayDir, atmosphereRadius);

    if (AtmosphereIntersect.y < 0.0)
        return float3(0.0, 0.0, 0.0);

    float RayStart = max(AtmosphereIntersect.x, 0.0);
    float RayEnd   = AtmosphereIntersect.y;

    float2 GroundIntersect = RaySphereIntersect(RayOrigin, RayDir, planetRadius);
    if (GroundIntersect.x > 0.0)
        RayEnd = min(RayEnd, GroundIntersect.x);

    float RayLength = RayEnd - RayStart;
    if (RayLength <= 0.0)
        return float3(0.0, 0.0, 0.0);

    const int PrimarySteps    = 12;
    float     PrimaryStepSize = RayLength / float(PrimarySteps);

    float3 TotalRayleigh      = float3(0.0, 0.0, 0.0);
    float3 TotalMie           = float3(0.0, 0.0, 0.0);
    float2 PrimaryOpticalDepth = float2(0.0, 0.0);

    float3 SunDir = GetSunDirection();

	[[unroll]]
    for (int i = 0; i < PrimarySteps; i++)
    {
        float3 SamplePoint    = RayOrigin + RayDir * (RayStart + (float(i) + 0.5) * PrimaryStepSize);
        float  PrimaryHeight  = length(SamplePoint) - planetRadius;

        if (PrimaryHeight < 0.0) break;

        float PrimaryRayleighDensity = exp(-PrimaryHeight / rayleighHeight);
        float PrimaryMieDensity      = exp(-PrimaryHeight / mieHeight);

        PrimaryOpticalDepth += float2(PrimaryRayleighDensity, PrimaryMieDensity) * PrimaryStepSize;

        float2 LightIntersect   = RaySphereIntersect(SamplePoint, SunDir, atmosphereRadius);
        float2 LightOpticalDepth = ComputeOpticalDepth(SamplePoint, SunDir, LightIntersect.y);
        float2 TotalOpticalDepth = PrimaryOpticalDepth + LightOpticalDepth;

        float3 RayleighTransmittance = exp(-rayleighCoeff * TotalOpticalDepth.x);
        float3 MieTransmittance      = exp(-float3(mieCoeff, mieCoeff, mieCoeff) * TotalOpticalDepth.y * 1.1);

        TotalRayleigh += PrimaryRayleighDensity * RayleighTransmittance * PrimaryStepSize;
        TotalMie      += PrimaryMieDensity      * MieTransmittance      * PrimaryStepSize;
    }

    float CosTheta          = dot(RayDir, SunDir);
    float3 RayleighColor    = rayleighCoeff * RayleighPhase(CosTheta) * TotalRayleigh;
    float3 MieColor         = float3(mieCoeff * MiePhase(CosTheta, mieG)) * TotalMie;

    return SunColor * (RayleighColor + MieColor);
}

float3 RenderSun(float3 RayDir, float3 SunColor)
{
    float3 SunDir  = GetSunDirection();
    float  SunDot  = dot(RayDir, SunDir);

    float CosDiscOuter = cos(uSunSize * 1.4);
    float CosDiscInner = cos(uSunSize * 0.5);
    float CosGlowOuter = cos(uSunSize * 1.8);
    float CosGlowInner = cos(uSunSize * 1.1);

    float SunDisc  = smoothstep(CosDiscOuter, CosDiscInner, SunDot);
    float GlowBase = smoothstep(CosGlowOuter, CosGlowInner, SunDot);
    float SunGlow  = GlowBase * GlowBase * GlowBase;
    float SunFade  = smoothstep(0.0, 2.5, SunDir.y);

    return SunColor * (SunDisc * 0.8 + SunGlow * 0.3) * SunFade;
}

// ============================================================================
//  Main
// ============================================================================

[shader("fragment")]
FragmentOutput FragmentMain(FragmentInput Input)
{
    float3 SunColor = CalculateSunColor();
    float3 RayDir   = GetViewRay(Input.UV);
    float3 CameraPos = float3(0.0, planetRadius + 50.0, 0.0);

    float3 Color = float3(0.0, 0.0, 0.0);
    Color += ComputeAtmosphericScattering(CameraPos, RayDir, SunColor);
    Color += RenderSun(RayDir, SunColor);

    float AvgLuminance = dot(Color, float3(0.299, 0.587, 0.114));
    float Exposure     = lerp(1.2, 2.5, smoothstep(-0.2, 0.3, GetSunDirection().y));

    Color = 1.0 - exp(-Color * Exposure);
    Color = lerp(Color, Color * Color * (3.0 - 2.0 * Color), 0.1);

    FragmentOutput Output;
    Output.Color = float4(Color, 1.0);
    return Output;
}