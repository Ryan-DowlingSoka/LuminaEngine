#pragma once
#include "Includes/Culling.slang"

// Per-cascade meshlet cull for directional CSM. Mirrors MeshletCull but tests
// each meshlet against every cascade frustum and emits per-cascade slices.

#define LOCAL_SIZE_X 64

[shader("compute")]
[numthreads(LOCAL_SIZE_X, 1, 1)]
void ComputeMain(uint3 GlobalID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    uint InstanceID      = GroupID.y;
    uint MeshletLocalIdx = GlobalID.x;
    uint NumInstances    = uSceneData.CullData.InstanceNum;

    if (InstanceID >= NumInstances)
        return;

    if (uSceneData.CullData.bHasDirectional == 0)
        return;

    FGPUInstance Inst = uInstanceData[InstanceID];

    if (!Inst.HasFlag(EInstanceFlags::CastShadow))
        return;

    if (Inst.SurfaceMeshletCount == 0)
        return;

    if (MeshletLocalIdx >= Inst.SurfaceMeshletCount)
        return;

    // Distance cull against camera.
    float3 InstCenter = Inst.BoundsCenter;
    float  InstRadius = Inst.BoundsRadius;
    float3 CamPos     = GetCameraPosition();
    if (length(InstCenter - CamPos) - InstRadius > uSceneData.CullData.ShadowMaxDistance)
        return;

    bool bSkinned = Inst.HasFlag(EInstanceFlags::Skinned);
    bool bNoCull  = Inst.HasFlag(EInstanceFlags::NoCull);

    uint GlobalMeshletIdx = Inst.SurfaceMeshletOffset + MeshletLocalIdx;
    FMeshletBounds Bounds = Inst.MeshletHeader.Bounds[GlobalMeshletIdx];

    // Meshlet bounds to world space.
    float3 WorldCenter = mul(Inst.ModelMatrix, float4(Bounds.Center, 1.0)).xyz;

    float SqScaleX = dot(Inst.ModelMatrix[0].xyz, Inst.ModelMatrix[0].xyz);
    float SqScaleY = dot(Inst.ModelMatrix[1].xyz, Inst.ModelMatrix[1].xyz);
    float SqScaleZ = dot(Inst.ModelMatrix[2].xyz, Inst.ModelMatrix[2].xyz);
    float MaxScale = sqrt(max(SqScaleX, max(SqScaleY, SqScaleZ)));
    float WorldRadius = Bounds.Radius * MaxScale;

    uint DrawID   = Inst.DrawID;
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
