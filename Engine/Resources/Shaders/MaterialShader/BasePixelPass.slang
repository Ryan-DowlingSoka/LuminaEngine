#include "Includes/SceneGlobals.slang"
#include "Includes/IBLRuntime.slang"

struct PSInput
{
    float4 Color        : COLOR;
    float4 NormalVS     : NORMAL_VS;
    float4 NormalWS     : NORMAL_WS;
    float4 TangentWS    : TANGENT_WS;
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

// Vertex-tangent TBN. The tangent is generated at import time by
// MikkTSpace (matches Substance / Marmoset / UE / Unity), interpolated
// per-pixel by the rasterizer, then Gram-Schmidt'd against the
// interpolated shading normal here so non-uniform model scale and
// linear-interpolation-induced skew don't tilt the basis. The
// handedness sign on TangentWS.w preserves the bitangent direction
// across mirrored UV islands.
//
// Replaces a screen-space-derivative TBN: ddx/ddy of linearly
// interpolated attributes are constant within a triangle, which made
// every triangle's tangent frame piecewise-flat. With normal mapping
// applied, that flatness traced visible faceting along triangle edges
// and a hard line at every UV mirror seam.
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

    float2 UVX = float2( sign(Dir.x) * Dir.z,  Dir.y)                / AbsDir.x;
    float2 UVY = float2(-Dir.x,                -sign(Dir.y) * Dir.z) / AbsDir.y;
    float2 UVZ = float2(-sign(Dir.z) * Dir.x,   Dir.y)                / AbsDir.z;

    Result.UV = IsXMax * UVX + IsYMax * UVY + IsZMax * UVZ;
    Result.UV = Result.UV * 0.5 + 0.5;

    return Result;
}

float ComputeShadowBias(uint LightFlags, float3 Normal, float3 LightDir, int CascadeIndex)
{
    float NdotL       = saturate(dot(Normal, LightDir));
    float SlopeFactor = sqrt(max(1.0 - NdotL * NdotL, 0.0));

    if (HasFlag(LightFlags, ELightFlags::Directional))
    {
        // Constant safety bias only; NormalOffsetWS() below removes most
        // slope-driven acne, so a large slope term would only cause peter-panning.
        return 0.00015 + 0.0003 * SlopeFactor;
    }

    // Point/spot: RefDepth is NDC z from the face VP, which clusters near 1.0
    // and needs a much smaller bias than the old linear DistToLight/Radius.
    return 0.00005 + 0.0003 * SlopeFactor;
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
    // width is 2 * Radius; resolution is per-cascade because the atlas
    // packs progressive sizes.
    float Radius     = uLightData[0].CascadeRadii[CascadeIndex];
    float Resolution = max(uLightData[0].CascadeResolutions[CascadeIndex], 1.0);
    float TexelSize  = (2.0 * Radius) / Resolution;

    // 1.4 ~= sqrt(2): push by one diagonal texel at maximum slope, the
    // minimum that reliably clears self-intersection from depth-map
    // quantization. Larger values cause visible peter-panning at contact.
    return Normal * (TexelSize * 1.4 * OffsetScale);
}

// Pack a per-cascade UV [0,1] into the shared cascade atlas. The atlas tile
// for this cascade is described by FLightShadow.AtlasUVOffset/Scale.
float2 CascadeAtlasUV(int ShadowDataIndex, int CascadeIndex, float2 CascadeUV)
{
    FLightShadow Tile = GetShadowTile(ShadowDataIndex, CascadeIndex);
    return CascadeUV * Tile.AtlasUVScale + Tile.AtlasUVOffset;
}

