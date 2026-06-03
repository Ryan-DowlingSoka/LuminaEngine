#pragma once
#include "Common.slang"

// IBL helpers shared by the BRDF LUT bake and runtime IBL sampling.
// Karis 2013 "Real Shading in Unreal Engine 4" split-sum approximation.

// Hammersley low-discrepancy (u, v) seed for GGX importance sampling.
float RadicalInverse_VdC(uint Bits)
{
    Bits = (Bits << 16u) | (Bits >> 16u);
    Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
    Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
    Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
    Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
    return float(Bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

// Importance-sample the GGX NDF in tangent space, producing a half-vector
// distributed proportional to D(H). Caller transforms to world space.
float3 ImportanceSampleGGX(float2 Xi, float3 N, float Roughness)
{
    float A = Roughness * Roughness;

    float Phi      = 2.0 * PI * Xi.x;
    float CosTheta = sqrt((1.0 - Xi.y) / (1.0 + (A * A - 1.0) * Xi.y));
    float SinTheta = sqrt(1.0 - CosTheta * CosTheta);

    // Tangent-space half vector.
    float3 H;
    H.x = SinTheta * cos(Phi);
    H.y = SinTheta * sin(Phi);
    H.z = CosTheta;

    // Build a tangent frame around N. Pick an Up axis that isn't degenerate
    // when N is itself near +Z.
    float3 Up        = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 Tangent   = normalize(cross(Up, N));
    float3 Bitangent = cross(N, Tangent);

    return normalize(Tangent * H.x + Bitangent * H.y + N * H.z);
}

// Smith geometry for IBL: k = a^2 / 2 (direct lighting uses k = (a + 1)^2 / 8).
// Mixing the two remaps produces dim or over-bright reflections.
float GeometrySchlickGGX_IBL(float NdotV, float Roughness)
{
    float A = Roughness;
    float K = (A * A) / 2.0;
    return NdotV / (NdotV * (1.0 - K) + K);
}

float GeometrySmith_IBL(float3 N, float3 V, float3 L, float Roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX_IBL(NdotV, Roughness) * GeometrySchlickGGX_IBL(NdotL, Roughness);
}

// Roughness-aware Fresnel for IBL ambient: clamp the (1 - cos)^5 envelope against max(1 - R, F0)
// to keep metals' colored grazing response without unrealistic rim brightness on rough surfaces.
float3 FresnelSchlickRoughness(float CosTheta, float3 F0, float Roughness)
{
    float3 OneMinusR = max(float3(1.0 - Roughness), F0);
    float  X  = saturate(1.0 - CosTheta);
    float  X2 = X * X;
    return F0 + (OneMinusR - F0) * (X2 * X2 * X);
}

// Karis 2013 split-sum BRDF integrand -> (scale, bias) stored in the LUT per (NdotV, Roughness).
// Runtime IBL specular reduces to Prefiltered * (F0 * scale + bias).
float2 IntegrateBRDF(float NdotV, float Roughness)
{
    // V is reconstructed in tangent space with N = +Z. Only NdotV matters
    // for the integral, so any V with the right z-component works.
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    const float3 N = float3(0.0, 0.0, 1.0);
    const uint   NumSamples = 1024u;

    // Smith's view term depends only on NdotV (constant across all samples); hoist it out of the loop.
    float Gv = GeometrySchlickGGX_IBL(NdotV, Roughness);

    for (uint i = 0u; i < NumSamples; ++i)
    {
        float2 Xi = Hammersley(i, NumSamples);
        float3 H  = ImportanceSampleGGX(Xi, N, Roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = saturate(L.z);
        float NdotH = saturate(H.z);
        float VdotH = saturate(dot(V, H));

        if (NdotL > 0.0)
        {
            // The GGX sample PDF cancels D(H), leaving G * VdotH / (NdotH * NdotV) per sample.
            float G     = Gv * GeometrySchlickGGX_IBL(NdotL, Roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            // (1 - VdotH)^5 via the multiply chain (cheaper than pow; VdotH already saturated).
            float X     = 1.0 - VdotH;
            float X2    = X * X;
            float Fc    = X2 * X2 * X;

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    return float2(A, B) / float(NumSamples);
}
