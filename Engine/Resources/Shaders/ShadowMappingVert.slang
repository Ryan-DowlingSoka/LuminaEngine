#pragma once
#include "Includes/SceneGlobals.slang"

// Unified shadow VS for CSM / point / spot. SV_InstanceID indexes the
// per-view slice of uMeshletDrawList; PC.ViewIndex picks the ViewProjection.

struct FPushConstants
{
    int ShadowDataIndex;
    int ViewIndex;  // CSM cascade, point face, or 0 for spot.
};

struct VSOutput
{
    float4 Position : SV_Position;
};

[shader("vertex")]
VSOutput VertexMain(
    uint VertexIndex   : SV_VulkanVertexID,
    uint InstanceIndex : SV_VulkanInstanceID,
    uniform FPushConstants PC)
{
    FMeshletDraw Draw        = uMeshletDrawList[InstanceIndex];
    FGPUInstance Inst        = GetInstance(Draw.InstanceID);
    float4x4     ModelMatrix = Inst.ModelMatrix;

    uint GlobalMeshletIdx = Inst.SurfaceMeshletOffset + Draw.MeshletLocalIndex;
    FMeshlet M = Inst.MeshletHeader.Meshlets[GlobalMeshletIdx];

    uint TriIdx = VertexIndex / 3u;
    uint Corner = VertexIndex % 3u;

    VSOutput Output;

    // Out-of-range tri invocations emit NaN to discard.
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
        LocalPos = LoadSkinnedPosition(Inst.MeshletHeader, M, LocalVertIdx, Inst.BoneOffset);
    }
    else
    {
        LocalPos = LoadStaticPosition(Inst.MeshletHeader, M, LocalVertIdx);
    }

    float4 WorldPos = mul(ModelMatrix, float4(LocalPos, 1.0));
    Output.Position = mul(GetShadowViewProjection(PC.ShadowDataIndex, (uint)PC.ViewIndex), WorldPos);
    return Output;
}
