#include "Includes/SceneGlobals.slang"

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

    uint4   LayerAlbedoIdx;
    float4  LayerUVScale;
};

[[vk::binding(0, 2)]] Sampler2D                         uHeightmap;
[[vk::binding(1, 2)]] Sampler2D                         uTerrainNormal;
[[vk::binding(2, 2)]] Sampler2DArray                    uLayerWeights;
[[vk::binding(3, 2)]] ConstantBuffer<FTerrainRenderParams> TerrainParams;

struct PSInput
{
    float3              WorldPos   : WORLD_POS;
    float3              WorldNormal: WORLD_NORMAL;
    float2              HeightUV   : HEIGHT_UV;
    float2              WorldUV    : WORLD_UV;
    nointerpolation uint EntityID  : ENTITY_ID;
    float4              Position   : SV_Position;
};

struct PSOutput
{
    float4 Color : SV_Target0;
};


float3 SampleLayerAlbedo(uint LayerIdx, float2 WorldUV)
{
    uint TexIdx = TerrainParams.LayerAlbedoIdx[LayerIdx];
    if (TexIdx == 0u)
    {
        const float3 Defaults[4] = {
            float3(0.45, 0.40, 0.30),
            float3(0.25, 0.45, 0.15),
            float3(0.55, 0.55, 0.55),
            float3(0.85, 0.80, 0.60)
        };
        return Defaults[LayerIdx];
    }
    float2 UV = WorldUV * TerrainParams.LayerUVScale[LayerIdx];
    return uGlobalTextures[TexIdx].Sample(UV).rgb;
}


float3 BlendLayers(float2 HeightUV, float2 WorldUV)
{
    float4 Weights = float4(0.0, 0.0, 0.0, 0.0);
    int LayerCount = min(TerrainParams.LayerCount, 4);
    [unroll] for (int i = 0; i < 4; ++i)
    {
        if (i >= LayerCount) break;
        Weights[i] = uLayerWeights.SampleLevel(float3(HeightUV, float(i)), 0).r;
    }

    // Layer 0 fills any unpainted weight so brushes blend smoothly over a
    // base layer instead of a tiny weight renormalising to 100%.
    float Total = Weights.x + Weights.y + Weights.z + Weights.w;
    if (Total < 1.0)
    {
        Weights[0] += 1.0 - Total;
        Total = 1.0;
    }
    Weights /= Total;

    float3 Result = float3(0.0, 0.0, 0.0);
    [unroll] for (int i = 0; i < 4; ++i)
    {
        if (i >= LayerCount) break;
        Result += SampleLayerAlbedo((uint)i, WorldUV) * Weights[i];
    }
    return Result;
}

[shader("pixel")]
PSOutput PixelMain(PSInput In)
{
    float3 Albedo = BlendLayers(In.HeightUV, In.WorldUV);
    float3 N      = normalize(In.WorldNormal);

    float3 L         = normalize(-GetSunDirection());
    float  NdotL     = saturate(dot(N, L));
    float3 Ambient   = GetAmbientLightColor() * GetAmbientLightIntensity();
    float3 DiffuseLt = Albedo * (NdotL + Ambient);

    PSOutput Out;
    Out.Color = float4(DiffuseLt, 1.0);
    return Out;
}
