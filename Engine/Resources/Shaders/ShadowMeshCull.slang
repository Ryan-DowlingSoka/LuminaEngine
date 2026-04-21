#pragma once
#include "Includes/Culling.slang"

// Shadow-caster culling for point + spot lights. Accepts any caster that can
// influence a shadow view even if it sits outside the camera frustum, so
// off-screen geometry still casts shadows into the camera view. Surviving
// casters are emitted into uIndirectDrawDataShadow / uMappingDataShadow.
//
// Directional casters are handled exclusively by the meshlet cascade path
// (CascadeMeshletCull.slang) — this pass intentionally skips them.
//
// Additional culls:
//   - Distance: casters farther than ShadowMaxDistance from the camera are
//     dropped.
//   - Hi-Z occlusion: casters inside the camera frustum that are fully
//     occluded in the camera's depth pyramid are dropped (they cannot cast
//     visible shadows onto camera-visible geometry).

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

    // Shadow-cull's hot path reads only the 32B Cull block — no transform, no
    // vertex-pulling pointers. This is the pass that benefits most from the
    // split layout.
    FGPUInstanceCull Cull = GetInstanceCull(GID);

    // Non-caster: never emit.
    if (!Cull.HasFlag(EInstanceFlags::CastShadow))
        return;

    float3 Center = Cull.BoundsCenter;
    float  Radius = Cull.BoundsRadius;

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
        !Cull.HasFlag(EInstanceFlags::NoCull) &&
        FrustumContainsSphere(uSceneData.CullData.Frustum, Center, Radius))
    {
        bOccluded = IsHiZOccluded(Center, Radius);
    }

    if (bOccluded)
        return;

    const uint BatchID = Cull.DrawID;

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
