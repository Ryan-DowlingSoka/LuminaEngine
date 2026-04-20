#pragma once
#include "Includes/SceneGlobals.slang"

// Shadow-caster culling. Unlike MeshCull.slang (which uses the camera frustum and
// the camera depth pyramid) this pass accepts any caster that can influence *any*
// shadow view in the scene, so that objects outside the camera frustum still
// cast shadows into it.
//
// Emission is split by light type:
//   - Directional: each cascade is tested independently against its own frustum,
//     and surviving casters are emitted into that cascade's slice of
//     uIndirectDrawDataCascade / uMappingDataCascade. A small caster that only
//     touches cascade 0 pays no VPC cost on cascades 1 and 2 — the main win vs
//     the previous single-frustum multiview path.
//   - Point + spot: emitted into uIndirectDrawDataShadow as before.
//
// Additional culls applied to both paths:
//   - Distance: instances farther than ShadowMaxDistance from the camera are dropped.
//   - Hi-Z occlusion: instances inside the camera frustum that are fully occluded
//     in the camera's depth pyramid are dropped (they cannot cast visible shadows).

#define LOCAL_SIZE 256

bool InFrustum(FFrustum F, float3 Center, float Radius)
{
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        float4 Plane = F.Planes[i];
        float  Dist  = dot(Plane.xyz, Center) + Plane.w;
        if (Dist < -Radius)
        {
            return false;
        }
    }
    return true;
}

// Spot lights use a bounding sphere of radius `Radius` around their position.
// This is conservative (overdraws a bit) but avoids building a tight spot cone
// on the GPU; real AAA engines trade a tight cone test for this at the cost of
// a handful of extra shader instructions per light.
bool InSphere(float3 LightPos, float LightRadius, float3 Center, float BoundsRadius)
{
    float3 D = Center - LightPos;
    float  R = LightRadius + BoundsRadius;
    return dot(D, D) <= R * R;
}

// Projects a view-space bounding sphere to a clip-space AABB for Hi-Z sampling.
// Returns false if the sphere straddles the near plane (treat as visible).
bool ProjectSphereClip(float3 C, float R, float ZNear, float P00, float P11, out float4 ClipAABB)
{
    ClipAABB = float4(-1.0, -1.0, 1.0, 1.0);
    if (C.z - R < ZNear)
        return false;

    float2 cx = float2(C.x, C.z);
    float  tx = sqrt(dot(cx, cx) - R * R);
    float2 minX = float2(tx * cx.x - R * cx.y,  R * cx.x + tx * cx.y);
    float2 maxX = float2(tx * cx.x + R * cx.y, -R * cx.x + tx * cx.y);

    float2 cy = float2(C.y, C.z);
    float  ty = sqrt(dot(cy, cy) - R * R);
    float2 minY = float2(ty * cy.x - R * cy.y,  R * cy.x + ty * cy.y);
    float2 maxY = float2(ty * cy.x + R * cy.y, -R * cy.x + ty * cy.y);

    ClipAABB = float4(
        (minX.x / minX.y) * P00,
        (minY.x / minY.y) * P11,
        (maxX.x / maxX.y) * P00,
        (maxY.x / maxY.y) * P11
    );
    return true;
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
    float3 CamPos        = GetCameraPosition();
    float  DistToCamera  = length(Center - CamPos);
    if (DistToCamera - Radius > uSceneData.CullData.ShadowMaxDistance)
        return;

    // Camera-space Hi-Z occlusion: if this caster is fully inside the camera
    // frustum and fully occluded from the camera's perspective, it cannot cast
    // any visible shadow on camera-visible geometry. Computed once and applied
    // to every emission path below.
    bool bOccluded = false;
    if (uSceneData.CullData.bShadowOcclusionCull != 0 &&
        !Instance.HasFlag(EInstanceFlags::NoCull) &&
        InFrustum(uSceneData.CullData.Frustum, Center, Radius))
    {
        float3 ViewCenter = mul(uSceneData.CullData.View, float4(Center, 1.0)).xyz;
        float  ViewRadius = Radius * 1.02;

        float4 ClipAABB;
        if (ProjectSphereClip(ViewCenter, ViewRadius,
                              uSceneData.CullData.zNear,
                              uSceneData.CullData.P00,
                              uSceneData.CullData.P11,
                              ClipAABB))
        {
            float4 UvAABB = saturate(ClipAABB * 0.5 + 0.5);

            float Width  = (UvAABB.z - UvAABB.x) * uSceneData.CullData.PyramidWidth;
            float Height = (UvAABB.w - UvAABB.y) * uSceneData.CullData.PyramidHeight;
            float Level  = max(ceil(log2(max(Width, Height) * 0.5)), 0.0);

            float2 SampleUV    = (UvAABB.xy + UvAABB.zw) * 0.5;
            float  PyramidDepth = uDepthPyramid.SampleLevel(SampleUV, Level).x;

            float FrontZ      = ViewCenter.z - ViewRadius;
            float ZNear       = uSceneData.CullData.zNear;
            float ZFar        = uSceneData.CullData.zFar;
            float DepthSphere = (ZNear * ZFar / FrontZ - ZNear) / (ZFar - ZNear);

            // Reverse-Z: occluded if sphere's nearest depth is farther than pyramid min.
            if (DepthSphere < PyramidDepth)
                bOccluded = true;
        }
    }

    if (bOccluded)
        return;

    const uint BatchID  = uInstanceData[GID].DrawID;
    const uint NumDraws = uSceneData.CullData.NumDraws;

    // Directional: per-cascade emission. Each cascade has its own frustum and its
    // own indirect/mapping slice, so small casters don't fan out to every cascade.
    if (uSceneData.CullData.bHasDirectional != 0)
    {
        [unroll]
        for (int c = 0; c < NUM_CASCADES; ++c)
        {
            if (!InFrustum(uSceneData.CullData.CascadeFrustum[c], Center, Radius))
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
