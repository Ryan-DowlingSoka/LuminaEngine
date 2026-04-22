#pragma once
#include "Includes/Culling.slang"
#include "Includes/MeshletPayload.slang"

// Amplification (task) shader for directional-light cascade shadow maps.
// Replaces the CascadeMeshletCull compute + SHADOW_CSM vertex-shader emulation:
// each workgroup tests a chunk of meshlets against one cascade and DispatchMesh's
// the survivors directly to the rasterizer. One task+mesh dispatch per cascade
// per draw; push const carries the cascade index, so the same task binary
// covers every cascade.
//
// Cull logic mirrors CascadeMeshletCull.slang (kept in lockstep; diverging
// produces a hybrid draw list that doesn't match the compute-path fallback):
//   1. Distance from camera.
//   2. Back-face cone cull against the sun (cascade-invariant; one shot).
//   3. Per-cascade frustum test.
//
// Dispatch layout (VkDrawMeshTasksIndirectCommandEXT per draw):
//   groupCountX = ceil(MeshletsPerInstance / TASK_GROUP_SIZE)
//   groupCountY = InstanceCount
//   groupCountZ = 1
//
// Reuses uMeshletDrawBatches at binding 18 for BaseInstance/MeshletsPerInstance
// lookup via SV_DrawIndex.

[[vk::binding(18, 0)]] StructuredBuffer<FMeshletDrawBatch> uMeshletDrawBatches;

struct FCascadePush
{
    int ShadowDataIndex;
    int CascadeIndex;
};

[[vk::push_constant]] FCascadePush uCascadePush;

groupshared FMeshTaskPayload SharedPayload;

bool CullMeshletForCascade(uint InstanceID, uint MeshletLocalIdx,
                           FGPUInstance Inst, uint CascadeIndex)
{
    if (!Inst.HasFlag(EInstanceFlags::CastShadow))
    {
        return false;
    }

    bool bSkinned = Inst.HasFlag(EInstanceFlags::Skinned);
    bool bNoCull  = Inst.HasFlag(EInstanceFlags::NoCull);

    // Instance-level distance cull. Meshes past ShadowMaxDistance never cast
    // visible shadows, so every meshlet on them is dead.
    float3 InstCenter = Inst.BoundsCenter;
    float  InstRadius = Inst.BoundsRadius;
    float3 CamPos     = GetCameraPosition();
    if (length(InstCenter - CamPos) - InstRadius > uSceneData.CullData.ShadowMaxDistance)
    {
        return false;
    }

    float4x4 Model = Inst.ModelMatrix;
    uint GlobalMeshletIdx = Inst.SurfaceMeshletOffset + MeshletLocalIdx;
    FMeshletBounds Bounds = Inst.MeshletHeader.Bounds[GlobalMeshletIdx];

    float3 WorldCenter = mul(Model, float4(Bounds.Center, 1.0)).xyz;

    float SqScaleX = dot(Model[0].xyz, Model[0].xyz);
    float SqScaleY = dot(Model[1].xyz, Model[1].xyz);
    float SqScaleZ = dot(Model[2].xyz, Model[2].xyz);
    float MaxScale = sqrt(max(SqScaleX, max(SqScaleY, SqScaleZ)));
    float WorldRadius = Bounds.Radius * MaxScale;

    // Cone cull against the sun. CSM pipeline uses SetCullBack() + depth bias,
    // so only FRONT-facing triangles (outward normals toward the sun) reach the
    // rasterizer. A meshlet whose entire cone points AWAY from the sun produces
    // zero raster output, so drop it here. Sun direction is cascade-invariant
    // so the same test fires every cascade with the same result.
    if (!bSkinned && !bNoCull && Bounds.ConeCutoff < 1.0)
    {
        float3 WorldAxis = normalize(mul((float3x3)Model, Bounds.ConeAxis));
        float3 SunDir    = GetSunDirection();
        if (dot(SunDir, WorldAxis) <= -Bounds.ConeCutoff)
        {
            return false;
        }
    }

    // Per-cascade frustum test.
    if (!bSkinned && !bNoCull)
    {
        if (!FrustumContainsSphere(uSceneData.CullData.CascadeFrustum[CascadeIndex],
                                   WorldCenter, WorldRadius))
        {
            return false;
        }
    }

    return true;
}

[shader("amplification")]
[numthreads(TASK_GROUP_SIZE, 1, 1)]
void TaskMain(uint3 GroupID : SV_GroupID,
              uint  LocalIndex : SV_GroupIndex,
              uint  DrawID : SV_DrawIndex)
{
    FMeshletDrawBatch Batch = uMeshletDrawBatches[DrawID];

    // groupCountY == Batch.InstanceCount exactly.
    uint InstanceID      = Batch.BaseInstance + GroupID.y;
    uint MeshletLocalIdx = GroupID.x * TASK_GROUP_SIZE + LocalIndex;

    FGPUInstance Inst = GetInstance(InstanceID);

    if (LocalIndex == 0)
    {
        SharedPayload.InstanceID        = InstanceID;
        SharedPayload.GlobalMeshletBase = Inst.SurfaceMeshletOffset;
    }

    bool bVisible = false;
    if (MeshletLocalIdx < Inst.SurfaceMeshletCount)
    {
        bVisible = CullMeshletForCascade(InstanceID, MeshletLocalIdx, Inst,
                                         (uint)uCascadePush.CascadeIndex);
    }

    uint VisibleMask  = WaveActiveBallot(bVisible).x;
    uint VisibleCount = countbits(VisibleMask);
    uint Slot         = WavePrefixCountBits(bVisible);

    if (bVisible)
    {
        SharedPayload.MeshletLocalIndices[Slot] = MeshletLocalIdx;
    }

    GroupMemoryBarrierWithGroupSync();

    DispatchMesh(VisibleCount, 1, 1, SharedPayload);
}
