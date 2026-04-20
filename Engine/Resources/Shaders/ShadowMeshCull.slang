#pragma once
#include "Includes/Culling.slang"

// Shadow-caster culling. Unlike MeshCull.slang (which uses the camera frustum
// and the camera depth pyramid) this pass accepts any caster that can
// influence *any* shadow view in the scene, so that objects outside the
// camera frustum still cast shadows into it.
//
// Emission is split by light type:
//   - Directional: each cascade is tested independently against its own
//     frustum, and surviving casters are emitted into that cascade's slice of
//     uIndirectDrawDataCascade / uMappingDataCascade. A small caster that
//     only touches cascade 0 pays no VPC cost on cascades 1 and 2 — the main
//     win vs the previous single-frustum multiview path.
//   - Point + spot: emitted into uIndirectDrawDataShadow as before.
//
// Additional culls applied to both paths:
//   - Distance: instances farther than ShadowMaxDistance from the camera are
//     dropped.
//   - Hi-Z occlusion: instances inside the camera frustum that are fully
//     occluded in the camera's depth pyramid are dropped (they cannot cast
//     visible shadows).

#define LOCAL_SIZE 256

// Spot lights use a bounding sphere of radius `Radius` around their position.
// Conservative (overdraws a bit) but avoids building a tight spot cone on the
// GPU; real AAA engines trade a tight cone test for this at the cost of a
// handful of extra shader instructions per light.
bool InSphere(float3 LightPos, float LightRadius, float3 Center, float BoundsRadius)
{
    float3 D = Center - LightPos;
    float  R = LightRadius + BoundsRadius;
    return dot(D, D) <= R * R;
}

[shader("compute")]
[numthreads(LOCAL_SIZE, 1, 1)]
void ComputeMain(uint3 GlobalID : SV_DispatchThreadID)
{
    uint GID          = GlobalID.x;
    uint NumInstances = uSceneData.CullData.InstanceNum;

    if (GID >= NumInstances)
        return;

    FGPUInstance Instance = uInstanceData[GID];

    // Non-caster: never emit.
    if (!Instance.HasFlag(EInstanceFlags::CastShadow))
        return;

    float3 Center = Instance.BoundsCenter;
    float  Radius = Instance.BoundsRadius;

    // Distance cull: skip any caster too far from the camera to matter.
    float3 CamPos       = GetCameraPosition();
    float  DistToCamera = length(Center - CamPos);
    if (DistToCamera - Radius > uSceneData.CullData.ShadowMaxDistance)
        return;

    // Camera-space Hi-Z occlusion: if this caster is fully inside the camera
    // frustum and fully occluded from the camera's perspective, it cannot cast
    // any visible shadow on camera-visible geometry. Computed once and applied
    // to every emission path below.
    bool bOccluded = false;
    if (uSceneData.CullData.bShadowOcclusionCull != 0 &&
        !Instance.HasFlag(EInstanceFlags::NoCull) &&
        FrustumContainsSphere(uSceneData.CullData.Frustum, Center, Radius))
    {
        bOccluded = IsHiZOccluded(Center, Radius);
    }

    if (bOccluded)
        return;

    const uint BatchID  = uInstanceData[GID].DrawID;
    const uint NumDraws = uSceneData.CullData.NumDraws;

    // Directional: per-cascade emission. Each cascade has its own frustum and
    // its own indirect/mapping slice, so small casters don't fan out to every
    // cascade.
    if (uSceneData.CullData.bHasDirectional != 0)
    {
        [unroll]
        for (int c = 0; c < NUM_CASCADES; ++c)
        {
            if (!FrustumContainsSphere(uSceneData.CullData.CascadeFrustum[c], Center, Radius))
                continue;

            uint IndirectIndex = c * NumDraws + BatchID;

            uint CascadeCount;
            InterlockedAdd(uIndirectDrawDataCascade[IndirectIndex].InstanceCount, 1, CascadeCount);
            uint MappingIndex = uIndirectDrawDataCascade[IndirectIndex].FirstInstance + CascadeCount;
            uMappingDataCascade[MappingIndex] = GID;
        }
    }

    // Point + spot: emit into the shared shadow buffer if any shadow-casting
    // light's bounding volume contains the caster.
    bool bLocalLightHit = false;
    uint NumLights = uLightData[0].NumLights;
    for (uint LightIdx = 0; LightIdx < NumLights; ++LightIdx)
    {
        FLight L = uLightData[0].Lights[LightIdx];
        if (L.Flags == LIGHT_FLAG_TYPE_DIRECTIONAL)
            continue;
        if (L.ShadowDataIndex < 0)
            continue;
        if (InSphere(L.Position, L.Radius, Center, Radius))
        {
            bLocalLightHit = true;
            break;
        }
    }

    if (bLocalLightHit)
    {
        uint ShadowCount;
        InterlockedAdd(uIndirectDrawDataShadow[BatchID].InstanceCount, 1, ShadowCount);
        uint MappingIndex = uIndirectDrawDataShadow[BatchID].FirstInstance + ShadowCount;
        uMappingDataShadow[MappingIndex] = GID;
    }
}
