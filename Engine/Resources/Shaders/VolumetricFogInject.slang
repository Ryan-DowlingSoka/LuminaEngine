#include "Includes/SceneGlobals.slang"
#include "Includes/ShadowSampling.slang"
#include "Includes/Fog.slang"
#include "Includes/Froxel.slang"

// Froxel volumetric fog -- inject pass. For each froxel in the camera-frustum-aligned
// 3D volume: compute the exponential-height-fog density (extinction) and the in-scattered
// radiance from the sun + opted-in point/spot lights (each shadow-sampled, Henyey-
// Greenstein phase). Writes (in-scatter rgb, extinction) into uFroxelScatter; the
// integrate pass then accumulates front-to-back.

#define FROXEL_MAX_LOCAL_LIGHTS 16

struct FPushConstants
{
    uint3  GridSize;
    float  NearPlane;
    float  FogRange;
    uint   bSunVolumetric;
    uint   NumLocalVolumetric;
    float  Time;
    uint   LocalLightIndices[FROXEL_MAX_LOCAL_LIGHTS];
};
[[vk::push_constant]] FPushConstants PC;

[[vk::binding(0, 2)]] RWTexture3D<float4>                          uFroxelScatter;
[[vk::binding(1, 2)]] ConstantBuffer<FExponentialHeightFogParams>  uFog;

float PhaseHG(float CosTheta, float G)
{
    float G2 = G * G;
    float Denom = 1.0 + G2 - 2.0 * G * CosTheta;
    return (1.0 - G2) / (4.0 * PI * pow(max(Denom, 1e-4), 1.5));
}

// Froxel center -> world position. Slices are view-Z planes (exponential), matching
// the clustered light grid; jitter breaks the slice banding before the integrate.
float3 FroxelToWorld(uint3 Coord, float3 Jitter)
{
    float2 UV  = (float2(Coord.xy) + Jitter.xy) / float2(PC.GridSize.xy);
    float2 NDC = UV * 2.0 - 1.0;

    float4 ViewH   = mul(GetInverseCameraProjection(), float4(NDC, 1.0, 1.0));
    float3 ViewRay = ViewH.xyz / ViewH.w;            // far-plane point in view space
    float  Dist    = Froxel_SliceDistance(float(Coord.z) + Jitter.z, float(PC.GridSize.z), PC.NearPlane, PC.FogRange);
    float3 ViewPos = ViewRay * (Dist / max(-ViewRay.z, 1e-4));  // view Z = -Dist
    return mul(GetInverseCameraView(), float4(ViewPos, 1.0)).xyz;
}

// Pick the first (smallest, highest-res) cascade whose ortho frustum actually
// contains the froxel. Containment-based selection is independent of the view-Z
// sign convention -- the split-distance test the lit pass uses assumes a positive
// view distance the froxel path doesn't have.
float SampleSunShadow(FLight Light, float3 WorldPos)
{
    if (Light.ShadowDataIndex == INDEX_NONE)
    {
        return 1.0;
    }

    [unroll]
    for (int c = 0; c < NUM_CASCADES; ++c)
    {
        float4x4 VP = GetShadowViewProjection(Light.ShadowDataIndex, c);
        float4   SC = mul(VP, float4(WorldPos, 1.0));
        float3   P  = SC.xyz / max(SC.w, 1e-6);
        float2   UV = P.xy * 0.5 + 0.5;

        if (all(UV >= 0.0) && all(UV <= 1.0) && P.z >= 0.0 && P.z <= 1.0)
        {
            FLightShadow Tile = GetShadowTile(Light.ShadowDataIndex, c);
            float2 AUV = UV * Tile.AtlasUVScale + Tile.AtlasUVOffset;
            return uShadowCascade.SampleCmpLevelZero(AUV, P.z - 0.0005);
        }
    }
    return 1.0;
}

