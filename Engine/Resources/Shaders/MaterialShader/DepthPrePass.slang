#pragma once
#include "Includes/SceneGlobals.slang"

// Per-material depth-prepass VS template; the vertex-graph token below is substituted with WPO so
// displaced depth matches the base pass (else [earlydepthstencil] would kill displaced fragments).
// NOTE: the substitution token (plain string find) must appear exactly once -- don't reference it anywhere else.

struct VertexOutput
{
    // Position math must match BaseVertexPass.slang for the equal-depth test.
    precise float4 Position : SV_Position;
};

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

    // Full vertex load (UV / normal / tangent / color) so the WPO graph has
    // the same inputs available as the base pass.
    FVertexData VertexData;
    if (Inst.HasFlag(EInstanceFlags::Skinned))
    {
        VertexData = LoadSkinnedVertex(M, LocalVertIdx, Inst.SkinnedVertexBase);
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
