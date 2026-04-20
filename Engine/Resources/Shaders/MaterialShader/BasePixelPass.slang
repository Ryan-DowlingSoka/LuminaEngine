#include "Includes/SceneGlobals.slang"

struct PSInput
{
    float4 Color        : COLOR;
    float4 NormalVS     : NORMAL_VS;
    float4 NormalWS     : NORMAL_WS;
    float4 FragPos      : FRAG_POS;
    float2 UV           : TEXCOORD0;
    uint InstanceIndex  : INSTANCE;
    float4 Position     : SV_Position;
};

struct PSOutput
{   
    #ifdef TRANSLUCENT

    float4  Accum       : SV_Target0;
    float   Revealage   : SV_Target1;
    uint    Picker      : SV_Target2;

    #else

    float4 Color        : SV_Target0;
    uint Picker         : SV_Target1;

    #endif

};

struct FCubemapCoord
{
    int Face;
    float2 UV;
};

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

FCubemapCoord DirectionToCubemapCoord(float3 Dir)
{
    FCubemapCoord Result;

    float3 AbsDir = abs(Dir);

    float IsXMax = step(AbsDir.y, AbsDir.x) * step(AbsDir.z, AbsDir.x);
    float IsYMax = step(AbsDir.x, AbsDir.y) * step(AbsDir.z, AbsDir.y);
    float IsZMax = 1.0 - IsXMax - IsYMax;

    float3 FaceIndex = float3(
        lerp(1.0, 0.0, step(0.0, Dir.x)),
        lerp(3.0, 2.0, step(0.0, Dir.y)),
        lerp(5.0, 4.0, step(0.0, Dir.z))
    );

    Result.Face = int(IsXMax * FaceIndex.x + IsYMax * FaceIndex.y + IsZMax * FaceIndex.z);
    bool bDownFace = Result.Face == 3;

    float2 UVX = float2(-sign(Dir.x) * Dir.z,  Dir.y)              / AbsDir.x;
    float2 UVY = float2( Dir.x,                 sign(Dir.y) * Dir.z) / AbsDir.y;
    float2 UVZ = float2( sign(Dir.z) * Dir.x,   Dir.y)              / AbsDir.z;

    Result.UV   = IsXMax * UVX + IsYMax * UVY + IsZMax * UVZ;
    Result.UV.y = lerp(Result.UV.y, -Result.UV.y, float(bDownFace));
    Result.UV   = Result.UV * 0.5 + 0.5;

    return Result;
}

float ComputeShadowBias(uint LightFlags, float3 Normal, float3 LightDir, int CascadeIndex)
{
    float NdotL       = saturate(dot(Normal, LightDir));
    float SlopeFactor = sqrt(max(1.0 - NdotL * NdotL, 0.0));

    if (LightFlags == LIGHT_FLAG_TYPE_DIRECTIONAL)
    {
        // Bias is in NDC-z space [0,1].
        float Bias = 0.0005 + 0.0015 * SlopeFactor;
        Bias *= lerp(1.0, 4.0, float(CascadeIndex) / float(max(NUM_CASCADES - 1, 1)));
        return Bias;
    }

    // Point/spot: CurrentDepth is DistToLight/Radius, linear [0,1].
    return 0.0015 + 0.004 * SlopeFactor;
}

// PCF-filtered sample of a single directional cascade. Returns 1.0 if the
// fragment falls outside the cascade's projected UV / depth range so the
// caller can decide to fall back to another cascade (or to fully-lit).
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

    // PCF footprint scales with cascade size so outer cascades get a wider
    // filter in world space. Counteracts the visible resolution cliff at the
    // boundary between the penultimate and last cascade.
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

