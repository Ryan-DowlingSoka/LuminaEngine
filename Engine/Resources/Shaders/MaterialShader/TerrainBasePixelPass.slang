#include "Includes/SceneGlobals.slang"

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
    uint    _Pad0;
    uint    _Pad1;
    uint    _Pad2;
};

[[vk::binding(0, 2)]] Sampler2D                            uHeightmap;
[[vk::binding(1, 2)]] Sampler2D                            uTerrainNormal;
[[vk::binding(2, 2)]] Sampler2DArray                       uLayerWeights;
[[vk::binding(3, 2)]] ConstantBuffer<FTerrainRenderParams> TerrainParams;

struct PSInput
{
    float4 FragColor                : COLOR;
    float4 NormalVS                 : NORMAL_VS;
    float4 NormalWS                 : NORMAL_WS;
    float4 FragPos                  : FRAG_POS;
    float2 UV                       : TEXCOORD0;
    float2 HeightUV                 : HEIGHT_UV;

    nointerpolation uint EntityID   : ENTITY_ID;
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;

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
    float3 Q1 = ddx(FragPos);
    float3 Q2 = ddy(FragPos);
    float2 St1 = ddx(UV);
    float2 St2 = ddy(UV);

    float3 T = normalize(Q1 * St2.y - Q2 * St1.y);
    float3 B = normalize(cross(FragNormal, T));

    float3x3 TBN = float3x3(T, B, FragNormal);

    return normalize(mul(TangentSpaceNormal, TBN));
}

float ComputeShadowBias(uint LightFlags, float3 Normal, float3 LightDir, int CascadeIndex)
{
    float NdotL       = saturate(dot(Normal, LightDir));
    float SlopeFactor = sqrt(max(1.0 - NdotL * NdotL, 0.0));

    if (LightFlags == LIGHT_FLAG_TYPE_DIRECTIONAL)
    {
        float Bias = 0.0005 + 0.0015 * SlopeFactor;
        Bias *= lerp(1.0, 4.0, float(CascadeIndex) / float(max(NUM_CASCADES - 1, 1)));
        return Bias;
    }

    return 0.0015 + 0.004 * SlopeFactor;
}

float SampleDirectionalCascade(FLight Light, int CascadeIndex, float3 FragmentPos, float Bias, float2 ScreenPos, out bool Inside)
{
    float4x4 CascadeVP  = GetShadowViewProjection(Light.ShadowDataIndex, CascadeIndex);
    float4 ShadowCoord  = mul(CascadeVP, float4(FragmentPos, 1.0));
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
    float FilterRadius = 0.0012 * (1.0 + 0.6 * float(CascadeIndex));

    float RefDepth = CurrentDepth - Bias;

    float Shadow = 0.0;
    for (int i = 0; i < SHADOW_SAMPLE_COUNT; i++)
    {
        float2 Offset    = VogelDiskSample(i, SHADOW_SAMPLE_COUNT, Angle) * FilterRadius;
        float2 CascadeUV = clamp(ProjectionUV + Offset, 0.001, 0.999);
        Shadow += uShadowCascade.SampleCmp(float3(CascadeUV, CascadeIndex), RefDepth);
    }
    return Shadow / float(SHADOW_SAMPLE_COUNT);
}

float ComputeTerrainShadowFactor(FLight Light, float3 FragmentPos, float3 Normal, float3 ViewPosition, float2 ScreenPos, bool ReceiveShadow)
{
    if (!ReceiveShadow)
    {
        return 1.0;
    }

    bool IsDirectionalLight = Light.Flags == LIGHT_FLAG_TYPE_DIRECTIONAL;
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
    float ShadowA = SampleDirectionalCascade(Light, CascadeIndex, FragmentPos, Bias, ScreenPos, InsideA);

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
        float ShadowB = SampleDirectionalCascade(Light, CascadeIndex + 1, FragmentPos, BiasB, ScreenPos, InsideB);

        if (InsideB)
        {
            return lerp(ShadowA, ShadowB, BlendT);
        }
    }

    return ShadowA;
}

float3 EvaluateLightContribution(FLight Light, float3 Position, float3 N, float3 V, float3 Albedo, float Roughness, float Metallic, float3 F0, float3 PrecomputedL, float PrecomputedDist)
{
    bool bSpotLight   = Light.Flags == LIGHT_FLAG_TYPE_SPOT;
    bool bDirectional = Light.Flags == LIGHT_FLAG_TYPE_DIRECTIONAL;

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
    float3 F            = FresnelSchlick(max(dot(H, V), 0.5), F0);

    float3 Spec         = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    float3 kD           = (1.0 - F) * (1.0 - Metallic);
    float  NdotL        = max(dot(N, L), 0.0);

    return (kD * Albedo / PI + Spec) * Radiance * NdotL;
}


int   GetTerrainLayerCount()                       { return TerrainParams.LayerCount; }
float GetTerrainLayerWeight(float2 HeightUV, int i)
{
    return uLayerWeights.Sample(float3(HeightUV, float(i))).r;
}

// Layer 0 implicitly fills any unpainted weight so brushes blend smoothly
// over a base layer. Without this, a tiny painted weight at a brush edge
// would renormalise to 100%, producing hard stroke boundaries.
float4 GetTerrainLayerWeights4(float2 HeightUV)
{
    float4 W = float4(0.0, 0.0, 0.0, 0.0);
    int Count = min(TerrainParams.LayerCount, 4);
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

    uint   ZTile     = uint((log(abs(ViewPosition.z) / ZNear) * float(GridSize.z)) / log(ZFar / ZNear));
    float2 TileSize  = float2(ScreenSize) / float2(GridSize.xy);
    uint3  Tile      = uint3(uint2(divide(Input.Position.xy, TileSize)), ZTile);
    uint   TileIndex = Tile.x + (Tile.y * GridSize.x) + (Tile.z * GridSize.x * GridSize.y);
    uint   LightCount = uClusters[TileIndex].Count;

    float3 Lo = float3(0.0);

    if (uLightData[0].bHasSun != 0)
    {
        FLight Light  = GetLightAt(0);
        float  Shadow = ComputeTerrainShadowFactor(Light, WorldPosition, N, ViewPosition, Input.Position.xy, true);
        Lo += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0, float3(0.0), 0.0) * Shadow;
    }

    for (uint i = 0; i < LightCount; ++i)
    {
        uint   LightIndex = uClusters[TileIndex].LightIndices[i];
        FLight Light      = GetLightAt(LightIndex);

        float3 LightToFrag = Light.Position - WorldPosition;
        float  DistToLight = length(LightToFrag);
        float3 L           = LightToFrag / max(DistToLight, 0.0001);

        Lo += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0, L, DistToLight);
    }

    float3 Ambient = GetAmbientLightColor() * GetAmbientLightIntensity() * AO;
    float3 Color   = Ambient + Lo + Material.Emissive;

#endif // UNLIT

    PSOutput Output;
    Output.Color  = float4(Color, Alpha);
    Output.Picker = EntityID;
    return Output;
}
