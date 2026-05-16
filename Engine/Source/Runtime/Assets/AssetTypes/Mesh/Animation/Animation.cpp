#include "pch.h"
#include "Animation.h"

#include "Animation/Pose.h"
#include "Memory/Memcpy.h"
#include "Renderer/MeshData.h"


namespace Lumina
{
    namespace Detail
    {
        static glm::vec3 SampleVec3(const TVector<float>& Times, const TVector<glm::vec3>& Values, float Time)
        {
            const size_t N = Times.size();
            if (N == 0 || Values.empty())
            {
                return glm::vec3(0.0f);
            }
            if (N == 1 || Time <= Times[0])
            {
                return Values[0];
            }
            if (Time >= Times[N - 1])
            {
                return Values[N - 1];
            }

            // Binary search keyframe interval; clips routinely have hundreds of keys.
            size_t Lo = 0;
            size_t Hi = N - 1;
            while (Lo + 1 < Hi)
            {
                const size_t Mid = (Lo + Hi) >> 1;
                (Time < Times[Mid] ? Hi : Lo) = Mid;
            }

            const float Dt = Times[Lo + 1] - Times[Lo];
            const float t  = Dt > 0.0f ? (Time - Times[Lo]) / Dt : 0.0f;
            return glm::mix(Values[Lo], Values[Lo + 1], t);
        }

        static glm::quat SampleQuat(const TVector<float>& Times, const TVector<glm::quat>& Values, float Time)
        {
            const size_t N = Times.size();
            if (N == 0 || Values.empty())
            {
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
            if (N == 1 || Time <= Times[0])
            {
                return Values[0];
            }
            if (Time >= Times[N - 1])
            {
                return Values[N - 1];
            }

            size_t Lo = 0;
            size_t Hi = N - 1;
            while (Lo + 1 < Hi)
            {
                const size_t Mid = (Lo + Hi) >> 1;
                (Time < Times[Mid] ? Hi : Lo) = Mid;
            }

            const float Dt = Times[Lo + 1] - Times[Lo];
            const float t  = Dt > 0.0f ? (Time - Times[Lo]) / Dt : 0.0f;

            glm::quat Q0 = Values[Lo];
            glm::quat Q1 = Values[Lo + 1];
            if (glm::dot(Q0, Q1) < 0.0f)
            {
                Q1 = -Q1;
            }
            return glm::slerp(Q0, Q1, t);
        }

        // Cheap TRS extract for rigid + per-axis-scale matrices; glm::decompose's polar iteration is overkill here.
        static FORCEINLINE void FastDecomposeTRS(const glm::mat4& M,
                                                  glm::vec3& OutT,
                                                  glm::quat& OutR,
                                                  glm::vec3& OutS)
        {
            OutT = glm::vec3(M[3]);

            const glm::vec3 C0(M[0]);
            const glm::vec3 C1(M[1]);
            const glm::vec3 C2(M[2]);

            OutS = glm::vec3(glm::length(C0), glm::length(C1), glm::length(C2));

            const float InvSx = OutS.x > 1e-8f ? 1.0f / OutS.x : 0.0f;
            const float InvSy = OutS.y > 1e-8f ? 1.0f / OutS.y : 0.0f;
            const float InvSz = OutS.z > 1e-8f ? 1.0f / OutS.z : 0.0f;

            glm::mat3 Rot;
            Rot[0] = C0 * InvSx;
            Rot[1] = C1 * InvSy;
            Rot[2] = C2 * InvSz;
            OutR = glm::quat_cast(Rot);
        }

        static constexpr uint8 TouchedT = 1u << 0;
        static constexpr uint8 TouchedR = 1u << 1;
        static constexpr uint8 TouchedS = 1u << 2;
        static constexpr uint8 TouchedAll = TouchedT | TouchedR | TouchedS;
    }


    void CAnimation::Serialize(FArchive& Ar)
    {
        CObject::Serialize(Ar);

        if (!AnimationResource)
        {
            AnimationResource = MakeUnique<FAnimationResource>();
        }

        Ar << *AnimationResource;
    }

