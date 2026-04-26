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
    int     MeshletsPerChunkSide;
    int     MeshletQuadSide;
    uint    _Pad0;
};

struct FTerrainChunkInfo
{
    float3 BoundsMin;
    float  _Pad0;
    float3 BoundsMax;
    float  _Pad1;
    int2   ChunkCoord;
    int2   QuadOrigin;
    uint   MeshletOffset;
    uint   MeshletCount;
    uint   _Pad2;
    uint   _Pad3;
};

struct FTerrainMeshletInfo
{
    float3 BoundsMin;
    float  _Pad0;
    float3 BoundsMax;
    float  _Pad1;
    int2   ChunkLocalQuadOrigin;
    int2   QuadExtent;
    uint   ChunkIndex;
    uint   _Pad2;
    uint   _Pad3;
    uint   _Pad4;
};

struct FTerrainVisibleMeshlet
{
    uint ChunkIndex;
    uint MeshletIndex;
};

[[vk::binding(0, 2)]] Sampler2D                                      uHeightmap;
[[vk::binding(1, 2)]] Sampler2D                                      uTerrainNormal;
[[vk::binding(2, 2)]] Sampler2DArray                                 uLayerWeights;
[[vk::binding(3, 2)]] ConstantBuffer<FTerrainRenderParams>           TerrainParams;
[[vk::binding(4, 2)]] StructuredBuffer<FTerrainChunkInfo>            uTerrainChunks;
[[vk::binding(5, 2)]] StructuredBuffer<FTerrainMeshletInfo>          uTerrainMeshlets;
[[vk::binding(6, 2)]] StructuredBuffer<FTerrainVisibleMeshlet>       uTerrainVisible;

struct VSOutput
{
    float4 FragColor                : COLOR;
    float4 NormalVS                 : NORMAL_VS;
    float4 NormalWS                 : NORMAL_WS;
    float4 FragPos                  : FRAG_POS;
    float2 UV                       : TEXCOORD0;
    float2 HeightUV                 : HEIGHT_UV;

    nointerpolation uint EntityID      : ENTITY_ID;
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;
    // Global meshlet index for the meshlet debug visualization. Carries
    // through the visible-meshlet entry's MeshletIndex so the pixel shader
    // can hash to a stable per-meshlet color.
    nointerpolation uint MeshletIndex  : MESHLET_INDEX;

    precise float4 Position         : SV_Position;
};

// Meshlet-driven terrain vertex shader. The terrain cull compute pass
// emits one entry per surviving meshlet into uTerrainVisible; this VS is
// drawn indirectly with InstanceCount = (number of survivors), so
// InstanceID indexes that visible-meshlet array. Each meshlet draws
// MeshletQuadSide^2 quads (six verts per quad).
[shader("vertex")]
VSOutput VertexMain(uint VertexID : SV_VulkanVertexID, uint InstanceID : SV_VulkanInstanceID)
{
    FTerrainVisibleMeshlet Vis     = uTerrainVisible[InstanceID];
    FTerrainMeshletInfo    Meshlet = uTerrainMeshlets[Vis.MeshletIndex];
    FTerrainChunkInfo      Chunk   = uTerrainChunks[Meshlet.ChunkIndex];

    const int MeshletQuads = TerrainParams.MeshletQuadSide;

    uint QuadIndex   = VertexID / 6u;
    uint CornerIndex = VertexID % 6u;

    const uint2 CornerLookup[6] = {
        uint2(0, 0), uint2(1, 0), uint2(1, 1),
        uint2(0, 0), uint2(1, 1), uint2(0, 1)
    };
    uint2 Corner = CornerLookup[CornerIndex];

    uint MqX = QuadIndex % (uint)MeshletQuads;
    uint MqY = QuadIndex / (uint)MeshletQuads;

    // Edge meshlets along the chunk's far side cover fewer than the full
    // MeshletQuadSide quads. Clamp the per-vertex coord so degenerate
    // (collapsed) quads still rasterize as zero-area triangles instead of
    // sampling outside the chunk's heightmap region.
    uint ClampedX = min(MqX, (uint)max(Meshlet.QuadExtent.x - 1, 0));
    uint ClampedY = min(MqY, (uint)max(Meshlet.QuadExtent.y - 1, 0));

    int LocalQuadX = Meshlet.ChunkLocalQuadOrigin.x + (int)ClampedX;
    int LocalQuadY = Meshlet.ChunkLocalQuadOrigin.y + (int)ClampedY;

    int SampleX = Chunk.QuadOrigin.x + LocalQuadX + (int)Corner.x;
    int SampleY = Chunk.QuadOrigin.y + LocalQuadY + (int)Corner.y;
    SampleX = clamp(SampleX, 0, TerrainParams.Resolution - 1);
    SampleY = clamp(SampleY, 0, TerrainParams.Resolution - 1);

    float2 HeightUV = (float2(SampleX, SampleY) + 0.5) / float(TerrainParams.Resolution);
    float  Height   = uHeightmap.SampleLevel(HeightUV, 0).r * TerrainParams.MaxHeight;

    float  Stride = TerrainParams.TileWorldSize / float(TerrainParams.Resolution - 1);
    float3 WorldPos;
    WorldPos.x = TerrainParams.OriginXZ.x + float(SampleX) * Stride;
    WorldPos.z = TerrainParams.OriginXZ.y + float(SampleY) * Stride;
    WorldPos.y = TerrainParams.WorldOriginY.x + Height;

    float4 NormalSample = uTerrainNormal.SampleLevel(HeightUV, 0);
    float3 NormalWS = normalize(float3(NormalSample.r * 2.0 - 1.0,
                                       1.0,
                                       NormalSample.g * 2.0 - 1.0));

    float4x4 View    = GetCameraView();
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
    Out.MeshletIndex  = Vis.MeshletIndex;
    Out.Position      = mul(GetCameraProjection(), ViewPos);
    return Out;
}
