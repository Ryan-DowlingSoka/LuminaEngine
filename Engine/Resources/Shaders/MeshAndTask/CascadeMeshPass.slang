#include "Includes/SceneGlobals.slang"
#include "Includes/MeshletPayload.slang"

// Position-only mesh shader for directional-light cascade shadow maps.
// Counterpart to the SHADOW_CSM variant of ShadowMappingVert.slang. The task
// shader (CascadeMeshletTask.slang) already filtered meshlets against this
// cascade's frustum, so every survivor rasterizes (no degenerate-NaN padding).
//
// Push constants match ShadowMappingVert's SHADOW_CSM layout so we can share
// push layouts across the task/mesh pair without duplicating structs on the
// command-list side.

struct FCascadePush
{
    int ShadowDataIndex;
    int CascadeIndex;
};

[[vk::push_constant]] FCascadePush uCascadePush;

struct MSOutput
{
    precise float4 Position : SV_Position;
};

#define MESH_GROUP_SIZE 128

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_GROUP_SIZE, 1, 1)]
void MeshMain(uint GroupID   : SV_GroupID,
              uint ThreadID  : SV_GroupIndex,
              in payload FMeshTaskPayload Payload,
              out indices uint3 OutTriangles[MESHLET_MAX_TRIANGLES],
              out vertices MSOutput OutVertices[MESHLET_MAX_VERTICES])
{
    uint InstanceID       = Payload.InstanceID;
    uint MeshletLocalIdx  = Payload.MeshletLocalIndices[GroupID];
    uint GlobalMeshletIdx = Payload.GlobalMeshletBase + MeshletLocalIdx;

    FGPUInstance Inst = GetInstance(InstanceID);

    FMeshlet M = Inst.MeshletHeader.Meshlets[GlobalMeshletIdx];

    SetMeshOutputCounts(M.VertexCount, M.TriangleCount);

    float4x4 Model            = Inst.ModelMatrix;
    float4x4 CascadeViewProj  = GetShadowViewProjection(uCascadePush.ShadowDataIndex,
                                                        (uint)uCascadePush.CascadeIndex);
    bool     bSkinned         = Inst.HasFlag(EInstanceFlags::Skinned);

    // Vertex lane.
    if (ThreadID < M.VertexCount)
    {
        uint VertexIdx = Inst.MeshletHeader.Vertices[M.VertexOffset + ThreadID];

        float3 LocalPos;
        if (bSkinned)
        {
            LocalPos = LoadSkinnedPositionByIndex(Inst.VBAddress, VertexIdx, Inst.BoneOffset);
        }
        else
        {
            LocalPos = LoadStaticPositionByIndex(Inst.VBAddress, VertexIdx);
        }

        // Same sequential M -> VP as ShadowMappingVert's SHADOW_CSM path. Do
        // not fuse into a single MVP mul: the legacy VS path (used as fallback
        // when mesh shaders are unavailable) writes into the same shadow map,
        // and diverging floating-point order would break cross-path consistency
        // when a frame switches paths.
        float4 WorldPos = mul(Model, float4(LocalPos, 1.0));
        MSOutput V;
        V.Position = mul(CascadeViewProj, WorldPos);
        OutVertices[ThreadID] = V;
    }

    // Primitive lane.
    if (ThreadID < M.TriangleCount)
    {
        uint PackedTri = Inst.MeshletHeader.Triangles[M.TriangleOffset + ThreadID];
        uint I0 =  PackedTri        & 0xFFu;
        uint I1 = (PackedTri >>  8) & 0xFFu;
        uint I2 = (PackedTri >> 16) & 0xFFu;
        OutTriangles[ThreadID] = uint3(I0, I1, I2);
    }
}
