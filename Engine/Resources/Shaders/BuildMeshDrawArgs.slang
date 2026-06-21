#include "Includes/SceneGlobals.slang"

// Converts the cull's FDrawIndirectArguments (VS-emulation format) into mesh-task indirect args:
// GroupCountX = surviving meshlets (= InstanceCount written by CullMeshlets), Y = Z = 1. One thread per
// (view,draw) arg. Lets the cull stay byte-identical between the VS and mesh paths.

struct FDrawMeshTasksIndirectArguments
{
    uint GroupCountX;
    uint GroupCountY;
    uint GroupCountZ;
};

struct FBuildMeshArgsPC
{
    FDrawIndirectArguments*          SrcArgs;   // cull output (read InstanceCount)
    FDrawMeshTasksIndirectArguments* DstArgs;   // mesh-task args (written)
    uint                            Count;      // NumViews * NumDrawsPerView
    uint                            _Pad;
};
FBuildMeshArgsPC* Pass() { return GetPassArgs<FBuildMeshArgsPC>(); }

[shader("compute")]
[numthreads(64, 1, 1)]
void Main(uint3 DTid : SV_DispatchThreadID)
{
    uint Index = DTid.x;
    if (Index >= Pass().Count)
    {
        return;
    }

    Pass().DstArgs[Index].GroupCountX = Pass().SrcArgs[Index].InstanceCount;
    Pass().DstArgs[Index].GroupCountY = 1u;
    Pass().DstArgs[Index].GroupCountZ = 1u;
}
