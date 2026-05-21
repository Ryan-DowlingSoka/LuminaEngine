#include "Includes/SceneGlobals.slang"
#include "Includes/IBLRuntime.slang"
#include "Includes/ShadowSampling.slang"

// Keep in sync with FTerrainRenderParams in TerrainRenderTypes.h.
struct FTerrainRenderParams
{
    float2  OriginXZ;
    float   TileWorldSize;
    float   MaxHeight;

    int     Resolution;
    int     ChunkResolution;
    int     ChunksPerSide;
    int     LayerCount;

    float3  WorldOriginY;
    uint    EntityID;

    uint    MaterialIndex;
    int     MeshletsPerChunkSide;
    int     MeshletQuadSide;
    uint    _Pad0;
};

[[vk::binding(0, 2)]] Sampler2D                            uHeightmap;
[[vk::binding(1, 2)]] Sampler2D                            uTerrainNormal;
[[vk::binding(2, 2)]] Sampler2DArray                       uLayerWeights;

// Same push-constant block as TerrainBaseVertexPass.slang. Materialised on
// each access; Slang CSEs repeated reads within one function.
struct FTerrainPushConstants
{
    ConstBufferPointer<FTerrainRenderParams> Params;
};
[[vk::push_constant]] FTerrainPushConstants PC;

struct PSInput
{
    float4 FragColor                : COLOR;
    float4 NormalVS                 : NORMAL_VS;
    float4 NormalWS                 : NORMAL_WS;
    float4 TangentWS                : TANGENT_WS;
    float4 FragPos                  : FRAG_POS;
    float2 UV                       : TEXCOORD0;
    float2 HeightUV                 : HEIGHT_UV;

    nointerpolation uint EntityID      : ENTITY_ID;
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;
    // Stable per-meshlet ID, hashed to a color for the meshlet debug view.
    nointerpolation uint MeshletIndex  : MESHLET_INDEX;

    float4 Position                 : SV_Position;
};

struct PSOutput
{
    float4 Color    : SV_Target0;
    uint   Picker   : SV_Target1;
};

// ============================================================================
//  PBR helpers (kept in sync with BasePixelPass.slang)
// ============================================================================

float DistributionGGX(float3 N, float3 H, float Roughness)
{
    float A     = Roughness * Roughness;
    float A2    = A * A;
    float NdotH = max(dot(N, H), 0.0);
    float Denom = (NdotH * NdotH * (A2 - 1.0) + 1.0);
    return A2 / (PI * Denom * Denom);
}

float GeometrySchlickGGX(float NdotV, float Roughness)
{
    float R = Roughness + 1.0;
    float K = (R * R) / 8.0;
    return NdotV / (NdotV * (1.0 - K) + K);
}

float GeometrySmith(float3 N, float3 V, float3 L, float Roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, Roughness) * GeometrySchlickGGX(NdotL, Roughness);
}

float3 FresnelSchlick(float CosTheta, float3 F0)
{
    float x = saturate(1.0 - CosTheta);
    float x2 = x * x;
    return F0 + (1.0 - F0) * (x2 * x2 * x);
}

// Vertex-tangent TBN; mirrors BasePixelPass::GetWorldNormal. The terrain VS
// emits a smooth analytic tangent derived from the heightfield, so per-pixel
// we just Gram-Schmidt it against the interpolated normal. Replaces a
// screen-space-derivative TBN that went piecewise-flat per triangle and
// produced visible lighting cut-offs along triangle edges.
float3 GetWorldNormal(float3 FragNormal, float4 FragTangent, float3 TangentSpaceNormal)
{
    float3 N = normalize(FragNormal);

    float3 T = FragTangent.xyz - N * dot(FragTangent.xyz, N);
    float TLen2 = dot(T, T);
    if (TLen2 < 1e-8)
    {
        return N;
    }
    T *= rsqrt(TLen2);

    float3 B = cross(N, T) * FragTangent.w;

    float3x3 TBN = float3x3(T, B, N);
    return normalize(mul(TangentSpaceNormal, TBN));
}

