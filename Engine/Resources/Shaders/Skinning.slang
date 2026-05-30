#pragma once
#include "Includes/SceneGlobals.slang"

// GPU vertex pre-skinning: one workgroup per skinned entity skins its vertices once into
// uPreSkinnedVertices, which all draw passes then read instead of re-skinning per pass.

struct FPushConstants
{
    uint DescriptorCount;
};

[[vk::push_constant]] FPushConstants PC;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeMain(uint3 GroupID : SV_GroupID, uint Lane : SV_GroupThreadID)
{
    // Workgroup index folded across X/Y (matches the CPU dispatch's 65535 cap).
    uint Descriptor = GroupID.y * 65535u + GroupID.x;
    if (Descriptor >= PC.DescriptorCount)
    {
        return;
    }

    FSkinDescriptor Desc   = uSkinDescriptors[Descriptor];
    FMeshletHeader* Header = Desc.MeshletHeader;

    // Only the rendered-LOD meshlet span; SkinnedVertexBase folds in the span start so the
    // write lands in the compacted slice (same index the draw VS reads). 64 lanes per meshlet.
    uint MeshletEnd = Desc.SkinMeshletStart + Desc.SkinMeshletCount;
    for (uint m = Desc.SkinMeshletStart; m < MeshletEnd; ++m)
    {
        FMeshlet M = Header.Meshlets[m];
        if (Lane < M.VertexCount)
        {
            uint OutIndex = Desc.SkinnedVertexBase + M.VertexOffset + Lane;
            uPreSkinnedVertices[OutIndex] = SkinVertex(Header, M, Lane, Desc.BoneOffset);
        }
    }
}
