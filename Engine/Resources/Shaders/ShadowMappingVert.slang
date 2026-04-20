#pragma once
#include "Includes/SceneGlobals.slang"


struct FPushConstants
{
    uint LightIndex;
    int  ShadowDataIndex;
};

struct VSOutput
{
    float3 WorldPos : TEXCOORD0;
    float3 LightPos : TEXCOORD1;
    float4 Position : SV_Position;
};

[shader("vertex")]
VSOutput VertexMain(uint VertexIndex: SV_VulkanVertexID, uint InstanceIndex: SV_VulkanInstanceID, uint ViewIndex : SV_ViewID, uniform FPushConstants PC)
{
    FGPUInstance InstanceData = GetShadowInstanceData(InstanceIndex);

    // Shadow passes pull through ShadowIBAddress — a position-only, deduped
    // index stream built at import time. Fewer unique vertices visit the
    // vertex shader, so VPC throughput on the CSM/spot/point shadow passes
    // drops directly. Aliases IBAddress for legacy assets without shadow
    // indices (see Mesh.cpp::GenerateGPUBuffers).
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

    float4x4 ModelMatrix = GetShadowModelMatrix(InstanceIndex);
    float4   WorldPos    = mul(ModelMatrix, float4(VertexData.Position, 1.0));

    VSOutput Output;
    Output.WorldPos  = WorldPos.xyz;
    Output.LightPos  = uLightData[0].Lights[PC.LightIndex].Position;
    Output.Position  = mul(GetShadowViewProjection(PC.ShadowDataIndex, ViewIndex), WorldPos);
    return Output;
}