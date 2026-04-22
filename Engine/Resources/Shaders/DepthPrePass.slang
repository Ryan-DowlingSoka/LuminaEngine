#pragma once
#include "Includes/SceneGlobals.slang"

struct VertexOutput
{
    // IMPORTANT: Position math must stay in lockstep with BaseVertexPass.slang so
    // the base pass's depth-equal test holds. Any change here must be mirrored
    // there (and vice versa).
    precise float4 Position : SV_Position;
};

// Meshlet-driven depth-only vertex shader. SV_VulkanInstanceID indexes into
// uMeshletDrawList (populated by MeshletCull.slang), matching BaseVertexPass.
// Padding invocations (TriIdx >= M.TriangleCount) emit a NaN triangle so the
// rasterizer discards them.
[shader("vertex")]
VertexOutput VertexMain(uint VertexIndex: SV_VulkanVertexID, uint MeshletSlot: SV_VulkanInstanceID)
{
    FMeshletDraw Draw = uMeshletDrawList[MeshletSlot];
    FGPUInstance Inst = GetInstance(Draw.InstanceID);

    uint GlobalMeshletIdx = Inst.SurfaceMeshletOffset + Draw.MeshletLocalIndex;
    FMeshlet M            = Inst.MeshletHeader.Meshlets[GlobalMeshletIdx];

    uint TriIdx = VertexIndex / 3u;
    uint Corner = VertexIndex % 3u;

    VertexOutput Output;

    if (TriIdx >= M.TriangleCount)
    {
        const float NaN = 0.0f / 0.0f;
        Output.Position = float4(NaN, NaN, NaN, 1.0);
        return Output;
    }

    uint PackedTri    = Inst.MeshletHeader.Triangles[M.TriangleOffset + TriIdx];
    uint LocalVertIdx = (PackedTri >> (Corner * 8u)) & 0xFFu;
    uint VertexIdx    = Inst.MeshletHeader.Vertices[M.VertexOffset + LocalVertIdx];

    // Depth-only pass: skip Normal/UV/Color unpack, read position directly.
    float3 LocalPos;
    if (Inst.HasFlag(EInstanceFlags::Skinned))
    {
        LocalPos = LoadSkinnedPositionByIndex(Inst.VBAddress, VertexIdx, Inst.BoneOffset);
    }
    else
    {
        LocalPos = LoadStaticPositionByIndex(Inst.VBAddress, VertexIdx);
    }

    float4x4 ModelMatrix = Inst.ModelMatrix;
    float4x4 View        = GetCameraView();
    float4x4 Projection  = GetCameraProjection();

    float4 WorldPos = mul(ModelMatrix, float4(LocalPos, 1.0));
    float4 ViewPos  = mul(View, WorldPos);
    Output.Position = mul(Projection, ViewPos);
    return Output;
}
