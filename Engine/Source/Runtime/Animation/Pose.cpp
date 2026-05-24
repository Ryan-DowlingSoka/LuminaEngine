#include "pch.h"
#include "Pose.h"

#include "Renderer/MeshData.h"

namespace Lumina
{
    namespace Detail
    {
        // Cheap TRS extract for rigid + per-axis-scale matrices; matches the
        // decomposition used by CAnimation so bind-pose math stays consistent.
        static FORCEINLINE void FastDecomposeTRS(const glm::mat4& M, glm::vec3& OutT, glm::quat& OutR, glm::vec3& OutS)
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

        static FORCEINLINE glm::mat4 ComposeTRS(const glm::vec3& T, const glm::quat& R, const glm::vec3& S)
        {
            return glm::translate(glm::mat4(1.0f), T) *
                   glm::mat4_cast(R) *
                   glm::scale(glm::mat4(1.0f), S);
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
        Alpha = glm::clamp(Alpha, 0.0f, 1.0f);

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

        for (int32 i = 0; i < NumBones; ++i)
        {
            Out.Translations[i] = glm::mix(A.Translations[i], B.Translations[i], Alpha);
            Out.Scales[i]       = glm::mix(A.Scales[i], B.Scales[i], Alpha);

            glm::quat QA = A.Rotations[i];
            glm::quat QB = B.Rotations[i];
            if (glm::dot(QA, QB) < 0.0f)
            {
                QB = -QB;
            }
            Out.Rotations[i] = glm::slerp(QA, QB, Alpha);
        }
    }

