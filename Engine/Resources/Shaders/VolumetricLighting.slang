#include "Includes/SceneGlobals.slang"
#include "Includes/ShadowSampling.slang"

// Half-resolution screen-space ray-marched volumetric lighting. Writes a
// scattering buffer that the bilateral upsample pass composites into HDR.
//
// Optimizations vs. the original full-res pass:
//   * Half-res march: 1/4 the pixels.
//   * 16 steps with IGN jitter (was 32). Combined with the upsample's depth
//     guidance the banding is below the tone-mapped noise floor.
//   * Sun phase + radiance hoisted out of the per-step loop -- both are
//     constant per pixel for a directional light.
//   * Inner light loop iterates a CPU-built list of volumetric-only light
//     indices instead of every active light with a flag check.
//
// Lights opt in by setting ELightFlags::Volumetric and a per-light
// VolumetricIntensity. Shadows reuse the cascade / spot / point atlas.

// Hard cap on the number of *non-sun* volumetric lights we accept per frame.
// 16 fits in a single 64B push-constant tail; in practice scenes only run a
// handful. Lights past this cap silently drop out of the pass; the CPU side
// clamps NumLocalVolumetric so the loop stays in bounds.
#define MAX_VOLUMETRIC_LOCAL_LIGHTS 16

struct FPushConstants
{
    uint    NumSteps;
    float   MaxDistance;
    float   Density;
    float   Anisotropy;
    uint2   ScreenSize;
    float2  InvScreenSize;
    float   Time;
    uint    bSunVolumetric;
    uint    NumLocalVolumetric;
    uint    _Pad0;
    uint    LocalLightIndices[MAX_VOLUMETRIC_LOCAL_LIGHTS];
};

[[vk::push_constant]] FPushConstants PC;

[[vk::binding(0, 2)]] Sampler2D uSceneDepth;

struct PSInput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

float PhaseHG(float CosTheta, float G)
{
    float G2 = G * G;
    float Denom = 1.0 + G2 - 2.0 * G * CosTheta;
    return (1.0 - G2) / (4.0 * PI * pow(max(Denom, 1e-4), 1.5));
}

float IGN(float2 ScreenPos, float Time)
{
    float2 P = ScreenPos + float2(5.588238, 5.588238) * frac(Time);
    return frac(52.9829189 * frac(dot(P, float2(0.06711056, 0.00583715))));
}

float SampleDirectionalShadowAtPoint(FLight Light, float3 WorldPos, float3 ViewPos)
{
    if (Light.ShadowDataIndex == INDEX_NONE)
    {
        return 1.0;
    }

    int CascadeIndex = 0;
    [unroll]
    for (int i = 0; i < NUM_CASCADES; ++i)
    {
        if (ViewPos.z > uLightData[0].CascadeSplits[i])
        {
            CascadeIndex = i + 1;
        }
    }
    if (CascadeIndex >= NUM_CASCADES)
    {
        return 1.0;
    }

    float4x4 CascadeVP   = GetShadowViewProjection(Light.ShadowDataIndex, CascadeIndex);
    float4   ShadowCoord = mul(CascadeVP, float4(WorldPos, 1.0));
    float3   Proj        = ShadowCoord.xyz / max(ShadowCoord.w, 1e-6);
    float2   ProjUV      = Proj.xy * 0.5 + 0.5;
    float    Ref         = Proj.z - 0.0005;

    if (any(ProjUV < 0.0) || any(ProjUV > 1.0) || Proj.z < 0.0 || Proj.z > 1.0)
    {
        return 1.0;
    }

    FLightShadow Tile = GetShadowTile(Light.ShadowDataIndex, CascadeIndex);
    float2 AtlasUV = ProjUV * Tile.AtlasUVScale + Tile.AtlasUVOffset;
    return uShadowCascade.SampleCmpLevelZero(AtlasUV, Ref);
}

float SampleSpotShadowAtPoint(FLight Light, float3 WorldPos)
{
    if (Light.ShadowDataIndex == INDEX_NONE)
    {
        return 1.0;
    }

    FLightShadow Tile = GetShadowTile(Light.ShadowDataIndex, 0);

    float4x4 ShadowVP    = GetShadowViewProjection(Light.ShadowDataIndex, 0);
    float4   ShadowCoord = mul(ShadowVP, float4(WorldPos, 1.0));
    float3   Proj        = ShadowCoord.xyz / max(ShadowCoord.w, 1e-6);
    float2   ProjUV      = Proj.xy * 0.5 + 0.5;
    float    Ref         = Proj.z - 0.0005;

    if (Proj.z < 0.0 || Proj.z > 1.0)
    {
        return 1.0;
    }

    float2 SafeUV  = saturate(ProjUV);
    float2 AtlasUV = Tile.AtlasUVOffset + SafeUV * Tile.AtlasUVScale;
    return uShadowAtlas.SampleCmpLevelZero(AtlasUV, Ref);
}