float SampleSpotShadow(FLight Light, float3 WorldPos)
{
    if (Light.ShadowDataIndex == INDEX_NONE)
    {
        return 1.0;
    }
    FLightShadow Tile = GetShadowTile(Light.ShadowDataIndex, 0);
    float4x4 VP = GetShadowViewProjection(Light.ShadowDataIndex, 0);
    float4   SC = mul(VP, float4(WorldPos, 1.0));
    float3   P  = SC.xyz / max(SC.w, 1e-6);
    if (P.z < 0.0 || P.z > 1.0)
    {
        return 1.0;
    }
    float2 UV  = saturate(P.xy * 0.5 + 0.5);
    float2 AUV = Tile.AtlasUVOffset + UV * Tile.AtlasUVScale;
    return uShadowAtlas.SampleCmpLevelZero(AUV, P.z - 0.0005);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ComputeMain(uint3 DispatchID : SV_DispatchThreadID)
{
    if (any(DispatchID >= PC.GridSize))
    {
        return;
    }

    // Sample the slice center. The apply-pass screen-space depth dither + 128 Z slices
    // keep banding down without per-frame jitter (which needs temporal accumulation).
    float3 WorldPos = FroxelToWorld(DispatchID, float3(0.5, 0.5, 0.5));
    float3 ToCam    = normalize(GetCameraPosition() - WorldPos);

    // Extinction from the exp-height profile (clamped so it can't explode below base).
    float  Density   = clamp(Fog_DensityAt(uFog, WorldPos), 0.0, uFog.InscatteringColor.w);
    float  SigmaT    = Density;
    float3 SigmaS    = uFog.InscatteringColor.rgb * Density;   // scattering = albedo * density
    float  G         = uFog.VolumetricParams.y;
    float  Intensity = uFog.VolumetricParams.x;

    // Ambient (skylight) in-scatter -- UNSHADOWED, so fog inside a shadow still catches
    // sky light and reads as dim hazy fog instead of a black void. Cheap stand-in for
    // multiple scattering; raise the environment's AmbientIntensity for brighter shadows.
    float3 InScatter = GetAmbientLightColor() * GetAmbientLightIntensity();

    // Sun.
    if (PC.bSunVolumetric != 0)
    {
        FLight Sun     = GetLightAt(0);
        float3 SunDir  = normalize(Sun.Direction);
        float  Phase   = PhaseHG(dot(SunDir, ToCam), G);
        float  Shadow  = SampleSunShadow(Sun, WorldPos);
        float3 SunColor = UnpackColor(Sun.Color).rgb;
        InScatter += SunColor * Sun.Intensity * Phase * Shadow * Sun.VolumetricIntensity;
    }

    // Opted-in local point/spot lights.
    uint NumLocal = min(PC.NumLocalVolumetric, (uint)FROXEL_MAX_LOCAL_LIGHTS);
    for (uint li = 0; li < NumLocal; ++li)
    {
        FLight L    = GetLightAt(PC.LocalLightIndices[li]);
        float3 ToL  = L.Position - WorldPos;
        float  Dist = length(ToL);
        if (Dist > L.Radius)
        {
            continue;
        }
        float3 Ldir  = ToL / max(Dist, 1e-4);
        float  Atten = 1.0 / max(Dist * Dist, 1e-4);
        float  DistR = Dist / L.Radius;
        Atten *= 1.0 - smoothstep(clamp(L.Falloff, 0.0, 1.0), 1.0, DistR);

        float Cone = 1.0;
        if (HasFlag(L.Flags, ELightFlags::Spot))
        {
            float3 SF    = normalize(L.Direction);
            float  CosAt = dot(SF, -Ldir);
            Cone = saturate((CosAt - L.Angles.y) / max(L.Angles.x - L.Angles.y, 1e-3));
            if (Cone <= 0.0)
            {
                continue;
            }
        }

        float Shadow = 1.0;
        if (L.ShadowDataIndex != INDEX_NONE && HasFlag(L.Flags, ELightFlags::Spot))
        {
            Shadow = SampleSpotShadow(L, WorldPos);
        }

        float  Phase = PhaseHG(dot(Ldir, ToCam), G);
        float3 LC    = UnpackColor(L.Color).rgb;
        InScatter += LC * L.Intensity * Atten * Cone * Phase * Shadow * L.VolumetricIntensity;
    }

    float3 Scatter = SigmaS * InScatter * Intensity;
    uFroxelScatter[DispatchID] = float4(Scatter, SigmaT);
}
