// Two-tier frustum-only terrain cull: reject whole chunks, then refine per-meshlet for silhouettes.
// No Hi-Z here (the inter-phase pyramid lacks full BasePass depth and caused chunk-granularity pop-out).
// Dispatch: one workgroup per chunk, one thread per meshlet; thread 0 runs the chunk test (groupshared).

#include "Includes/Culling.slang"

// Sized so the default ChunkResolution (64) -> 81 meshlets/chunk fits one wave; larger chunks stride.
#define LOCAL_SIZE_X 128

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

struct FTerrainCullPushConstants
{
    uint ChunkCount;
    uint MeshletCount;
    uint _Pad0;
    uint _Pad1;
};

[[vk::binding(0, 2)]] StructuredBuffer<FTerrainChunkInfo>            uChunks;
[[vk::binding(1, 2)]] StructuredBuffer<FTerrainMeshletInfo>          uMeshlets;
[[vk::binding(2, 2)]] RWStructuredBuffer<FTerrainVisibleMeshlet>     uVisibleMeshlets;
[[vk::binding(3, 2)]] RWStructuredBuffer<FDrawIndirectArguments>     uTerrainIndirect;

groupshared bool gChunkVisible;

// Smallest sphere enclosing the AABB. Conservative for plane-vs-sphere tests.
void AABBToSphere(float3 BoundsMin, float3 BoundsMax, out float3 Center, out float Radius)
{
    Center = (BoundsMin + BoundsMax) * 0.5;
    Radius = length(BoundsMax - Center);
}

// Frustum-vs-sphere on the AABB's enclosing sphere: conservative (errs toward "keep"). Reuses the
// meshlet cull's helper so frustum-convention quirks (plane orientation, normalization) are shared.
bool ChunkVisible(FFrustum F, float3 BoundsMin, float3 BoundsMax)
{
    float3 Center;
    float  Radius;
    AABBToSphere(BoundsMin, BoundsMax, Center, Radius);
    return FrustumContainsSphere(F, Center, Radius);
}

[shader("compute")]
[numthreads(LOCAL_SIZE_X, 1, 1)]
void ComputeMain(
    uint3   GroupID    : SV_GroupID,
    uint    LocalIdx   : SV_GroupIndex,
    uniform FTerrainCullPushConstants PC)
{
    const uint ChunkIndex = GroupID.x;
    if (ChunkIndex >= PC.ChunkCount)
        return;

    // Thread 0 owns the chunk-level test. Broadcast through groupshared so
    // every meshlet thread can short-circuit on a coarse rejection.
    if (LocalIdx == 0)
    {
        FTerrainChunkInfo Chunk = uChunks[ChunkIndex];
        gChunkVisible = ChunkVisible(uSceneData.CullData.Frustum, Chunk.BoundsMin, Chunk.BoundsMax);
    }
    GroupMemoryBarrierWithGroupSync();

    if (!gChunkVisible)
        return;

    FTerrainChunkInfo Chunk = uChunks[ChunkIndex];

    // Stride the meshlet test by LOCAL_SIZE_X so chunks larger than the workgroup still cover
    // every meshlet with one thread per pass.
    for (uint Local = LocalIdx; Local < Chunk.MeshletCount; Local += LOCAL_SIZE_X)
    {
        const uint MeshletIndex = Chunk.MeshletOffset + Local;
        if (MeshletIndex >= PC.MeshletCount)
            break;

        FTerrainMeshletInfo Meshlet = uMeshlets[MeshletIndex];

        // Per-meshlet frustum refinement; tighter bounds catch silhouettes the chunk sphere misses.
        if (!ChunkVisible(uSceneData.CullData.Frustum, Meshlet.BoundsMin, Meshlet.BoundsMax))
            continue;

        // Append: survivors race for a unique slot in the shared indirect-arg via atomic add.
        uint OldCount;
        InterlockedAdd(uTerrainIndirect[0].InstanceCount, 1, OldCount);

        const uint Slot = uTerrainIndirect[0].FirstInstance + OldCount;

        FTerrainVisibleMeshlet Out;
        Out.ChunkIndex   = ChunkIndex;
        Out.MeshletIndex = MeshletIndex;
        uVisibleMeshlets[Slot] = Out;
    }
}
