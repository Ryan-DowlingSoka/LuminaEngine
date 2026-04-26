// Terrain chunk + meshlet GPU cull. Two-tier frustum-only:
//
//   Tier 1: chunks. A whole chunk falling outside the camera frustum lets
//           us skip every meshlet inside it without touching their bounds.
//           This is the big win for massive open worlds where most chunks
//           are off-screen.
//
//   Tier 2: meshlets. Per-meshlet bounds catch silhouettes the chunk
//           bound is too coarse for, especially at the screen edge and for
//           tall mountain meshlets that occlude neighbors.
//
// Hi-Z occlusion is intentionally not run here -- the depth pyramid the
// meshlet cull builds between its early/late phases doesn't include the
// full BasePass depth, and testing terrain against it produced false
// chunk-granularity pop-out. Frustum cull alone still skips ~90% of chunks
// in any reasonable view; we'll add occlusion once terrain joins the
// unified meshlet pipeline and reads a fresh pyramid.
//
// Survivors get appended into the visible-meshlet SSBO and the single
// indirect-draw entry's InstanceCount is atomic-incremented. The terrain
// render pass then issues one indirect draw call sized to that count.
//
// Dispatch is one workgroup per chunk (X = ChunkCount), one thread per
// meshlet inside the chunk (LOCAL_SIZE_X). The first thread in every
// workgroup runs the chunk-level frustum test; if it rejects, every other
// thread short-circuits via groupshared bool.

#include "Includes/Culling.slang"

// Sized so the default ChunkResolution (64) -> 9x9=81 meshlets per chunk
// fits in a single wave on AMD/NV. Larger chunks use the inner loop below
// to chunk(meshlet) work across multiple iterations.
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

// Frustum-vs-sphere using the existing helper. Sphere is the smallest sphere
// containing the AABB, so this is conservative (a sphere outside the frustum
// implies its AABB is also outside; a sphere inside doesn't necessarily mean
// the AABB is inside, but the test errs toward "keep" which is the correct
// direction for a culling broad phase).
//
// We use sphere-vs-frustum instead of the AABB positive-vertex test because
// the existing meshlet cull uses the same routine on its FMeshletBounds, so
// any frustum-convention quirks (plane orientation, normalization) are
// absorbed by reusing the well-tested helper.
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

    // Stride the meshlet test by LOCAL_SIZE_X so chunks larger than the
    // workgroup (uncommon, but possible with very small MeshletQuadSide)
    // still get every meshlet covered by exactly one thread per pass.
    for (uint Local = LocalIdx; Local < Chunk.MeshletCount; Local += LOCAL_SIZE_X)
    {
        const uint MeshletIndex = Chunk.MeshletOffset + Local;
        if (MeshletIndex >= PC.MeshletCount)
            break;

        FTerrainMeshletInfo Meshlet = uMeshlets[MeshletIndex];

        // Per-meshlet frustum refinement. Same sphere-vs-frustum as the
        // chunk-level test; tighter bounds catch silhouettes the chunk's
        // sphere is too coarse for.
        if (!ChunkVisible(uSceneData.CullData.Frustum, Meshlet.BoundsMin, Meshlet.BoundsMax))
            continue;

        // Append. The single indirect-arg slot is shared across all meshlets
        // in this terrain so each survivor races for a unique slot via
        // atomic add.
        uint OldCount;
        InterlockedAdd(uTerrainIndirect[0].InstanceCount, 1, OldCount);

        const uint Slot = uTerrainIndirect[0].FirstInstance + OldCount;

        FTerrainVisibleMeshlet Out;
        Out.ChunkIndex   = ChunkIndex;
        Out.MeshletIndex = MeshletIndex;
        uVisibleMeshlets[Slot] = Out;
    }
}
