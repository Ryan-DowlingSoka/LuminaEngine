#pragma once
#include "SceneGlobals.slang"

// Helpers shared by the GPU culling compute passes. Kept in one place so the
// passes can't drift on the Hi-Z math that has to agree with how the depth
// pyramid is generated (MIN-reduction sampler, reverse-Z).

bool FrustumContainsSphere(FFrustum F, float3 Center, float Radius)
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

    if (C.z - R < ZNear)
    {
        return false;
    }

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

// Reverse-Z depth matching glm::perspective(fov, asp, Far, Near):
// depth(z) = (N * F / z - N) / (F - N).
float SphereFrontDepth(float FrontZ, float ZNear, float ZFar)
{
    return (ZNear * ZFar / FrontZ - ZNear) / (ZFar - ZNear);
}

// Tests a world-space sphere against the camera depth pyramid.
// Returns true if the sphere is fully occluded and can be skipped.
bool IsHiZOccluded(float3 CenterWS, float RadiusWS)
{
    float  ZNear = uSceneData.NearPlane;
    float  ZFar  = uSceneData.FarPlane;

    float3 ViewCenter = mul(uSceneData.CameraView.CameraView, float4(CenterWS, 1.0)).xyz;
    float  ViewRadius = RadiusWS * 1.02; // Tiny dilation for conservatism.

    float4 ClipAABB;
    if (!ProjectSphereClip(ViewCenter, ViewRadius, ZNear,
                           uSceneData.CameraView.CameraProjection[0][0],
                           uSceneData.CameraView.CameraProjection[1][1],
                           ClipAABB))
    {
        // Sphere straddles the near plane: treat as visible.
        return false;
    }

    // Clip [-1,1] -> Vulkan UV [0,1].
    float4 UvAABB = saturate(ClipAABB * 0.5 + 0.5);

    float Width  = (UvAABB.z - UvAABB.x) * uSceneData.CullData.PyramidWidth;
    float Height = (UvAABB.w - UvAABB.y) * uSceneData.CullData.PyramidHeight;

    // Pick a mip where a single bilinear tap (2x2) covers the AABB.
    float Level = max(ceil(log2(max(Width, Height) * 0.5)), 0.0);

    float2 SampleUV = (UvAABB.xy + UvAABB.zw) * 0.5;

    // uDepthPyramid uses a MIN-reduction sampler, so this tap returns the
    // minimum depth of the 2x2 footprint. In reverse-Z, min == farthest.
    float PyramidDepth = uDepthPyramid.SampleLevel(SampleUV, Level).x;

    float FrontZ      = ViewCenter.z - ViewRadius;
    float DepthSphere = SphereFrontDepth(FrontZ, ZNear, ZFar);

    // Reverse-Z: occluded iff the sphere's nearest depth is farther than the
    // farthest occluder in its footprint.
    return DepthSphere < PyramidDepth;
}
