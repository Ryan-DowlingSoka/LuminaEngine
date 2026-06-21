#pragma once
#include "Includes/MeshletGeometry.slang"

// VisBuffer geometry mesh shader: rasterize meshlets writing ONLY a per-primitive visibility ID + depth.
// No interpolated attributes, no shading -- the deferred material pass reconstructs everything (positions,
// barycentrics, UVs, normals, derivatives) from the ID by re-fetching the meshlet triangle. WPO still
// applies here, since the rasterized geometry must be displaced. One workgroup per meshlet, like MeshletMesh.
//
// VisID packing (uint32): [31:7] = global MeshletDrawList slot, [6:0] = local triangle index (<= 124).
// Stored as VisID + 1 so the natural clear-to-0 marks empty pixels (0 is otherwise a valid triangle ID);
// the deferred pass tests for 0 and subtracts 1 to recover the VisID.

struct FVisPassArgs
{
    uint MeshletBase;   // global MeshletDrawList offset for this draw (the VS path's FirstInstance)
};
FVisPassArgs* Pass() { return GetPassArgs<FVisPassArgs>(); }

// Per-vertex output: clip position only (rasterizer needs nothing else).
struct FVisVertex
{
    float4 Position : SV_Position;
};

// Per-primitive output: the flat visibility ID, written once per triangle.
struct FVisPrimitive
{
    nointerpolation uint VisID : VIS_ID;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void MeshMain(
    uint3 GroupID  : SV_GroupID,
    uint  ThreadID : SV_GroupIndex,
    out vertices   FVisVertex    Verts[MESHLET_MAX_VERTICES],
    out indices    uint3         Tris[MESHLET_MAX_TRIANGLES],
    out primitives FVisPrimitive Prims[MESHLET_MAX_TRIANGLES])
{
    uint MeshletSlot = Pass().MeshletBase + GroupID.x;
    FMeshletRef Ref  = ResolveSurfaceMeshlet(MeshletSlot);

    const uint VertCount = Ref.M.VertexCount;
    const uint TriCount  = Ref.M.TriangleCount;
    SetMeshOutputCounts(VertCount, TriCount);

    if (ThreadID < VertCount)
    {
        FVertexData VertexData = LoadMeshletVertexData(Ref, ThreadID);
        FMeshletWorldVertex W  = TransformMeshletVertex(Ref, VertexData);

        // Same material vertex-graph local contract as MeshletMesh/MeshletVertex (WPO must match so the
        // VisBuffer depth agrees with what the deferred pass reconstructs).
        FGPUInstance Inst    = Ref.Inst;
        float4x4 ModelMatrix = Ref.Inst.ModelMatrix;
        float4   WorldPos    = float4(W.WorldPos, 1.0);
        float3   NormalWS    = W.NormalWS;
        float3   TangentWS   = W.TangentWS.xyz;

        FMaterialVertexInputs Material;
        $MATERIAL_VERTEX_INPUTS
        WorldPos.xyz += Material.WorldPositionOffset;

        Verts[ThreadID].Position = CameraClipPosition(CameraViewPosition(WorldPos.xyz));
    }

    if (ThreadID < TriCount)
    {
        Tris[ThreadID]        = UnpackMeshletTriangle(Ref, ThreadID);
        Prims[ThreadID].VisID = ((MeshletSlot << 7u) | (ThreadID & 0x7Fu)) + 1u;
    }
}
