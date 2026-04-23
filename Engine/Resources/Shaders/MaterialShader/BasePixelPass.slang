#include "Includes/SceneGlobals.slang"

struct PSInput
{
    float4 Color        : COLOR;
    float4 NormalVS     : NORMAL_VS;
    float4 NormalWS     : NORMAL_WS;
    float4 FragPos      : FRAG_POS;
    float2 UV           : TEXCOORD0;
    uint InstanceIndex  : INSTANCE;
    nointerpolation uint MeshletIndex : MESHLET_INDEX;
    float4 Position     : SV_Position;
};

// Hash-to-color used by the meshlet debug visualization. Good spread from a
// few mix-and-shift rounds; produces stable per-meshlet hues across frames.
float3 MeshletDebugColor(uint Index)
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

// Discrete heatmap used for the light-complexity visualization. Bands chosen
// so the transition between step counts reads at a glance.
float3 LightComplexityColor(uint Count)
{
    if (Count == 0u)  return float3(0.0, 0.0, 0.0);
    if (Count <= 2u)  return float3(0.0, 0.25, 0.6);
    if (Count <= 4u)  return float3(0.0, 0.6, 0.3);
    if (Count <= 8u)  return float3(0.7, 0.8, 0.0);
    if (Count <= 16u) return float3(1.0, 0.5, 0.0);
    return float3(1.0, 0.0, 0.0);
}

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

    // Y faces: +Y's view basis ends up with view-up = -Z world (see
    // FViewVolume::SetView re-orthogonalization + point shadow face 2 config),
    // and -Y's view-up = +Z world. Negating sign(Dir.y) * Dir.z produces v that
    // matches the rendered Y axis for both top and bottom faces.
    float2 UVX = float2(-sign(Dir.x) * Dir.z,  Dir.y)                / AbsDir.x;
    float2 UVY = float2( Dir.x,                -sign(Dir.y) * Dir.z) / AbsDir.y;
    float2 UVZ = float2( sign(Dir.z) * Dir.x,   Dir.y)                / AbsDir.z;

    Result.UV = IsXMax * UVX + IsYMax * UVY + IsZMax * UVZ;
    Result.UV = Result.UV * 0.5 + 0.5;

    return Result;
}

float ComputeShadowBias(uint LightFlags, float3 Normal, float3 LightDir, int CascadeIndex)
{
    float NdotL       = saturate(dot(Normal, LightDir));
    float SlopeFactor = sqrt(max(1.0 - NdotL * NdotL, 0.0));

    if (LightFlags == LIGHT_FLAG_TYPE_DIRECTIONAL)
    {
        // Constant safety bias only — most slope-driven acne is removed by the
        // normal-offset push in NormalOffsetWS() below, so we don't need a
        // large slope term here (which would cause peter-panning).
        return 0.00015 + 0.0003 * SlopeFactor;
    }

    // Point/spot: CurrentDepth is DistToLight/Radius, linear [0,1].
    return 0.0015 + 0.004 * SlopeFactor;
}

// Normal-offset bias (Daniel Holbert, GDC 2011). Push the receiver position
// along its surface normal by ~one shadow texel before projecting into shadow
// space, scaled by (1 - NdotL) so it only kicks in at grazing angles where
// shadow acne lives.

// Returns the offset in world space; caller adds it to the fragment position.
float3 NormalOffsetWS(float3 Normal, float3 LightDir, int CascadeIndex)
{
    float NdotL       = saturate(dot(Normal, LightDir));
    float OffsetScale = saturate(1.0 - NdotL);

    // World-space size of one shadow texel for this cascade. Cascade ortho
    // width is 2 * Radius and resolution is GCSM_RESOLUTION.
    float Radius    = uLightData[0].CascadeRadii[CascadeIndex];
    float TexelSize = (2.0 * Radius) / float(GCSM_RESOLUTION);

    // 1.4 ≈ sqrt(2) — push by one diagonal texel at maximum slope, which is
    // the minimum that reliably clears self-intersection from depth-map
    // quantization. Larger values cause visible peter-panning at contact.
    return Normal * (TexelSize * 1.4 * OffsetScale);
}

