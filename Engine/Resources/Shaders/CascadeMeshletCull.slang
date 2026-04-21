#pragma once
#include "Includes/Culling.slang"

// Per-cascade meshlet cull for directional CSM. Mirrors MeshletCull but tests
// each meshlet against every cascade frustum and emits per-cascade slices.

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
