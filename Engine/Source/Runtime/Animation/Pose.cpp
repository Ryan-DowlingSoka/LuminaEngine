#include "pch.h"
#include "Pose.h"

#include "Renderer/MeshData.h"
#include "Core/Math/SIMD/SIMD.h"

namespace Lumina
{
    namespace Detail
    {
        // Cheap TRS extract for rigid + per-axis-scale matrices; matches the
        // decomposition used by CAnimation so bind-pose math stays consistent.
        static FORCEINLINE void FastDecomposeTRS(const FMatrix4& M, FVector3& OutT, FQuat& OutR, FVector3& OutS)
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

        static FORCEINLINE FMatrix4 ComposeTRS(const FVector3& T, const FQuat& R, const FVector3& S)
        {
            return Math::Translate(FMatrix4(1.0f), T) *
                   Math::ToMatrix4(R) *
                   Math::Scale(FMatrix4(1.0f), S);
        }

    }

    void FPose::ResetToBindPose(const FSkeletonResource* Skeleton)
    {
        const int32 NumBones = Skeleton ? Skeleton->GetNumBones() : 0;
        SetNumBones(NumBones);

        for (int32 i = 0; i < NumBones; ++i)
        {
            Detail::FastDecomposeTRS(Skeleton->GetBone(i).LocalTransform, Translations[i], Rotations[i], Scales[i]);
        }
    }

    void AnimPose::Blend(const FPose& A, const FPose& B, float Alpha, FPose& Out)
    {
        Alpha = Math::Clamp(Alpha, 0.0f, 1.0f);

        const int32 NumBones = A.GetNumBones();
        Out.SetNumBones(NumBones);

        if (Alpha <= 0.0f)
        {
            if (&Out != &A)
            {
                Out = A;
            }
            return;
        }
        if (Alpha >= 1.0f)
        {
            if (&Out != &B)
            {
                Out = B;
            }
            return;
        }

        // Translation + scale lerp as flat float streams (8-wide); rotation needs slerp, stays scalar.
        const int32 NumComponents = NumBones * 3;
        SIMD::LerpArray(reinterpret_cast<float*>(Out.Translations.data()),
                        reinterpret_cast<const float*>(A.Translations.data()),
                        reinterpret_cast<const float*>(B.Translations.data()), NumComponents, Alpha);
        SIMD::LerpArray(reinterpret_cast<float*>(Out.Scales.data()),
                        reinterpret_cast<const float*>(A.Scales.data()),
                        reinterpret_cast<const float*>(B.Scales.data()), NumComponents, Alpha);

        for (int32 i = 0; i < NumBones; ++i)
        {
            FQuat QA = A.Rotations[i];
            FQuat QB = B.Rotations[i];
            if (Math::Dot(QA, QB) < 0.0f)
            {
                QB = -QB;
            }
            Out.Rotations[i] = Math::Slerp(QA, QB, Alpha);
        }
    }

    void AnimPose::BlendMasked(const FPose& A, const FPose& B, float Alpha, const TVector<float>& BoneWeights, FPose& Out)
    {
        Alpha = Math::Clamp(Alpha, 0.0f, 1.0f);

        const int32 NumBones = A.GetNumBones();
        const int32 NumWeights = (int32)BoneWeights.size();
        Out.SetNumBones(NumBones);

        for (int32 i = 0; i < NumBones; ++i)
        {
            const float Weight = i < NumWeights ? BoneWeights[i] : 1.0f;
            const float BoneAlpha = Math::Clamp(Alpha * Weight, 0.0f, 1.0f);

            Out.Translations[i] = Math::Mix(A.Translations[i], B.Translations[i], BoneAlpha);
            Out.Scales[i]       = Math::Mix(A.Scales[i], B.Scales[i], BoneAlpha);

            FQuat QA = A.Rotations[i];
            FQuat QB = B.Rotations[i];
            if (Math::Dot(QA, QB) < 0.0f)
            {
                QB = -QB;
            }
            Out.Rotations[i] = Math::Slerp(QA, QB, BoneAlpha);
        }
    }

