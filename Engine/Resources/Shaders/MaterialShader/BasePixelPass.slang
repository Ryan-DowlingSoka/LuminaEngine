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
    float4  Color   : SV_Target0;
    uint    Picker  : SV_Target1;
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
    return F0 + (1.0 - F0) * pow(1.0 - CosTheta, 5.0);
}

float3 GetWorldNormal(float3 FragNormal, float2 UV, float3 FragPos, float3 TangentSpaceNormal)
{
    float3 N = normalize(FragNormal);
    float3 Q1 = ddx(FragPos);
    float3 Q2 = ddy(FragPos);
    float2 St1 = ddx(UV);
    float2 St2 = ddy(UV);

    float3 T = normalize(Q1 * St2.y - Q2 * St1.y);
    float3 B = normalize(cross(N, T));

    float3x3 TBN = float3x3(T, B, N);

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

float ComputeShadowFactor(FLight Light, float3 FragmentPos, float Bias, float3 ViewPosition, bool ReceiveShadow)
{
    if (!ReceiveShadow)
    {
        return 1.0;
    }

    bool IsPointLight       = Light.Flags == LIGHT_FLAG_TYPE_POINT;
    bool IsSpotLight        = Light.Flags == LIGHT_FLAG_TYPE_SPOT;
    bool IsDirectionalLight = Light.Flags == LIGHT_FLAG_TYPE_DIRECTIONAL;

    int ViewProjectionIndex = 0;
    if (IsDirectionalLight)
    {
        for (int i = 0; i < NUM_CASCADES; ++i)
        {
            if (ViewPosition.z > uLightData[0].CascadeSplits[i])
                ViewProjectionIndex = i + 1;
        }
        ViewProjectionIndex = clamp(ViewProjectionIndex, 0, 3);
    }

    float4 ShadowCoord  = mul(Light.ViewProjection[ViewProjectionIndex], float4(FragmentPos, 1.0));
    float3 ProjCoords   = ShadowCoord.xyz / ShadowCoord.w;
    ProjCoords          = ProjCoords * 0.5 + 0.5;
    float2 ProjectionUV = ProjCoords.xy;

    float3 L              = FragmentPos - Light.Position;
    float  DistToLight    = length(L);
    float  CurrentDepth   = DistToLight / max(Light.Radius, 1.0);
    float3 Dir            = normalize(L);

    FCubemapCoord Coord = DirectionToCubemapCoord(Dir);
    Coord.UV   = lerp(Coord.UV, ProjectionUV, float(IsSpotLight));
    Coord.Face = int(lerp(float(Coord.Face), 0.0, float(IsSpotLight)));

    FLightShadow ShadowTile = Light.Shadow[Coord.Face];
    float Angle = frac(sin(dot(FragmentPos.xy, float2(12.9898, 78.233))) * 43758.5453) * TWO_PI;

    float BlockerDepth = 0.0;
    if (IsDirectionalLight)
    {
        CurrentDepth = (ShadowCoord.xyz / ShadowCoord.w).z;
        BlockerDepth = uShadowCascade.Sample(float3(ProjectionUV, ViewProjectionIndex)).r;
    }
    else
    {
        float2 ClampedUV = clamp(Coord.UV, 0.001, 0.999);
        float2 SampleUV  = ShadowTile.AtlasUVOffset + (ClampedUV * ShadowTile.AtlasUVScale);
        BlockerDepth     = uShadowAtlas.Sample(float3(SampleUV, ShadowTile.ShadowMapLayer)).r;
    }

    float DepthDiff    = abs(CurrentDepth - BlockerDepth);
    float FilterRadius = lerp(0.0, 0.0009, pow(DepthDiff, 0.025));

    float Shadow = 0.0;
    for (int i = 0; i < SHADOW_SAMPLE_COUNT; i++)
    {
        float2 Offset = VogelDiskSample(i, SHADOW_SAMPLE_COUNT, Angle) * FilterRadius;

        if (IsDirectionalLight)
        {
            float2 CascadeUV = ProjectionUV + Offset;
            float  Depth     = uShadowCascade.Sample(float3(CascadeUV, ViewProjectionIndex)).r;
            Shadow += Depth < CurrentDepth ? 0.0 : 1.0;
        }
        else
        {
            float2 ClampedUV = clamp(Coord.UV + (Offset / ShadowTile.AtlasUVScale), 0.001, 0.999);
            float2 SampleUV  = ShadowTile.AtlasUVOffset + (ClampedUV * ShadowTile.AtlasUVScale);
            float  Depth     = uShadowAtlas.Sample(float3(SampleUV, ShadowTile.ShadowMapLayer)).r;
            Shadow += step(CurrentDepth - Bias, Depth);
        }
    }

    return Shadow / float(SHADOW_SAMPLE_COUNT);
}

float ComputeShadowBias(FLight Light, float3 Normal, float3 LightDir, float CascadeIndex)
{
    float ConstBias     = 0.000025;
    float SlopeBias     = 0.00001;
    float DiffuseFactor = clamp(dot(Normal, LightDir), 0.0, 1.0);
    float Bias          = lerp(ConstBias, SlopeBias, DiffuseFactor);

    if (HasFlag(Light.Flags, LIGHT_FLAG_TYPE_DIRECTIONAL))
        Bias *= lerp(1.0, 2.0, CascadeIndex / 3.0);

    return 0.0;
}


float3 EvaluateLightContribution(FLight Light, float3 Position, float3 N, float3 V, float3 Albedo, float Roughness, float Metallic, float3 F0)
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
        float3 LightToFrag = Light.Position - Position;
        float  Distance    = length(LightToFrag);
        L                  = LightToFrag / Distance;
        Attenuation        = 1.0 / (Distance * Distance);

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
    float3 ViewNormal       = normalize(Input.NormalVS.xyz);
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

    // Masked alpha clip - discard fragments below the clip threshold
    uint MatFlags = uMaterialUniforms[MaterialIndex].Flags;
    if (HasFlag(MatFlags, MatFlag_Masked))
    {
        float ClipValue = uMaterialUniforms[MaterialIndex].OpacityClipValue;
        if (Alpha < ClipValue)
        {
            discard;
        }
    }

    // Unlit path - skip all lighting, output diffuse directly
    if (HasFlag(MatFlags, MatFlag_Unlit))
    {
        float3 Color = Albedo + Material.Emissive;

        PSOutput Output;
        Output.Color    = float4(Color, Alpha);
        Output.Picker   = InstanceData.EntityID;
        return Output;
    }

    float3 Position  = mul(GetInverseCameraView(), float4(ViewPosition, 1.0)).xyz;

    float3 N  = GetWorldNormal(WorldNormal, Input.UV, WorldPosition, Normal);
    float3 V  = normalize(GetCameraPosition() - WorldPosition);

    float3 IOR = float3(0.5);
    float3 F0  = abs((1.0 - IOR) / (1.0 + IOR));
    F0         = F0 * F0;
    F0         = lerp(F0, Albedo, Metallic);

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
        float  Shadow       = ComputeShadowFactor(Light, WorldPosition, 0.0005, ViewPosition, InstanceData.HasFlag(EInstanceFlags::ReceiveShadow));
        Lo                  += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0) * Shadow;
    }

    for (uint i = 0; i < LightCount; ++i)
    {
        uint   LightIndex = uClusters[TileIndex].LightIndices[i];
        FLight Light      = GetLightAt(LightIndex);

        float Shadow = 1.0;
        if (Light.Shadow[0].ShadowMapIndex != INDEX_NONE)
        {
            float3 L                    = WorldPosition - Light.Position;
            float3 DirectionToLight     = normalize(L);
            float  DistToLight          = length(L);
            float  AttenuationFactor    = 1.0 / (DistToLight * DistToLight);
            float  BiasScale            = lerp(0.1, 1.0, AttenuationFactor);
            float Bias                  = ComputeShadowBias(Light, N, DirectionToLight, 0) * BiasScale;
            Shadow                      = ComputeShadowFactor(Light, WorldPosition, Bias, ViewPosition, InstanceData.HasFlag(EInstanceFlags::ReceiveShadow));
        }

        Lo += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0) * Shadow;
    }

    float3 Ambient = GetAmbientLightColor() * GetAmbientLightIntensity() * AO;
    float3 Color   = Ambient + Lo + Material.Emissive;

    PSOutput Output;
    Output.Color    = float4(Color, Alpha);
    Output.Picker   = InstanceData.EntityID;
    return Output;
}