// PCSS (Percentage-Closer Soft Shadows) tuning. The contact gap is caused
// by the fixed PCF footprint averaging in texels on the occluder; PCSS
// scales the PCF radius by receiver-to-blocker depth so the kernel
// collapses to a near-point sample at contact and opens to a soft penumbra
// at distance.
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
    // Push receiver off the surface along its normal before projecting,
    // sized to one cascade shadow texel. Removes harsh self-shadow banding
    // on detailed meshes that depth bias alone cannot fix without
    // trading acne for peter-panning.
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

    // Outer cascades cover proportionally more world per shadow texel, so a
    // fixed-UV kernel under-blurs them and texel-boundary blockiness shows
    // through. Scale the search and the filter cap by the same factor so
    // outer cascades blur to a wider, scene-proportional penumbra. The
    // multiplier is a single tuning knob (smaller = sharper outer shadows
    // but more blockiness, larger = softer but distant edges lose contact).
    float CascadeScale = 1.0 + 1.5 * float(CascadeIndex);

    // --- Stage 1: blocker search -------------------------------------------------
    //
    // Tap the shadow map through the *point* sampler at binding 16 so each
    // fetch is a raw texel depth (binding 7's comparison sampler would return
    // a PCF-filtered result, useless for classifying individual texels).
    // If no taps hit an occluder the fragment is fully lit; early-out skips
    // the PCF pass entirely.
    float BlockerDepthSum = 0.0;
    int   NumBlockers     = 0;

    float SearchRadius = PCSS_BlockerSearchRadius * CascadeScale;

    [unroll]
    for (int s = 0; s < PCSS_BLOCKER_SEARCH_TAPS; ++s)
    {
        float2 Offset   = VogelDiskSample(s, PCSS_BLOCKER_SEARCH_TAPS, Angle) * SearchRadius;
        float2 SampleUV = clamp(ProjectionUV + Offset, 0.001, 0.999);
        float2 AtlasUV  = CascadeAtlasUV(Light.ShadowDataIndex, CascadeIndex, SampleUV);
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

    // --- Stage 2: penumbra estimate ---------------------------------------------
    //
    // For an orthographic directional light the penumbra grows linearly with
    // receiver-to-blocker distance. Depths are NDC-z [0,1] so the delta is
    // already normalized; PCSS_LightSize absorbs the conversion to UV space
    // and sun apparent size. At contact the delta collapses and the filter
    // shrinks to PCSS_MinFilterRadius, keeping the edge glued to the mesh.
    float DepthDelta   = max(CurrentDepth - AvgBlockerDepth, 0.0);
    float Penumbra     = DepthDelta * PCSS_LightSize;
    float MaxRadius    = PCSS_MaxFilterRadius * CascadeScale;
    float FilterRadius = clamp(Penumbra, PCSS_MinFilterRadius, MaxRadius);

    // Low blocker coverage means partial occlusion, so pull the filter in
    // slightly to keep penumbra stable across cascades.
    float BlockerCoverage = float(NumBlockers) / float(PCSS_BLOCKER_SEARCH_TAPS);
    FilterRadius *= lerp(0.75, 1.0, BlockerCoverage);

    // --- Stage 3: PCF with contact-hardened radius ------------------------------
    float Shadow = 0.0;
    [unroll]
    for (int i = 0; i < SHADOW_SAMPLE_COUNT; ++i)
    {
        float2 Offset    = VogelDiskSample(i, SHADOW_SAMPLE_COUNT, Angle + PI) * FilterRadius;
        float2 CascadeUV = clamp(ProjectionUV + Offset, 0.001, 0.999);
        float2 AtlasUV   = CascadeAtlasUV(Light.ShadowDataIndex, CascadeIndex, CascadeUV);
        Shadow += uShadowCascade.SampleCmp(AtlasUV, RefDepth);
    }
    return Shadow / float(SHADOW_SAMPLE_COUNT);
}

