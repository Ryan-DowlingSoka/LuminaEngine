#include "Includes/SceneGlobals.slang"
#include "Includes/MeshletPayload.slang"

// Position-only mesh shader for the depth pre-pass. Opaque only; masked
// materials route through BaseMeshPass.

struct MSOutput
{
    // Position math must match BaseMeshPass / BaseVertexPass / DepthPrePass
    // for the equal-depth test.
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

    float4x4 ModelMatrix = Inst.ModelMatrix;
    float4x4 View        = GetCameraView();
    float4x4 Projection  = GetCameraProjection();
    bool     bSkinned    = Inst.HasFlag(EInstanceFlags::Skinned);

    if (ThreadID < M.VertexCount)
    {
        float3 LocalPos = bSkinned
            ? LoadSkinnedPosition(Inst.MeshletHeader, M, ThreadID, Inst.BoneOffset)
            : LoadStaticPosition(Inst.MeshletHeader, M, ThreadID);

        // Sequential muls must match BaseMeshPass; do not fuse.
        float4 WorldPos = mul(ModelMatrix, float4(LocalPos, 1.0));
        float4 ViewPos  = mul(View, WorldPos);

        MSOutput V;
        V.Position = mul(Projection, ViewPos);
        OutVertices[ThreadID] = V;
    }

    if (ThreadID < M.TriangleCount)
    {
        uint PackedTri = Inst.MeshletHeader.Triangles[M.TriangleOffset + ThreadID];
        OutTriangles[ThreadID] = uint3(PackedTri & 0xFFu, (PackedTri >> 8) & 0xFFu, (PackedTri >> 16) & 0xFFu);
    }
}
