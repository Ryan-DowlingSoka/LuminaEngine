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

struct VSOutput
{
    float4 FragColor                : COLOR;
    float4 NormalVS                 : NORMAL_VS;
    float4 NormalWS                 : NORMAL_WS;
    float4 FragPos                  : FRAG_POS;
    float2 UV                       : TEXCOORD0;
    float2 HeightUV                 : HEIGHT_UV;

    nointerpolation uint EntityID   : ENTITY_ID;
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;

    precise float4 Position         : SV_Position;
};

// Procedural-chunk vertex: each chunk is (R-1)^2 quads, 6 verts per quad, laid
// out row-major, instanced once per chunk. Matches TerrainVertex.slang.
[shader("vertex")]
VSOutput VertexMain(uint VertexID : SV_VulkanVertexID, uint InstanceID : SV_VulkanInstanceID)
{
    const int CR           = TerrainParams.ChunkResolution;
    const int QuadsPerSide = CR - 1;

    uint QuadIndex   = VertexID / 6u;
    uint CornerIndex = VertexID % 6u;

    const uint2 CornerLookup[6] = {
        uint2(0, 0), uint2(1, 0), uint2(1, 1),
        uint2(0, 0), uint2(1, 1), uint2(0, 1)
    };
    uint2 Corner = CornerLookup[CornerIndex];

    uint QuadX = QuadIndex % (uint)QuadsPerSide;
    uint QuadY = QuadIndex / (uint)QuadsPerSide;

    uint ChunkX = InstanceID % (uint)TerrainParams.ChunksPerSide;
    uint ChunkY = InstanceID / (uint)TerrainParams.ChunksPerSide;

    int SampleX = int(ChunkX) * QuadsPerSide + int(QuadX + Corner.x);
    int SampleY = int(ChunkY) * QuadsPerSide + int(QuadY + Corner.y);
    SampleX = clamp(SampleX, 0, TerrainParams.Resolution - 1);
    SampleY = clamp(SampleY, 0, TerrainParams.Resolution - 1);

    float2 HeightUV = (float2(SampleX, SampleY) + 0.5) / float(TerrainParams.Resolution);
    float  Height   = uHeightmap.SampleLevel(HeightUV, 0).r * TerrainParams.MaxHeight;

    float  Stride = TerrainParams.TileWorldSize / float(TerrainParams.Resolution - 1);
    float3 WorldPos;
    WorldPos.x = TerrainParams.OriginXZ.x + float(SampleX) * Stride;
    WorldPos.z = TerrainParams.OriginXZ.y + float(SampleY) * Stride;
    WorldPos.y = TerrainParams.WorldOriginY.x + Height;

    // Normal texture stores XZ slope only; Y is reconstructed here.
    float4 NormalSample = uTerrainNormal.SampleLevel(HeightUV, 0);
    float3 NormalWS = normalize(float3(NormalSample.r * 2.0 - 1.0,
                                       1.0,
                                       NormalSample.g * 2.0 - 1.0));

    float4x4 View = GetCameraView();
    float4   ViewPos = mul(View, float4(WorldPos, 1.0));
    float3   NormalVS = normalize(mul((float3x3)View, NormalWS));

    VSOutput Out;
    Out.FragColor     = float4(1.0, 1.0, 1.0, 1.0);
    Out.NormalVS      = float4(NormalVS, 0.0);
    Out.NormalWS      = float4(NormalWS, 0.0);
    Out.FragPos       = ViewPos;
    Out.UV            = WorldPos.xz;
    Out.HeightUV      = HeightUV;
    Out.EntityID      = TerrainParams.EntityID;
    Out.MaterialIndex = TerrainParams.MaterialIndex;
    Out.Position      = mul(GetCameraProjection(), ViewPos);
    return Out;
}
