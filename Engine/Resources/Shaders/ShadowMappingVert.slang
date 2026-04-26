#pragma once
#include "Includes/SceneGlobals.slang"

// Unified shadow vertex shader.
//
// One vertex shader for every shadow view type: CSM cascade, point face, spot.
// The CPU builds a per-view entry in uCullViews and promotes surviving meshlets
// into uMeshletDrawList[View.DrawListOffset + ...]. The indirect draw points at
// uIndirectArgs[View.IndirectArgsOffset + DrawID] so SV_InstanceID indexes
// directly into the per-view slice with no extra mapping buffer.
//
// Point cube faces emit six separate views and six indirect draws instead of
// using multiview, so each face draws only the meshlets the cull pass proved
// visible to that face. ShadowDataIndex/ViewIndex reaches the right
// ViewProjection[] slot.

struct FPushConstants
{
    int ShadowDataIndex;
    int ViewIndex;          // 0..NumCascades-1 for CSM, 0..5 for point, 0 for spot.
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
    FMeshlet       M = Inst.MeshletHeader.Meshlets[GlobalMeshletIdx];
    FMeshletBounds B = Inst.MeshletHeader.Bounds[GlobalMeshletIdx];

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

    // Position-only; skip Normal/UV/Color unpack.
    float3 LocalPos;
    if (Inst.HasFlag(EInstanceFlags::Skinned))
    {
        LocalPos = LoadSkinnedPosition(Inst.MeshletHeader, M, B, LocalVertIdx, Inst.BoneOffset);
    }
    else
    {
        LocalPos = LoadStaticPosition(Inst.MeshletHeader, M, B, LocalVertIdx);
    }

    float4 WorldPos = mul(ModelMatrix, float4(LocalPos, 1.0));
    Output.Position = mul(GetShadowViewProjection(PC.ShadowDataIndex, (uint)PC.ViewIndex), WorldPos);
    return Output;
}