    void CAnimation::SamplePose(float Time, FSkeletonResource* RESTRICT InSkeleton, TVector<glm::mat4>& RESTRICT OutBoneTransforms) const
    {
        LUMINA_PROFILE_SCOPE();

        const int32 NumBones = InSkeleton->GetNumBones();
        OutBoneTransforms.resize(NumBones);

        if (NumBones == 0)
        {
            return;
        }

        // Per-thread scratch reused across frames; thread_local required since SamplePose runs in ParallelFor.
        thread_local TVector<glm::vec3> ScratchT;
        thread_local TVector<glm::quat> ScratchR;
        thread_local TVector<glm::vec3> ScratchS;
        thread_local TVector<uint8>     ScratchTouched;

        if ((int32)ScratchT.size() < NumBones)
        {
            ScratchT.resize(NumBones);
            ScratchR.resize(NumBones);
            ScratchS.resize(NumBones);
            ScratchTouched.resize(NumBones);
        }

        Memory::Memset(ScratchTouched.data(), 0, (size_t)NumBones * sizeof(uint8));

        // Pass 1: gather per-bone TRS overrides; one sample per channel, zero decompositions.
        for (const FAnimationChannel& Channel : AnimationResource->Channels)
        {
            const int32 BoneIdx = InSkeleton->FindBoneIndex(Channel.TargetBone);
            if (BoneIdx < 0 || BoneIdx >= NumBones)
            {
                continue;
            }

            switch (Channel.TargetPath)
            {
            case FAnimationChannel::ETargetPath::Translation:
                ScratchT[BoneIdx]       = Detail::SampleVec3(Channel.Timestamps, Channel.Translations, Time);
                ScratchTouched[BoneIdx] |= Detail::TouchedT;
                break;
            case FAnimationChannel::ETargetPath::Rotation:
                ScratchR[BoneIdx]       = Detail::SampleQuat(Channel.Timestamps, Channel.Rotations, Time);
                ScratchTouched[BoneIdx] |= Detail::TouchedR;
                break;
            case FAnimationChannel::ETargetPath::Scale:
                ScratchS[BoneIdx]       = Detail::SampleVec3(Channel.Timestamps, Channel.Scales, Time);
                ScratchTouched[BoneIdx] |= Detail::TouchedS;
                break;
            default:
                break;
            }
        }

        // Pass 2: build per-bone local matrices into OutBoneTransforms (scratch; later passes overwrite).
        for (int32 i = 0; i < NumBones; ++i)
        {
            const FSkeletonResource::FBoneInfo& Bone = InSkeleton->GetBone(i);
            const uint8 Touched = ScratchTouched[i];

            if (Touched == 0)
            {
                OutBoneTransforms[i] = Bone.LocalTransform;
                continue;
            }

            if (Touched == Detail::TouchedAll)
            {
                OutBoneTransforms[i] =
                    glm::translate(glm::mat4(1.0f), ScratchT[i]) *
                    glm::mat4_cast(ScratchR[i]) *
                    glm::scale(glm::mat4(1.0f), ScratchS[i]);
                continue;
            }

            glm::vec3 T, S;
            glm::quat R;
            Detail::FastDecomposeTRS(Bone.LocalTransform, T, R, S);

            if (Touched & Detail::TouchedT) T = ScratchT[i];
            if (Touched & Detail::TouchedR) R = ScratchR[i];
            if (Touched & Detail::TouchedS) S = ScratchS[i];

            OutBoneTransforms[i] =
                glm::translate(glm::mat4(1.0f), T) *
                glm::mat4_cast(R) *
                glm::scale(glm::mat4(1.0f), S);
        }

        // Pass 3: FK in skeleton order; Bones[] is parents-before-children so a single linear pass works.
        for (int32 i = 0; i < NumBones; ++i)
        {
            const FSkeletonResource::FBoneInfo& Bone = InSkeleton->GetBone(i);
            if (Bone.ParentIndex != INDEX_NONE)
            {
                OutBoneTransforms[i] = OutBoneTransforms[Bone.ParentIndex] * OutBoneTransforms[i];
            }
        }

        // Pass 4: fold in InvBind to produce the GPU skinning matrix.
        for (int32 i = 0; i < NumBones; ++i)
        {
            const FSkeletonResource::FBoneInfo& Bone = InSkeleton->GetBone(i);
            OutBoneTransforms[i] = OutBoneTransforms[i] * Bone.InvBindMatrix;
        }
    }

