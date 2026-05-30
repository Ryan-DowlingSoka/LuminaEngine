#include "Includes/SceneGlobals.slang"
#include "Includes/IBLRuntime.slang"
#include "Includes/ShadowSampling.slang"

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

// Hash-to-color for the meshlet debug view; stable per-meshlet hues across frames.
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

// Vertex-tangent TBN: MikkTSpace import tangent, Gram-Schmidt'd against the interpolated normal here;
// TangentWS.w preserves the bitangent across UV mirrors. (A ddx/ddy TBN faceted normal maps per triangle.)
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
        // Constant safety bias only; NormalOffsetWS() handles slope acne. ShadowParams.y is the per-light constant bias.
        return 0.00015 + 0.0003 * SlopeFactor + uLightData[0].ShadowParams.y;
    }

    // Point/spot: RefDepth is NDC z from the face VP, which clusters near 1.0
    // and needs a much smaller bias than the old linear DistToLight/Radius.
    return 0.00005 + 0.0003 * SlopeFactor;
}

// Normal-offset bias (Holbert, GDC 2011): push the receiver ~one shadow texel along its normal before
// projecting, scaled by (1 - NdotL) so it only kicks in at grazing angles. Returns the world-space offset.
float3 NormalOffsetWS(float3 Normal, float3 LightDir, int CascadeIndex)
{
    float NdotL       = saturate(dot(Normal, LightDir));
    float OffsetScale = saturate(1.0 - NdotL);

    // World-space size of one shadow texel: ortho width 2*Radius / per-cascade resolution.
    float Radius     = uLightData[0].CascadeRadii[CascadeIndex];
    float Resolution = max(uLightData[0].CascadeResolutions[CascadeIndex], 1.0);
    float TexelSize  = (2.0 * Radius) / Resolution;

    // 1.4 ~= sqrt(2): one diagonal texel at max slope, the minimum that clears depth-quantization
    // self-intersection without peter-panning. ShadowParams.x scales per-light (1.0 = default).
    return Normal * (TexelSize * 1.4 * OffsetScale * uLightData[0].ShadowParams.x);
}

// Pack a per-cascade UV [0,1] into the shared cascade atlas. The atlas tile
// for this cascade is described by FLightShadow.AtlasUVOffset/Scale.
float2 CascadeAtlasUV(int ShadowDataIndex, int CascadeIndex, float2 CascadeUV)
{
    FLightShadow Tile = GetShadowTile(ShadowDataIndex, CascadeIndex);
    return CascadeUV * Tile.AtlasUVScale + Tile.AtlasUVOffset;
}

// PCSS tuning: scale the PCF radius by receiver-to-blocker depth so the kernel collapses to a
// near-point sample at contact and opens to a soft penumbra at distance.
#define PCSS_BLOCKER_SEARCH_TAPS   16
static const float PCSS_BlockerSearchRadius = 0.0035;  // UV radius of blocker search
static const float PCSS_LightSize           = 0.05;     // Tunable: larger = softer far shadows
static const float PCSS_MinFilterRadius     = 0.00001; // Near-point sample at contact
static const float PCSS_MaxFilterRadius     = 0.0018;  // Cap so far shadows don't over-blur
static const float PCSS_BlockerDepthBias    = 0.0004;  // Bias for blocker classification

