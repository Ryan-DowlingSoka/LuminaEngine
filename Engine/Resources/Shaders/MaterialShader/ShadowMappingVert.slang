#pragma once
#include "Includes/SceneGlobals.slang"

// Per-material shadow VS template; substitutes the same vertex graph as base/depth so shadows match
// displaced geometry (else animated grass casts a rigid shadow).
// NOTE: the substitution token (plain string find) must appear exactly once -- don't reference it anywhere else.

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

    // MeshletLocalIndex indexes the shadow range, so use ShadowMeshletOffset, not Surface.
    uint GlobalMeshletIdx = Inst.ShadowMeshletOffset + Draw.MeshletLocalIndex;
    FMeshlet M = Inst.MeshletHeader.Meshlets[GlobalMeshletIdx];

    uint TriIdx = VertexIndex / 3u;
    uint Corner = VertexIndex % 3u;

    VSOutput Output;

    if (TriIdx >= M.TriangleCount)
    {
        const float NaN = 0.0f / 0.0f;
        Output.Position = float4(NaN, NaN, NaN, 1.0);
        return Output;
    }

    uint PackedTri    = Inst.MeshletHeader.Triangles[M.TriangleOffset + TriIdx];
    uint LocalVertIdx = (PackedTri >> (Corner * 8u)) & 0xFFu;

    FVertexData VertexData;
    if (Inst.HasFlag(EInstanceFlags::Skinned))
    {
        VertexData = LoadSkinnedVertex(M, LocalVertIdx, Inst.SkinnedVertexBase);
    }
    else
    {
        VertexData = LoadStaticVertex(Inst.MeshletHeader, M, LocalVertIdx);
    }

    float4 WorldPos        = mul(ModelMatrix, float4(VertexData.Position, 1.0));
    float3x3 NormalMatrixWS = CalculateNormalMatrix(ModelMatrix);
    float3 NormalWS         = mul(NormalMatrixWS, VertexData.Normal);
    float3 TangentWS        = mul((float3x3)ModelMatrix, VertexData.Tangent.xyz);

    FMaterialVertexInputs Material;
    $MATERIAL_VERTEX_INPUTS
    WorldPos.xyz += Material.WorldPositionOffset;

    Output.Position = mul(GetShadowViewProjection(PC.ShadowDataIndex, (uint)PC.ViewIndex), WorldPos);
    return Output;
}
