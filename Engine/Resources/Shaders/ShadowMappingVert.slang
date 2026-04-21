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
    FMeshletDraw       Draw        = uMeshletDrawListCascade[InstanceIndex];
    FGPUInstanceCull   Cull        = GetInstanceCull(Draw.InstanceID);
    FGPUInstanceRender Render      = GetInstanceRender(Draw.InstanceID);
    float4x4           ModelMatrix = GetInstanceTransform(Draw.InstanceID);
    uint               ViewIndex   = (uint)PC.CascadeIndex;

    uint GlobalMeshletIdx = Cull.SurfaceMeshletOffset + Draw.MeshletLocalIndex;
    FMeshlet M            = Render.MeshletHeader.Meshlets[GlobalMeshletIdx];

    uint TriIdx = VertexIndex / 3u;
    uint Corner = VertexIndex % 3u;

    VSOutput Output;

    if (TriIdx >= M.TriangleCount)
    {
        const float NaN = 0.0f / 0.0f;
        Output.Position = float4(NaN, NaN, NaN, 1.0);
        return Output;
    }

    uint PackedTri    = Render.MeshletHeader.Triangles[M.TriangleOffset + TriIdx];
    uint LocalVertIdx = (PackedTri >> (Corner * 8u)) & 0xFFu;
    uint VertexIdx    = Render.MeshletHeader.Vertices[M.VertexOffset + LocalVertIdx];

    // Shadow CSM: position-only, skip Normal/UV/Color unpack.
    float3 LocalPos;
    if (Cull.HasFlag(EInstanceFlags::Skinned))
    {
        LocalPos = LoadSkinnedPositionByIndex(Render.VBAddress, VertexIdx, Render.BoneOffset);
    }
    else
    {
        LocalPos = LoadStaticPositionByIndex(Render.VBAddress, VertexIdx);
    }

    float4 WorldPos = mul(ModelMatrix, float4(LocalPos, 1.0));
    Output.Position = mul(GetShadowViewProjection(PC.ShadowDataIndex, ViewIndex), WorldPos);
    return Output;
#else
    // Spot/point shadow pass. Index path (not meshlet-driven) — pull through
    // the per-instance shadow index buffer.
    uint               InstanceID  = ShadowDrawIDToInstanceID(InstanceIndex);
    FGPUInstanceCull   Cull        = GetInstanceCull(InstanceID);
    FGPUInstanceRender Render      = GetInstanceRender(InstanceID);
    float4x4           ModelMatrix = GetInstanceTransform(InstanceID);

    // Spot/point shadow: position-only, skip Normal/UV/Color unpack.
    float3 LocalPos;
    if (Cull.HasFlag(EInstanceFlags::Skinned))
    {
        LocalPos = LoadSkinnedPosition(
            Render.VBAddress,
            Render.ShadowIBAddress,
            VertexIndex,
            Render.BoneOffset);
    }
    else
    {
        LocalPos = LoadStaticPosition(
            Render.VBAddress,
            Render.ShadowIBAddress,
            VertexIndex);
    }

    float4 WorldPos = mul(ModelMatrix, float4(LocalPos, 1.0));

    VSOutput Output;
    Output.WorldPos = WorldPos.xyz;
    Output.LightPos = uLightData[0].Lights[PC.LightIndex].Position;
    Output.Position = mul(GetShadowViewProjection(PC.ShadowDataIndex, ViewIndex), WorldPos);
    return Output;
#endif
}