float ComputeShadowBias(uint LightFlags, float3 Normal, float3 LightDir, int CascadeIndex)
{
    float NdotL       = saturate(dot(Normal, LightDir));
    float SlopeFactor = sqrt(max(1.0 - NdotL * NdotL, 0.0));

    if (HasFlag(LightFlags, ELightFlags::Directional))
    {
        // Constant safety bias only; NormalOffsetWS below handles the
        // slope-driven acne, so a large slope term would only cause peter-panning.
        // ShadowParams.y is the per-light constant depth bias.
        return 0.00015 + 0.0003 * SlopeFactor + uLightData[0].ShadowParams.y;
    }

    return 0.0015 + 0.004 * SlopeFactor;
}

// See BasePixelPass.slang::NormalOffsetWS for the full rationale.
float3 NormalOffsetWS(float3 Normal, float3 LightDir, int CascadeIndex)
{
    float NdotL       = saturate(dot(Normal, LightDir));
    float OffsetScale = saturate(1.0 - NdotL);
    float Radius      = uLightData[0].CascadeRadii[CascadeIndex];
    float Resolution  = max(uLightData[0].CascadeResolutions[CascadeIndex], 1.0);
    float TexelSize   = (2.0 * Radius) / Resolution;
    // ShadowParams.x scales the normal-offset bias per-light (1.0 = default).
    return Normal * (TexelSize * 1.4 * OffsetScale * uLightData[0].ShadowParams.x);
}

// Per-cascade UV -> shared atlas UV. Mirrors BasePixelPass.slang::CascadeAtlasUV.
float2 TerrainCascadeAtlasUV(int ShadowDataIndex, int CascadeIndex, float2 CascadeUV)
{
    FLightShadow Tile = GetShadowTile(ShadowDataIndex, CascadeIndex);
    return CascadeUV * Tile.AtlasUVScale + Tile.AtlasUVOffset;
}

// Mirrors BasePixelPass.slang PCSS so terrain matches mesh shadow behavior.
#define PCSS_BLOCKER_SEARCH_TAPS   8
static const float PCSS_BlockerSearchRadius = 0.0035;
static const float PCSS_LightSize           = 0.05;
static const float PCSS_MinFilterRadius     = 0.00001;
static const float PCSS_MaxFilterRadius     = 0.0018;
static const float PCSS_BlockerDepthBias    = 0.0004;

