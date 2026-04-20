#pragma once
#include "Includes/SceneGlobals.slang"

// CSM vertex shader. Unlike ShadowMappingVert.slang (used for spot + point
// shadows, which still rely on SV_ViewID multiview for the 6 faces of a point
// light), the CSM path issues NUM_CASCADES separate DrawIndirect calls against
// per-cascade slices of the indirect/mapping buffers — so the cascade index
// comes in via push constant and instance lookups go through
// uMappingDataCascade.

struct FPushConstants
{
    int ShadowDataIndex;    // Index into FLightShadowData[] for the directional light.
    int CascadeIndex;       // Which cascade we're rasterizing this draw.
};

struct VSOutput
{
    float4 Position : SV_Position;
};

[shader("vertex")]
VSOutput VertexMain(uint VertexIndex: SV_VulkanVertexID, uint InstanceIndex: SV_VulkanInstanceID, uniform FPushConstants PC)
{
    FGPUInstance InstanceData = GetCascadeInstanceData(InstanceIndex);

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

    float4x4 ModelMatrix = GetCascadeModelMatrix(InstanceIndex);
    float4   WorldPos    = mul(ModelMatrix, float4(VertexData.Position, 1.0));

    VSOutput Output;
    Output.Position = mul(GetShadowViewProjection(PC.ShadowDataIndex, PC.CascadeIndex), WorldPos);
    return Output;
}
