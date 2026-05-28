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

    void CSkeleton::ComputeBindPoseSkinningMatrices(TVector<FMatrix4>& OutMatrices) const
    {
        const int32 NumBones = SkeletonResource->GetNumBones();
        OutMatrices.resize(NumBones);

        // FK in skeleton order; Bones[] is parents-before-children.
        for (int32 i = 0; i < NumBones; ++i)
        {
            const FSkeletonResource::FBoneInfo& Bone = SkeletonResource->GetBone(i);
            const FMatrix4 World = (Bone.ParentIndex == INDEX_NONE)
                ? Bone.LocalTransform
                : OutMatrices[Bone.ParentIndex] * Bone.LocalTransform;
            OutMatrices[i] = World;
        }

        // Fold in InvBind in-place; safe because the FK pass already finalized parents before children.
        for (int32 i = 0; i < NumBones; ++i)
        {
            const FSkeletonResource::FBoneInfo& Bone = SkeletonResource->GetBone(i);
            OutMatrices[i] = OutMatrices[i] * Bone.InvBindMatrix;
        }
    }
}