float ComputeShadowFactor(FLight Light, float3 FragmentPos, float3 Normal, float3 ViewPosition, float2 ScreenPos, bool ReceiveShadow)
{
    if (!ReceiveShadow)
    {
        return 1.0;
    }

    bool IsSpotLight        = Light.Flags == LIGHT_FLAG_TYPE_SPOT;
    bool IsDirectionalLight = Light.Flags == LIGHT_FLAG_TYPE_DIRECTIONAL;

    if (IsDirectionalLight)
    {
        // Pick current cascade based on view-space Z against the split table.
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

        // Blend into the next cascade near the split boundary so the
        // transition between sharp/near and blurry/far cascades is smooth
        // rather than a hard seam.
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

    // ----- Spot / point lights (atlas-based) -----
    float3 LightDirToFrag = normalize(Light.Position - FragmentPos);
    float  Bias           = ComputeShadowBias(Light.Flags, Normal, LightDirToFrag, 0);

    float4x4 SpotVP     = GetShadowViewProjection(Light.ShadowDataIndex, 0);
    float4 ShadowCoord  = mul(SpotVP, float4(FragmentPos, 1.0));
    float3 ProjCoords   = ShadowCoord.xyz / max(ShadowCoord.w, 1e-6);
    float2 ProjectionUV = ProjCoords.xy * 0.5 + 0.5;

    float3 L            = FragmentPos - Light.Position;
    float  DistToLight  = length(L);
    float  CurrentDepth = DistToLight / max(Light.Radius, 1.0);
    float3 Dir          = normalize(L);

    FCubemapCoord Coord = DirectionToCubemapCoord(Dir);
    Coord.UV   = lerp(Coord.UV, ProjectionUV, float(IsSpotLight));
    Coord.Face = int(lerp(float(Coord.Face), 0.0, float(IsSpotLight)));

    FLightShadow ShadowTile = GetShadowTile(Light.ShadowDataIndex, Coord.Face);

    if (IsSpotLight)
    {
        if (any(ProjectionUV < 0.0) || any(ProjectionUV > 1.0))
            return 1.0;
    }

    float IGN   = frac(52.9829189 * frac(dot(ScreenPos, float2(0.06711056, 0.00583715))));
    float Angle = IGN * TWO_PI;
    float FilterRadius = 0.002;

    float RefDepth = CurrentDepth - Bias;

    float Shadow = 0.0;
    for (int i = 0; i < SHADOW_SAMPLE_COUNT; i++)
    {
        float2 Offset     = VogelDiskSample(i, SHADOW_SAMPLE_COUNT, Angle) * FilterRadius;
        float2 TileOffset = Offset / max(ShadowTile.AtlasUVScale, float2(1e-6));
        float2 ClampedUV  = clamp(Coord.UV + TileOffset, 0.001, 0.999);
        float2 SampleUV   = ShadowTile.AtlasUVOffset + (ClampedUV * ShadowTile.AtlasUVScale);
        Shadow += uShadowAtlas.SampleCmp(float3(SampleUV, ShadowTile.ShadowMapLayer), RefDepth);
    }

    return Shadow / float(SHADOW_SAMPLE_COUNT);
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


[earlydepthstencil]
[shader("fragment")]
PSOutput FragmentMain(PSInput Input)
{
    FGPUInstance InstanceData = GetInstanceData(Input.InstanceIndex);

    uint EntityID           = InstanceData.EntityID;
    uint MaterialIndex      = InstanceData.MaterialIndex;
    float3 WorldNormal      = normalize(Input.NormalWS.xyz);
    float3 ViewPosition     = Input.FragPos.xyz;
    float3 WorldPosition    = mul(GetInverseCameraView(), float4(Input.FragPos.xyz, 1.0)).xyz;
    float2 UV0              = Input.UV;
    float4 VertexColor      = Input.Color;

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

    float3 Color    = float3(Albedo + Material.Emissive);

#else

    float3 N  = GetWorldNormal(WorldNormal, Input.UV, WorldPosition, Normal);
    float3 V  = normalize(GetCameraPosition() - WorldPosition);

    static const float3 F0_DIELECTRIC = float3(0.04);
    float3 F0 = lerp(F0_DIELECTRIC, Albedo, Metallic);

    float  ZNear      = GetNearPlane();
    float  ZFar       = GetFarPlane();
    uint3  GridSize   = uSceneData.GridSize.xyz;
    uint2  ScreenSize = uSceneData.ScreenSize.xy;

    uint   ZTile        = uint((log(abs(ViewPosition.z) / ZNear) * float(GridSize.z)) / log(ZFar / ZNear));
    float2 TileSize     = float2(ScreenSize) / float2(GridSize.xy);
    uint3  Tile          = uint3(uint2(divide(Input.Position.xy, TileSize)), ZTile);
    uint   TileIndex    = Tile.x + (Tile.y * GridSize.x) + (Tile.z * GridSize.x * GridSize.y);
    uint   LightCount   = uClusters[TileIndex].Count;

    float3 Lo = float3(0.0);

    if (uLightData[0].bHasSun != 0)
    {
        FLight Light        = GetLightAt(0);
        float  Shadow       = ComputeShadowFactor(Light, WorldPosition, N, ViewPosition, Input.Position.xy, InstanceData.HasFlag(EInstanceFlags::ReceiveShadow));
        Lo                  += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0, float3(0.0), 0.0) * Shadow;
    }

    for (uint i = 0; i < LightCount; ++i)
    {
        uint   LightIndex = uClusters[TileIndex].LightIndices[i];
        FLight Light      = GetLightAt(LightIndex);

        float3 LightToFrag = Light.Position - WorldPosition;
        float  DistToLight = length(LightToFrag);
        float3 L           = LightToFrag / max(DistToLight, 0.0001);

        float Shadow = 1.0;
        if (Light.ShadowDataIndex != INDEX_NONE)
        {
            Shadow = ComputeShadowFactor(Light, WorldPosition, N, ViewPosition, Input.Position.xy, InstanceData.HasFlag(EInstanceFlags::ReceiveShadow));
        }

        Lo += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0, L, DistToLight) * Shadow;
    }

    float3 Ambient = GetAmbientLightColor() * GetAmbientLightIntensity() * AO;
    float3 Color   = Ambient + Lo + Material.Emissive;

#endif // UNLIT

    PSOutput Output;

#ifdef TRANSLUCENT

    float AlphaFactor = min(1.0, Alpha * 10.0) + 0.01;
    float DepthFactor = 1.0 - Input.Position.z * 0.9;
    float Weight = clamp(AlphaFactor * AlphaFactor * AlphaFactor * 1e8 * DepthFactor * DepthFactor * DepthFactor, 1e-2, 3e3);
    Output.Accum = float4(Color.rgb * Alpha, Alpha) * Weight;
    Output.Revealage = Alpha;

#else

    Output.Color    = float4(Color, Alpha);

#endif // TRANSLUCENT


    Output.Picker   = InstanceData.EntityID;
    return Output;
}