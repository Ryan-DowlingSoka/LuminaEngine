#pragma once
#include "Includes/Culling.slang"
#include "Includes/MeshletPayload.slang"

// Amplification (task) shader for the mesh-shader meshlet path. Replaces the
// MeshletCull compute dispatch *and* the per-meshlet intermediate uMeshletDrawList
// slot the legacy VS path consumes; surviving meshlets are emitted directly as
// mesh workgroups through the task payload, skipping the atomic append + global
// memory roundtrip.
//
// Dispatch layout (VkDrawMeshTasksIndirectCommandEXT per draw batch):
//   groupCountX = ceil(MeshletsPerInstance / TASK_GROUP_SIZE)
//   groupCountY = InstanceCount (for this draw)
//   groupCountZ = 1
// Draw batch identity is pulled from uMeshletDrawBatches via SV_DrawIndex; each
// draw command in the indirect buffer corresponds to one entry.
//
// Because groupCountY is sized exactly to InstanceCount, GroupID.y is always
// in-range and needs no guard. groupCountX is rounded up, so the meshlet-index
// guard (MeshletLocalIdx < SurfaceMeshletCount) stays.

[[vk::binding(18, 0)]] StructuredBuffer<FMeshletDrawBatch> uMeshletDrawBatches;

groupshared FMeshTaskPayload SharedPayload;

// Mirrors the logic in CullMeshlets.slang. Keep symmetric or the task-shader
// path will silently over/under-draw vs the compute fallback.
bool CullMeshletForCamera(uint InstanceID, uint MeshletLocalIdx, FGPUInstance Inst)
{
    bool bSkinned  = Inst.HasFlag(EInstanceFlags::Skinned);
    bool bNoCull   = Inst.HasFlag(EInstanceFlags::NoCull);
    bool bTwoSided = Inst.HasFlag(EInstanceFlags::TwoSided);

    // Skinned: bind-pose bounds don't match deformed geometry. NoCull: opt-out.
    if (bSkinned || bNoCull)
    {
        return true;
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

    if (uSceneData.CullData.bFrustumCull != 0)
    {
        if (!FrustumContainsSphere(uSceneData.CullData.Frustum, WorldCenter, WorldRadius))
        {
            return false;
        }
    }

    // Back-face cone cull (skipped for two-sided). Rearranged to avoid the
    // normalize on `apex - cam`.
    if (!bTwoSided && Bounds.ConeCutoff < 1.0)
    {
        float3 WorldApex    = mul(Model, float4(Bounds.ConeApex, 1.0)).xyz;
        float3 WorldAxis    = normalize(mul((float3x3)Model, Bounds.ConeAxis));
        float3 ApexToCam    = WorldApex - GetCameraPosition();
        float  ApexToCamLen = length(ApexToCam);

        if (dot(ApexToCam, WorldAxis) >= Bounds.ConeCutoff * ApexToCamLen)
        {
            return false;
        }
    }

    // Micro-poly rejection: project the meshlet sphere to screen, reject if
    // its AABB misses every pixel-center sample in either axis.
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
                return false;
            }
        }
        // ProjectSphereClip false => sphere straddles near plane, keep visible.
    }

    if (uSceneData.CullData.bOcclusionCull != 0)
    {
        if (IsHiZOccluded(WorldCenter, WorldRadius))
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

    // groupCountY == Batch.InstanceCount exactly, so this is always in range.
    uint InstanceID      = Batch.BaseInstance + GroupID.y;
    uint MeshletLocalIdx = GroupID.x * TASK_GROUP_SIZE + LocalIndex;

    FGPUInstance Inst = GetInstance(InstanceID);

    // Thread 0 seeds the shared payload with the per-workgroup uniform data.
    // Doing this from a single lane (rather than redundantly from every lane
    // or behind a separate barrier) lets the compiler issue the fill in the
    // same pass as the cull test.
    if (LocalIndex == 0)
    {
        SharedPayload.InstanceID        = InstanceID;
        SharedPayload.GlobalMeshletBase = Inst.SurfaceMeshletOffset;
    }

    // groupCountX rounds up; lanes past SurfaceMeshletCount are out-of-range
    // and can skip the heavy cull test. Short-circuits trivial rejects so
    // only live lanes do the matrix math.
    bool bVisible = false;
    if (MeshletLocalIdx < Inst.SurfaceMeshletCount)
    {
        bVisible = CullMeshletForCamera(InstanceID, MeshletLocalIdx, Inst);
    }

    // Compact the surviving meshlets into the payload. The wave-scan avoids
    // an atomic and keeps the emit order deterministic per chunk, which helps
    // the mesh shader stay cache-friendly when adjacent meshlets share verts.
    // TASK_GROUP_SIZE == 32 fits in a single subgroup on NV and AMD (wave32),
    // so the ballot is free and no cross-wave reduction is needed.
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
