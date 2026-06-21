#pragma once
#include "Includes/MeshletGeometry.slang"

// VisBuffer geometry, vertex-shader EMULATION path -- the fallback for GPUs without mesh shaders.
// Mirrors MeshletVertex.slang's non-indexed corner decode but writes ONLY the visibility ID + clip
// position; the deferred material pass reconstructs everything else from the ID. Pairs with the same
// VisBufferPixel.slang as the mesh path (both feed a flat uint VisID : VIS_ID to the fragment stage).
//
// VisID packing (uint32): [31:7] = global MeshletDrawList slot, [6:0] = local triangle index (<= 124).
// Stored as VisID + 1 so the natural clear-to-0 marks empty pixels (see MeshletVisBuffer.slang).
// FirstInstance carries the meshlet base, so SV_VulkanInstanceID IS the global draw-list slot.

struct FVisVSOutput
{
    nointerpolation uint VisID : VIS_ID;   // flat: all three corners of a triangle write the same ID
    precise float4 Position    : SV_Position;
};

[shader("vertex")]
FVisVSOutput VertexMain(uint VertexIndex: SV_VulkanVertexID, uint MeshletSlot: SV_VulkanInstanceID)
{
    FMeshletRef Ref = ResolveSurfaceMeshlet(MeshletSlot);

    uint TriIdx = VertexIndex / 3u;
    uint Corner = VertexIndex % 3u;

    FVisVSOutput O;
    O.VisID = ((MeshletSlot << 7u) | (TriIdx & 0x7Fu)) + 1u;

    if (TriIdx >= Ref.M.TriangleCount)
    {
        const float NaN = 0.0f / 0.0f;
        O.Position = float4(NaN, NaN, NaN, 1.0);
        return O;
    }

    uint LocalVertIdx      = MeshletCornerVertexIndex(Ref, TriIdx, Corner);
    FVertexData VertexData = LoadMeshletVertexData(Ref, LocalVertIdx);
    FMeshletWorldVertex W  = TransformMeshletVertex(Ref, VertexData);

    // Same material vertex-graph local contract as MeshletVertex/MeshletMesh so WPO matches bit-for-bit
    // (the deferred pass re-applies the identical WPO when it reconstructs this triangle).
    FGPUInstance Inst    = Ref.Inst;
    float4x4 ModelMatrix = Ref.Inst.ModelMatrix;
    float4   WorldPos    = float4(W.WorldPos, 1.0);
    float3   NormalWS    = W.NormalWS;
    float3   TangentWS   = W.TangentWS.xyz;

    FMaterialVertexInputs Material;
    $MATERIAL_VERTEX_INPUTS
    WorldPos.xyz += Material.WorldPositionOffset;

    O.Position = CameraClipPosition(CameraViewPosition(WorldPos.xyz));
    return O;
}