float SampleDirectionalCascade(FLight Light, int CascadeIndex, float3 FragmentPos, float3 Normal, float3 LightDir, float Bias, float2 ScreenPos, out bool Inside)
{
    float3 OffsetPos    = FragmentPos + NormalOffsetWS(Normal, LightDir, CascadeIndex);
    float4x4 CascadeVP  = GetShadowViewProjection(Light.ShadowDataIndex, CascadeIndex);
    float4 ShadowCoord  = mul(CascadeVP, float4(OffsetPos, 1.0));
    float3 ProjCoords   = ShadowCoord.xyz / max(ShadowCoord.w, 1e-6);
    float2 ProjectionUV = ProjCoords.xy * 0.5 + 0.5;
    float  CurrentDepth = ProjCoords.z;

    Inside = !(any(ProjectionUV < 0.0) || any(ProjectionUV > 1.0) ||
               CurrentDepth < 0.0 || CurrentDepth > 1.0);
    if (!Inside)
    {
        return 1.0;
    }

    float IGN   = frac(52.9829189 * frac(dot(ScreenPos, float2(0.06711056, 0.00583715))));
    float Angle = IGN * TWO_PI;

    float RefDepth = CurrentDepth - Bias;

    float CascadeScale = 1.0 + 1.5 * float(CascadeIndex);

    float BlockerDepthSum = 0.0;
    int   NumBlockers     = 0;

    float SearchRadius = PCSS_BlockerSearchRadius * CascadeScale;

    [unroll]
    for (int s = 0; s < PCSS_BLOCKER_SEARCH_TAPS; ++s)
    {
        float2 Offset   = VogelDiskSample(s, PCSS_BLOCKER_SEARCH_TAPS, Angle) * SearchRadius;
        float2 SampleUV = clamp(ProjectionUV + Offset, 0.001, 0.999);
        float2 AtlasUV  = TerrainCascadeAtlasUV(Light.ShadowDataIndex, CascadeIndex, SampleUV);
        float  Depth    = uShadowCascadePoint.SampleLevel(AtlasUV, 0.0).r;

        if (Depth < CurrentDepth - PCSS_BlockerDepthBias)
        {
            BlockerDepthSum += Depth;
            NumBlockers     += 1;
        }
    }

    if (NumBlockers == 0)
    {
        return 1.0;
    }

    float AvgBlockerDepth = BlockerDepthSum / float(NumBlockers);
    float DepthDelta      = max(CurrentDepth - AvgBlockerDepth, 0.0);
    // Per-light penumbra softness (PCSS light size); PCSS_LightSize is the default.
    float Penumbra        = DepthDelta * uLightData[0].ShadowParams.z;
    float MaxRadius       = PCSS_MaxFilterRadius * CascadeScale;
    float FilterRadius    = clamp(Penumbra, PCSS_MinFilterRadius, MaxRadius);

    float BlockerCoverage = float(NumBlockers) / float(PCSS_BLOCKER_SEARCH_TAPS);
    FilterRadius *= lerp(0.75, 1.0, BlockerCoverage);

    // Per-light runtime tap count (ShadowParams2.y); uniform, so no wavefront
    // divergence from the dynamic loop. Matches BasePixelPass.
    int SampleCount = clamp(int(uLightData[0].ShadowParams2.y), 1, SHADOW_MAX_SAMPLE_COUNT);
    float Shadow = 0.0;
    [loop]
    for (int i = 0; i < SampleCount; ++i)
    {
        float2 Offset    = VogelDiskSample(i, SampleCount, Angle + PI) * FilterRadius;
        float2 CascadeUV = clamp(ProjectionUV + Offset, 0.001, 0.999);
        float2 AtlasUV   = TerrainCascadeAtlasUV(Light.ShadowDataIndex, CascadeIndex, CascadeUV);
        Shadow += uShadowCascade.SampleCmp(AtlasUV, RefDepth);
    }
    return Shadow / float(SampleCount);
}

