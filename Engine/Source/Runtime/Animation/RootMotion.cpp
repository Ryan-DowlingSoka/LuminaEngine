#include "pch.h"
#include "RootMotion.h"

#include "Animation/Pose.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "Core/Math/Transform.h"
#include "Renderer/MeshData.h"

namespace Lumina::RootMotion
{
    namespace
    {
        // Cheap rigid + per-axis-scale TRS extract; matches the decomposition used elsewhere in the
        // animation pipeline so bind-pose math stays consistent.
        void FastDecomposeTRS(const FMatrix4& M, FVector3& OutT, FQuat& OutR, FVector3& OutS)
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

        // Root bone's local (== component) transform at a clip time, translation + rotation only.
        FTransform SampleRoot(const CAnimation* Animation, FSkeletonResource* Skeleton, int32 RootIndex, float Time)
        {
            FVector3 T, S;
            FQuat R;
            Animation->SampleBoneLocal(Time, Skeleton, RootIndex, T, R, S);

            FTransform Out;
            Out.Location = T;
            Out.Rotation = R;
            Out.Scale    = FVector3(1.0f);
            return Out;
        }
    }

    int32 ResolveRootBoneIndex(const FSkeletonResource* Skeleton, const FName& NameOverride)
    {
        if (Skeleton == nullptr || Skeleton->GetNumBones() == 0)
        {
            return INDEX_NONE;
        }

        if (!NameOverride.IsNone())
        {
            const int32 Named = Skeleton->FindBoneIndex(NameOverride);
            if (Named != INDEX_NONE)
            {
                return Named;
            }
        }

        for (int32 i = 0; i < Skeleton->GetNumBones(); ++i)
        {
            if (Skeleton->GetBone(i).ParentIndex < 0)
            {
                return i;
            }
        }
        return INDEX_NONE;
    }

    void PinRootToBindPose(FPose& Pose, const FSkeletonResource* Skeleton, int32 RootIndex)
    {
        if (Skeleton == nullptr || RootIndex < 0 || RootIndex >= Pose.GetNumBones())
        {
            return;
        }

        FVector3 T, S;
        FQuat R;
        FastDecomposeTRS(Skeleton->GetBone(RootIndex).LocalTransform, T, R, S);
        Pose.Translations[RootIndex] = T;
        Pose.Rotations[RootIndex]    = R;
        Pose.Scales[RootIndex]       = S;
    }

    FRootMotionDelta ExtractRootDelta(const CAnimation* Animation,
                                      FSkeletonResource* Skeleton,
                                      int32 RootIndex,
                                      float PrevTime,
                                      float CurTime,
                                      bool bLooping,
                                      float Duration)
    {
        FRootMotionDelta Delta;

        if (Animation == nullptr || Skeleton == nullptr || RootIndex < 0 || Duration <= 0.0f)
        {
            return Delta;
        }

        FTransform Combined;
        Combined.Scale = FVector3(1.0f);

        // Component-space (left/fixed-frame) delta D = M_cur * M_prev^-1. The entity then absorbs it as
        // E_new = E_prev * D, reproducing the root's world motion. Using the body-frame delta
        // (M_prev^-1 * M_cur) instead would rotate the translation by the root bone's rest orientation --
        // DCC exports often bake a tilt into the root -- turning forward motion into vertical drift.
        if (bLooping && CurTime < PrevTime)
        {
            // Playhead wrapped this frame: accumulate [Prev, Duration] then [0, Cur] so a full
            // loop contributes the clip's whole root displacement.
            const FTransform Prev  = SampleRoot(Animation, Skeleton, RootIndex, PrevTime);
            const FTransform End   = SampleRoot(Animation, Skeleton, RootIndex, Duration);
            const FTransform Start = SampleRoot(Animation, Skeleton, RootIndex, 0.0f);
            const FTransform Cur   = SampleRoot(Animation, Skeleton, RootIndex, CurTime);

            const FTransform Seg1 = End * Prev.Inverse();
            const FTransform Seg2 = Cur * Start.Inverse();
            Combined = Seg1 * Seg2;
        }
        else
        {
            const FTransform Prev = SampleRoot(Animation, Skeleton, RootIndex, PrevTime);
            const FTransform Cur  = SampleRoot(Animation, Skeleton, RootIndex, CurTime);
            Combined = Cur * Prev.Inverse();
        }

        Delta.Translation = Combined.Location;
        Delta.Rotation    = Combined.Rotation;
        Delta.bHasMotion  = true;
        return Delta;
    }
}
