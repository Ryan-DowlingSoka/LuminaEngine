#pragma once

#include "Containers/Array.h"
#include "Core/Math/AABB.h"
#include "Core/Serialization/Archiver.h"

namespace Lumina
{
    struct FSkeletonResource;

    // Local-space skeletal pose: per-bone TRS relative to the parent bone.
    // The animation graph VM blends in this space; FK and InvBind folding into
    // GPU skinning matrices happens once when the final pose is resolved.
    struct RUNTIME_API FPose
    {
        TVector<glm::vec3> Translations;
        TVector<glm::quat> Rotations;
        TVector<glm::vec3> Scales;

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

        // OutDelta := Src relative to the skeleton's bind pose (TRS-wise).
        // Translation/scale are differences/ratios; rotation is the residual
        // quaternion Src * inverse(Bind). Pair with ApplyAdditive.
        RUNTIME_API void MakeAdditive(const FPose& Src, const FSkeletonResource* Skeleton, FPose& OutDelta);

        // Out := Base + Alpha * Delta (TRS-wise). Translations add, scales lerp
        // (1 -> Delta), rotations slerp (identity -> Delta) then post-multiply
        // the base rotation. Layer additive overlays on a base pose.
        RUNTIME_API void ApplyAdditive(const FPose& Base, const FPose& Delta, float Alpha, FPose& Out);

        // Resolves a local-space pose into GPU skinning matrices (Global * InvBind).
        RUNTIME_API void ToSkinningMatrices(const FPose& Pose, const FSkeletonResource* Skeleton, TVector<glm::mat4>& OutMatrices);

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

        // In-place: applies (T, R, S) to a single bone of Pose. Space selects the
        // frame the offset is interpreted in; Mode selects layered-on-top vs
        // replace. Alpha scales how strongly the modification is applied.
        RUNTIME_API void ApplyBoneTransform(FPose& Pose,
                                            const FSkeletonResource* Skeleton,
                                            int32 BoneIndex,
                                            EBoneSpace Space,
                                            EBoneApplyMode Mode,
                                            const glm::vec3& Translation,
                                            const glm::quat& Rotation,
                                            const glm::vec3& Scale,
                                            float Alpha);

        // In-place analytical two-bone IK: rotates RootIdx + MidIdx so that the
        // tip bone (EndIdx) reaches Target (component space). Pole picks the
        // bend side. Alpha slerps the result against the input pose.
        // MidIdx's parent must be RootIdx, EndIdx's parent must be MidIdx.
        RUNTIME_API void TwoBoneIK(FPose& Pose,
                                   const FSkeletonResource* Skeleton,
                                   int32 RootIdx, int32 MidIdx, int32 EndIdx,
                                   const glm::vec3& Target,
                                   const glm::vec3& Pole,
                                   float Alpha);
    }
}
