#pragma once
#include "SceneGlobals.slang"
#include "IBL.slang"

// ============================================================================
//  Runtime IBL ambient
//
//  Karis 2013 split-sum approximation:
//
//      Ambient = kD * SkyIrradiance(N) * Albedo
//              + SkyPrefilter(R, roughness) * (F * BRDF_LUT.x + BRDF_LUT.y)
//
//  Where the irradiance map stores Irradiance/pi (so no extra /pi here),
//  the prefilter map's mip is selected from material roughness, and the
//  BRDF LUT integrates F0's scale + bias for the runtime to dot against.
// ============================================================================

float3 SampleIrradiance(float3 N)
{
    return uSkyIrradiance.SampleLevel(N, 0.0).rgb;
}

// Roughness-driven mip selection. The prefilter cube's mip 0 is mirror,
// last mip is fully rough (matches the bake at PrefilterEnvMapPass).
float3 SamplePrefilter(float3 R, float Roughness)
{
    uint Width, Height, NumMips;
    uSkyPrefilter.GetDimensions(0, Width, Height, NumMips);

    float MaxLod = max(float(NumMips) - 1.0, 0.0);
    float Lod    = saturate(Roughness) * MaxLod;
    return uSkyPrefilter.SampleLevel(R, Lod).rgb;
}

float2 SampleBRDFLUT(float NdotV, float Roughness)
{
    return uBRDFLut.SampleLevel(float2(saturate(NdotV), saturate(Roughness)), 0.0).rg;
}

// Final IBL ambient term. Returns the lit color (caller multiplies by AO,
// adds direct lighting). N must be the *shading* normal; V points from the
// surface to the camera; F0 / Roughness / Metallic / Albedo are the usual
// material inputs.
float3 EvaluateIBLAmbient(float3 N, float3 V, float3 Albedo, float3 F0, float Roughness, float Metallic)
{
    float  NdotV = saturate(dot(N, V));
    float3 R     = reflect(-V, N);

    // Roughness-aware Fresnel: standard Schlick falls off too fast on rough
    // surfaces, this variant clamps the (1 - cos)^5 envelope to (1 - R) so
    // metals keep their colored grazing response without overbrightening.
    float3 F     = FresnelSchlickRoughness(NdotV, F0, Roughness);
    float3 kD    = (1.0 - F) * (1.0 - Metallic);

    float3 Diffuse  = SampleIrradiance(N) * Albedo;
    float3 PreFilt  = SamplePrefilter(R, Roughness);
    float2 EnvBRDF  = SampleBRDFLUT(NdotV, Roughness);
    float3 Specular = PreFilt * (F * EnvBRDF.x + EnvBRDF.y);

    return kD * Diffuse + Specular;
}
