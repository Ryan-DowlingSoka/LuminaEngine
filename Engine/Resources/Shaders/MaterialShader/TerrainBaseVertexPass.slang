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
[[vk::binding(4, 2)]] StructuredBuffer<FTerrainChunkInfo>            uTerrainChunks;
[[vk::binding(5, 2)]] StructuredBuffer<FTerrainMeshletInfo>          uTerrainMeshlets;
[[vk::binding(6, 2)]] StructuredBuffer<FTerrainVisibleMeshlet>       uTerrainVisible;

// Pulled from a transient ring slot via buffer device address. Same block
// declared in TerrainBasePixelPass.slang so VS and PS share the push range.
struct FTerrainPushConstants
{
    ConstBufferPointer<FTerrainRenderParams> Params;
};
[[vk::push_constant]] FTerrainPushConstants PC;

struct VSOutput
{
    float4 FragColor                : COLOR;
    float4 NormalVS                 : NORMAL_VS;
    float4 NormalWS                 : NORMAL_WS;
    float4 TangentWS                : TANGENT_WS;
    float4 FragPos                  : FRAG_POS;
    float2 UV                       : TEXCOORD0;
    float2 HeightUV                 : HEIGHT_UV;

    nointerpolation uint EntityID      : ENTITY_ID;
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;
    // Global meshlet index, carried for the pixel shader's stable per-meshlet debug color.
    nointerpolation uint MeshletIndex  : MESHLET_INDEX;

    precise float4 Position         : SV_Position;
};

// Meshlet-driven terrain VS, drawn indirectly with InstanceCount = surviving meshlets, so InstanceID
// indexes uTerrainVisible. Each meshlet draws MeshletQuadSide^2 quads (six verts each).
[shader("vertex")]
VSOutput VertexMain(uint VertexID : SV_VulkanVertexID, uint InstanceID : SV_VulkanInstanceID)
{
    FTerrainRenderParams TerrainParams = PC.Params.get();

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

    // Edge meshlets cover fewer than MeshletQuadSide quads; clamp so degenerate quads collapse to
    // zero-area triangles instead of sampling outside the chunk's heightmap region.
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

    // Compute pass packs Normal.xz; recover Y from unit length (hardcoding Y=1 tilts toward +Y, flattening slopes).
    float4 NormalSample = uTerrainNormal.SampleLevel(HeightUV, 0);
    float  Nx = NormalSample.r * 2.0 - 1.0;
    float  Nz = NormalSample.g * 2.0 - 1.0;
    float  Ny = sqrt(saturate(1.0 - Nx * Nx - Nz * Nz));
    float3 NormalWS = float3(Nx, Ny, Nz);

    // Material vertex graph emission (WPO). Terrain typically leaves this at
    // zero; the substitution still runs for symmetry with the base pass.
    FMaterialVertexInputs Material;
    $MATERIAL_VERTEX_INPUTS
    WorldPos += Material.WorldPositionOffset;

    float4x4 View    = GetCameraView();
    float4   ViewPos = mul(View, float4(WorldPos, 1.0));
    float3   NormalVS = normalize(mul((float3x3)View, NormalWS));

    // Analytic surface tangent: UV = WorldPos.xz so dP/du = (1, dHdx, 0), with dHdx = -N.x / N.y from
    // the packed normal. A smooth per-vertex tangent avoids the faceting of a per-triangle ddx/ddy TBN.
    float  InvNy     = 1.0 / max(NormalWS.y, 1e-4);
    float  DhDx      = -NormalWS.x * InvNy;
    float3 TangentWS = normalize(float3(1.0, DhDx, 0.0));

    VSOutput Out;
    Out.FragColor     = float4(1.0, 1.0, 1.0, 1.0);
    Out.NormalVS      = float4(NormalVS, 0.0);
    Out.NormalWS      = float4(NormalWS, 0.0);
    Out.TangentWS     = float4(TangentWS, 1.0);
    Out.FragPos       = ViewPos;
    Out.UV            = WorldPos.xz;
    Out.HeightUV      = HeightUV;
    Out.EntityID      = TerrainParams.EntityID;
    Out.MaterialIndex = TerrainParams.MaterialIndex;
    Out.MeshletIndex  = Vis.MeshletIndex;
    Out.Position      = mul(GetCameraProjection(), ViewPos);
    return Out;
}