    void AnimPose::MakeAdditive(const FPose& Src, const FSkeletonResource* Skeleton, FPose& OutDelta)
    {
        LUMINA_PROFILE_SCOPE();

        const int32 NumBones = Skeleton ? Skeleton->GetNumBones() : 0;
        OutDelta.SetNumBones(NumBones);

        if (NumBones == 0 || Src.GetNumBones() != NumBones)
        {
            return;
        }

        for (int32 i = 0; i < NumBones; ++i)
        {
            FVector3 BindT, BindS;
            FQuat BindR;
            Detail::FastDecomposeTRS(Skeleton->GetBone(i).LocalTransform, BindT, BindR, BindS);

            // Delta := Src "relative to" Bind. The product Src * inverse(Bind)
            // yields the rotation you must post-multiply onto Bind to recover Src.
            OutDelta.Translations[i] = Src.Translations[i] - BindT;
            OutDelta.Rotations[i]    = Src.Rotations[i] * Math::Inverse(BindR);

            // Component-wise scale ratio; guard against degenerate bind scales.
            const FVector3 BindInv(
                BindS.x > 1e-8f ? 1.0f / BindS.x : 1.0f,
                BindS.y > 1e-8f ? 1.0f / BindS.y : 1.0f,
                BindS.z > 1e-8f ? 1.0f / BindS.z : 1.0f);
            OutDelta.Scales[i] = Src.Scales[i] * BindInv;
        }
    }

    void AnimPose::ApplyAdditive(const FPose& Base, const FPose& Delta, float Alpha, FPose& Out)
    {
        LUMINA_PROFILE_SCOPE();

        const int32 NumBones = Base.GetNumBones();
        Out.SetNumBones(NumBones);

        if (NumBones == 0 || Delta.GetNumBones() != NumBones || Alpha <= 0.0f)
        {
            if (&Out != &Base)
            {
                Out = Base;
            }
            return;
        }

        const FQuat Identity(1.0f, 0.0f, 0.0f, 0.0f);

        for (int32 i = 0; i < NumBones; ++i)
        {
            // Translation: straight add scaled by alpha.
            Out.Translations[i] = Base.Translations[i] + Alpha * Delta.Translations[i];

            // Rotation: slerp identity -> Delta by alpha, then layer onto Base.
            FQuat ScaledDelta = Delta.Rotations[i];
            if (Math::Dot(Identity, ScaledDelta) < 0.0f)
            {
                ScaledDelta = -ScaledDelta;
            }
            ScaledDelta = Math::Slerp(Identity, ScaledDelta, Alpha);
            Out.Rotations[i] = ScaledDelta * Base.Rotations[i];

            // Scale: lerp 1 -> Delta (component-wise) then multiply onto Base.
            const FVector3 ScaledScale = Math::Mix(FVector3(1.0f), Delta.Scales[i], Alpha);
            Out.Scales[i] = Base.Scales[i] * ScaledScale;
        }
    }

    namespace Detail
    {
        // Uses Pose's current TRS so chained BoneTransform nodes compose correctly in component space.
        static FMatrix4 ComputeParentGlobal(const FPose& Pose, const FSkeletonResource* Skeleton, int32 BoneIndex)
        {
            const int32 ParentIndex = Skeleton->GetBone(BoneIndex).ParentIndex;
            if (ParentIndex < 0)
            {
                return FMatrix4(1.0f);
            }

            int32 Chain[64];
            int32 ChainLen = 0;
            int32 Cursor = ParentIndex;
            while (Cursor >= 0 && ChainLen < (int32)(sizeof(Chain) / sizeof(Chain[0])))
            {
                Chain[ChainLen++] = Cursor;
                Cursor = Skeleton->GetBone(Cursor).ParentIndex;
            }

            FMatrix4 Global(1.0f);
            for (int32 i = ChainLen - 1; i >= 0; --i)
            {
                const int32 b = Chain[i];
                Global = Global * ComposeTRS(Pose.Translations[b], Pose.Rotations[b], Pose.Scales[b]);
            }
            return Global;
        }
    }

