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

float3 GetWorldNormal(float3 FragNormal, float2 UV, float3 FragPos, float3 TangentSpaceNormal)
{
    float3 Q1  = ddx(FragPos);
    float3 Q2  = ddy(FragPos);
    float2 St1 = ddx(UV);
    float2 St2 = ddy(UV);

    // See the matching GetWorldNormal in BasePixelPass.slang for the full
    // rationale: normalize T/B before Gram-Schmidt so close-range tiny
    // derivatives don't trip the "TBN collapsed, fall back" guard, and
    // drive B from St1.x / St2.x so it follows the actual UV layout's
    // dP/dv (cross(N, T) only matches dP/dv when T x B = N).
    float3 T = Q1 * St2.y - Q2 * St1.y;
    float3 B = Q2 * St1.x - Q1 * St2.x;

    float TGeomLen2 = dot(T, T);
    float BGeomLen2 = dot(B, B);
    if (TGeomLen2 < 1e-20 || BGeomLen2 < 1e-20)
    {
        return FragNormal;
    }
    T *= rsqrt(TGeomLen2);
    B *= rsqrt(BGeomLen2);

    T = T - FragNormal * dot(T, FragNormal);
    float TLen2 = dot(T, T);
    if (TLen2 < 1e-6)
    {
        return FragNormal;
    }
    T *= rsqrt(TLen2);

    B = B - FragNormal * dot(B, FragNormal) - T * dot(B, T);
    float BLen2 = dot(B, B);
    if (BLen2 < 1e-6)
    {
        return FragNormal;
    }
    B *= rsqrt(BLen2);

    float3x3 TBN = float3x3(T, B, FragNormal);
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
        return 0.00015 + 0.0003 * SlopeFactor;
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
    return Normal * (TexelSize * 1.4 * OffsetScale);
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
    float Penumbra        = DepthDelta * PCSS_LightSize;
    float MaxRadius       = PCSS_MaxFilterRadius * CascadeScale;
    float FilterRadius    = clamp(Penumbra, PCSS_MinFilterRadius, MaxRadius);

    float BlockerCoverage = float(NumBlockers) / float(PCSS_BLOCKER_SEARCH_TAPS);
    FilterRadius *= lerp(0.75, 1.0, BlockerCoverage);

    float Shadow = 0.0;
    [unroll]
    for (int i = 0; i < SHADOW_SAMPLE_COUNT; ++i)
    {
        float2 Offset    = VogelDiskSample(i, SHADOW_SAMPLE_COUNT, Angle + PI) * FilterRadius;
        float2 CascadeUV = clamp(ProjectionUV + Offset, 0.001, 0.999);
        float2 AtlasUV   = TerrainCascadeAtlasUV(Light.ShadowDataIndex, CascadeIndex, CascadeUV);
        Shadow += uShadowCascade.SampleCmp(AtlasUV, RefDepth);
    }
    return Shadow / float(SHADOW_SAMPLE_COUNT);
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

    const float BlendStart = 0.80;
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

    return ShadowA;
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

    float3 N  = GetWorldNormal(WorldNormal, UV0, WorldPosition, Normal);
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
        FLight Light  = GetLightAt(0);
        float  Shadow = ComputeTerrainShadowFactor(Light, WorldPosition, N, ViewPosition, Input.Position.xy, true);
        Lo += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0, float3(0.0), 0.0) * Shadow;
    }

    // Mirror the cap LightCull enforces on writes.
    uint SafeLightCount = min(LightCount, (uint)LIGHTS_PER_CLUSTER);
    for (uint i = 0; i < SafeLightCount; ++i)
    {
        uint   LightIndex = uClusters[TileIndex].LightIndices[i];
        FLight Light      = GetLightAt(LightIndex);

        float3 LightToFrag = Light.Position - WorldPosition;
        float  DistToLight = length(LightToFrag);
        float3 L           = LightToFrag / max(DistToLight, 0.0001);

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
            default:
                break;
        }
    }

    PSOutput Output;
    Output.Color  = float4(Color, Alpha);
    Output.Picker = EntityID;
    return Output;
}