// Tuning for PCSS (Percentage-Closer Soft Shadows).
//
// The visible gap near contact points isn't depth-bias — it's the fixed PCF
// footprint averaging in texels that sit on the occluder, which drags the
// shadow edge away from the mesh. PCSS fixes this by making the PCF kernel
// contact-aware: we first search for blockers in a small region, estimate
// the depth delta from receiver to average blocker, and scale the PCF
// radius with that delta. At contact the delta is ~zero, so the filter
// collapses to a near-point sample and the shadow edge stays glued to the
// mesh; at distance the penumbra opens up to a natural soft shadow.
#define PCSS_BLOCKER_SEARCH_TAPS   8
static const float PCSS_BlockerSearchRadius = 0.0035;  // UV radius of blocker search
static const float PCSS_LightSize           = 0.05;     // Tunable: larger = softer far shadows
static const float PCSS_MinFilterRadius     = 0.00001; // Near-point sample at contact
static const float PCSS_MaxFilterRadius     = 0.0018;  // Cap so far shadows don't over-blur
static const float PCSS_BlockerDepthBias    = 0.0004;  // Bias for blocker classification

// PCSS-filtered sample of a single directional cascade. Returns 1.0 if the
// fragment falls outside the cascade's projected UV / depth range so the
// caller can decide to fall back to another cascade (or to fully-lit).
float SampleDirectionalCascade(FLight Light, int CascadeIndex, float3 FragmentPos, float3 Normal, float3 LightDir, float Bias, float2 ScreenPos, out bool Inside)
{
    // Push the receiver off the surface along its normal before projecting,
    // sized to one shadow texel of the chosen cascade. This is what removes
    // the harsh self-shadow banding on detailed meshes — depth bias alone
    // cannot fix it without trading acne for peter-panning.
    float3 OffsetPos = FragmentPos + NormalOffsetWS(Normal, LightDir, CascadeIndex);

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

    // --- Stage 1: blocker search -------------------------------------------------
    //
    // Tap the shadow map through the *point* sampler at binding 16 so each
    // fetch is a raw texel depth (binding 7's comparison sampler would return
    // a PCF-filtered result, useless for classifying individual texels).
    // If no taps hit an occluder the fragment is fully lit; early-out skips
    // the PCF pass entirely.
    float BlockerDepthSum = 0.0;
    int   NumBlockers     = 0;

    // Outer cascades cover much more world-space per UV, so scale the search
    // radius a little to keep it roughly scene-proportional.
    float SearchRadius = PCSS_BlockerSearchRadius * (1.0 + 0.4 * float(CascadeIndex));

    [unroll]
    for (int s = 0; s < PCSS_BLOCKER_SEARCH_TAPS; ++s)
    {
        float2 Offset   = VogelDiskSample(s, PCSS_BLOCKER_SEARCH_TAPS, Angle) * SearchRadius;
        float2 SampleUV = clamp(ProjectionUV + Offset, 0.001, 0.999);
        float  Depth    = uShadowCascadePoint.SampleLevel(float3(SampleUV, float(CascadeIndex)), 0.0).r;

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

    // --- Stage 2: penumbra estimate ---------------------------------------------
    //
    // For an orthographic directional light the penumbra grows linearly with
    // receiver-to-blocker distance. Depths live in NDC-z [0,1] (a fraction of
    // the cascade's ortho range), so the delta is already normalized;
    // PCSS_LightSize absorbs the conversion back to UV space and the sun's
    // apparent size. At true contact the delta collapses and the filter
    // shrinks to PCSS_MinFilterRadius — a near-point sample that keeps the
    // shadow edge glued to the mesh.
    float DepthDelta   = max(CurrentDepth - AvgBlockerDepth, 0.0);
    float Penumbra     = DepthDelta * PCSS_LightSize;
    float FilterRadius = clamp(Penumbra, PCSS_MinFilterRadius, PCSS_MaxFilterRadius);

    // Low blocker coverage → partial occlusion → pull the filter in slightly.
    // Mostly a robustness tweak so penumbra doesn't pop when the blocker
    // count straddles the threshold across cascades.
    float BlockerCoverage = float(NumBlockers) / float(PCSS_BLOCKER_SEARCH_TAPS);
    FilterRadius *= lerp(0.75, 1.0, BlockerCoverage);

    // --- Stage 3: PCF with contact-hardened radius ------------------------------
    float Shadow = 0.0;
    [unroll]
    for (int i = 0; i < SHADOW_SAMPLE_COUNT; ++i)
    {
        float2 Offset    = VogelDiskSample(i, SHADOW_SAMPLE_COUNT, Angle + PI) * FilterRadius;
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
        float ShadowA = SampleDirectionalCascade(Light, CascadeIndex, FragmentPos, Normal, LightDirToFrag, Bias, ScreenPos, InsideA);

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
            float ShadowB = SampleDirectionalCascade(Light, CascadeIndex + 1, FragmentPos, Normal, LightDirToFrag, BiasB, ScreenPos, InsideB);

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
    FGPUInstance Inst = GetInstance(Input.InstanceIndex);

    uint EntityID           = Inst.EntityID;
    uint MaterialIndex      = Inst.MaterialIndex;
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
        float  Shadow       = ComputeShadowFactor(Light, WorldPosition, N, ViewPosition, Input.Position.xy, Inst.HasFlag(EInstanceFlags::ReceiveShadow));
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
            Shadow = ComputeShadowFactor(Light, WorldPosition, N, ViewPosition, Input.Position.xy, Inst.HasFlag(EInstanceFlags::ReceiveShadow));
        }

        Lo += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0, L, DistToLight) * Shadow;
    }

    float3 Ambient = GetAmbientLightColor() * GetAmbientLightIntensity() * AO;
    float3 Color   = Ambient + Lo + Material.Emissive;

