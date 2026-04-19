#pragma once
#include "Includes/SceneGlobals.slang"

// Shadow-caster culling. Unlike MeshCull.slang (which uses the camera frustum and
// the camera depth pyramid) this pass accepts any caster that can influence *any*
// shadow view in the scene, so that objects outside the camera frustum still
// cast shadows into it.
//
// An instance survives if it is a shadow caster AND at least one of:
//   - There is a directional light and the instance intersects the extruded
//     camera frustum swept along the sun direction.
//   - Any shadow-casting point light's sphere contains the instance's bounds.
//   - Any shadow-casting spot light's cone (approximated by its bounding sphere)
//     contains the instance's bounds.
//
// Additional culls applied after the above:
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

    bool bVisible = false;

    // Directional: test the extruded frustum swept along the sun direction.
    if (uSceneData.CullData.bHasDirectional != 0)
    {
        if (InFrustum(uSceneData.CullData.ShadowFrustum, Center, Radius))
            bVisible = true;
    }

    // Point + spot lights: check each shadow-casting light's bounds.
    if (!bVisible)
    {
        uint NumLights = uLightData[0].NumLights;
        for (uint LightIdx = 0; LightIdx < NumLights; ++LightIdx)
        {
            FLight L = uLightData[0].Lights[LightIdx];
            if (L.Flags == LIGHT_FLAG_TYPE_DIRECTIONAL)
                continue;
            if (L.Shadow[0].ShadowMapIndex < 0)
                continue;
            if (InSphere(L.Position, L.Radius, Center, Radius))
            {
                bVisible = true;
                break;
            }
        }
    }

    if (!bVisible)
        return;

    // Camera-space Hi-Z occlusion: if this caster is fully inside the camera
    // frustum and fully occluded from the camera's perspective, it cannot cast
    // any visible shadow on camera-visible geometry, skip it.
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
                return;
        }
    }

    uint BatchID       = uInstanceData[GID].DrawID;
    uint InstanceCount = 0;
    InterlockedAdd(uIndirectDrawDataShadow[BatchID].InstanceCount, 1, InstanceCount);
    uint InstanceIndex = uIndirectDrawDataShadow[BatchID].FirstInstance + InstanceCount;

    uMappingDataShadow[InstanceIndex] = GID;
}