float ComputeTerrainShadowFactor(FLight Light, float3 FragmentPos, float3 Normal, float3 ViewPosition, float2 ScreenPos, bool ReceiveShadow)
{
    if (!ReceiveShadow)
    {
        return 1.0;
    }

    bool IsDirectionalLight = HasFlag(Light.Flags, ELightFlags::Directional);
    if (!IsDirectionalLight)
    {
        return 1.0;
    }

    // bCastShadows = false on the sun => ProcessDirectionalLight leaves
    // ShadowDataIndex as INDEX_NONE. Treat as fully lit.
    if (Light.ShadowDataIndex == INDEX_NONE)
    {
        return 1.0;
    }

    int CascadeIndex = 0;
    for (int i = 0; i < NUM_CASCADES; ++i)
    {
        if (ViewPosition.z > uLightData[0].CascadeSplits[i])
            CascadeIndex = i + 1;
    }
    if (CascadeIndex >= NUM_CASCADES)
        return 1.0;

    float3 LightDirToFrag = -normalize(Light.Direction);
    float  Bias           = ComputeShadowBias(Light.Flags, Normal, LightDirToFrag, CascadeIndex);

    bool  InsideA;
    float ShadowA = SampleDirectionalCascade(Light, CascadeIndex, FragmentPos, Normal, LightDirToFrag, Bias, ScreenPos, InsideA);

    float CascadeNear = (CascadeIndex == 0)
        ? 0.0
        : uLightData[0].CascadeSplits[CascadeIndex - 1];
    float CascadeFar  = uLightData[0].CascadeSplits[CascadeIndex];
    float CascadeLen  = max(CascadeFar - CascadeNear, 1e-4);
    float LocalT      = saturate((ViewPosition.z - CascadeNear) / CascadeLen);

    // Cross-fade fraction is per-light (ShadowParams.w); BlendStart = 1 - blend.
    const float BlendStart = saturate(1.0 - uLightData[0].ShadowParams.w);
    if (LocalT > BlendStart && CascadeIndex + 1 < NUM_CASCADES)
    {
        float BlendT  = saturate((LocalT - BlendStart) / (1.0 - BlendStart));
        float BiasB   = ComputeShadowBias(Light.Flags, Normal, LightDirToFrag, CascadeIndex + 1);

        bool  InsideB;
        float ShadowB = SampleDirectionalCascade(Light, CascadeIndex + 1, FragmentPos, Normal, LightDirToFrag, BiasB, ScreenPos, InsideB);

        if (InsideB)
        {
            return lerp(ShadowA, ShadowB, BlendT);
        }
    }

    // Final cascade: fade to fully lit over the outer slice so shadows
    // dissolve at ShadowMaxDistance instead of popping off.
    if (CascadeIndex + 1 == NUM_CASCADES)
    {
        // Distance-fade fraction is per-light (ShadowParams2.x); start = 1 - fade.
        const float FarFadeStart = saturate(1.0 - uLightData[0].ShadowParams2.x);
        if (LocalT > FarFadeStart)
        {
            float FadeT = saturate((LocalT - FarFadeStart) / (1.0 - FarFadeStart));
            return lerp(ShadowA, 1.0, FadeT);
        }
    }

    return ShadowA;
}

int FindSunShadowDataIndexTerrain()
{
    if (uLightData[0].bHasSun == 0u)
    {
        return INDEX_NONE;
    }
    uint Count = GetNumLights();
    for (uint i = 0; i < Count; ++i)
    {
        FLight L = GetLightAt(i);
        if (HasFlag(L.Flags, ELightFlags::Directional) && L.ShadowDataIndex != INDEX_NONE)
        {
            return L.ShadowDataIndex;
        }
    }
    return INDEX_NONE;
}

// Mirror of CascadeDebugColor in BasePixelPass.slang so terrain and meshes
// share the same Shadow Cascades view-mode visualization.
float3 CascadeDebugColorTerrain(float3 FragmentPos, float3 ViewPosition, float3 ShadedBase)
{
    int SunShadowIdx = FindSunShadowDataIndexTerrain();
    if (SunShadowIdx == INDEX_NONE)
    {
        return float3(0.08, 0.08, 0.08);
    }

    int CascadeIndex = 0;
    for (int i = 0; i < NUM_CASCADES; ++i)
    {
        if (ViewPosition.z > uLightData[0].CascadeSplits[i])
            CascadeIndex = i + 1;
    }
    if (CascadeIndex >= NUM_CASCADES)
    {
        return float3(0.18, 0.18, 0.18);
    }

    float4x4 CascadeVP   = GetShadowViewProjection(SunShadowIdx, CascadeIndex);
    float4   ShadowCoord = mul(CascadeVP, float4(FragmentPos, 1.0));
    float3   ProjCoords  = ShadowCoord.xyz / max(ShadowCoord.w, 1e-6);
    float2   ProjUV      = ProjCoords.xy * 0.5 + 0.5;
    bool     Inside      = !(any(ProjUV < 0.0) || any(ProjUV > 1.0) ||
                              ProjCoords.z < 0.0 || ProjCoords.z > 1.0);

    const float3 CascadeTints[3] = {
        float3(1.00, 0.25, 0.25),
        float3(0.25, 1.00, 0.30),
        float3(0.30, 0.45, 1.00),
    };
    float3 Tint = CascadeTints[CascadeIndex];

    float Luma = saturate(dot(ShadedBase, float3(0.299, 0.587, 0.114)));
    float3 Color = lerp(Tint * 0.4, Tint, 0.5 + 0.5 * Luma);

    if (!Inside)
    {
        Color *= 0.35;
    }
    return Color;
}


