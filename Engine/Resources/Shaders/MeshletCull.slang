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

    FGPUInstanceCull Cull = GetInstanceCull(InstanceID);

    // Per-surface meshlet range. Instances whose mesh had no meshlets built
    // (import failure, zero-triangle surface) will short-circuit here.
    if (Cull.SurfaceMeshletCount == 0)
        return;

    if (MeshletLocalIdx >= Cull.SurfaceMeshletCount)
        return;

    bool bVisible = true;
    bool bSkinned = Cull.HasFlag(EInstanceFlags::Skinned);
    bool bNoCull  = Cull.HasFlag(EInstanceFlags::NoCull);

    if (!bSkinned && !bNoCull)
    {
        // MeshletHeader and the model matrix only matter when we're actually
        // going to test the meshlet bounds; reading them behind the !bSkinned
        // guard keeps the skinned fast path from touching the Render+Transform
        // buffers at all.
        FGPUInstanceRender Render = GetInstanceRender(InstanceID);
        float4x4           Model  = GetInstanceTransform(InstanceID);

        uint GlobalMeshletIdx = Cull.SurfaceMeshletOffset + MeshletLocalIdx;
        FMeshletBounds Bounds = Render.MeshletHeader.Bounds[GlobalMeshletIdx];

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
        uint DrawID = Cull.DrawID;
        uint OldCount;
        InterlockedAdd(uIndirectDrawDataMeshlet[DrawID].InstanceCount, 1, OldCount);
        uint Slot = uIndirectDrawDataMeshlet[DrawID].FirstInstance + OldCount;

        FMeshletDraw Draw;
        Draw.InstanceID        = InstanceID;
        Draw.MeshletLocalIndex = MeshletLocalIdx;
        uMeshletDrawList[Slot] = Draw;
    }
}