    void AnimPose::ApplyBoneTransform(FPose& Pose,
                                      const FSkeletonResource* Skeleton,
                                      int32 BoneIndex,
                                      EBoneSpace Space,
                                      EBoneApplyMode Mode,
                                      const FVector3& InT,
                                      const FQuat& InR,
                                      const FVector3& InS,
                                      float Alpha)
    {
        LUMINA_PROFILE_SCOPE();

        if (Skeleton == nullptr || BoneIndex < 0 || BoneIndex >= Skeleton->GetNumBones())
        {
            return;
        }
        if (Pose.GetNumBones() != Skeleton->GetNumBones())
        {
            return;
        }

        Alpha = Math::Clamp(Alpha, 0.0f, 1.0f);
        if (Alpha <= 0.0f)
        {
            return;
        }

        FQuat Rotation = InR;
        const float QLenSq = Math::Dot(Rotation, Rotation);
        Rotation = (QLenSq > 1e-8f) ? Rotation * (1.0f / Math::Sqrt(QLenSq)) : FQuat(1.0f, 0.0f, 0.0f, 0.0f);

        FVector3& T = Pose.Translations[BoneIndex];
        FQuat& R = Pose.Rotations[BoneIndex];
        FVector3& S = Pose.Scales[BoneIndex];

        const FQuat Identity(1.0f, 0.0f, 0.0f, 0.0f);

        if (Space == EBoneSpace::LocalBone)
        {
            if (Mode == EBoneApplyMode::Replace)
            {
                T = Math::Mix(T, InT, Alpha);
                S = Math::Mix(S, InS, Alpha);

                FQuat Target = Rotation;
                if (Math::Dot(R, Target) < 0.0f)
                {
                    Target = -Target;
                }
                R = Math::Normalize(Math::Slerp(R, Target, Alpha));
                return;
            }

            T += InT * Alpha;
            S *= Math::Mix(FVector3(1.0f), InS, Alpha);

            FQuat Scaled = Rotation;
            if (Math::Dot(Identity, Scaled) < 0.0f)
            {
                Scaled = -Scaled;
            }
            Scaled = Math::Slerp(Identity, Scaled, Alpha);
            R = Math::Normalize(Scaled * R);
            return;
        }

        const FMatrix4 ParentGlobal    = Detail::ComputeParentGlobal(Pose, Skeleton, BoneIndex);
        const FMatrix4 InvParentGlobal = Math::Inverse(ParentGlobal);

        const FMatrix4 BoneLocal  = Detail::ComposeTRS(T, R, S);
        const FMatrix4 BoneGlobal = ParentGlobal * BoneLocal;

        FMatrix4 NewGlobal;
        if (Mode == EBoneApplyMode::Replace)
        {
            const FMatrix4 Target = Detail::ComposeTRS(InT, Rotation, InS);
            FVector3 GT, GS; FQuat GR;
            Detail::FastDecomposeTRS(BoneGlobal, GT, GR, GS);
            FVector3 TgtT, TgtS; FQuat TgtR;
            Detail::FastDecomposeTRS(Target, TgtT, TgtR, TgtS);

            FQuat BlendedR = TgtR;
            if (Math::Dot(GR, BlendedR) < 0.0f)
            {
                BlendedR = -BlendedR;
            }
            BlendedR = Math::Normalize(Math::Slerp(GR, BlendedR, Alpha));
            NewGlobal = Detail::ComposeTRS(Math::Mix(GT, TgtT, Alpha), BlendedR, Math::Mix(GS, TgtS, Alpha));
        }
        else
        {
            FQuat Scaled = Rotation;
            if (Math::Dot(Identity, Scaled) < 0.0f)
            {
                Scaled = -Scaled;
            }
            Scaled = Math::Slerp(Identity, Scaled, Alpha);

            const FMatrix4 Offset = Detail::ComposeTRS(InT * Alpha, Scaled, Math::Mix(FVector3(1.0f), InS, Alpha));
            NewGlobal = Offset * BoneGlobal;
        }

        const FMatrix4 NewLocal = InvParentGlobal * NewGlobal;
        Detail::FastDecomposeTRS(NewLocal, T, R, S);
        R = Math::Normalize(R);
    }

    namespace Detail
    {
        // Shortest-arc rotation that takes unit vector A onto unit vector B.
        // Handles the antipodal case without producing NaN.
        static FQuat QuatFromTo(const FVector3& A, const FVector3& B)
        {
            const float d = Math::Dot(A, B);
            if (d > 0.99999f)
            {
                return FQuat(1.0f, 0.0f, 0.0f, 0.0f);
            }
            if (d < -0.99999f)
            {
                const FVector3 Ortho = Math::Abs(A.x) < 0.9f ? FVector3(1, 0, 0) : FVector3(0, 1, 0);
                const FVector3 Axis = Math::Normalize(Math::Cross(A, Ortho));
                return Math::AngleAxis(Math::Pi<float>(), Axis);
            }
            const FVector3 Axis = Math::Cross(A, B);
            const float S = Math::Sqrt((1.0f + d) * 2.0f);
            return Math::Normalize(FQuat(S * 0.5f, Axis.x / S, Axis.y / S, Axis.z / S));
        }

