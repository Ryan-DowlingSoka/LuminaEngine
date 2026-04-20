#pragma once
#include "Includes/SceneGlobals.slang"

// Unified shadow vertex shader, compiled twice:
//
//   Default (spot + point)
//     - Push constant: { LightIndex, ShadowDataIndex }.
//     - Face/cascade index comes from SV_ViewID (multiview) — 6 faces for a
//       point light, 1 for a spot light.
//     - Instance lookups go through uMappingDataShadow (shared across all
//       non-directional shadow casters).
//     - Outputs WorldPos + LightPos so ShadowMappingPixel.slang can write
//       linearized distance to depth for point lights.
//
//   With SHADOW_CSM defined (directional cascades)
//     - Push constant: { ShadowDataIndex, CascadeIndex }.
//     - The CSM path issues NUM_CASCADES separate DrawIndirect calls against
//       per-cascade slices, so the cascade index arrives via push constant
//       rather than SV_ViewID.
//     - Instance lookups go through uMappingDataCascade.
//     - Depth-only — no pixel shader is bound, so no varyings are emitted.
//
// Both paths pull vertices through ShadowIBAddress (position-only, deduped
// index stream built at import time) so fewer unique vertices visit the
// vertex shader and VPC throughput on shadow passes drops directly.

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
    FGPUInstance InstanceData = GetCascadeInstanceData(InstanceIndex);
    float4x4     ModelMatrix  = GetCascadeModelMatrix(InstanceIndex);
    uint         ViewIndex    = (uint)PC.CascadeIndex;
#else
    FGPUInstance InstanceData = GetShadowInstanceData(InstanceIndex);
    float4x4     ModelMatrix  = GetShadowModelMatrix(InstanceIndex);
#endif

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
#if !SHADOW_CSM
    Output.WorldPos = WorldPos.xyz;
    Output.LightPos = uLightData[0].Lights[PC.LightIndex].Position;
#endif
    Output.Position = mul(GetShadowViewProjection(PC.ShadowDataIndex, ViewIndex), WorldPos);
    return Output;
}
