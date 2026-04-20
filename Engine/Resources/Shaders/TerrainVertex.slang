#include "Includes/SceneGlobals.slang"

// Keep in sync with FTerrainRenderParams in TerrainRenderTypes.h.
struct FTerrainRenderParams
{
    float2  OriginXZ;           // World-space XZ of the terrain's minimum corner.
    float   TileWorldSize;      // Total extent of the terrain square in world units.
    float   MaxHeight;          // Scales the [0,1] heightmap sample into world units.

    int     Resolution;         // Heightmap samples per side.
    int     ChunkResolution;    // Heightmap samples per chunk side.
    int     ChunksPerSide;      // Chunks laid out along one axis.
    int     LayerCount;         // Active layer count in LayerAlbedoIdx[].

    float3  WorldOriginY;       // World-space origin base height + two pads.
    uint    EntityID;           // Picker ID for the owning terrain entity.

    uint4   LayerAlbedoIdx;     // Bindless texture indices for layers 0..3.
    float4  LayerUVScale;       // Per-layer UV tiling, same order as LayerAlbedoIdx.
};

[[vk::binding(0, 2)]] Sampler2D                         uHeightmap;
[[vk::binding(1, 2)]] Sampler2D                         uTerrainNormal;
[[vk::binding(2, 2)]] Sampler2DArray                    uLayerWeights;
[[vk::binding(3, 2)]] ConstantBuffer<FTerrainRenderParams> TerrainParams;

struct VSOutput
{
    float3              WorldPos   : WORLD_POS;
    float3              WorldNormal: WORLD_NORMAL;
    float2              HeightUV   : HEIGHT_UV;
    float2              WorldUV    : WORLD_UV;
    nointerpolation uint EntityID  : ENTITY_ID;
    float4              Position   : SV_Position;
};

// Decode VertexID into a chunk-local quad corner. Each chunk has (R-1)^2 quads,
// six verts per quad, laid out row-major. The draw uses one instance per chunk.
[shader("vertex")]
VSOutput VertexMain(uint VertexID : SV_VulkanVertexID, uint InstanceID : SV_VulkanInstanceID)
{
    const int  CR          = TerrainParams.ChunkResolution;
    const int  QuadsPerSide = CR - 1;

    uint QuadIndex   = VertexID / 6u;
    uint CornerIndex = VertexID % 6u;

    // Map the six tri verts of a quad to its four unique corners.
    const uint2 CornerLookup[6] = {
        uint2(0, 0), uint2(1, 0), uint2(1, 1),
        uint2(0, 0), uint2(1, 1), uint2(0, 1)
    };
    uint2 Corner = CornerLookup[CornerIndex];

    uint QuadX = QuadIndex % (uint)QuadsPerSide;
    uint QuadY = QuadIndex / (uint)QuadsPerSide;

    uint ChunkX = InstanceID % (uint)TerrainParams.ChunksPerSide;
    uint ChunkY = InstanceID / (uint)TerrainParams.ChunksPerSide;

    // Sample coordinates in the global heightmap grid.
    int SampleX = int(ChunkX) * QuadsPerSide + int(QuadX + Corner.x);
    int SampleY = int(ChunkY) * QuadsPerSide + int(QuadY + Corner.y);
    SampleX = clamp(SampleX, 0, TerrainParams.Resolution - 1);
    SampleY = clamp(SampleY, 0, TerrainParams.Resolution - 1);

    float2 HeightUV = (float2(SampleX, SampleY) + 0.5) / float(TerrainParams.Resolution);
    float  Height   = uHeightmap.SampleLevel(HeightUV, 0).r * TerrainParams.MaxHeight;

    // World position: grid stride = TileWorldSize / (Resolution - 1).
    float Stride = TerrainParams.TileWorldSize / float(TerrainParams.Resolution - 1);
    float3 WorldPos;
    WorldPos.x = TerrainParams.OriginXZ.x + float(SampleX) * Stride;
    WorldPos.z = TerrainParams.OriginXZ.y + float(SampleY) * Stride;
    WorldPos.y = TerrainParams.WorldOriginY.x + Height;

    float4 NormalSample = uTerrainNormal.SampleLevel(HeightUV, 0);
    float3 Normal = normalize(float3(NormalSample.x * 2.0 - 1.0,
                                     NormalSample.z * 2.0 - 1.0,
                                     NormalSample.y * 2.0 - 1.0));
    // Heightmap stores XY slope; reconstruct Y.
    Normal = normalize(float3(NormalSample.r * 2.0 - 1.0,
                              1.0,
                              NormalSample.g * 2.0 - 1.0));

    VSOutput Out;
    Out.WorldPos    = WorldPos;
    Out.WorldNormal = Normal;
    Out.HeightUV    = HeightUV;
    Out.WorldUV     = WorldPos.xz; // layer sampling in world units; pixel scales per-layer.
    Out.EntityID    = TerrainParams.EntityID;
    Out.Position    = WorldToClip(WorldPos);
    return Out;
}
