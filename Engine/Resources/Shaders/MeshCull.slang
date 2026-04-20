#pragma once
#include "Includes/Culling.slang"

// Camera-view culling. Tests each instance against the camera frustum and
// (optionally) the camera depth pyramid, then emits surviving instances into
// the main indirect/mapping buffers used by the depth pre-pass, base pass,
// billboards, etc.

#define LOCAL_SIZE 256

[shader("compute")]
[numthreads(LOCAL_SIZE, 1, 1)]
void ComputeMain(uint3 GlobalID : SV_DispatchThreadID)
{
    uint GID          = GlobalID.x;
    uint NumInstances = uSceneData.CullData.InstanceNum;

    if (GID >= NumInstances)
        return;

    float4 SphereBounds = uInstanceData[GID].SphereBounds;
    float3 Center       = SphereBounds.xyz;
    float  Radius       = SphereBounds.w;

    bool bVisible = true;

    if (uSceneData.CullData.bFrustumCull != 0)
    {
        bVisible = FrustumContainsSphere(uSceneData.CullData.Frustum, Center, Radius);
    }

    bool bSkipOcclusion = uInstanceData[GID].HasFlag(EInstanceFlags::NoCull);

    if (bVisible && !bSkipOcclusion && uSceneData.CullData.bOcclusionCull != 0)
    {
        if (IsHiZOccluded(Center, Radius))
        {
            bVisible = false;
        }
    }

    if (bVisible)
    {
        uint BatchID       = uInstanceData[GID].DrawID;
        uint InstanceCount = 0;
        InterlockedAdd(uIndirectDrawData[BatchID].InstanceCount, 1, InstanceCount);
        uint InstanceIndex = uIndirectDrawData[BatchID].FirstInstance + InstanceCount;

        uMappingData[InstanceIndex] = GID;
    }
}