#endif // UNLIT

    uint DebugMode = uSceneData.CullData.DebugMode;
    if (DebugMode != DEBUG_MODE_NONE)
    {
        switch (DebugMode)
        {
            case DEBUG_MODE_UNLIT:
                Color = Albedo + Material.Emissive;
                break;
            case DEBUG_MODE_MESHLETS:
                Color = MeshletDebugColor(Input.MeshletIndex);
                break;
            case DEBUG_MODE_WORLD_NORMAL:
                Color = WorldNormal * 0.5 + 0.5;
                break;
            case DEBUG_MODE_SHADING_NORMAL:
            {
            #ifdef UNLIT
                float3 DebugN = GetWorldNormal(WorldNormal, Input.UV, WorldPosition, Material.Normal);
            #else
                float3 DebugN = N;
            #endif
                Color = DebugN * 0.5 + 0.5;
                break;
            }
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
                Color = float3(frac(UV0), 0.0);
                break;
            case DEBUG_MODE_LIGHT_COMPLEXITY:
            {
                float DbgZN       = GetNearPlane();
                float DbgZF       = GetFarPlane();
                uint3 DbgGrid     = uSceneData.GridSize.xyz;
                uint2 DbgScreen   = uSceneData.ScreenSize.xy;
                uint  DbgZTile    = uint((log(abs(ViewPosition.z) / DbgZN) * float(DbgGrid.z)) / log(DbgZF / DbgZN));
                float2 DbgTile    = float2(DbgScreen) / float2(DbgGrid.xy);
                uint3 DbgTileIdx  = uint3(uint2(divide(Input.Position.xy, DbgTile)), DbgZTile);
                uint DbgIndex     = DbgTileIdx.x + (DbgTileIdx.y * DbgGrid.x) + (DbgTileIdx.z * DbgGrid.x * DbgGrid.y);
                Color = LightComplexityColor(uClusters[DbgIndex].Count);
                break;
            }
        }
    }

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


    Output.Picker   = EntityID;
    return Output;
}