        static FMatrix4 ComputeBoneGlobalLocal(const FPose& Pose, const FSkeletonResource* Skeleton, int32 BoneIndex)
        {
            int32 Chain[64];
            int32 ChainLen = 0;
            int32 Cursor = BoneIndex;
            while (Cursor >= 0 && ChainLen < (int32)(sizeof(Chain) / sizeof(Chain[0])))
            {
                Chain[ChainLen++] = Cursor;
                Cursor = Skeleton->GetBone(Cursor).ParentIndex;
            }

            FMatrix4 Global(1.0f);
            for (int32 i = ChainLen - 1; i >= 0; --i)
            {
                const int32 b = Chain[i];
                Global = Global * ComposeTRS(Pose.Translations[b], Pose.Rotations[b], Pose.Scales[b]);
            }
            return Global;
        }
    }

    void AnimPose::TwoBoneIK(FPose& Pose, const FSkeletonResource* Skeleton,
                             int32 RootIdx, int32 MidIdx, int32 EndIdx,
                             const FVector3& Target, const FVector3& Pole, float Alpha)
    {
        LUMINA_PROFILE_SCOPE();

        if (Skeleton == nullptr) return;
        const int32 NumBones = Skeleton->GetNumBones();
        if (Pose.GetNumBones() != NumBones) return;

        if (RootIdx < 0 || MidIdx < 0 || EndIdx < 0) return;
        if (RootIdx >= NumBones || MidIdx >= NumBones || EndIdx >= NumBones) return;
        if (Skeleton->GetBone(MidIdx).ParentIndex != RootIdx) return;
        if (Skeleton->GetBone(EndIdx).ParentIndex != MidIdx) return;

        Alpha = Math::Clamp(Alpha, 0.0f, 1.0f);
        if (Alpha <= 0.0f) return;

        const int32 RootParent = Skeleton->GetBone(RootIdx).ParentIndex;
        const FMatrix4 RootParentG = RootParent >= 0
            ? Detail::ComputeBoneGlobalLocal(Pose, Skeleton, RootParent)
            : FMatrix4(1.0f);

        const FMatrix4 RootLocal = Detail::ComposeTRS(Pose.Translations[RootIdx], Pose.Rotations[RootIdx], Pose.Scales[RootIdx]);
        const FMatrix4 MidLocal  = Detail::ComposeTRS(Pose.Translations[MidIdx],  Pose.Rotations[MidIdx],  Pose.Scales[MidIdx]);
        const FMatrix4 EndLocal  = Detail::ComposeTRS(Pose.Translations[EndIdx],  Pose.Rotations[EndIdx],  Pose.Scales[EndIdx]);

        const FMatrix4 RootG = RootParentG * RootLocal;
        const FMatrix4 MidG  = RootG * MidLocal;
        const FMatrix4 EndG  = MidG * EndLocal;

        const FVector3 R = FVector3(RootG[3]);
        const FVector3 M = FVector3(MidG[3]);
        const FVector3 E = FVector3(EndG[3]);

        const float L1 = Math::Length(M - R);
        const float L2 = Math::Length(E - M);
        if (L1 < 1e-5f || L2 < 1e-5f) return;

        const float MaxReach = L1 + L2;

        FVector3 ToTargetVec = Target - R;
        const float TargetDist = Math::Length(ToTargetVec);
        if (TargetDist < 1e-5f) return;

        const float ClampedDist = Math::Clamp(TargetDist, Math::Abs(L1 - L2) + 1e-4f, MaxReach - 1e-4f);
        const FVector3 ToTarget = ToTargetVec / TargetDist;

        FVector3 PoleDir = Pole - R;
        const float PoleLen = Math::Length(PoleDir);
        if (PoleLen > 1e-5f)
        {
            PoleDir = PoleDir / PoleLen;
        }
        else
        {
            const FVector3 CurrentBend = M - (R + ToTarget * Math::Dot(M - R, ToTarget));
            PoleDir = Math::Length(CurrentBend) > 1e-5f ? Math::Normalize(CurrentBend) : FVector3(0, 1, 0);
        }

        FVector3 BendAxis = Math::Cross(ToTarget, PoleDir);
        const float BendAxisLen = Math::Length(BendAxis);
        BendAxis = BendAxisLen > 1e-5f ? BendAxis / BendAxisLen : FVector3(0, 0, 1);

        const float CosUpper = Math::Clamp((L1 * L1 + ClampedDist * ClampedDist - L2 * L2) / (2.0f * L1 * ClampedDist), -1.0f, 1.0f);
        const float UpperAngle = Math::Acos(CosUpper);

        const FQuat RotUpper = Math::AngleAxis(-UpperAngle, BendAxis);
        const FVector3 NewDirRoot = Math::Normalize(RotUpper * ToTarget);
        const FVector3 NewM = R + NewDirRoot * L1;
        const FVector3 NewE = R + ToTarget * ClampedDist;
        const FVector3 NewDirMid = Math::Normalize(NewE - NewM);

        const FVector3 OldDirRoot = Math::Normalize(M - R);
        const FVector3 OldDirMid  = Math::Normalize(E - M);

        FVector3 RootGT, RootGS; FQuat RootGR;
        Detail::FastDecomposeTRS(RootG, RootGT, RootGR, RootGS);
        FVector3 RPGT, RPGS;     FQuat RPGR;
        Detail::FastDecomposeTRS(RootParentG, RPGT, RPGR, RPGS);

        const FQuat DeltaRoot     = Detail::QuatFromTo(OldDirRoot, NewDirRoot);
        const FQuat NewRootGlobal = DeltaRoot * RootGR;
        FQuat       NewRootLocal  = Math::Conjugate(RPGR) * NewRootGlobal;

        FQuat& RootLocalRotRef = Pose.Rotations[RootIdx];
        if (Math::Dot(RootLocalRotRef, NewRootLocal) < 0.0f) NewRootLocal = -NewRootLocal;
        RootLocalRotRef = Math::Normalize(Math::Slerp(RootLocalRotRef, NewRootLocal, Alpha));

        const FMatrix4 NewRootLocalMat = Detail::ComposeTRS(Pose.Translations[RootIdx], RootLocalRotRef, Pose.Scales[RootIdx]);
        const FMatrix4 NewRootG        = RootParentG * NewRootLocalMat;
        const FMatrix4 NewMidGCurrent  = NewRootG * MidLocal;
        const FMatrix4 NewEndGCurrent  = NewMidGCurrent * EndLocal;

        const FVector3 NewMpos = FVector3(NewMidGCurrent[3]);
        const FVector3 CurrEnd = FVector3(NewEndGCurrent[3]);
        const FVector3 CurrDirMid = Math::Normalize(CurrEnd - NewMpos);

        FVector3 MidGT, MidGS; FQuat MidGR;
        Detail::FastDecomposeTRS(NewMidGCurrent, MidGT, MidGR, MidGS);
        FVector3 NRGT, NRGS;   FQuat NRGR;
        Detail::FastDecomposeTRS(NewRootG, NRGT, NRGR, NRGS);

        const FQuat DeltaMid     = Detail::QuatFromTo(CurrDirMid, NewDirMid);
        const FQuat NewMidGlobal = DeltaMid * MidGR;
        FQuat       NewMidLocal  = Math::Conjugate(NRGR) * NewMidGlobal;

        FQuat& MidLocalRotRef = Pose.Rotations[MidIdx];
        if (Math::Dot(MidLocalRotRef, NewMidLocal) < 0.0f) NewMidLocal = -NewMidLocal;
        MidLocalRotRef = Math::Normalize(Math::Slerp(MidLocalRotRef, NewMidLocal, Alpha));
    }