// PCSS-filtered sample of one directional cascade. Returns 1.0 (and Inside=false) when the fragment
// is outside the cascade's projected range so the caller can fall back to another cascade or fully-lit.
float SampleDirectionalCascade(FLight Light, int CascadeIndex, float3 FragmentPos, float3 Normal, float3 LightDir, float Bias, float2 ScreenPos, out bool Inside)
{
    // Normal-offset the receiver before projecting; removes self-shadow banding depth bias can't fix.
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

    // Outer cascades cover more world per texel, so scale the search and filter cap to keep the
    // penumbra scene-proportional (smaller = sharper but blockier, larger = softer but loses contact).
    float CascadeScale = 1.0 + 1.5 * float(CascadeIndex);

    // Stage 1: blocker search through the point sampler for raw texel depths (the comparison sampler
    // would return a PCF result, useless for classification). No occluder taps -> fully lit, early out.
    float BlockerDepthSum = 0.0;
    int   NumBlockers     = 0;

    float SearchRadius = PCSS_BlockerSearchRadius * CascadeScale;

    // Fixed (non per-pixel) rotation for the blocker search: per-pixel rotation of this sparse pattern
    // stipples thin-occluder penumbras; a shared angle keeps the lit/shadow boundary coherent. PCF below still rotates.
    [unroll]
    for (int s = 0; s < PCSS_BLOCKER_SEARCH_TAPS; ++s)
    {
        float2 Offset   = VogelDiskSample(s, PCSS_BLOCKER_SEARCH_TAPS, 0.0) * SearchRadius;
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

    // Stage 2: penumbra estimate. For an ortho light it grows linearly with receiver-to-blocker depth
    // (NDC-z delta); at contact the delta collapses and the filter shrinks to PCSS_MinFilterRadius.
    float DepthDelta   = max(CurrentDepth - AvgBlockerDepth, 0.0);
    // Per-light penumbra softness (PCSS light size); PCSS_LightSize is the default value.
    float Penumbra     = DepthDelta * uLightData[0].ShadowParams.z;
    float MaxRadius    = PCSS_MaxFilterRadius * CascadeScale;
    float FilterRadius = clamp(Penumbra, PCSS_MinFilterRadius, MaxRadius);

    // Low blocker coverage means partial occlusion, so pull the filter in
    // slightly to keep penumbra stable across cascades.
    float BlockerCoverage = float(NumBlockers) / float(PCSS_BLOCKER_SEARCH_TAPS);
    FilterRadius *= lerp(0.75, 1.0, BlockerCoverage);

    // Stage 3: PCF with contact-hardened radius. Scale the tap count (base = ShadowParams2.y) with
    // penumbra width so density stays roughly constant -- a fixed count grains out on wide penumbras.
    int   BaseTaps    = clamp(int(uLightData[0].ShadowParams2.y), 1, SHADOW_MAX_SAMPLE_COUNT);
    float RadiusFrac  = saturate(FilterRadius / max(MaxRadius, 1e-6));
    int   SampleCount = clamp(int(lerp(float(BaseTaps), float(SHADOW_MAX_SAMPLE_COUNT), RadiusFrac)),
                              BaseTaps, SHADOW_MAX_SAMPLE_COUNT);
    float Shadow = 0.0;
    [loop]
    for (int i = 0; i < SampleCount; ++i)
    {
        float2 Offset    = VogelDiskSample(i, SampleCount, Angle + PI) * FilterRadius;
        float2 CascadeUV = clamp(ProjectionUV + Offset, 0.001, 0.999);
        float2 AtlasUV   = CascadeAtlasUV(Light.ShadowDataIndex, CascadeIndex, CascadeUV);
        Shadow += uShadowCascade.SampleCmp(AtlasUV, RefDepth);
    }
    return Shadow / float(SampleCount);
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
        // Sun with bCastShadows = false leaves ShadowDataIndex at INDEX_NONE;
        // skip the cascade work and treat as fully lit.
        if (Light.ShadowDataIndex == INDEX_NONE)
        {
            return 1.0;
        }

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

        // Blend into the next cascade near the split boundary so the transition is smooth, not a seam.
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

    // Spot / point lights (atlas-based). Atlas stores rasterized NDC z from the shadow VS, so sample
    // by projecting through the same face VP and using ProjCoords.xy/z, not cubemap UV.
    float3 LightDirToFrag = normalize(Light.Position - FragmentPos);

    // Pick the cube face for point lights; spot lights always use face 0.
    int Face = 0;
    if (!IsSpotLight)
    {
        float3 L   = FragmentPos - Light.Position;
        float3 Dir = normalize(L);
        Face = DirectionToCubemapCoord(Dir).Face;
    }

    // Normal-offset the receiver before projecting; without it walls self-shadow at grazing angles
    // inside the face cone, leaving a dimmer square centered on the light.
    FLightShadow ShadowTile = GetShadowTile(Light.ShadowDataIndex, Face);

    uint AtlasW, AtlasH;
    uShadowAtlas.GetDimensions(AtlasW, AtlasH);

    float  AtlasTileTexels  = max(ShadowTile.AtlasUVScale.x, 1e-6) * float(AtlasW);
    float  WorldTexel       = (Light.Radius * 2.0) / max(AtlasTileTexels, 1.0);
    float  NdotL            = saturate(dot(Normal, LightDirToFrag));
    float  OffsetScale      = saturate(1.0 - NdotL);
    float3 OffsetPos        = FragmentPos + Normal * (WorldTexel * 1.4 * OffsetScale);

    // Receiver bias: constant + slope + tile-quantization term (NDC z quantizes to ~2/AtlasTileTexels),
    // so a fixed bias tuned for 4096 texels self-shadows at 512 and below.
    float SlopeFactor = sqrt(max(1.0 - NdotL * NdotL, 0.0));
    float Bias        = 0.00005 + 0.0003 * SlopeFactor + 1.5 / max(AtlasTileTexels, 1.0);

    float4x4 ShadowVP   = GetShadowViewProjection(Light.ShadowDataIndex, Face);
    float4 ShadowCoord  = mul(ShadowVP, float4(OffsetPos, 1.0));
    float3 ProjCoords   = ShadowCoord.xyz / max(ShadowCoord.w, 1e-6);
    float2 ProjectionUV = ProjCoords.xy * 0.5 + 0.5;

    // Only early-out on z (past the far plane). A UV early-out on [0,1] left a visible square at the
    // face-cone boundary; the AM_Clamp atlas sampler already handles taps straying beyond the tile.
    if (ProjCoords.z < 0.0 || ProjCoords.z > 1.0)
        return 1.0;

    float2 SafeUV = saturate(ProjectionUV);

    float IGN   = frac(52.9829189 * frac(dot(ScreenPos, float2(0.06711056, 0.00583715))));
    float Angle = IGN * TWO_PI;
    // Filter radius (atlas UV) capped at 30% of the tile extent so the Vogel disk stays in the face rect.
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
        // Skip taps that leave the face rect (those texels belong to another light's tile);
        // the clamped center tap below guarantees at least one valid sample.
        if (any(TapUV < 0.0) || any(TapUV > 1.0))
        {
            continue;
        }
        float2 SampleUV = ShadowTile.AtlasUVOffset + (TapUV * ShadowTile.AtlasUVScale);
        Shadow += uShadowAtlas.SampleCmpLevelZero(SampleUV, RefDepth);
        Weight += 1.0;
    }

    if (Weight <= 0.0)
    {
        float2 CenterUV = ShadowTile.AtlasUVOffset + (SafeUV * ShadowTile.AtlasUVScale);
        return uShadowAtlas.SampleCmpLevelZero(CenterUV, RefDepth);
    }
    return Shadow / Weight;
}

