#pragma once
#include "SceneGlobals.slang"
#include "IBL.slang"

// Runtime IBL ambient, Karis 2013 split-sum:
//   Ambient = kD * SkyIrradiance(N) * Albedo + SkyPrefilter(R, roughness) * (F * BRDF_LUT.x + BRDF_LUT.y)

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

// Frostbite specular occlusion (Lagarde & de Rousiers, "Moving Frostbite to PBR", 2014).
// Widens AO with roughness/grazing angle so the narrower specular cone doesn't leak into cavities.
float ComputeSpecularOcclusion(float NdotV, float AO, float Roughness)
{
    return saturate(pow(NdotV + AO, exp2(-16.0 * Roughness - 1.0)) - 1.0 + AO);
}

// Energy-conserving IBL ambient: Karis split-sum + Fdez-Aguera 2019 multi-scatter compensation.
// N is the shading normal; V points surface->camera. AO occludes diffuse+multi-scatter, SpecAO the mirror.
float3 EvaluateIBLAmbient(float3 N, float3 V, float3 Albedo, float3 F0, float Roughness, float Metallic, float AO)
{
    float  NdotV = saturate(dot(N, V));
    float3 R     = reflect(-V, N);

    float2 DFG = SampleBRDFLUT(NdotV, Roughness);   // (scale, bias)

    // Roughness-aware Fresnel as the specular weight (clamps the (1 - cos)^5 envelope to (1 - R)).
    float3 kS     = FresnelSchlickRoughness(NdotV, F0, Roughness);
    float3 FssEss = kS * DFG.x + DFG.y;             // single-scatter specular

    // Multi-scatter compensation (Fdez-Aguera): Ems = energy the single-scatter lobe missed,
    // Favg = cosine-averaged Fresnel; the closed form sums the remaining bounces.
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
