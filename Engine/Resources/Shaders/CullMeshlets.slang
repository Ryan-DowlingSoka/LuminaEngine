#pragma once
#include "Includes/Culling.slang"

// Two-phase meshlet culling. Follows the Ubisoft GPU-driven (SIGGRAPH 2015) /
// UE5 Nanite approach: the early phase runs against the *previous frame's*
// Hi-Z pyramid (stale but cheap), then the pipeline renders depth for the
// survivors, rebuilds the HZB from current-frame depth, and the late phase
// re-tests only the meshlets the early phase rejected as occluded.
//
// Why split:
//   Single-pass against last-frame HZB mis-occludes disoccluded geometry
//   (fast camera motion, uncovered surfaces) because last frame's pyramid
//   doesn't know about those pixels yet. Meshlets being ~124 tris each, a
//   false-occlusion shows up as visible pop-in/pop-out at the meshlet
//   granularity. Two phases get the throughput win of conservative early
//   rejection without sacrificing correctness on disocclusion.
//
// Per-phase behaviour:
//
//   Phase 0 (EARLY):
//     * Each thread owns one (instance, meshlet-local) pair.
//     * For every non-late view (camera, cascades, spot/point faces), runs
//       distance/frustum/cone tests. Shadow views' camera-occlusion flag is
//       NOT set, so they only pay frustum+cone here and emit straight into
//       their own slice.
//     * For the camera view, HZB-visible meshlets emit to uMeshletDrawList;
//       HZB-occluded meshlets append to uMeshletDeferList for phase 1.
//     * Skips the camera-late view entirely (phase 1 owns it).
//
//   Phase 1 (LATE):
//     * One thread per deferred entry. Dispatch X = ceil(DeferCount / 64),
//       but DeferCount is a GPU value so the CPU dispatches the worst-case
//       (ceil(TotalMeshletBound / 64)) and threads beyond DeferCount early-out.
//     * Re-tests the defer entry's meshlet against the freshly-built HZB.
//       Survivors emit into the camera-late view slice. No frustum/cone
//       retest -- those already passed in phase 0, and transforms do not
//       change between phases.
//     * Reads only the camera-late view; shadow views ignored.
//
// One compute shader owns both phases so the cull logic doesn't drift
// between two parallel pipelines. The Phase push-constant branch is
// uniform across a warp so the cost is a single jump.

#define LOCAL_SIZE_X 64

struct FPushConstants
{
    uint NumViews;
    uint Phase;              // CULL_PHASE_EARLY or CULL_PHASE_LATE
    uint CameraLateViewIndex; // Only consumed in LATE; index into uCullViews.
    uint _Pad;
};

// -----------------------------------------------------------------------------
// Emit helper. Appends a surviving meshlet into the given view's slice.
// Centralized so phase 0 (camera + shadow views) and phase 1 (camera-late)
// share the atomic protocol exactly.
// -----------------------------------------------------------------------------
void EmitMeshlet(FCullView View, uint DrawID, uint InstanceID, uint MeshletLocalIdx)
{
    uint IndirectIndex = View.IndirectArgsOffset + DrawID;
    uint OldCount;
    InterlockedAdd(uIndirectArgs[IndirectIndex].InstanceCount, 1, OldCount);

    uint Slot = uIndirectArgs[IndirectIndex].FirstInstance + OldCount;

    FMeshletDraw Draw;
    Draw.InstanceID        = InstanceID;
    Draw.MeshletLocalIndex = MeshletLocalIdx;
    uMeshletDrawList[Slot] = Draw;
}

