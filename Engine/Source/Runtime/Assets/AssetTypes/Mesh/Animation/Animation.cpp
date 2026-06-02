#include "pch.h"
#include "Animation.h"

#include "Animation/Pose.h"
#include "Memory/Memcpy.h"
#include "Renderer/MeshData.h"


namespace Lumina
{
    namespace Detail
    {
        static FVector3 SampleVec3(const TVector<float>& Times, const TVector<FVector3>& Values, float Time)
        {
            const size_t N = Times.size();
            if (N == 0 || Values.empty())
            {
                return FVector3(0.0f);
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
            return Math::Mix(Values[Lo], Values[Lo + 1], t);
        }

        static FQuat SampleQuat(const TVector<float>& Times, const TVector<FQuat>& Values, float Time)
        {
            const size_t N = Times.size();
            if (N == 0 || Values.empty())
            {
                return FQuat(1.0f, 0.0f, 0.0f, 0.0f);
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

            FQuat Q0 = Values[Lo];
            FQuat Q1 = Values[Lo + 1];
            if (Math::Dot(Q0, Q1) < 0.0f)
            {
                Q1 = -Q1;
            }
            return Math::Slerp(Q0, Q1, t);
        }

        // Cheap TRS extract for rigid + per-axis-scale matrices; Math::Decompose's polar iteration is overkill here.
        static FORCEINLINE void FastDecomposeTRS(const FMatrix4& M,
                                                  FVector3& OutT,
                                                  FQuat& OutR,
                                                  FVector3& OutS)
        {
            OutT = FVector3(M[3]);

            const FVector3 C0(M[0]);
            const FVector3 C1(M[1]);
            const FVector3 C2(M[2]);

            OutS = FVector3(Math::Length(C0), Math::Length(C1), Math::Length(C2));

            const float InvSx = OutS.x > 1e-8f ? 1.0f / OutS.x : 0.0f;
            const float InvSy = OutS.y > 1e-8f ? 1.0f / OutS.y : 0.0f;
            const float InvSz = OutS.z > 1e-8f ? 1.0f / OutS.z : 0.0f;

            FMatrix3 Rot;
            Rot[0] = C0 * InvSx;
            Rot[1] = C1 * InvSy;
            Rot[2] = C2 * InvSz;
            OutR = Math::ToQuat(Rot);
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

    void CAnimation::SamplePose(float Time, FSkeletonResource* RESTRICT InSkeleton, TVector<FMatrix4>& RESTRICT OutBoneTransforms) const
    {
        LUMINA_PROFILE_SCOPE();

        const int32 NumBones = InSkeleton->GetNumBones();
        OutBoneTransforms.resize(NumBones);

        if (NumBones == 0)
        {
            return;
        }

        // Per-thread scratch reused across frames; thread_local required since SamplePose runs in ParallelFor.
        thread_local TVector<FVector3> ScratchT;
        thread_local TVector<FQuat> ScratchR;
        thread_local TVector<FVector3> ScratchS;
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
                    Math::Translate(FMatrix4(1.0f), ScratchT[i]) *
                    Math::ToMatrix4(ScratchR[i]) *
                    Math::Scale(FMatrix4(1.0f), ScratchS[i]);
                continue;
            }

            FVector3 T, S;
            FQuat R;
            Detail::FastDecomposeTRS(Bone.LocalTransform, T, R, S);

            if (Touched & Detail::TouchedT) T = ScratchT[i];
            if (Touched & Detail::TouchedR) R = ScratchR[i];
            if (Touched & Detail::TouchedS) S = ScratchS[i];

            OutBoneTransforms[i] =
                Math::Translate(FMatrix4(1.0f), T) *
                Math::ToMatrix4(R) *
                Math::Scale(FMatrix4(1.0f), S);
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
        thread_local TVector<FVector3> ScratchT;
        thread_local TVector<FQuat> ScratchR;
        thread_local TVector<FVector3> ScratchS;
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

            FVector3 T, S;
            FQuat R;
            Detail::FastDecomposeTRS(Bone.LocalTransform, T, R, S);

            if (Touched & Detail::TouchedT) T = ScratchT[i];
            if (Touched & Detail::TouchedR) R = ScratchR[i];
            if (Touched & Detail::TouchedS) S = ScratchS[i];

            OutPose.Translations[i] = T;
            OutPose.Rotations[i]    = R;
            OutPose.Scales[i]       = S;
        }
    }

    void CAnimation::SampleBoneLocal(float Time, FSkeletonResource* RESTRICT InSkeleton, int32 BoneIndex,
                                     FVector3& OutT, FQuat& OutR, FVector3& OutS) const
    {
        Detail::FastDecomposeTRS(InSkeleton->GetBone(BoneIndex).LocalTransform, OutT, OutR, OutS);

        const FName& BoneName = InSkeleton->GetBone(BoneIndex).Name;
        for (const FAnimationChannel& Channel : AnimationResource->Channels)
        {
            if (Channel.TargetBone != BoneName)
            {
                continue;
            }

            switch (Channel.TargetPath)
            {
            case FAnimationChannel::ETargetPath::Translation:
                OutT = Detail::SampleVec3(Channel.Timestamps, Channel.Translations, Time);
                break;
            case FAnimationChannel::ETargetPath::Rotation:
                OutR = Detail::SampleQuat(Channel.Timestamps, Channel.Rotations, Time);
                break;
            case FAnimationChannel::ETargetPath::Scale:
                OutS = Detail::SampleVec3(Channel.Timestamps, Channel.Scales, Time);
                break;
            default:
                break;
            }
        }
    }
}
