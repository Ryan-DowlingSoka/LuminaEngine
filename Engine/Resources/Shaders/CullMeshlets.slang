#pragma once
#include "Includes/Culling.slang"

// Unified meshlet culling.
//
// Each thread owns one (instance, meshlet-local) pair
// and tests its world-space bounding sphere against every active FCullView.
// Surviving meshlets are atomically appended into the slice of uMeshletDrawList
// owned by that view (View.DrawListOffset + atomic counter), with the per-view
// indirect slot in uIndirectArgs[View.IndirectArgsOffset + DrawID] tracking the
// instance count the draw pass will consume.
//
// The cone cull fires ONCE per meshlet against whichever axis the view asks for
// (view origin for point/spot/camera, sun direction for directional cascades)
// and short-circuits all that view's tests when it hits.
//
// One shader owns every view, so adding a new light type or changing the main
// camera's cull policy only touches the FCullView flags the CPU writes.

#define LOCAL_SIZE_X 64

// Push constant: NumViews (uniform, matches uSceneData view count but kept in
// push so the shader doesn't pay a scene fetch per thread).
struct FPushConstants
{
    uint NumViews;
};

[shader("compute")]
[numthreads(LOCAL_SIZE_X, 1, 1)]
void ComputeMain(uint3 GlobalID : SV_DispatchThreadID, uint3 GroupID  : SV_GroupID, uniform FPushConstants PC)
{
    // CPU splits instance count across Y/Z when Num > 65535 (Vulkan groupCountY limit).
    uint InstanceID      = GroupID.y + GroupID.z * 65535;
    uint MeshletLocalIdx = GlobalID.x;
    uint NumInstances    = uSceneData.CullData.InstanceNum;

    if (InstanceID >= NumInstances)
        return;

    FGPUInstance Inst = GetInstance(InstanceID);

    if (Inst.SurfaceMeshletCount == 0)
        return;

    if (MeshletLocalIdx >= Inst.SurfaceMeshletCount)
        return;

    // Per-instance / per-meshlet data reused across every view.
    bool bSkinned = Inst.HasFlag(EInstanceFlags::Skinned);
    bool bNoCull  = Inst.HasFlag(EInstanceFlags::NoCull);
    bool bCaster  = Inst.HasFlag(EInstanceFlags::CastShadow);

    float4x4 Model = Inst.ModelMatrix;

    uint GlobalMeshletIdx = Inst.SurfaceMeshletOffset + MeshletLocalIdx;
    FMeshletBounds Bounds = Inst.MeshletHeader.Bounds[GlobalMeshletIdx];

    float3 WorldCenter = mul(Model, float4(Bounds.Center, 1.0)).xyz;

    float SqScaleX = dot(Model[0].xyz, Model[0].xyz);
    float SqScaleY = dot(Model[1].xyz, Model[1].xyz);
    float SqScaleZ = dot(Model[2].xyz, Model[2].xyz);
    float MaxScale = sqrt(max(SqScaleX, max(SqScaleY, SqScaleZ)));
    float WorldRadius = Bounds.Radius * MaxScale;

    // Cone cull wants the world-space apex + axis but only when the view asks
    // for it, so derive them lazily (most shadow views enable cone cull, the
    // camera view enables cone+micro-poly, cascades skip per-cascade cone).
    float3 WorldApex = mul(Model, float4(Bounds.ConeApex, 1.0)).xyz;
    float3 WorldAxis = normalize(mul((float3x3)Model, Bounds.ConeAxis));
    bool   bHasCone  = Bounds.ConeCutoff < 1.0;

    uint DrawID = Inst.DrawID;
    uint NumViews = PC.NumViews;

    // Instance-level camera distance reused for CastShadowOnly views.
    float3 CamPos        = GetCameraPosition();
    float3 InstCenter    = Inst.BoundsCenter;
    float  InstRadius    = Inst.BoundsRadius;
    float  DistToCamera  = length(InstCenter - CamPos);
    float  MaxShadowDist = uSceneData.CullData.ShadowMaxDistance;

    for (uint v = 0; v < NumViews; ++v)
    {
        FCullView View = uCullViews[v];
        uint Flags = View.Flags;

        if (View.HasFlag(CULL_VIEW_FLAG_CAST_SHADOW_ONLY) && !bCaster)
            continue;

        if (View.HasFlag(CULL_VIEW_FLAG_DISTANCE))
        {
            if (DistToCamera - InstRadius > MaxShadowDist)
                continue;
        }

        // Cone cull (cheapest reject before frustum). Sun views test against the
        // sun direction; camera/point/spot test against apex-to-origin.
        if (!bSkinned && !bNoCull && bHasCone && View.HasFlag(CULL_VIEW_FLAG_CONE))
        {
            if (View.HasFlag(CULL_VIEW_FLAG_SUN_ALIGNED))
            {
                float3 SunDir = GetSunDirection();
                if (dot(SunDir, WorldAxis) <= -Bounds.ConeCutoff)
                {
                  continue;
                }
            }
            else
            {
                float3 ApexToOrigin   = WorldApex - View.ViewOrigin;
                float  ApexToOriginLen = length(ApexToOrigin);
                if (dot(ApexToOrigin, WorldAxis) >= Bounds.ConeCutoff * ApexToOriginLen)
                    continue;
            }
        }

        if (!bSkinned && !bNoCull && View.HasFlag(CULL_VIEW_FLAG_FRUSTUM))
        {
            if (!FrustumContainsSphere(View.FrustumPlanes, WorldCenter, WorldRadius))
                continue;
        }

        // Hi-Z camera occlusion, only meaningful for view 0 (camera) since
        // the depth pyramid tracks the camera's depth buffer. Shadow views
        // either skip this or rely on the camera occlusion flag on the main
        // view having already filtered the caster before promotion.
        if (!bSkinned && !bNoCull && View.HasFlag(CULL_VIEW_FLAG_OCCLUSION))
        {
            // Micro-poly reject for camera view: if the meshlet projects
            // sub-pixel we can drop it entirely. Packaged with occlusion so
            // shadow views (which don't set OCCLUSION) don't pay this.
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
                    continue;
            }

            if (IsHiZOccluded(WorldCenter, WorldRadius))
                continue;
        }

        // Emit into this view's slice.
        uint IndirectIndex = View.IndirectArgsOffset + DrawID;
        uint OldCount;
        InterlockedAdd(uIndirectArgs[IndirectIndex].InstanceCount, 1, OldCount);

        uint Slot = uIndirectArgs[IndirectIndex].FirstInstance + OldCount;

        FMeshletDraw Draw;
        Draw.InstanceID        = InstanceID;
        Draw.MeshletLocalIndex = MeshletLocalIdx;
        uMeshletDrawList[Slot] = Draw;
    }
}
