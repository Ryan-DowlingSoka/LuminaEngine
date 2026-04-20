#pragma once
#include "Includes/SceneGlobals.slang"

// Shadow vertex shader. Two compile variants:
//   Default: spot/point, push const {LightIndex, ShadowDataIndex}, ViewID per face.
//   SHADOW_CSM: directional, push const {ShadowDataIndex, CascadeIndex}, meshlet-driven.

struct FPushConstants
{
#if SHADOW_CSM
    int ShadowDataIndex;
    int CascadeIndex;
#else
    uint LightIndex;
    int  ShadowDataIndex;
#endif
};

struct VSOutput
{
#if !SHADOW_CSM
    float3 WorldPos : TEXCOORD0;
    float3 LightPos : TEXCOORD1;
#endif
    float4 Position : SV_Position;
};

[shader("vertex")]
VSOutput VertexMain(
    uint VertexIndex   : SV_VulkanVertexID,
    uint InstanceIndex : SV_VulkanInstanceID,
#if !SHADOW_CSM
    uint ViewIndex     : SV_ViewID,
#endif
    uniform FPushConstants PC)
{
#if SHADOW_CSM
    // Meshlet-driven CSM. Out-of-range tri invocations emit NaN to discard.
    FMeshletDraw Draw         = uMeshletDrawListCascade[InstanceIndex];
    FGPUInstance InstanceData = GetInstanceDataByID(Draw.InstanceID);
    float4x4     ModelMatrix  = InstanceData.ModelMatrix;
    uint         ViewIndex    = (uint)PC.CascadeIndex;

    uint GlobalMeshletIdx = InstanceData.SurfaceMeshletOffset + Draw.MeshletLocalIndex;
    FMeshlet M            = InstanceData.MeshletHeader.Meshlets[GlobalMeshletIdx];

    uint TriIdx = VertexIndex / 3u;
    uint Corner = VertexIndex % 3u;

    VSOutput Output;

    if (TriIdx >= M.TriangleCount)
    {
        const float NaN = 0.0f / 0.0f;
        Output.Position = float4(NaN, NaN, NaN, 1.0);
        return Output;
    }

    uint PackedTri    = InstanceData.MeshletHeader.Triangles[M.TriangleOffset + TriIdx];
    uint LocalVertIdx = (PackedTri >> (Corner * 8u)) & 0xFFu;
    uint VertexIdx    = InstanceData.MeshletHeader.Vertices[M.VertexOffset + LocalVertIdx];

    FVertexData VertexData;
    if (InstanceData.HasFlag(EInstanceFlags::Skinned))
    {
        VertexData = LoadSkinnedVertexByIndex(InstanceData.VBAddress, VertexIdx, InstanceData.BoneOffset);
    }
    else
    {
        VertexData = LoadStaticVertexByIndex(InstanceData.VBAddress, VertexIdx);
    }

    float4 WorldPos = mul(ModelMatrix, float4(VertexData.Position, 1.0));
    Output.Position = mul(GetShadowViewProjection(PC.ShadowDataIndex, ViewIndex), WorldPos);
    return Output;
#else
    FGPUInstance InstanceData = GetShadowInstanceData(InstanceIndex);
    float4x4     ModelMatrix  = GetShadowModelMatrix(InstanceIndex);

    FVertexData VertexData;
    if (InstanceData.HasFlag(EInstanceFlags::Skinned))
    {
        VertexData = LoadSkinnedVertex(
            InstanceData.VBAddress,
            InstanceData.ShadowIBAddress,
            VertexIndex,
            InstanceData.BoneOffset);
    }
    else
    {
        VertexData = LoadStaticVertex(
            InstanceData.VBAddress,
            InstanceData.ShadowIBAddress,
            VertexIndex);
    }

    float4 WorldPos = mul(ModelMatrix, float4(VertexData.Position, 1.0));

    VSOutput Output;
    Output.WorldPos = WorldPos.xyz;
    Output.LightPos = uLightData[0].Lights[PC.LightIndex].Position;
    Output.Position = mul(GetShadowViewProjection(PC.ShadowDataIndex, ViewIndex), WorldPos);
    return Output;
#endif
}
