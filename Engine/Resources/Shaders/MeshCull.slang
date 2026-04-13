#pragma once
#include "Includes/SceneGlobals.slang"

#define LOCAL_SIZE 256

bool IsInFrustum(uint Index)
{
    float4 SphereBounds = uInstanceData[Index].SphereBounds;
    float3 Center       = SphereBounds.xyz;
    float  Radius       = SphereBounds.w;

    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        float4 Plane = uSceneData.CullData.Frustum.Planes[i];
        float  Dist  = dot(Plane.xyz, Center) + Plane.w;
        if (Dist < -Radius)
        {
            return false;
        }
    }
    return true;
}

// 2D polyhedral bounds of a clipped perspective-projected sphere.
// Michael Mara, Morgan McGuire (2013), adapted for left-handed view space
// where the camera looks down +Z (visible points have C.z > 0).
//
// On success, ClipAABB = (minClipX, minClipY, maxClipX, maxClipY) with each
// component already divided by clip.w. Returns false if the sphere clips the
// near plane, in which case the caller should treat the object as visible.
bool ProjectSphereClip(float3 C, float R, float ZNear, float P00, float P11, out float4 ClipAABB)
{
    ClipAABB = float4(-1.0, -1.0, 1.0, 1.0);

    // Sphere straddles (or is behind) the near plane, don't occlusion cull.
    if (C.z - R < ZNear)
    {
      return false;
    }
    
    float2 cx = float2(C.x, C.z);
    float  tx = sqrt(dot(cx, cx) - R * R);

    float2 minX = float2(tx * cx.x - R  * cx.y,  R  * cx.x + tx * cx.y);
    float2 maxX = float2(tx * cx.x + R  * cx.y, -R  * cx.x + tx * cx.y);

    float2 cy = float2(C.y, C.z);
    float  ty = sqrt(dot(cy, cy) - R * R);

    float2 minY = float2(ty * cy.x - R  * cy.y,  R  * cy.x + ty * cy.y);
    float2 maxY = float2(ty * cy.x + R  * cy.y, -R  * cy.x + ty * cy.y);

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

    float ZNear = uSceneData.CullData.zNear;
    float ZFar  = uSceneData.CullData.zFar;

    bool bVisible = true;

    if (uSceneData.CullData.bFrustumCull != 0)
    {
        bVisible = IsInFrustum(GID);
    }

    bool bSkipOcclusion = uInstanceData[GID].HasFlag(EInstanceFlags::NoCull);

    if (bVisible && !bSkipOcclusion && uSceneData.CullData.bOcclusionCull != 0)
    {
        float4 SphereWS = uInstanceData[GID].SphereBounds;

        // Transform world-space bounding sphere into view space.
        float3 Center = mul(uSceneData.CullData.View, float4(SphereWS.xyz, 1.0)).xyz;
        float  Radius = SphereWS.w * 1.02; // Tiny dilation for conservatism.

        float4 ClipAABB;
        if (ProjectSphereClip(Center, Radius, ZNear,
                              uSceneData.CullData.P00,
                              uSceneData.CullData.P11,
                              ClipAABB))
        {
            // Clip [-1,1] -> Vulkan UV [0,1].
            float4 UvAABB = saturate(ClipAABB * 0.5 + 0.5);

            float Width  = (UvAABB.z - UvAABB.x) * uSceneData.CullData.PyramidWidth;
            float Height = (UvAABB.w - UvAABB.y) * uSceneData.CullData.PyramidHeight;

            // Pick a mip where a single bilinear tap (2x2) covers the AABB.
            float Level = ceil(log2(max(Width, Height) * 0.5));
            Level = max(Level, 0.0);

            float2 SampleUV = (UvAABB.xy + UvAABB.zw) * 0.5;

            // uDepthPyramid uses a MIN-reduction sampler, so this tap returns the
            // minimum depth of the 2x2 footprint. In reverse-Z, min == farthest.
            float PyramidDepth = uDepthPyramid.SampleLevel(SampleUV, Level).x;

            // Nearest point of the sphere toward the camera.
            float FrontZ = Center.z - Radius;

            // Exact reverse-Z depth matching glm::perspective(fov, asp, Far, Near):
            // depth(z) = (N * F / z - N) / (F - N)
            float DepthSphere = (ZNear * ZFar / FrontZ - ZNear) / (ZFar - ZNear);

            // Reverse-Z: larger depth == closer. Sphere is visible iff its
            // nearest point is at least as close as the farthest occluder.
            bVisible = bVisible && (DepthSphere >= PyramidDepth);
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
