#include "pch.h"
#include "Skeleton.h"

namespace Lumina
{
    void CSkeleton::Serialize(FArchive& Ar)
    {
        CObject::Serialize(Ar);

        if (!SkeletonResource)
        {
            SkeletonResource = MakeUnique<FSkeletonResource>();
        }

        Ar << *SkeletonResource;
    }

    void CSkeleton::ComputeBindPoseSkinningMatrices(TVector<glm::mat4>& OutMatrices) const
    {
        const int32 NumBones = SkeletonResource->GetNumBones();
        OutMatrices.resize(NumBones);

        // Forward kinematics in skeleton order: parents always precede children
        // in Bones[], so a single linear pass produces world-space transforms
        // without any per-bone scratch.
        for (int32 i = 0; i < NumBones; ++i)
        {
            const FSkeletonResource::FBoneInfo& Bone = SkeletonResource->GetBone(i);
            const glm::mat4 World = (Bone.ParentIndex == INDEX_NONE)
                ? Bone.LocalTransform
                : OutMatrices[Bone.ParentIndex] * Bone.LocalTransform;
            OutMatrices[i] = World;
        }

        // Second pass folds in InvBind. Doing it in-place after the FK pass is
        // safe because the FK pass only ever reads OutMatrices[Parent] which is
        // already finalised before any child reads it.
        for (int32 i = 0; i < NumBones; ++i)
        {
            const FSkeletonResource::FBoneInfo& Bone = SkeletonResource->GetBone(i);
            OutMatrices[i] = OutMatrices[i] * Bone.InvBindMatrix;
        }
    }
}
