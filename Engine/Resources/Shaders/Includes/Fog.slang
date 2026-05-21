#pragma once

// GPU mirror of FExponentialHeightFogParams (EnvironmentRenderTypes.h). Shared by
// the analytic height-fog composite and the volumetric light-shaft march so both
// agree on the medium's density profile.
struct FExponentialHeightFogParams
{
    float4 InscatteringColor;   // rgb = fog color, w = density at base height
    float4 HeightParams;        // x = falloff, y = baseHeight, z = startDistance, w = maxOpacity
    float4 DirectionalColor;    // rgb = sun inscatter color, w = exponent
    float4 VolumetricParams;    // x = scatterIntensity, y = anisotropy, z = maxDistance, w = dirStartDistance
};

// Local fog density at a world-space point: exponential falloff with height.
float Fog_DensityAt(FExponentialHeightFogParams Fog, float3 WorldPos)
{
    float Falloff = Fog.HeightParams.x;
    float BaseH   = Fog.HeightParams.y;
    return Fog.InscatteringColor.w * exp(-Falloff * (WorldPos.y - BaseH));
}

// Closed-form optical depth of the exp-height medium along a ray segment of
// length SegLen starting at StartPos in direction RayDir (unit). Integrates
// density * exp(-falloff * (y - baseHeight)) analytically.
float Fog_OpticalDepth(FExponentialHeightFogParams Fog, float3 StartPos, float3 RayDir, float SegLen)
{
    float Falloff   = Fog.HeightParams.x;
    float BaseH     = Fog.HeightParams.y;
    float BaseDens  = Fog.InscatteringColor.w * exp(-Falloff * (StartPos.y - BaseH));
    float Vy        = RayDir.y;

    // For near-horizontal rays the height term is ~constant over the segment;
    // fall back to the rectangular integral to avoid a divide-by-tiny blowup.
    if (abs(Vy) > 1e-4)
    {
        return BaseDens * (1.0 - exp(-Falloff * Vy * SegLen)) / (Falloff * Vy);
    }
    return BaseDens * SegLen;
}
