#pragma once

#include "Containers/Array.h"
#include "Core/Math/AABB.h"
#include "Core/Serialization/Archiver.h"

namespace Lumina
{
    struct FSkeletonResource;

    // Local-space skeletal pose (per-bone TRS relative to parent); the VM blends in this space,
    // FK + InvBind fold into skinning matrices once when the final pose is resolved.
    struct RUNTIME_API FPose
    {
        TVector<FVector3> Translations;
        TVector<FQuat> Rotations;
        TVector<FVector3> Scales;

        FORCEINLINE int32 GetNumBones() const { return (int32)Rotations.size(); }
        FORCEINLINE bool IsValid() const { return !Rotations.empty(); }

        void SetNumBones(int32 NumBones)
        {
            Translations.resize(NumBones);
            Rotations.resize(NumBones);
            Scales.resize(NumBones);
        }

        // Fills every bone with the skeleton's bind-pose local transform.
        void ResetToBindPose(const FSkeletonResource* Skeleton);
    };

    namespace AnimPose
    {
        // Out = Lerp(A, B, Alpha). A, B and Out may alias. Alpha is clamped to [0,1].
        RUNTIME_API void Blend(const FPose& A, const FPose& B, float Alpha, FPose& Out);

        // Per-bone masked blend: the blend alpha for bone i is Alpha * BoneWeights[i].
        // BoneWeights shorter than the bone count treats missing entries as 1.0.
        RUNTIME_API void BlendMasked(const FPose& A, const FPose& B, float Alpha, const TVector<float>& BoneWeights, FPose& Out);

        // OutDelta := Src relative to bind pose (T/S differences/ratios, R = Src * inverse(Bind)). Pair with ApplyAdditive.
        RUNTIME_API void MakeAdditive(const FPose& Src, const FSkeletonResource* Skeleton, FPose& OutDelta);

        // Out := Base + Alpha * Delta (TRS-wise): T adds, S lerps from 1, R slerps from identity then post-multiplies base.
        RUNTIME_API void ApplyAdditive(const FPose& Base, const FPose& Delta, float Alpha, FPose& Out);

        // Resolves a local-space pose into GPU skinning matrices (Global * InvBind).
        RUNTIME_API void ToSkinningMatrices(const FPose& Pose, const FSkeletonResource* Skeleton, TVector<FMatrix4>& OutMatrices);

        enum class EBoneSpace : uint8
        {
            LocalBone,
            ComponentSpace,
        };

        enum class EBoneApplyMode : uint8
        {
            Add,
            Replace,
        };

        // In-place (T, R, S) on a single bone; Space picks the offset frame, Mode picks layer-vs-replace, Alpha scales it.
        RUNTIME_API void ApplyBoneTransform(FPose& Pose,
                                            const FSkeletonResource* Skeleton,
                                            int32 BoneIndex,
                                            EBoneSpace Space,
                                            EBoneApplyMode Mode,
                                            const FVector3& Translation,
                                            const FQuat& Rotation,
                                            const FVector3& Scale,
                                            float Alpha);

        // In-place analytical two-bone IK so EndIdx reaches Target (component space); Pole picks bend side, Alpha slerps.
        // Requires MidIdx's parent == RootIdx and EndIdx's parent == MidIdx.
        RUNTIME_API void TwoBoneIK(FPose& Pose,
                                   const FSkeletonResource* Skeleton,
                                   int32 RootIdx, int32 MidIdx, int32 EndIdx,
                                   const FVector3& Target,
                                   const FVector3& Pole,
                                   float Alpha);
    }
}
