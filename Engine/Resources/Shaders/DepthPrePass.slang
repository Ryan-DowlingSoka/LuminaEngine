#pragma once
#include "Includes/SceneGlobals.slang"

struct VertexOutput
{
    // Position math must match BaseVertexPass.slang for the equal-depth test.
    precise float4 Position : SV_Position;
};

// Depth-only meshlet VS. Padding invocations emit NaN triangles for discard.
[shader("vertex")]
VertexOutput VertexMain(uint VertexIndex: SV_VulkanVertexID, uint MeshletSlot: SV_VulkanInstanceID)
{
    FMeshletDraw Draw = MeshletDrawList()[MeshletSlot];
    FGPUInstance Inst = GetInstance(Draw.InstanceID);

    uint GlobalMeshletIdx = Inst.SurfaceMeshletOffset + Draw.MeshletLocalIndex;
    FMeshlet M = Inst.MeshletHeader.Meshlets[GlobalMeshletIdx];

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

    float3 LocalPos;
    if (Inst.HasFlag(EInstanceFlags::Skinned))
    {
        LocalPos = LoadSkinnedPosition(Inst.MeshletHeader, M, LocalVertIdx, Inst.SkinnedVertexBase);
    }
    else
    {
        LocalPos = LoadStaticPosition(Inst.MeshletHeader, M, LocalVertIdx);
    }

    float4x4 ModelMatrix = Inst.ModelMatrix;
    float4x4 View        = GetCameraView();
    float4x4 Projection  = GetCameraProjection();

    float4 WorldPos = mul(ModelMatrix, float4(LocalPos, 1.0));
    float4 ViewPos  = mul(View, WorldPos);
    Output.Position = mul(Projection, ViewPos);
    return Output;
}