    void CAnimation::SampleLocalPose(float Time, FSkeletonResource* RESTRICT InSkeleton, FPose& RESTRICT OutPose) const
    {
        LUMINA_PROFILE_SCOPE();

        const int32 NumBones = InSkeleton->GetNumBones();
        OutPose.SetNumBones(NumBones);

        if (NumBones == 0)
        {
            return;
        }

        // Per-thread scratch reused across frames; thread_local required since this runs in ParallelFor.
        thread_local TVector<glm::vec3> ScratchT;
        thread_local TVector<glm::quat> ScratchR;
        thread_local TVector<glm::vec3> ScratchS;
        thread_local TVector<uint8>     ScratchTouched;

        if ((int32)ScratchT.size() < NumBones)
        {
            ScratchT.resize(NumBones);
            ScratchR.resize(NumBones);
            ScratchS.resize(NumBones);
            ScratchTouched.resize(NumBones);
        }

        Memory::Memset(ScratchTouched.data(), 0, (size_t)NumBones * sizeof(uint8));

        // Pass 1: gather per-bone TRS overrides; one sample per channel, zero decompositions.
        for (const FAnimationChannel& Channel : AnimationResource->Channels)
        {
            const int32 BoneIdx = InSkeleton->FindBoneIndex(Channel.TargetBone);
            if (BoneIdx < 0 || BoneIdx >= NumBones)
            {
                continue;
            }

            switch (Channel.TargetPath)
            {
            case FAnimationChannel::ETargetPath::Translation:
                ScratchT[BoneIdx]       = Detail::SampleVec3(Channel.Timestamps, Channel.Translations, Time);
                ScratchTouched[BoneIdx] |= Detail::TouchedT;
                break;
            case FAnimationChannel::ETargetPath::Rotation:
                ScratchR[BoneIdx]       = Detail::SampleQuat(Channel.Timestamps, Channel.Rotations, Time);
                ScratchTouched[BoneIdx] |= Detail::TouchedR;
                break;
            case FAnimationChannel::ETargetPath::Scale:
                ScratchS[BoneIdx]       = Detail::SampleVec3(Channel.Timestamps, Channel.Scales, Time);
                ScratchTouched[BoneIdx] |= Detail::TouchedS;
                break;
            default:
                break;
            }
        }

        // Pass 2: resolve each bone's local TRS, falling back to the bind pose for untouched channels.
        for (int32 i = 0; i < NumBones; ++i)
        {
            const FSkeletonResource::FBoneInfo& Bone = InSkeleton->GetBone(i);
            const uint8 Touched = ScratchTouched[i];

            if (Touched == Detail::TouchedAll)
            {
                OutPose.Translations[i] = ScratchT[i];
                OutPose.Rotations[i]    = ScratchR[i];
                OutPose.Scales[i]       = ScratchS[i];
                continue;
            }

            glm::vec3 T, S;
            glm::quat R;
            Detail::FastDecomposeTRS(Bone.LocalTransform, T, R, S);

            if (Touched & Detail::TouchedT) T = ScratchT[i];
            if (Touched & Detail::TouchedR) R = ScratchR[i];
            if (Touched & Detail::TouchedS) S = ScratchS[i];

            OutPose.Translations[i] = T;
            OutPose.Rotations[i]    = R;
            OutPose.Scales[i]       = S;
        }
    }
}