    void AnimPose::BlendMasked(const FPose& A, const FPose& B, float Alpha, const TVector<float>& BoneWeights, FPose& Out)
    {
        Alpha = glm::clamp(Alpha, 0.0f, 1.0f);

        const int32 NumBones = A.GetNumBones();
        const int32 NumWeights = (int32)BoneWeights.size();
        Out.SetNumBones(NumBones);

        for (int32 i = 0; i < NumBones; ++i)
        {
            const float Weight = i < NumWeights ? BoneWeights[i] : 1.0f;
            const float BoneAlpha = glm::clamp(Alpha * Weight, 0.0f, 1.0f);

            Out.Translations[i] = glm::mix(A.Translations[i], B.Translations[i], BoneAlpha);
            Out.Scales[i]       = glm::mix(A.Scales[i], B.Scales[i], BoneAlpha);

            glm::quat QA = A.Rotations[i];
            glm::quat QB = B.Rotations[i];
            if (glm::dot(QA, QB) < 0.0f)
            {
                QB = -QB;
            }
            Out.Rotations[i] = glm::slerp(QA, QB, BoneAlpha);
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
            glm::vec3 BindT, BindS;
            glm::quat BindR;
            Detail::FastDecomposeTRS(Skeleton->GetBone(i).LocalTransform, BindT, BindR, BindS);

            // Delta := Src "relative to" Bind. The product Src * inverse(Bind)
            // yields the rotation you must post-multiply onto Bind to recover Src.
            OutDelta.Translations[i] = Src.Translations[i] - BindT;
            OutDelta.Rotations[i]    = Src.Rotations[i] * glm::inverse(BindR);

            // Component-wise scale ratio; guard against degenerate bind scales.
            const glm::vec3 BindInv(
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

        const glm::quat Identity(1.0f, 0.0f, 0.0f, 0.0f);

        for (int32 i = 0; i < NumBones; ++i)
        {
            // Translation: straight add scaled by alpha.
            Out.Translations[i] = Base.Translations[i] + Alpha * Delta.Translations[i];

            // Rotation: slerp identity -> Delta by alpha, then layer onto Base.
            glm::quat ScaledDelta = Delta.Rotations[i];
            if (glm::dot(Identity, ScaledDelta) < 0.0f)
            {
                ScaledDelta = -ScaledDelta;
            }
            ScaledDelta = glm::slerp(Identity, ScaledDelta, Alpha);
            Out.Rotations[i] = ScaledDelta * Base.Rotations[i];

            // Scale: lerp 1 -> Delta (component-wise) then multiply onto Base.
            const glm::vec3 ScaledScale = glm::mix(glm::vec3(1.0f), Delta.Scales[i], Alpha);
            Out.Scales[i] = Base.Scales[i] * ScaledScale;
        }
    }

    namespace Detail
    {
        // Uses Pose's current TRS so chained BoneTransform nodes compose correctly in component space.
        static glm::mat4 ComputeParentGlobal(const FPose& Pose, const FSkeletonResource* Skeleton, int32 BoneIndex)
        {
            const int32 ParentIndex = Skeleton->GetBone(BoneIndex).ParentIndex;
            if (ParentIndex < 0)
            {
                return glm::mat4(1.0f);
            }

            int32 Chain[64];
            int32 ChainLen = 0;
            int32 Cursor = ParentIndex;
            while (Cursor >= 0 && ChainLen < (int32)(sizeof(Chain) / sizeof(Chain[0])))
            {
                Chain[ChainLen++] = Cursor;
                Cursor = Skeleton->GetBone(Cursor).ParentIndex;
            }

            glm::mat4 Global(1.0f);
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
                                      const glm::vec3& InT,
                                      const glm::quat& InR,
                                      const glm::vec3& InS,
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

        Alpha = glm::clamp(Alpha, 0.0f, 1.0f);
        if (Alpha <= 0.0f)
        {
            return;
        }

        glm::quat Rotation = InR;
        const float QLenSq = glm::dot(Rotation, Rotation);
        Rotation = (QLenSq > 1e-8f) ? Rotation * (1.0f / glm::sqrt(QLenSq)) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        glm::vec3& T = Pose.Translations[BoneIndex];
        glm::quat& R = Pose.Rotations[BoneIndex];
        glm::vec3& S = Pose.Scales[BoneIndex];

        const glm::quat Identity(1.0f, 0.0f, 0.0f, 0.0f);

        if (Space == EBoneSpace::LocalBone)
        {
            if (Mode == EBoneApplyMode::Replace)
            {
                T = glm::mix(T, InT, Alpha);
                S = glm::mix(S, InS, Alpha);

                glm::quat Target = Rotation;
                if (glm::dot(R, Target) < 0.0f)
                {
                    Target = -Target;
                }
                R = glm::normalize(glm::slerp(R, Target, Alpha));
                return;
            }

            T += InT * Alpha;
            S *= glm::mix(glm::vec3(1.0f), InS, Alpha);

            glm::quat Scaled = Rotation;
            if (glm::dot(Identity, Scaled) < 0.0f)
            {
                Scaled = -Scaled;
            }
            Scaled = glm::slerp(Identity, Scaled, Alpha);
            R = glm::normalize(Scaled * R);
            return;
        }

        const glm::mat4 ParentGlobal    = Detail::ComputeParentGlobal(Pose, Skeleton, BoneIndex);
        const glm::mat4 InvParentGlobal = glm::inverse(ParentGlobal);

        const glm::mat4 BoneLocal  = Detail::ComposeTRS(T, R, S);
        const glm::mat4 BoneGlobal = ParentGlobal * BoneLocal;

        glm::mat4 NewGlobal;
        if (Mode == EBoneApplyMode::Replace)
        {
            const glm::mat4 Target = Detail::ComposeTRS(InT, Rotation, InS);
            glm::vec3 GT, GS; glm::quat GR;
            Detail::FastDecomposeTRS(BoneGlobal, GT, GR, GS);
            glm::vec3 TgtT, TgtS; glm::quat TgtR;
            Detail::FastDecomposeTRS(Target, TgtT, TgtR, TgtS);

            glm::quat BlendedR = TgtR;
            if (glm::dot(GR, BlendedR) < 0.0f)
            {
                BlendedR = -BlendedR;
            }
            BlendedR = glm::normalize(glm::slerp(GR, BlendedR, Alpha));
            NewGlobal = Detail::ComposeTRS(glm::mix(GT, TgtT, Alpha), BlendedR, glm::mix(GS, TgtS, Alpha));
        }
        else
        {
            glm::quat Scaled = Rotation;
            if (glm::dot(Identity, Scaled) < 0.0f)
            {
                Scaled = -Scaled;
            }
            Scaled = glm::slerp(Identity, Scaled, Alpha);

            const glm::mat4 Offset = Detail::ComposeTRS(InT * Alpha, Scaled, glm::mix(glm::vec3(1.0f), InS, Alpha));
            NewGlobal = Offset * BoneGlobal;
        }

        const glm::mat4 NewLocal = InvParentGlobal * NewGlobal;
        Detail::FastDecomposeTRS(NewLocal, T, R, S);
        R = glm::normalize(R);
    }

    namespace Detail
    {
        // Shortest-arc rotation that takes unit vector A onto unit vector B.
        // Handles the antipodal case without producing NaN.
        static glm::quat QuatFromTo(const glm::vec3& A, const glm::vec3& B)
        {
            const float d = glm::dot(A, B);
            if (d > 0.99999f)
            {
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
            if (d < -0.99999f)
            {
                const glm::vec3 Ortho = glm::abs(A.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                const glm::vec3 Axis = glm::normalize(glm::cross(A, Ortho));
                return glm::angleAxis(glm::pi<float>(), Axis);
            }
            const glm::vec3 Axis = glm::cross(A, B);
            const float S = glm::sqrt((1.0f + d) * 2.0f);
            return glm::normalize(glm::quat(S * 0.5f, Axis.x / S, Axis.y / S, Axis.z / S));
        }

        static glm::mat4 ComputeBoneGlobalLocal(const FPose& Pose, const FSkeletonResource* Skeleton, int32 BoneIndex)
        {
            int32 Chain[64];
            int32 ChainLen = 0;
            int32 Cursor = BoneIndex;
            while (Cursor >= 0 && ChainLen < (int32)(sizeof(Chain) / sizeof(Chain[0])))
            {
                Chain[ChainLen++] = Cursor;
                Cursor = Skeleton->GetBone(Cursor).ParentIndex;
            }

            glm::mat4 Global(1.0f);
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
                             const glm::vec3& Target, const glm::vec3& Pole, float Alpha)
    {
        LUMINA_PROFILE_SCOPE();

        if (Skeleton == nullptr) return;
        const int32 NumBones = Skeleton->GetNumBones();
        if (Pose.GetNumBones() != NumBones) return;

        if (RootIdx < 0 || MidIdx < 0 || EndIdx < 0) return;
        if (RootIdx >= NumBones || MidIdx >= NumBones || EndIdx >= NumBones) return;
        if (Skeleton->GetBone(MidIdx).ParentIndex != RootIdx) return;
        if (Skeleton->GetBone(EndIdx).ParentIndex != MidIdx) return;

        Alpha = glm::clamp(Alpha, 0.0f, 1.0f);
        if (Alpha <= 0.0f) return;

        const int32 RootParent = Skeleton->GetBone(RootIdx).ParentIndex;
        const glm::mat4 RootParentG = RootParent >= 0
            ? Detail::ComputeBoneGlobalLocal(Pose, Skeleton, RootParent)
            : glm::mat4(1.0f);

        const glm::mat4 RootLocal = Detail::ComposeTRS(Pose.Translations[RootIdx], Pose.Rotations[RootIdx], Pose.Scales[RootIdx]);
        const glm::mat4 MidLocal  = Detail::ComposeTRS(Pose.Translations[MidIdx],  Pose.Rotations[MidIdx],  Pose.Scales[MidIdx]);
        const glm::mat4 EndLocal  = Detail::ComposeTRS(Pose.Translations[EndIdx],  Pose.Rotations[EndIdx],  Pose.Scales[EndIdx]);

        const glm::mat4 RootG = RootParentG * RootLocal;
        const glm::mat4 MidG  = RootG * MidLocal;
        const glm::mat4 EndG  = MidG * EndLocal;

        const glm::vec3 R = glm::vec3(RootG[3]);
        const glm::vec3 M = glm::vec3(MidG[3]);
        const glm::vec3 E = glm::vec3(EndG[3]);

        const float L1 = glm::length(M - R);
        const float L2 = glm::length(E - M);
        if (L1 < 1e-5f || L2 < 1e-5f) return;

        const float MaxReach = L1 + L2;

        glm::vec3 ToTargetVec = Target - R;
        const float TargetDist = glm::length(ToTargetVec);
        if (TargetDist < 1e-5f) return;

        const float ClampedDist = glm::clamp(TargetDist, glm::abs(L1 - L2) + 1e-4f, MaxReach - 1e-4f);
        const glm::vec3 ToTarget = ToTargetVec / TargetDist;

        glm::vec3 PoleDir = Pole - R;
        const float PoleLen = glm::length(PoleDir);
        if (PoleLen > 1e-5f)
        {
            PoleDir = PoleDir / PoleLen;
        }
        else
        {
            const glm::vec3 CurrentBend = M - (R + ToTarget * glm::dot(M - R, ToTarget));
            PoleDir = glm::length(CurrentBend) > 1e-5f ? glm::normalize(CurrentBend) : glm::vec3(0, 1, 0);
        }

        glm::vec3 BendAxis = glm::cross(ToTarget, PoleDir);
        const float BendAxisLen = glm::length(BendAxis);
        BendAxis = BendAxisLen > 1e-5f ? BendAxis / BendAxisLen : glm::vec3(0, 0, 1);

        const float CosUpper = glm::clamp((L1 * L1 + ClampedDist * ClampedDist - L2 * L2) / (2.0f * L1 * ClampedDist), -1.0f, 1.0f);
        const float UpperAngle = glm::acos(CosUpper);

        const glm::quat RotUpper = glm::angleAxis(-UpperAngle, BendAxis);
        const glm::vec3 NewDirRoot = glm::normalize(RotUpper * ToTarget);
        const glm::vec3 NewM = R + NewDirRoot * L1;
        const glm::vec3 NewE = R + ToTarget * ClampedDist;
        const glm::vec3 NewDirMid = glm::normalize(NewE - NewM);

        const glm::vec3 OldDirRoot = glm::normalize(M - R);
        const glm::vec3 OldDirMid  = glm::normalize(E - M);

        glm::vec3 RootGT, RootGS; glm::quat RootGR;
        Detail::FastDecomposeTRS(RootG, RootGT, RootGR, RootGS);
        glm::vec3 RPGT, RPGS;     glm::quat RPGR;
        Detail::FastDecomposeTRS(RootParentG, RPGT, RPGR, RPGS);

        const glm::quat DeltaRoot     = Detail::QuatFromTo(OldDirRoot, NewDirRoot);
        const glm::quat NewRootGlobal = DeltaRoot * RootGR;
        glm::quat       NewRootLocal  = glm::conjugate(RPGR) * NewRootGlobal;

        glm::quat& RootLocalRotRef = Pose.Rotations[RootIdx];
        if (glm::dot(RootLocalRotRef, NewRootLocal) < 0.0f) NewRootLocal = -NewRootLocal;
        RootLocalRotRef = glm::normalize(glm::slerp(RootLocalRotRef, NewRootLocal, Alpha));

        const glm::mat4 NewRootLocalMat = Detail::ComposeTRS(Pose.Translations[RootIdx], RootLocalRotRef, Pose.Scales[RootIdx]);
        const glm::mat4 NewRootG        = RootParentG * NewRootLocalMat;
        const glm::mat4 NewMidGCurrent  = NewRootG * MidLocal;
        const glm::mat4 NewEndGCurrent  = NewMidGCurrent * EndLocal;

        const glm::vec3 NewMpos = glm::vec3(NewMidGCurrent[3]);
        const glm::vec3 CurrEnd = glm::vec3(NewEndGCurrent[3]);
        const glm::vec3 CurrDirMid = glm::normalize(CurrEnd - NewMpos);

        glm::vec3 MidGT, MidGS; glm::quat MidGR;
        Detail::FastDecomposeTRS(NewMidGCurrent, MidGT, MidGR, MidGS);
        glm::vec3 NRGT, NRGS;   glm::quat NRGR;
        Detail::FastDecomposeTRS(NewRootG, NRGT, NRGR, NRGS);

        const glm::quat DeltaMid     = Detail::QuatFromTo(CurrDirMid, NewDirMid);
        const glm::quat NewMidGlobal = DeltaMid * MidGR;
        glm::quat       NewMidLocal  = glm::conjugate(NRGR) * NewMidGlobal;

        glm::quat& MidLocalRotRef = Pose.Rotations[MidIdx];
        if (glm::dot(MidLocalRotRef, NewMidLocal) < 0.0f) NewMidLocal = -NewMidLocal;
        MidLocalRotRef = glm::normalize(glm::slerp(MidLocalRotRef, NewMidLocal, Alpha));
    }

    void AnimPose::ToSkinningMatrices(const FPose& Pose, const FSkeletonResource* Skeleton, TVector<glm::mat4>& OutMatrices)
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