float ComputeShadowFactor(FLight Light, float3 FragmentPos, float3 Normal, float3 ViewPosition, float2 ScreenPos, bool ReceiveShadow)
{
    if (!ReceiveShadow)
    {
        return 1.0;
    }

    bool IsSpotLight        = HasFlag(Light.Flags, ELightFlags::Spot);
    bool IsDirectionalLight = HasFlag(Light.Flags, ELightFlags::Directional);

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
    // Atlas stores rasterized NDC z from the shadow VS. Sampling must project
    // through the same face VP and use ProjCoords.xy/z, not cubemap UV.
    float3 LightDirToFrag = normalize(Light.Position - FragmentPos);

    // Pick the cube face for point lights; spot lights always use face 0.
    int Face = 0;
    if (!IsSpotLight)
    {
        float3 L   = FragmentPos - Light.Position;
        float3 Dir = normalize(L);
        Face = DirectionToCubemapCoord(Dir).Face;
    }

    // Push the receiver a shadow-texel along its surface normal before
    // projecting. Without this the wall self-shadows at grazing angles inside
    // the face's visible cone, producing a dimmer square centred on the light
    // where outside the cone the UV early-out returns fully-lit.
    FLightShadow ShadowTile = GetShadowTile(Light.ShadowDataIndex, Face);

    uint AtlasW, AtlasH;
    uShadowAtlas.GetDimensions(AtlasW, AtlasH);

    float  AtlasTileTexels  = max(ShadowTile.AtlasUVScale.x, 1e-6) * float(AtlasW);
    float  WorldTexel       = (Light.Radius * 2.0) / max(AtlasTileTexels, 1.0);
    float  NdotL            = saturate(dot(Normal, LightDirToFrag));
    float  OffsetScale      = saturate(1.0 - NdotL);
    float3 OffsetPos        = FragmentPos + Normal * (WorldTexel * 1.4 * OffsetScale);

    // Receiver bias: constant + slope + tile-quantization term. NDC z at a
    // pixel quantizes to ~2/AtlasTileTexels in depth, so a fixed 0.00005
    // tuned for 4096 texels self-shadows against its own rasterized
    // triangle at 512 and below, producing a dimmer face-shaped square.
    float SlopeFactor = sqrt(max(1.0 - NdotL * NdotL, 0.0));
    float Bias        = 0.00005 + 0.0003 * SlopeFactor + 1.5 / max(AtlasTileTexels, 1.0);

    float4x4 ShadowVP   = GetShadowViewProjection(Light.ShadowDataIndex, Face);
    float4 ShadowCoord  = mul(ShadowVP, float4(OffsetPos, 1.0));
    float3 ProjCoords   = ShadowCoord.xyz / max(ShadowCoord.w, 1e-6);
    float2 ProjectionUV = ProjCoords.xy * 0.5 + 0.5;

    // Only early-out on z: fragments past the light's far plane aren't lit
    // anyway. A UV early-out on [0,1] created a visible square where the
    // picked face's cone met the receiver; the atlas sampler (AM_Clamp)
    // already handles taps that stray beyond the tile.
    if (ProjCoords.z < 0.0 || ProjCoords.z > 1.0)
        return 1.0;

    float2 SafeUV = saturate(ProjectionUV);

    float IGN   = frac(52.9829189 * frac(dot(ScreenPos, float2(0.06711056, 0.00583715))));
    float Angle = IGN * TWO_PI;
    // Filter radius is in atlas UV; scale down for tiny tiles so the Vogel
    // disk stays inside the face rect. Without the cap, small tiles see
    // every tap fall outside [0,1]. Cap at 30% of the tile's atlas UV extent.
    float TileUVExtent  = min(ShadowTile.AtlasUVScale.x, ShadowTile.AtlasUVScale.y);
    float FilterRadius  = min(0.002, TileUVExtent * 0.3);

    float RefDepth = ProjCoords.z - Bias;

    float Shadow = 0.0;
    float Weight = 0.0;
    for (int i = 0; i < SHADOW_SAMPLE_COUNT; i++)
    {
        float2 Offset     = VogelDiskSample(i, SHADOW_SAMPLE_COUNT, Angle) * FilterRadius;
        float2 TileOffset = Offset / max(ShadowTile.AtlasUVScale, float2(1e-6));
        float2 TapUV      = SafeUV + TileOffset;
        // Skip taps that leave the face rect: those atlas texels belong to
        // a different light's tile. The clamped center tap below guarantees
        // at least one valid sample.
        if (any(TapUV < 0.0) || any(TapUV > 1.0))
        {
            continue;
        }
        float2 SampleUV = ShadowTile.AtlasUVOffset + (TapUV * ShadowTile.AtlasUVScale);
        Shadow += uShadowAtlas.SampleCmp(SampleUV, RefDepth);
        Weight += 1.0;
    }

    if (Weight <= 0.0)
    {
        float2 CenterUV = ShadowTile.AtlasUVOffset + (SafeUV * ShadowTile.AtlasUVScale);
        return uShadowAtlas.SampleCmp(CenterUV, RefDepth);
    }
    return Shadow / Weight;
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

    float3 Spec         = (NDF * G * F) / (4.0 * max(dot(N, V), 1e-4) * max(dot(N, L), 1e-4));
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
    float  Roughness = max(Material.Roughness, 0.045);
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

    float3 N  = GetWorldNormal(WorldNormal, Input.TangentWS, Normal);
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

    // IBL ambient: split-sum convolutions of the captured sky cube. Diffuse
    // term reads the cosine-convolved irradiance cube by N; specular reads
    // the GGX-prefiltered cube by R at a roughness-driven mip and combines
    // it with the BRDF integration LUT. Metals get colored reflections of
    // the sky (F0 = Albedo) instead of going black in shadowed regions.
    // GetAmbientLightIntensity() now scales the entire IBL ambient so the
    // existing Environment.AmbientIntensity slider still drives sky strength.
    float3 IBLAmbient = EvaluateIBLAmbient(N, V, Albedo, F0, Roughness, Metallic);
    float3 Ambient    = IBLAmbient * GetAmbientLightIntensity() * AO;
    float3 Color      = Ambient + Lo + Material.Emissive;

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
                float3 DebugN = GetWorldNormal(WorldNormal, Input.TangentWS, Material.Normal);
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