float3 EvaluateLightContribution(FLight Light, float3 Position, float3 N, float3 V, float3 Albedo, float Roughness, float Metallic, float3 F0, float3 PrecomputedL, float PrecomputedDist)
{
    bool bSpotLight   = HasFlag(Light.Flags, ELightFlags::Spot);
    bool bDirectional = HasFlag(Light.Flags, ELightFlags::Directional);

    float3 L            = float3(0.0);
    float  Attenuation  = 1.0;
    float  Falloff      = 1.0;
    float4 LightColor   = GetLightColor(Light);

    if (bDirectional)
    {
        L = normalize(Light.Direction.xyz);
    }
    else
    {
        float Distance = PrecomputedDist;
        L              = PrecomputedL;
        Attenuation    = 1.0 / (Distance * Distance);

        float DistRatio = Distance / Light.Radius;
        Attenuation    *= 1.0 - smoothstep(clamp(Light.Falloff, 0.0, 1.0), 1.0, DistRatio);

        if (bSpotLight)
        {
            float3 LightDir = normalize(Light.Direction.xyz);
            float  CosTheta = dot(LightDir, L);
            Falloff = clamp((CosTheta - Light.Angles.y) / max(Light.Angles.x - Light.Angles.y, 0.001), 0.0, 1.0);
        }
    }

    float3 Radiance     = LightColor.rgb * Light.Intensity * Attenuation * Falloff;
    float3 H            = normalize(V + L);

    float  NDF          = DistributionGGX(N, H, Roughness);
    float  G            = GeometrySmith(N, V, L, Roughness);
    float3 F            = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 Spec         = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    float3 kD           = (1.0 - F) * (1.0 - Metallic);
    float  NdotL        = max(dot(N, L), 0.0);

    return (kD * Albedo / PI + Spec) * Radiance * NdotL;
}


int   GetTerrainLayerCount()                       { return PC.Params.get().LayerCount; }
float GetTerrainLayerWeight(float2 HeightUV, int i)
{
    return uLayerWeights.Sample(float3(HeightUV, float(i))).r;
}

// Hash-to-color used by the meshlet debug visualization. Mirrors
// MeshletDebugColor in BasePixelPass.slang so meshes and terrain colorize
// identically under the same debug mode.
float3 TerrainMeshletDebugColor(uint Index)
{
    uint H = Index * 2654435761u;
    H ^= H >> 16;
    H *= 2246822519u;
    H ^= H >> 13;
    H *= 3266489917u;
    H ^= H >> 16;

    return float3(
        float( H         & 0xFFu) / 255.0,
        float((H >>  8)  & 0xFFu) / 255.0,
        float((H >> 16)  & 0xFFu) / 255.0
    );
}

// Layer 0 implicitly fills any unpainted weight so brushes blend smoothly
// over a base layer. Without this, a tiny painted weight at a brush edge
// would renormalise to 100%, producing hard stroke boundaries.
float4 GetTerrainLayerWeights4(float2 HeightUV)
{
    float4 W = float4(0.0, 0.0, 0.0, 0.0);
    int Count = min(PC.Params.get().LayerCount, 4);
    [unroll] for (int i = 0; i < 4; ++i)
    {
        if (i < Count)
        {
            W[i] = uLayerWeights.Sample(float3(HeightUV, float(i))).r;
        }
    }
    float Total = W.x + W.y + W.z + W.w;
    if (Total < 1.0)
    {
        W[0] += 1.0 - Total;
        Total = 1.0;
    }
    W /= Total;
    return W;
}

// ============================================================================
//  Main
// ============================================================================

