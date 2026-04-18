#pragma once
#include "Includes/SceneGlobals.slang"


struct FPushConstants
{
    uint LightIndex;
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

    FVertexData VertexData;
    if (InstanceData.HasFlag(EInstanceFlags::Skinned))
    {
        VertexData = LoadSkinnedVertex(
            InstanceData.VBAddress,
            InstanceData.IBAddress,
            VertexIndex,
            InstanceData.BoneOffset);
    }
    else
    {
        VertexData = LoadStaticVertex(
            InstanceData.VBAddress,
            InstanceData.IBAddress,
            VertexIndex);
    }

    float4x4 ModelMatrix = GetShadowModelMatrix(InstanceIndex);
    float4   WorldPos    = mul(ModelMatrix, float4(VertexData.Position, 1.0));

    VSOutput Output;
    Output.WorldPos  = WorldPos.xyz;
    Output.LightPos  = uLightData[0].Lights[PC.LightIndex].Position;
    Output.Position  = mul(uLightData[0].Lights[PC.LightIndex].ViewProjection[ViewIndex], WorldPos);
    return Output;
}