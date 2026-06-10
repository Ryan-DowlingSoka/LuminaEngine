#pragma once
#include "Includes/SceneGlobals.slang"

// GPU vertex pre-skinning: one workgroup per meshlet skins its vertices once into
// PreSkinnedVertices(), which all draw passes then read instead of re-skinning per pass.

struct FPushConstants
{
    uint DescriptorCount;
};

FPushConstants* Pass() { return GetPassArgs<FPushConstants>(); }

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeMain(uint3 GroupID : SV_GroupID, uint Lane : SV_GroupThreadID)
{
    // One workgroup per meshlet. Index folded across X/Y (matches the CPU dispatch's 65535 cap).
    uint Descriptor = GroupID.y * 65535u + GroupID.x;
    if (Descriptor >= Pass().DescriptorCount)
    {
        return;
    }

    FSkinDescriptor Desc   = SkinDescriptors()[Descriptor];
    FMeshletHeader* Header = Desc.MeshletHeader;

    // SkinnedVertexBase folds in the span start so the write lands in the compacted slice
    // (same index the draw VS reads). 64 lanes per meshlet.
    FMeshlet M = Header.Meshlets[Desc.MeshletIndex];
    if (Lane < M.VertexCount)
    {
        uint OutIndex = Desc.SkinnedVertexBase + M.VertexOffset + Lane;
        PreSkinnedVertices()[OutIndex] = SkinVertex(Header, M, Lane, Desc.BoneOffset);
    }
}