[shader("fragment")]
PSOutput FragmentMain(PSInput Input)
{
    uint   EntityID        = Input.EntityID;
    uint   MaterialIndex   = Input.MaterialIndex;
    float3 WorldNormal     = normalize(Input.NormalWS.xyz);
    float3 ViewPosition    = Input.FragPos.xyz;
    float3 WorldPosition   = mul(GetInverseCameraView(), float4(Input.FragPos.xyz, 1.0)).xyz;
    float2 UV0             = Input.UV;
    float2 HeightUV        = Input.HeightUV;
    float4 VertexColor     = Input.FragColor;

    $MATERIAL_INPUTS

    float3 Albedo    = Material.Diffuse;
    float3 Normal    = Material.Normal;
    float  Alpha     = Material.Opacity;
    float  AO        = Material.AmbientOcclusion;
    float  Roughness = Material.Roughness;
    float  Metallic  = Material.Metallic;

    uint MatFlags = uMaterialUniforms[MaterialIndex].Flags;
    if (HasFlag(MatFlags, MatFlag_Masked))
    {
        float ClipValue = uMaterialUniforms[MaterialIndex].OpacityClipValue;
        if (Alpha < ClipValue)
        {
            discard;
        }
    }

#ifdef UNLIT

    float3 Color = Albedo + Material.Emissive;

#else

    float3 N  = GetWorldNormal(WorldNormal, Input.TangentWS, Normal);
    float3 V  = normalize(GetCameraPosition() - WorldPosition);

    static const float3 F0_DIELECTRIC = float3(0.04);
    float3 F0 = lerp(F0_DIELECTRIC, Albedo, Metallic);

    float  ZNear      = GetNearPlane();
    float  ZFar       = GetFarPlane();
    uint3  GridSize   = uSceneData.GridSize.xyz;
    uint2  ScreenSize = uSceneData.ScreenSize.xy;

    // Clamp tile index: huge terrain quads lose enough interpolation precision
    // at the edges that ViewPosition.z lands just below ZNear and ZTile wraps.
    float  ZForTile  = max(abs(ViewPosition.z), ZNear);
    float  ZTileF    = (log(ZForTile / ZNear) * float(GridSize.z)) / log(ZFar / ZNear);
    uint   ZTile     = (uint)clamp(ZTileF, 0.0, float(GridSize.z) - 1.0);
    float2 TileSize  = float2(ScreenSize) / float2(GridSize.xy);
    uint2  TileXY    = (uint2)clamp(divide(Input.Position.xy, TileSize),
                                    float2(0.0, 0.0),
                                    float2(GridSize.xy) - float2(1.0, 1.0));
    uint3  Tile      = uint3(TileXY, ZTile);
    uint   TileIndex = Tile.x + (Tile.y * GridSize.x) + (Tile.z * GridSize.x * GridSize.y);
    uint   LightCount = uClusters[TileIndex].Count;

    float3 Lo = float3(0.0);

    if (uLightData[0].bHasSun != 0)
    {
        FLight Light = GetLightAt(0);

        // The sun's contribution is scaled by max(dot(N, L), 0), so a pixel facing
        // away from the sun is fully self-shadowed regardless of the shadow map.
        // Skip the 24-48 tap cascade PCSS for those pixels (~half a typical scene).
        float SunNdotL = dot(N, normalize(Light.Direction.xyz));
        if (SunNdotL > 0.0)
        {
            float Shadow = ComputeTerrainShadowFactor(Light, WorldPosition, N, ViewPosition, Input.Position.xy, true);
            Lo += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0, float3(0.0), 0.0) * Shadow;
        }
    }

    // Mirror the cap LightCull enforces on writes.
    uint SafeLightCount = min(LightCount, (uint)LIGHTS_PER_CLUSTER);
    for (uint i = 0; i < SafeLightCount; ++i)
    {
        uint   LightIndex = uClusters[TileIndex].LightIndices[i];
        // Cluster contents can be stale/uninitialized right after a scene switch
        // (LightCull hasn't repopulated this froxel for the new light set yet).
        // An index past the live count reads beyond Lights[MAX_LIGHTS] -> MMU
        // fault in GetLightAt (SceneGlobals.slang). Skip out-of-range indices.
        if (LightIndex >= GetNumLights())
        {
            continue;
        }
        FLight Light      = GetLightAt(LightIndex);

        float3 LightToFrag = Light.Position - WorldPosition;
        float  DistToLight = length(LightToFrag);
        float3 L           = LightToFrag / max(DistToLight, 0.0001);

        // Cluster membership is conservative: a froxel can list a light whose sphere
        // overlaps it but doesn't reach this pixel, or that lights only its back face.
        // Both make EvaluateLightContribution return zero (NdotL clamp / falloff hits
        // 1.0 at the radius), so skip the BRDF for them.
        if (dot(N, L) <= 0.0 || DistToLight >= Light.Radius)
        {
            continue;
        }

        Lo += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0, L, DistToLight);
    }

    // IBL ambient: see BasePixelPass for the rationale; mirrored here so
    // terrain reflects the same sky cube and stays visually consistent
    // with mesh surfaces under the same lighting.
    float3 IBLAmbient = EvaluateIBLAmbient(N, V, Albedo, F0, Roughness, Metallic);
    float3 Ambient    = IBLAmbient * GetAmbientLightIntensity() * AO;
    float3 Color      = Ambient + Lo + Material.Emissive;