    void AnimPose::ToSkinningMatrices(const FPose& Pose, const FSkeletonResource* Skeleton, TVector<FMatrix4>& OutMatrices)
    {
        LUMINA_PROFILE_SCOPE();

        const int32 NumBones = Skeleton ? Skeleton->GetNumBones() : 0;
        OutMatrices.resize(NumBones);

        if (NumBones == 0 || Pose.GetNumBones() != NumBones)
        {
            return;
        }

        // Pass 1: local TRS -> local matrices.
        for (int32 i = 0; i < NumBones; ++i)
        {
            OutMatrices[i] = Detail::ComposeTRS(Pose.Translations[i], Pose.Rotations[i], Pose.Scales[i]);
        }

        // Pass 2: FK in skeleton order; Bones[] is parents-before-children so a single linear pass works.
        for (int32 i = 0; i < NumBones; ++i)
        {
            const FSkeletonResource::FBoneInfo& Bone = Skeleton->GetBone(i);
            if (Bone.ParentIndex != INDEX_NONE)
            {
                OutMatrices[i] = OutMatrices[Bone.ParentIndex] * OutMatrices[i];
            }
        }

        // Pass 3: fold in InvBind to produce the GPU skinning matrix.
        for (int32 i = 0; i < NumBones; ++i)
        {
            OutMatrices[i] = OutMatrices[i] * Skeleton->GetBone(i).InvBindMatrix;
        }
    }
}
