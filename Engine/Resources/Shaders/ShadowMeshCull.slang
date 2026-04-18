#pragma once
#include "Includes/SceneGlobals.slang"

// Shadow-caster culling. Unlike MeshCull.slang (which uses the camera frustum and
// the camera depth pyramid) this pass accepts any caster that can influence *any*
// shadow view in the scene, so that objects outside the camera frustum still
// cast shadows into it.
//
// An instance survives if it is a shadow caster AND at least one of:
//   - There is a directional light and the instance intersects the extruded
//     camera frustum swept along the sun direction.
//   - Any shadow-casting point light's sphere contains the instance's bounds.
//   - Any shadow-casting spot light's cone (approximated by its bounding sphere)
//     contains the instance's bounds.
//
// Survivors are compacted into the shadow indirect/mapping buffers that mirror
// the camera-view buffers but have their own per-batch instance counts.

#define LOCAL_SIZE 256

bool InFrustum(FFrustum F, float3 Center, float Radius)
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

// Spot lights use a bounding sphere of radius `Radius` around their position.
// This is conservative (overdraws a bit) but avoids building a tight spot cone
// on the GPU; real AAA engines trade a tight cone test for this at the cost of
// a handful of extra shader instructions per light.
bool InSphere(float3 LightPos, float LightRadius, float3 Center, float BoundsRadius)
{
    float3 D = Center - LightPos;
    float  R = LightRadius + BoundsRadius;
    return dot(D, D) <= R * R;
}

[shader("compute")]
[numthreads(LOCAL_SIZE, 1, 1)]
void ComputeMain(uint3 GlobalID : SV_DispatchThreadID)
{
    uint GID          = GlobalID.x;
    uint NumInstances = uSceneData.CullData.InstanceNum;

    if (GID >= NumInstances)
        return;

    FGPUInstance Instance = uInstanceData[GID];

    // Non-caster: never emit. The camera cull pass already handled this
    // instance for visible draws.
    if (!Instance.HasFlag(EInstanceFlags::CastShadow))
    {
        return;
    }

    float3 Center = Instance.BoundsCenter;
    float  Radius = Instance.BoundsRadius;

    bool bVisible = false;

    // Directional: test the extruded frustum swept along the sun direction.
    if (uSceneData.CullData.bHasDirectional != 0)
    {
        if (InFrustum(uSceneData.CullData.ShadowFrustum, Center, Radius))
        {
            bVisible = true;
        }
    }

    // Point + spot lights: check each shadow-casting light's bounds. Lights
    // whose ShadowMapIndex is -1 do not own a tile and can be skipped.
    if (!bVisible)
    {
        uint NumLights = uLightData[0].NumLights;
        for (uint LightIdx = 0; LightIdx < NumLights; ++LightIdx)
        {
            FLight L = uLightData[0].Lights[LightIdx];
            if (L.Flags == LIGHT_FLAG_TYPE_DIRECTIONAL)
            {
                continue;
            }
            if (L.Shadow[0].ShadowMapIndex < 0)
            {
                continue;
            }
            if (InSphere(L.Position, L.Radius, Center, Radius))
            {
                bVisible = true;
                break;
            }
        }
    }

    if (!bVisible)
    {
        return;
    }

    uint BatchID       = uInstanceData[GID].DrawID;
    uint InstanceCount = 0;
    InterlockedAdd(uIndirectDrawDataShadow[BatchID].InstanceCount, 1, InstanceCount);
    uint InstanceIndex = uIndirectDrawDataShadow[BatchID].FirstInstance + InstanceCount;

    uMappingDataShadow[InstanceIndex] = GID;
}