// Scattering at one march sample for a *local* (point/spot) light. Sun is
// handled inline in main() so its phase + radiance can be hoisted out of
// the step loop.
float3 EvaluateLocalScatter(FLight Light, float3 P, float3 ViewDirToCam)
{
    float3 LightToP = P - Light.Position;
    float  Dist     = length(LightToP);
    if (Dist > Light.Radius)
    {
        return float3(0.0);
    }

    float3 LightDirAtP = LightToP / max(Dist, 1e-4);
    float3 LtoCam      = -LightDirAtP;

    float Atten     = 1.0 / max(Dist * Dist, 1e-4);
    float DistRatio = Dist / Light.Radius;
    Atten *= 1.0 - smoothstep(clamp(Light.Falloff, 0.0, 1.0), 1.0, DistRatio);

    float Cone = 1.0;
    if (HasFlag(Light.Flags, ELightFlags::Spot))
    {
        float3 SpotForward = normalize(Light.Direction);
        float CosAtP = dot(SpotForward, LtoCam);
        Cone = saturate((CosAtP - Light.Angles.y) / max(Light.Angles.x - Light.Angles.y, 1e-3));
        if (Cone <= 0.0)
        {
            return float3(0.0);
        }
    }

    float CosScatter = dot(LtoCam, ViewDirToCam);
    float Phase      = PhaseHG(CosScatter, PC.Anisotropy);
    float3 LightColor = UnpackColor(Light.Color).rgb;

    return LightColor * Light.Intensity * Atten * Cone * Phase;
}

[shader("fragment")]
float4 FragmentMain(PSInput Input) : SV_Target0
{
    float2 UV = Input.UV;

    // Point-sample full-res depth at the half-res pixel center. The bilateral
    // upsample reads the four corresponding full-res depths to reconstruct
    // edges on the way out.
    float Depth = uSceneDepth.SampleLevel(UV, 0.0).r;

    float3 CameraPos = GetCameraPosition();
    float4x4 InvProj = GetInverseCameraProjection();
    float4x4 InvView = GetInverseCameraView();

    float SurfaceDepth = Depth > 0.0 ? Depth : 1e-4;
    float3 SurfaceWorld = ReconstructWorldPos(UV, SurfaceDepth, InvProj, InvView);

    float3 RayVec = SurfaceWorld - CameraPos;
    float  RayLen = length(RayVec);
    if (RayLen < 1e-3)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    float3 RayDir = RayVec / RayLen;
    float  March  = min(RayLen, PC.MaxDistance);

    int   Steps    = max(1, int(PC.NumSteps));
    float StepSize = March / float(Steps);

    float Jitter = IGN(Input.Position.xy, PC.Time);
    float t      = StepSize * Jitter;

    float3 ViewDirToCam = -RayDir;
    float3 Scattered    = float3(0.0);

    // Sun pre-compute. Phase + intensity + color are constant per pixel for
    // a directional light; only the cascaded shadow sample varies per step.
    bool   bSun            = (PC.bSunVolumetric != 0);
    float3 SunRadiance     = float3(0.0);
    float  SunVolIntensity = 0.0;
    FLight Sun;
    if (bSun)
    {
        Sun = GetLightAt(0);
        float3 SunDir   = normalize(Sun.Direction);
        float  CosPhase = dot(SunDir, ViewDirToCam);
        float  Phase    = PhaseHG(CosPhase, PC.Anisotropy);
        float3 SunColor = UnpackColor(Sun.Color).rgb;
        SunRadiance     = SunColor * Sun.Intensity * Phase;
        SunVolIntensity = Sun.VolumetricIntensity;
    }

    uint NumLocal = min(PC.NumLocalVolumetric, (uint)MAX_VOLUMETRIC_LOCAL_LIGHTS);

    [loop]
    for (int s = 0; s < Steps; ++s)
    {
        float3 P = CameraPos + RayDir * t;

        if (bSun)
        {
            float3 ViewPos = WorldToView(P);
            float  Shadow  = SampleDirectionalShadowAtPoint(Sun, P, ViewPos);
            if (Shadow > 0.0)
            {
                Scattered += SunRadiance * Shadow * SunVolIntensity;
            }
        }

        for (uint li = 0; li < NumLocal; ++li)
        {
            uint   Index = PC.LocalLightIndices[li];
            FLight Light = GetLightAt(Index);

            float3 LightToP = P - Light.Position;
            float  DistSq   = dot(LightToP, LightToP);
            if (DistSq > Light.Radius * Light.Radius)
            {
                continue;
            }

            float Shadow = 1.0;
            if (Light.ShadowDataIndex != INDEX_NONE && HasFlag(Light.Flags, ELightFlags::Spot))
            {
                Shadow = SampleSpotShadowAtPoint(Light, P);
                if (Shadow <= 0.0)
                {
                    continue;
                }
            }

            Scattered += EvaluateLocalScatter(Light, P, ViewDirToCam) * Shadow * Light.VolumetricIntensity;
        }

        t += StepSize;
    }

    float3 Result = Scattered * StepSize * PC.Density;
    return float4(Result, 0.0);
}
