#pragma once
#include "Common.slang"

// Shared amplification-to-mesh payload contract. Kept in its own header so the
// task shader and the material mesh shaders can both see the type without the
// mesh side pulling in task-only bindings (uMeshletDrawBatches, groupshared
// payload instance).

#define TASK_GROUP_SIZE 32

struct FMeshTaskPayload
{
    uint InstanceID;
    uint GlobalMeshletBase;                     // Inst.SurfaceMeshletOffset; mesh shader adds its slot to reach the final global meshlet.
    uint MeshletLocalIndices[TASK_GROUP_SIZE];  // Surviving meshlets from this chunk's cull pass.
};

// Per-draw meshlet batch descriptor. One entry per indirect draw command,
// filled by the CPU-side prefix-sum parallel to FDrawIndirectArguments.
struct FMeshletDrawBatch
{
    uint BaseInstance;           // Offset into uInstance.
    uint InstanceCount;          // Instances in this batch.
    uint MeshletsPerInstance;    // Max meshlets for the batch's mesh; used only as an upper bound on the task's X-dim.
    uint _Pad;
};