// -----------------------------------------------------------------------------
// Phase 0. Walks every (instance, meshlet) pair and routes each survivor
// either into a view's draw list (shadow views, camera-visible) or the
// defer list (camera + HZB-occluded).
// -----------------------------------------------------------------------------
void RunEarlyPhase(uint3 GlobalID, uint3 GroupID, uint NumViews)
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

    float3 WorldApex = mul(Model, float4(Bounds.ConeApex, 1.0)).xyz;
    float3 WorldAxis = normalize(mul((float3x3)Model, Bounds.ConeAxis));
    bool   bHasCone  = Bounds.ConeCutoff < 1.0;

    uint DrawID = Inst.DrawID;

    // Instance-level camera distance reused for CastShadowOnly views.
    float3 CamPos        = GetCameraPosition();
    float3 InstCenter    = Inst.BoundsCenter;
    float  InstRadius    = Inst.BoundsRadius;
    float  DistToCamera  = length(InstCenter - CamPos);
    float  MaxShadowDist = uSceneData.CullData.ShadowMaxDistance;

    for (uint v = 0; v < NumViews; ++v)
    {
        FCullView View = uCullViews[v];

        // Camera-late view is owned by phase 1; skip in phase 0 so we don't
        // emit doubles when a meshlet passes both HZB tests.
        if (View.HasFlag(CULL_VIEW_FLAG_PHASE_LATE))
            continue;

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
                float3 ApexToOrigin    = WorldApex - View.ViewOrigin;
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

        // Hi-Z occlusion only runs on the camera view (uDepthPyramid tracks the
        // camera). Shadow views skip this block entirely. On an early-phase
        // occluded meshlet we defer it to phase 1 instead of dropping.
        if (!bSkinned && !bNoCull && View.HasFlag(CULL_VIEW_FLAG_OCCLUSION))
        {
            // Micro-poly reject: if the meshlet projects sub-pixel we can drop
            // it entirely. Packaged with occlusion so shadow views don't pay.
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
            {
                // Defer to phase 1. Phase 1 owns the camera-late view slice
                // and re-tests against the rebuilt HZB before emitting.
                uint DeferSlot;
                InterlockedAdd(uDeferCount[0], 1, DeferSlot);

                FMeshletDeferred Entry;
                Entry.InstanceID        = InstanceID;
                Entry.MeshletLocalIndex = MeshletLocalIdx;
                Entry.DrawID            = DrawID;
                Entry._Pad              = 0u;
                uMeshletDeferList[DeferSlot] = Entry;
                continue;
            }
        }

        EmitMeshlet(View, DrawID, InstanceID, MeshletLocalIdx);
    }
}

// -----------------------------------------------------------------------------
// Phase 1. One thread per deferred entry. No scene walk, no per-view loop --
// every survivor goes to the single camera-late view.
// -----------------------------------------------------------------------------
void RunLatePhase(uint3 GlobalID, uint CameraLateViewIndex)
{
    uint ThreadIdx = GlobalID.x;
    uint Count     = uDeferCount[0];

    if (ThreadIdx >= Count)
        return;

    FMeshletDeferred Entry = uMeshletDeferList[ThreadIdx];

    // Reconstruct world-space bounds the same way phase 0 did. Transforms
    // can't change between phases (they're baked into the Instance buffer
    // that was uploaded before phase 0) so re-deriving is stable.
    FGPUInstance Inst = GetInstance(Entry.InstanceID);
    if (Inst.SurfaceMeshletCount == 0)
        return;

    uint GlobalMeshletIdx = Inst.SurfaceMeshletOffset + Entry.MeshletLocalIndex;
    FMeshletBounds Bounds = Inst.MeshletHeader.Bounds[GlobalMeshletIdx];

    float4x4 Model = Inst.ModelMatrix;
    float3 WorldCenter = mul(Model, float4(Bounds.Center, 1.0)).xyz;

    float SqScaleX = dot(Model[0].xyz, Model[0].xyz);
    float SqScaleY = dot(Model[1].xyz, Model[1].xyz);
    float SqScaleZ = dot(Model[2].xyz, Model[2].xyz);
    float MaxScale = sqrt(max(SqScaleX, max(SqScaleY, SqScaleZ)));
    float WorldRadius = Bounds.Radius * MaxScale;

    // Re-test against the rebuilt HZB. A deferred meshlet that is still
    // occluded against the fresh pyramid is genuinely behind an occluder in
    // this frame and can be dropped; a meshlet that now passes was hidden
    // by a stale pyramid and has to reach the base pass. The micro-poly
    // reject already fired in phase 0; no need to rerun it.
    if (IsHiZOccluded(WorldCenter, WorldRadius))
        return;

    FCullView CameraLate = uCullViews[CameraLateViewIndex];
    EmitMeshlet(CameraLate, Entry.DrawID, Entry.InstanceID, Entry.MeshletLocalIndex);
}

[shader("compute")]
[numthreads(LOCAL_SIZE_X, 1, 1)]
void ComputeMain(uint3 GlobalID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID, uniform FPushConstants PC)
{
    if (PC.Phase == CULL_PHASE_LATE)
    {
        RunLatePhase(GlobalID, PC.CameraLateViewIndex);
    }
    else
    {
        RunEarlyPhase(GlobalID, GroupID, PC.NumViews);
    }
}
