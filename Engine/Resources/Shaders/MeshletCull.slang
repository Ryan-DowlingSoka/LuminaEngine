#pragma once
#include "Includes/Culling.slang"

// Meshlet-level culling.
//
// Each meshlet's bind-pose bounding sphere is transformed to world space via
// the instance's ModelMatrix and tested against the camera frustum + Hi-Z
// pyramid. Skinned instances skip per-meshlet culling (bind-pose bounds don't
// match the deformed mesh) but still emit every meshlet so the base vertex
// shader can pull them through the meshlet header.

#define LOCAL_SIZE_X 64

[shader("compute")]
[numthreads(LOCAL_SIZE_X, 1, 1)]
void ComputeMain(uint3 GlobalID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    // CPU splits instance count across Y/Z when Num > 65535 (Vulkan groupCountY limit).
    uint InstanceID      = GroupID.y + GroupID.z * 65535;
    uint MeshletLocalIdx = GlobalID.x;
    uint NumInstances    = uSceneData.CullData.InstanceNum;

    if (InstanceID >= NumInstances)
        return;

    FGPUInstance Inst = GetInstance(InstanceID);

    // Per-surface meshlet range. Instances whose mesh had no meshlets built
    // (import failure, zero-triangle surface) will short-circuit here.
    if (Inst.SurfaceMeshletCount == 0)
        return;

    if (MeshletLocalIdx >= Inst.SurfaceMeshletCount)
        return;

    bool bVisible = true;
    bool bSkinned = Inst.HasFlag(EInstanceFlags::Skinned);
    bool bNoCull  = Inst.HasFlag(EInstanceFlags::NoCull);

    if (!bSkinned && !bNoCull)
    {
        float4x4 Model = Inst.ModelMatrix;

        uint GlobalMeshletIdx = Inst.SurfaceMeshletOffset + MeshletLocalIdx;
        FMeshletBounds Bounds = Inst.MeshletHeader.Bounds[GlobalMeshletIdx];

        // Meshlet bounds live in mesh-local space; lift them into world space
        // through the instance transform. Radius scales by the largest column
        // magnitude so non-uniform scales stay conservative.
        float3 WorldCenter = mul(Model, float4(Bounds.Center, 1.0)).xyz;

        float SqScaleX = dot(Model[0].xyz, Model[0].xyz);
        float SqScaleY = dot(Model[1].xyz, Model[1].xyz);
        float SqScaleZ = dot(Model[2].xyz, Model[2].xyz);
        float MaxScale = sqrt(max(SqScaleX, max(SqScaleY, SqScaleZ)));
        float WorldRadius = Bounds.Radius * MaxScale;

        if (uSceneData.CullData.bFrustumCull != 0)
        {
            bVisible = FrustumContainsSphere(uSceneData.CullData.Frustum, WorldCenter, WorldRadius);
        }

        // Back-face cone cull. ConeCutoff == 1.0 signals the meshlet's triangle
        // normals span >180 deg (degenerate cone), in which case the test can't
        // prove back-facing and we skip it. The rearranged form avoids a
        // normalize: dot(normalize(apex - cam), axis) >= cutoff  <=>
        // dot(apex - cam, axis) >= cutoff * length(apex - cam).
        if (bVisible && Bounds.ConeCutoff < 1.0)
        {
            float3 WorldApex  = mul(Model, float4(Bounds.ConeApex, 1.0)).xyz;
            float3 WorldAxis  = normalize(mul((float3x3)Model, Bounds.ConeAxis));
            float3 ApexToCam  = WorldApex - GetCameraPosition();
            float  ApexToCamLen = length(ApexToCam);

            if (dot(ApexToCam, WorldAxis) >= Bounds.ConeCutoff * ApexToCamLen)
            {
                bVisible = false;
            }
        }

        // Micro-poly rejection. Project the meshlet's bounding sphere to the
        // screen and reject if its AABB contains no pixel-center sample point
        // in either axis. D3D/Vulkan samples are at (n + 0.5, n + 0.5) for
        // integer n; for an AABB [MinPx, MaxPx] to cover a sample its range
        // must contain some n + 0.5, i.e. there must exist integer n in
        // [ceil(MinPx - 0.5), floor(MaxPx - 0.5)]. That interval is empty iff
        // floor(MinPx - 0.5) == floor(MaxPx - 0.5). If either axis is empty
        // the meshlet lands between two pixel grids and never rasterizes.
        //
        // In pathologically geometry-dense scenes, the majority of meshlets
        // project subpixel and get rejected here, saving a whole draw slot
        // and the PS warp the rasterizer would otherwise spawn for zero
        // coverage.
        if (bVisible)
        {
            float3 ViewCenter = mul(uSceneData.CameraView.CameraView, float4(WorldCenter, 1.0)).xyz;
            float4 ClipAABB;
            if (ProjectSphereClip(ViewCenter, WorldRadius,
                                  uSceneData.NearPlane,
                                  uSceneData.CameraView.CameraProjection[0][0],
                                  uSceneData.CameraView.CameraProjection[1][1],
                                  ClipAABB))
            {
                float2 ScreenSize = float2(uSceneData.ScreenSize.xy);
                float2 MinPx = (ClipAABB.xy * 0.5 + 0.5) * ScreenSize;
                float2 MaxPx = (ClipAABB.zw * 0.5 + 0.5) * ScreenSize;

                int2 MinI = int2(floor(MinPx - 0.5));
                int2 MaxI = int2(floor(MaxPx - 0.5));

                if (MinI.x == MaxI.x || MinI.y == MaxI.y)
                {
                    bVisible = false;
                }
            }
            // ProjectSphereClip returning false means the sphere straddles
            // the near plane; leave bVisible=true so we don't wrongly cull
            // geometry the camera is inside.
        }

        if (bVisible && uSceneData.CullData.bOcclusionCull != 0)
        {
            if (IsHiZOccluded(WorldCenter, WorldRadius))
            {
                bVisible = false;
            }
        }
    }

    if (bVisible)
    {
        uint DrawID = Inst.DrawID;
        uint OldCount;
        InterlockedAdd(uIndirectDrawDataMeshlet[DrawID].InstanceCount, 1, OldCount);
        uint Slot = uIndirectDrawDataMeshlet[DrawID].FirstInstance + OldCount;

        FMeshletDraw Draw;
        Draw.InstanceID        = InstanceID;
        Draw.MeshletLocalIndex = MeshletLocalIdx;
        uMeshletDrawList[Slot] = Draw;
    }
}
