#include "Includes/SceneGlobals.slang"
#include "Includes/MeshletPayload.slang"

// Position-only mesh shader for the depth pre-pass. Mirrors DepthPrePass.slang's
// role in the legacy VS path. The clip-space math is kept bit-for-bit identical
// to BaseMeshPass.slang (sequential M->V->P muls, same operand order) because
// BasePass runs with EComparisonFunc::Equal against this pass's output. Any
// drift in floating-point order produces Z-equal misses → black pixels.
//
// What this shader DOES skip vs BaseMeshPass:
//   - Normal / UV / Color unpack from the vertex record (LoadStaticPositionByIndex
//     reads only the float3 Position field and skips the Unpack* ALU).
//   - NormalMatrix build and normal transform.
//   - ViewPos / Normal / UV / Color interpolant writes.
//
// Masked materials still route through BaseMeshPass (the material PS needs UV
// for alpha clip); only fully-opaque draws pick this shader.

struct MSOutput
{
    // IMPORTANT: position math must stay in lockstep with BaseMeshPass.slang
    // and BaseVertexPass.slang / DepthPrePass.slang. Diverging produces
    // Z-fighting against BasePass's depth-equal test.
    precise float4 Position : SV_Position;
};

// Workgroup covers max(vertCount=64, triCount=124). 128 threads matches
// BaseMeshPass so the backend can share pipeline-stage resources.
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

    // Hoist transform/projection reads outside the predicate so every lane in
    // the vertex range shares the same matrices.
    float4x4 ModelMatrix = Inst.ModelMatrix;
    float4x4 View        = GetCameraView();
    float4x4 Projection  = GetCameraProjection();
    bool     bSkinned    = Inst.HasFlag(EInstanceFlags::Skinned);

    // Vertex lane: first VertexCount threads each emit one vertex.
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

        // Same three sequential muls as BaseMeshPass. Do not fuse.
        float4 WorldPos = mul(ModelMatrix, float4(LocalPos, 1.0));
        float4 ViewPos  = mul(View, WorldPos);

        MSOutput V;
        V.Position = mul(Projection, ViewPos);
        OutVertices[ThreadID] = V;
    }

    // Primitive lane: first TriangleCount threads each emit one triangle.
    // Triangles are packed as 4x uint8 (three corners + pad) per uint32.
    if (ThreadID < M.TriangleCount)
    {
        uint PackedTri = Inst.MeshletHeader.Triangles[M.TriangleOffset + ThreadID];
        uint I0 =  PackedTri        & 0xFFu;
        uint I1 = (PackedTri >>  8) & 0xFFu;
        uint I2 = (PackedTri >> 16) & 0xFFu;
        OutTriangles[ThreadID] = uint3(I0, I1, I2);
    }
}