#endif // UNLIT

    // Debug overrides. Kept in sync with BasePixelPass.slang so toggling a
    // mode lights up meshes and terrain together; the meshlet hue uses the
    // same hash so two meshlet IDs colliding (rare) at least look identical
    // across mesh and terrain surfaces.
    uint DebugMode = uSceneData.CullData.DebugMode;
    if (DebugMode != DEBUG_MODE_NONE)
    {
        switch (DebugMode)
        {
            case DEBUG_MODE_UNLIT:
                Color = Albedo + Material.Emissive;
                break;
            case DEBUG_MODE_MESHLETS:
                Color = TerrainMeshletDebugColor(Input.MeshletIndex);
                break;
            case DEBUG_MODE_WORLD_NORMAL:
                Color = normalize(Input.NormalWS.xyz) * 0.5 + 0.5;
                break;
            case DEBUG_MODE_BASE_COLOR:
                Color = Albedo;
                break;
            case DEBUG_MODE_ROUGHNESS:
                Color = float3(Roughness, Roughness, Roughness);
                break;
            case DEBUG_MODE_METALLIC:
                Color = float3(Metallic, Metallic, Metallic);
                break;
            case DEBUG_MODE_AMBIENT_OCCLUSION:
                Color = float3(AO, AO, AO);
                break;
            case DEBUG_MODE_EMISSIVE:
                Color = Material.Emissive;
                break;
            case DEBUG_MODE_UV:
                Color = float3(frac(Input.UV), 0.0);
                break;
            case DEBUG_MODE_LIGHT_COMPLEXITY:
            {
                uint DbgIndex = ComputeClusterIndex(ViewPosition.z, Input.Position.xy);
                Color = LightComplexityColor(uClusters[DbgIndex].Count);
                break;
            }
            case DEBUG_MODE_CLUSTER_GRID:
            {
                uint DbgIndex = ComputeClusterIndex(ViewPosition.z, Input.Position.xy);
                Color = ClusterDebugColor(DbgIndex, uClusters[DbgIndex].Count);
                break;
            }
            case DEBUG_MODE_SHADOW_CASCADES:
                Color = CascadeDebugColorTerrain(WorldPosition, ViewPosition, Color);
                break;
            default:
                break;
        }
    }

    PSOutput Output;
    Output.Color  = float4(Color, Alpha);
    Output.Picker = EntityID;
    return Output;
}
