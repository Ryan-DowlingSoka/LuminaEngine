#pragma once
#include "Includes/SceneGlobals.slang"

// Per-material depth-prepass VS template. The vertex-graph token below is
// substituted at material-compile time with the WPO emission so the
// displaced depth matches the base pass. Without per-material specialization,
// [earlydepthstencil] in BasePixelPass would kill displaced fragments because
// their depth wouldn't match the un-displaced prepass write.
//
// NOTE: the substitution token must appear exactly once in this file. Don't
// reference it elsewhere (comments included) -- the substitution uses a
// plain string find().

struct VertexOutput
{
    // Position math must match BaseVertexPass.slang for the equal-depth test.
    precise float4 Position : SV_Position;
};

[shader("vertex")]
VertexOutput VertexMain(uint VertexIndex: SV_VulkanVertexID, uint MeshletSlot: SV_VulkanInstanceID)
{
    FMeshletDraw Draw = uMeshletDrawList[MeshletSlot];
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

    // Full vertex load (UV / normal / tangent / color) so the WPO graph has
    // the same inputs available as the base pass.
    FVertexData VertexData;
    if (Inst.HasFlag(EInstanceFlags::Skinned))
    {
        VertexData = LoadSkinnedVertex(Inst.MeshletHeader, M, LocalVertIdx, Inst.BoneOffset);
    }
    else
    {
        VertexData = LoadStaticVertex(Inst.MeshletHeader, M, LocalVertIdx);
    }

    float4x4 ModelMatrix = Inst.ModelMatrix;
    float4x4 View        = GetCameraView();
    float4x4 Projection  = GetCameraProjection();

    float4 WorldPos        = mul(ModelMatrix, float4(VertexData.Position, 1.0));
    float3x3 NormalMatrixWS = CalculateNormalMatrix(ModelMatrix);
    float3 NormalWS         = mul(NormalMatrixWS, VertexData.Normal);
    float3 TangentWS        = mul((float3x3)ModelMatrix, VertexData.Tangent.xyz);

    // Material vertex graph emission. Must match BaseVertexPass.slang.
    FMaterialVertexInputs Material;
    $MATERIAL_VERTEX_INPUTS
    WorldPos.xyz += Material.WorldPositionOffset;

    float4 ViewPos  = mul(View, WorldPos);
    Output.Position = mul(Projection, ViewPos);
    return Output;
}