// Walk the light table once to find the sun's shadow-data slot. Used by the
// cascade debug visualizer; pixel-shader cost is irrelevant in that mode.
int FindSunShadowDataIndex()
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

// Per-cascade tint for the Shadow Cascades view, mirroring ComputeShadowFactor's selection.
// Dark gray = no sun shadow data; muted tint = fragment outside the selected cascade's bounds.
float3 CascadeDebugColor(float3 FragmentPos, float3 ViewPosition, float3 ShadedBase)
{
    int SunShadowIdx = FindSunShadowDataIndex();
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

    float4x4 CascadeVP  = GetShadowViewProjection(SunShadowIdx, CascadeIndex);
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

    // Blend with a luminance proxy from the lit shading so silhouettes stay
    // readable while the cascade color dominates.
    float Luma = saturate(dot(ShadedBase, float3(0.299, 0.587, 0.114)));
    float3 Color = lerp(Tint * 0.4, Tint, 0.5 + 0.5 * Luma);

    if (!Inside)
    {
        Color *= 0.35;
    }
    return Color;
}


float3 EvaluateLightContribution(FLight Light, float3 Position, float3 N, float3 V, float3 Albedo, float Roughness, float Metallic, float3 F0, float3 PrecomputedL, float PrecomputedDist, float3 EnergyComp)
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
    // Multi-scatter energy compensation (Turquin 2019); EnergyComp precomputed per-pixel from the BRDF LUT.
    Spec               *= EnergyComp;
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

    // Dielectric F0 = 0.08 * Specular (UE4 convention; default 0.5 -> 0.04). Metals override F0 with albedo.
    float3 F0_DIELECTRIC = float3(0.08 * Material.Specular);
    float3 F0 = lerp(F0_DIELECTRIC, Albedo, Metallic);

    // Multi-scatter energy compensation factor (Turquin 2019), constant per pixel; tinted by F0
    // so colored metals get colored multi-bounce. Ess = single-scatter albedo (BRDF LUT scale+bias).
    float  NdotV_EC    = saturate(dot(N, V));
    float2 DFG_EC      = SampleBRDFLUT(NdotV_EC, Roughness);
    float3 EnergyComp  = 1.0 + F0 * (1.0 / max(DFG_EC.x + DFG_EC.y, 1e-4) - 1.0);

    float  ZNear      = GetNearPlane();
    float  ZFar       = GetFarPlane();
    uint3  GridSize   = uSceneData.GridSize.xyz;
    uint2  ScreenSize = uSceneData.ScreenSize.xy;

    // Clamp to ZNear: edge fragments below it make log() negative, wrapping ZTile to a huge value
    // that overflows uClusters -> garbage light list and an MMU fault. Same hardening as TerrainBasePixelPass.
    float  ZForTile     = max(abs(ViewPosition.z), ZNear);
    float  ZTileF       = (log(ZForTile / ZNear) * float(GridSize.z)) / log(ZFar / ZNear);
    uint   ZTile        = (uint)clamp(ZTileF, 0.0, float(GridSize.z) - 1.0);
    float2 TileSize     = float2(ScreenSize) / float2(GridSize.xy);
    uint2  TileXY       = (uint2)clamp(divide(Input.Position.xy, TileSize),
                                       float2(0.0, 0.0),
                                       float2(GridSize.xy) - float2(1.0, 1.0));
    uint3  Tile         = uint3(TileXY, ZTile);
    uint   TileIndex    = Tile.x + (Tile.y * GridSize.x) + (Tile.z * GridSize.x * GridSize.y);
    uint   LightCount   = uClusters[TileIndex].Count;

    float3 Lo = float3(0.0);

    if (uLightData[0].bHasSun != 0)
    {
        FLight Light = GetLightAt(0);

        // Pixels facing away from the sun are fully self-shadowed (NdotL clamp), so skip the
        // expensive cascade PCSS for them (~half a typical scene).
        float SunNdotL = dot(N, normalize(Light.Direction.xyz));
        if (SunNdotL > 0.0)
        {
            float Shadow = ComputeShadowFactor(Light, WorldPosition, N, ViewPosition, Input.Position.xy, Inst.HasFlag(EInstanceFlags::ReceiveShadow));
            Lo += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0, float3(0.0), 0.0, EnergyComp) * Shadow;
        }
    }

    // Hard upper bound mirrors LightCull's write cap; protects against any path
    // (e.g. uninitialised cluster memory) that leaves Count outside [0, LIGHTS_PER_CLUSTER].
    uint SafeLightCount = min(LightCount, (uint)LIGHTS_PER_CLUSTER);
    for (uint i = 0; i < SafeLightCount; ++i)
    {
        uint   LightIndex = uClusters[TileIndex].LightIndices[i];
        // Cluster contents can be stale after a scene switch; an index past the live count reads
        // beyond Lights[MAX_LIGHTS] -> MMU fault in GetLightAt. Skip out-of-range indices.
        if (LightIndex >= GetNumLights())
        {
            continue;
        }
        FLight Light      = GetLightAt(LightIndex);

        float3 LightToFrag = Light.Position - WorldPosition;
        float  DistToLight = length(LightToFrag);
        float3 L           = LightToFrag / max(DistToLight, 0.0001);

        // Cluster membership is conservative: a listed light may not reach this pixel or lights only
        // its back face. Both contribute zero, so skip the shadow + BRDF work for them.
        if (dot(N, L) <= 0.0 || DistToLight >= Light.Radius)
        {
            continue;
        }

        float Shadow = 1.0;
        if (Light.ShadowDataIndex != INDEX_NONE)
        {
            Shadow = ComputeShadowFactor(Light, WorldPosition, N, ViewPosition, Input.Position.xy, Inst.HasFlag(EInstanceFlags::ReceiveShadow));
        }

        Lo += EvaluateLightContribution(Light, WorldPosition, N, V, Albedo, Roughness, Metallic, F0, L, DistToLight, EnergyComp) * Shadow;
    }

    // IBL ambient: split-sum convolutions of the sky cube (irradiance by N, GGX-prefiltered specular
    // by R + BRDF LUT). GetAmbientLightIntensity() scales it so the SkyLight Intensity slider applies.
    float3 IBLAmbient = EvaluateIBLAmbient(N, V, Albedo, F0, Roughness, Metallic, AO);
    float3 Ambient    = IBLAmbient * GetAmbientLightIntensity();
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
                Color = CascadeDebugColor(WorldPosition, ViewPosition, Color);
                break;
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