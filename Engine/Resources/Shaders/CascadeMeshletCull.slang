#pragma once
#include "Includes/Culling.slang"

// Per-cascade meshlet cull for directional CSM. Mirrors MeshletCull but tests
// each meshlet against every cascade frustum and emits per-cascade slices.
//
// Culls applied (cheap-to-expensive):
//   1. Distance from camera (pre-fetch, 32B cull block only).
//   2. Back-face cone cull against the sun. The CSM pipeline uses
//      SetCullFront(), so only back-faces (relative to the sun) reach the
//      rasterizer — a meshlet whose cone is entirely front-facing to the sun
//      has all triangles front-face-culled at raster and produces no output.
//      This fires ONCE per meshlet (the sun direction is cascade-invariant),
//      and short-circuits every cascade at once when it hits.
//   3. Per-cascade frustum.
//
// Note: Per-cascade micro-poly rejection is intentionally omitted. Meshes are
// already binned across cascades by distance on the CPU side, so the effective
// pixel footprint in each cascade is similar to the main view's. The cone cull
// above (fires once across all cascades) is where the real savings come from.

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

    if (uSceneData.CullData.bHasDirectional == 0)
        return;

    FGPUInstanceCull Cull = GetInstanceCull(InstanceID);

    if (!Cull.HasFlag(EInstanceFlags::CastShadow))
        return;

    if (Cull.SurfaceMeshletCount == 0)
        return;

    if (MeshletLocalIdx >= Cull.SurfaceMeshletCount)
        return;

    // Distance cull against camera. Bounds live in the cheap Cull block; bail
    // before we touch Render/Transform so distant casters don't pollute L2.
    float3 InstCenter = Cull.BoundsCenter;
    float  InstRadius = Cull.BoundsRadius;
    float3 CamPos     = GetCameraPosition();
    if (length(InstCenter - CamPos) - InstRadius > uSceneData.CullData.ShadowMaxDistance)
        return;

    bool bSkinned = Cull.HasFlag(EInstanceFlags::Skinned);
    bool bNoCull  = Cull.HasFlag(EInstanceFlags::NoCull);

    FGPUInstanceRender Render = GetInstanceRender(InstanceID);
    float4x4           Model  = GetInstanceTransform(InstanceID);

    uint GlobalMeshletIdx = Cull.SurfaceMeshletOffset + MeshletLocalIdx;
    FMeshletBounds Bounds = Render.MeshletHeader.Bounds[GlobalMeshletIdx];

    // Meshlet bounds to world space.
    float3 WorldCenter = mul(Model, float4(Bounds.Center, 1.0)).xyz;

    float SqScaleX = dot(Model[0].xyz, Model[0].xyz);
    float SqScaleY = dot(Model[1].xyz, Model[1].xyz);
    float SqScaleZ = dot(Model[2].xyz, Model[2].xyz);
    float MaxScale = sqrt(max(SqScaleX, max(SqScaleY, SqScaleZ)));
    float WorldRadius = Bounds.Radius * MaxScale;

    // Back-face cone cull against the sun. The CSM pipeline uses SetCullFront
    // (standard anti-acne / peter-panning trick: render only back-faces from
    // the light's POV so receiver depth comparisons happen against the
    // occluder's far side). That means FRONT-facing meshlets never reach the
    // depth buffer — we can drop them here and save a full draw slot.
    //
    // Convention: GetSunDirection() points from the surface TOWARD the sun
    // (same direction Light.Direction uses for N.L in the lit pass). A
    // triangle is front-facing to the sun iff dot(Normal, SunDir) > 0. For
    // the whole cone to be front-facing, the worst-case normal (at angle
    // acos(cutoff) from the axis) must also satisfy that, which gives:
    //     dot(axis, SunDir) >= cutoff   =>   entire cone lit => cull.
    // One-shot across all cascades (sun direction is cascade-invariant).
    if (!bSkinned && !bNoCull && Bounds.ConeCutoff < 1.0)
    {
        float3 WorldAxis = normalize(mul((float3x3)Model, Bounds.ConeAxis));
        float3 SunDir    = GetSunDirection();
        if (dot(SunDir, WorldAxis) >= Bounds.ConeCutoff)
        {
            return;
        }
    }

    uint DrawID   = Cull.DrawID;
    uint NumDraws = uSceneData.CullData.NumDraws;

    [unroll]
    for (int c = 0; c < NUM_CASCADES; ++c)
    {
        bool bVisible = true;
        if (!bSkinned && !bNoCull)
        {
            bVisible = FrustumContainsSphere(uSceneData.CullData.CascadeFrustum[c], WorldCenter, WorldRadius);
        }

        if (!bVisible)
        {
            continue;
        }

        uint IndirectIndex = c * NumDraws + DrawID;
        uint OldCount;
        InterlockedAdd(uIndirectDrawDataMeshletCascade[IndirectIndex].InstanceCount, 1, OldCount);
        uint Slot = uIndirectDrawDataMeshletCascade[IndirectIndex].FirstInstance + OldCount;

        FMeshletDraw Draw;
        Draw.InstanceID        = InstanceID;
        Draw.MeshletLocalIndex = MeshletLocalIdx;
        uMeshletDrawListCascade[Slot] = Draw;
    }
}
