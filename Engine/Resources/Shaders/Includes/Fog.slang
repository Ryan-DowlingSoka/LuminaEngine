#pragma once

// GPU mirror of FExponentialHeightFogParams (EnvironmentRenderTypes.h). One medium definition shared
// by the froxel volumetric passes (near range, shadowed) and the analytic height-fog composite
// (beyond the froxel range + sky), so both agree on density, albedo, and lighting and the hand-off
// at the froxel far plane is seamless.
struct FExponentialHeightFogParams
{
    float4 InscatteringColor;   // rgb = fog albedo, w = density at base height
    float4 HeightParams;        // x = falloff, y = baseHeight, z = startDistance, w = maxOpacity
    float4 DirectionalColor;    // rgb = sun inscatter tint, w = exponent
    float4 VolumetricParams;    // x = scatterIntensity, y = anisotropy, z = froxel range, w = dirStartDistance
};

// Local fog density at a world-space point: exponential falloff with height.
float Fog_DensityAt(FExponentialHeightFogParams Fog, float3 WorldPos)
{
    float Falloff = Fog.HeightParams.x;
    float BaseH   = Fog.HeightParams.y;
    return Fog.InscatteringColor.w * exp(-Falloff * (WorldPos.y - BaseH));
}

// Closed-form optical depth of the exp-height medium along a ray segment (StartPos, unit RayDir,
// length SegLen): analytic integral of density * exp(-falloff * (y - baseHeight)).
float Fog_OpticalDepth(FExponentialHeightFogParams Fog, float3 StartPos, float3 RayDir, float SegLen)
{
    float Falloff   = Fog.HeightParams.x;
    float BaseH     = Fog.HeightParams.y;
    float BaseDens  = Fog.InscatteringColor.w * exp(clamp(-Falloff * (StartPos.y - BaseH), -60.0, 60.0));
    float Vy        = RayDir.y;

    // For near-horizontal rays the height term is ~constant over the segment;
    // fall back to the rectangular integral to avoid a divide-by-tiny blowup.
    if (abs(Falloff * Vy) > 1e-5)
    {
        // Clamp the exponent so steep downward rays stay finite (the medium densifies below).
        return BaseDens * (1.0 - exp(clamp(-Falloff * Vy * SegLen, -60.0, 60.0))) / (Falloff * Vy);
    }
    return BaseDens * SegLen;
}

// Optical depth from StartPos to infinity along the ray (sky pixels). Converges for rays that
// climb out of the medium; level/descending rays are treated as a long capped segment.
float Fog_OpticalDepthToInfinity(FExponentialHeightFogParams Fog, float3 StartPos, float3 RayDir)
{
    float Falloff  = Fog.HeightParams.x;
    float BaseH    = Fog.HeightParams.y;
    float BaseDens = Fog.InscatteringColor.w * exp(clamp(-Falloff * (StartPos.y - BaseH), -60.0, 60.0));
    float FVy      = Falloff * RayDir.y;

    if (FVy > 1e-5)
    {
        return BaseDens / FVy;
    }
    return Fog_OpticalDepth(Fog, StartPos, RayDir, 100000.0);
}

// Cornette-Shanks: a smoother, energy-normalized HG. Same `G` control, but the
// lobe isn't as much of a needle.
float Fog_PhaseCornetteShanks(float CosTheta, float G)
{
    float G2  = G * G;
    float Num = 3.0 * (1.0 - G2) * (1.0 + CosTheta * CosTheta);
    float Den = 8.0 * PI * (2.0 + G2) * pow(max(1.0 + G2 - 2.0 * G * CosTheta, 1e-4), 1.5);
    return Num / Den;
}

// Two-lobe phase: forward Cornette-Shanks lobe blended with an isotropic floor so side-on shafts
// stay visible (a single high-G lobe goes near-black off-axis) while the sun-facing glow remains.
float Fog_Phase(float CosTheta, float G)
{
    const float Isotropic = 1.0 / (4.0 * PI);
    return lerp(Isotropic, Fog_PhaseCornetteShanks(CosTheta, G), 0.5);
}

// Density ramp for FogStartDistance: fog fades in over the half-start-distance after the start,
// instead of a hard front. Dist is distance from the camera along the ray.
float Fog_StartRamp(FExponentialHeightFogParams Fog, float Dist)
{
    float Start = Fog.HeightParams.z;
    return Start > 0.0 ? saturate((Dist - Start) / max(Start * 0.5, 1.0)) : 1.0;
}

// Fog albedo with the sun-facing directional inscatter tint: lerped toward DirectionalColor by a
// forward pow-lobe, ramped in past the directional start distance. CosSun is dot(sun travel
// direction, point-to-camera direction): +1 when looking straight at the sun.
float3 Fog_Albedo(FExponentialHeightFogParams Fog, float CosSun, float Dist)
{
    float DirStart = Fog.VolumetricParams.w;
    float Ramp     = DirStart > 0.0 ? saturate(Dist / DirStart - 1.0) : 1.0;
    float Lobe     = pow(saturate(CosSun), Fog.DirectionalColor.w) * Ramp;
    return lerp(Fog.InscatteringColor.rgb, Fog.DirectionalColor.rgb, Lobe);
}
