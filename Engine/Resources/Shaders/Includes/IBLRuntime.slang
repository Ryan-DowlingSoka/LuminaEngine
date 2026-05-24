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

// Frostbite specular occlusion (Lagarde & de Rousiers, "Moving Frostbite to
// PBR", 2014). Scalar AO occludes diffuse uniformly, but the specular cone is
// narrower and view-dependent, so reusing AO directly leaks glossy reflections
// into cavities. This widens the occlusion with roughness and grazing angle so
// the mirror term darkens in creases where it should.
float ComputeSpecularOcclusion(float NdotV, float AO, float Roughness)
{
    return saturate(pow(NdotV + AO, exp2(-16.0 * Roughness - 1.0)) - 1.0 + AO);
}

// Final IBL ambient term, energy-conserving. Adds Fdez-Aguera 2019 multiple-
// scattering compensation on top of the Karis split-sum: a single-scatter
// microfacet model discards the energy that bounces a second time off the
// microsurface, which makes rough metals/dielectrics read too dark and
// desaturated. The precomputed split-sum integral already carries everything
// needed to add those bounces back at near-zero cost. AO occludes the diffuse
// + multi-scatter term; specular occlusion (above) occludes the mirror term.
//
// N must be the *shading* normal; V points from the surface to the camera;
// F0 / Roughness / Metallic / Albedo are the usual material inputs.
float3 EvaluateIBLAmbient(float3 N, float3 V, float3 Albedo, float3 F0, float Roughness, float Metallic, float AO)
{
    float  NdotV = saturate(dot(N, V));
    float3 R     = reflect(-V, N);

    float2 DFG = SampleBRDFLUT(NdotV, Roughness);   // (scale, bias)

    // Roughness-aware Fresnel as the specular weight. Standard Schlick falls
    // off too fast on rough surfaces; this clamps the (1 - cos)^5 envelope to
    // (1 - R) so metals keep their colored grazing response.
    float3 kS     = FresnelSchlickRoughness(NdotV, F0, Roughness);
    float3 FssEss = kS * DFG.x + DFG.y;             // single-scatter specular

    // Multiple-scattering compensation (Fdez-Aguera). Ems is the energy the
    // single-scatter lobe failed to reflect; F_avg is the cosine-averaged
    // Fresnel; the closed form sums the geometric series of remaining bounces.
    float  Ems    = saturate(1.0 - (DFG.x + DFG.y));
    float3 Favg   = F0 + (1.0 - F0) / 21.0;
    float3 FmsEms = (Ems * FssEss * Favg) / max(1.0 - Favg * Ems, 1e-4);

    // Metals carry no diffuse; whatever energy isn't reflected as single- or
    // multi-scatter specular goes to diffuse, keeping the surface conserving.
    float3 DiffuseColor = Albedo * (1.0 - Metallic);
    float3 kD           = DiffuseColor * (1.0 - FssEss - FmsEms);

    float3 Irradiance = SampleIrradiance(N);
    float3 PreFilt    = SamplePrefilter(R, Roughness);

    float SpecAO = ComputeSpecularOcclusion(NdotV, AO, Roughness);

    return FssEss * PreFilt * SpecAO + (FmsEms + kD) * Irradiance * AO;
}
