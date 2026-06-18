#include "pch.h"
#include "AnimationGraphVM.h"

#include "Animation/RootMotion.h"
#include "Assets/AssetTypes/Animation/AnimationGraph/AnimationGraph.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "Memory/Memcpy.h"
#include "Renderer/MeshData.h"

namespace Lumina
{
    namespace Detail
    {
        // Linear cursor over the flat bytecode array. Operand widths are fixed
        // per opcode (see EAnimOp); the compiler and VM must agree on layout.
        struct FByteReader
        {
            const uint8* Data = nullptr;
            SIZE_T Size = 0;
            SIZE_T Cursor = 0;

            FORCEINLINE bool AtEnd() const { return Cursor >= Size; }

            template <typename T>
            FORCEINLINE T Read()
            {
                T Value{};
                if (Cursor + sizeof(T) <= Size)
                {
                    Memory::Memcpy(&Value, Data + Cursor, sizeof(T));
                }
                Cursor += sizeof(T);
                return Value;
            }
        };

        static FORCEINLINE bool EvalTransitionCondition(const FAnimGraphTransition& Transition,
                                                        const CAnimationGraph* Graph,
                                                        const TVector<float>& Parameters)
        {
            // Resolved at load/compile; the fallback covers graphs built outside those paths.
            const int32 ParamIdx = Transition.CachedParamIndex != FAnimGraphTransition::ParamUnresolved
                ? Transition.CachedParamIndex
                : Graph->FindParameterIndex(Transition.ConditionParameter);
            const float Value = (ParamIdx >= 0 && ParamIdx < (int32)Parameters.size())
                ? Parameters[ParamIdx]
                : 0.0f;

            switch (Transition.Compare)
            {
            case EAnimTransitionCompare::Greater:      return Value >  Transition.CompareValue;
            case EAnimTransitionCompare::GreaterEqual: return Value >= Transition.CompareValue;
            case EAnimTransitionCompare::Less:         return Value <  Transition.CompareValue;
            case EAnimTransitionCompare::LessEqual:    return Value <= Transition.CompareValue;
            case EAnimTransitionCompare::Equal:        return Value == Transition.CompareValue;
            case EAnimTransitionCompare::NotEqual:     return Value != Transition.CompareValue;
            }
            return false;
        }

        template <typename TEnum>
        static FORCEINLINE TEnum ReadEnumReg(const float* Scalars, SIZE_T NumScalar, uint16 Reg, int32 MaxValue)
        {
            const float Value = Reg < NumScalar ? Scalars[Reg] : 0.0f;
            const int32 Index = Math::Clamp((int32)Math::Round(Value), 0, MaxValue);
            return (TEnum)Index;
        }

        static FORCEINLINE float ApplyScalarOp(EAnimScalarOp Op, float A, float B)
        {
            switch (Op)
            {
            case EAnimScalarOp::Add:      return A + B;
            case EAnimScalarOp::Sub:      return A - B;
            case EAnimScalarOp::Mul:      return A * B;
            case EAnimScalarOp::Div:      return B != 0.0f ? A / B : 0.0f;
            case EAnimScalarOp::Min:      return Math::Min(A, B);
            case EAnimScalarOp::Max:      return Math::Max(A, B);
            case EAnimScalarOp::Clamp01:  return Math::Clamp(A, 0.0f, 1.0f);
            case EAnimScalarOp::OneMinus: return 1.0f - A;
            case EAnimScalarOp::Abs:      return Math::Abs(A);
            case EAnimScalarOp::Sin:      return Math::Sin(A);
            case EAnimScalarOp::Cos:      return Math::Cos(A);
            case EAnimScalarOp::Mod:      return B != 0.0f ? fmodf(A, B) : 0.0f;
            case EAnimScalarOp::Pow:      return Math::Pow(A, B);
            case EAnimScalarOp::Atan2:    return Math::Atan2(A, B);
            case EAnimScalarOp::Less:     return A < B ? 1.0f : 0.0f;
            case EAnimScalarOp::Greater:  return A > B ? 1.0f : 0.0f;
            case EAnimScalarOp::Floor:    return Math::Floor(A);
            case EAnimScalarOp::Ceil:     return Math::Ceil(A);
            case EAnimScalarOp::Frac:     return Math::Fract(A);
            case EAnimScalarOp::Sqrt:     return Math::Sqrt(Math::Max(A, 0.0f));
            case EAnimScalarOp::Negate:   return -A;
            case EAnimScalarOp::Sign:     return Math::Sign(A);
            }
            return 0.0f;
        }

        // Quintic offset-decay (Bollo 2018) with x(0)=X0, x'(0)=V0, x''(0)=0 and x(t1)=x'(t1)=x''(t1)=0.
        static float InertEval(float X0, float V0, float T1, float T)
        {
            if (T1 <= 1e-5f || X0 <= 1e-7f)
            {
                return 0.0f;
            }
            if (V0 < 0.0f)
            {
                T1 = Math::Min(T1, -5.0f * X0 / V0); // clamp duration so the curve never overshoots below zero
                if (T1 <= 1e-5f)
                {
                    return 0.0f;
                }
            }
            if (T >= T1)
            {
                return 0.0f;
            }
            const float t1_2 = T1 * T1; const float t1_3 = t1_2 * T1; const float t1_4 = t1_3 * T1; const float t1_5 = t1_4 * T1;
            const float A = -(3.0f * V0 * T1 + 6.0f  * X0) / t1_5;
            const float B =  (8.0f * V0 * T1 + 15.0f * X0) / t1_4;
            const float C = -(6.0f * V0 * T1 + 10.0f * X0) / t1_3;
            const float t2 = T * T; const float t3 = t2 * T; const float t4 = t3 * T; const float t5 = t4 * T;
            const float X = A * t5 + B * t4 + C * t3 + V0 * T + X0;
            return X > 0.0f ? X : 0.0f;
        }

        // Signed angle (rad) of quaternion Q about unit Axis.
        static FORCEINLINE float QuatAngleAbout(const FQuat& Q, const FVector3& Axis)
        {
            return 2.0f * Math::Atan2(Math::Dot(FVector3(Q.x, Q.y, Q.z), Axis), Q.w);
        }

        // Capture the per-bone offset (Source vs Target) and its velocity (Source vs SourcePrev) at a seam.
        static void InertCapture(FAnimInertializer& In, const FPose& Source, const FPose& SourcePrev,
                                 const FPose& Target, float Duration, float Dt, bool bHasVel)
        {
            const int32 N = Target.GetNumBones();
            In.Rot.resize(N);
            In.Trans.resize(N);
            In.Scale.resize(N);
            In.Duration = Math::Max(Duration, 0.0f);
            In.bActive  = In.Duration > 1e-5f;

            const bool  bSrc  = Source.GetNumBones() == N;
            const bool  bVel  = bHasVel && Dt > 1e-6f && bSrc && SourcePrev.GetNumBones() == N;
            const float InvDt = bVel ? 1.0f / Dt : 0.0f;

            for (int32 i = 0; i < N; ++i)
            {
                // Rotation: offset quaternion (Source relative to Target) as axis + angle.
                {
                    const FQuat Qs = bSrc ? Source.Rotations[i] : Target.Rotations[i];
                    FQuat Q0 = Math::Normalize(Qs * Math::Inverse(Target.Rotations[i]));
                    if (Q0.w < 0.0f)
                    {
                        Q0 = Q0 * -1.0f; // shortest arc
                    }
                    const FVector3 V(Q0.x, Q0.y, Q0.z);
                    const float Len  = Math::Length(V);
                    const float X0   = 2.0f * Math::Atan2(Len, Q0.w);
                    const FVector3 Axis = Len > 1e-5f ? V * (1.0f / Len) : FVector3(0.0f, 0.0f, 1.0f);
                    float V0 = 0.0f;
                    if (InvDt > 0.0f)
                    {
                        const FQuat Qp = Math::Normalize(SourcePrev.Rotations[i] * Math::Inverse(Target.Rotations[i]));
                        V0 = (X0 - QuatAngleAbout(Qp, Axis)) * InvDt;
                    }
                    In.Rot[i] = FInertChannel{ Axis, X0, V0 };
                }
                // Translation: vector offset, fixed direction, decaying length.
                {
                    const FVector3 Ps  = bSrc ? Source.Translations[i] : Target.Translations[i];
                    const FVector3 Off = Ps - Target.Translations[i];
                    const float    X0  = Math::Length(Off);
                    const FVector3 Dir = X0 > 1e-6f ? Off * (1.0f / X0) : FVector3(0.0f);
                    float V0 = 0.0f;
                    if (InvDt > 0.0f)
                    {
                        V0 = (X0 - Math::Dot(SourcePrev.Translations[i] - Target.Translations[i], Dir)) * InvDt;
                    }
                    In.Trans[i] = FInertChannel{ Dir, X0, V0 };
                }
                // Scale: same vector treatment as translation.
                {
                    const FVector3 Ss  = bSrc ? Source.Scales[i] : Target.Scales[i];
                    const FVector3 Off = Ss - Target.Scales[i];
                    const float    X0  = Math::Length(Off);
                    const FVector3 Dir = X0 > 1e-6f ? Off * (1.0f / X0) : FVector3(0.0f);
                    float V0 = 0.0f;
                    if (InvDt > 0.0f)
                    {
                        V0 = (X0 - Math::Dot(SourcePrev.Scales[i] - Target.Scales[i], Dir)) * InvDt;
                    }
                    In.Scale[i] = FInertChannel{ Dir, X0, V0 };
                }
            }
        }

        // Out := Target + the decaying offset at In.Elapsed. Out may alias Target.
        static void InertApply(const FAnimInertializer& In, const FPose& Target, FPose& Out)
        {
            const int32 N  = Target.GetNumBones();
            const int32 NR = (int32)In.Rot.size();
            Out.SetNumBones(N);
            const float T = In.Elapsed;

            for (int32 i = 0; i < N; ++i)
            {
                if (i < NR)
                {
                    const float Xr = InertEval(In.Rot[i].X0,   In.Rot[i].V0,   In.Duration, T);
                    const float Xt = InertEval(In.Trans[i].X0, In.Trans[i].V0, In.Duration, T);
                    const float Xs = InertEval(In.Scale[i].X0, In.Scale[i].V0, In.Duration, T);

                    FQuat R = Target.Rotations[i];
                    if (Xr > 1e-6f)
                    {
                        R = Math::Normalize(Math::AngleAxis(Xr, In.Rot[i].Direction) * R);
                    }
                    Out.Rotations[i]    = R;
                    Out.Translations[i] = Target.Translations[i] + In.Trans[i].Direction * Xt;
                    Out.Scales[i]       = Target.Scales[i] + In.Scale[i].Direction * Xs;
                }
                else
                {
                    Out.Rotations[i]    = Target.Rotations[i];
                    Out.Translations[i] = Target.Translations[i];
                    Out.Scales[i]       = Target.Scales[i];
                }
            }
        }
    }

    void FAnimationGraphVM::InitState(const CAnimationGraph* Graph, FAnimGraphVMState& State)
    {
        if (Graph == nullptr)
        {
            State = FAnimGraphVMState();
            return;
        }

        State.ScalarRegisters.assign(Graph->NumScalarRegisters, 0.0f);
        State.PoseRegisters.assign(Graph->NumPoseRegisters, FPose());
        State.StateSlots.assign(Graph->NumStateSlots, 0.0f);

        State.Parameters.resize(Graph->Parameters.size());
        for (SIZE_T i = 0; i < Graph->Parameters.size(); ++i)
        {
            State.Parameters[i] = Graph->Parameters[i].DefaultValue;
        }

        // Seed non-zero slot values (entry state index, From = -1); zeroed slots are already correct.
        const SIZE_T NumSlots = State.StateSlots.size();
        for (const FAnimGraphStateMachine& SM : Graph->StateMachines)
        {
            if (SM.CurrentStateSlot < NumSlots)
            {
                State.StateSlots[SM.CurrentStateSlot] = (float)SM.EntryState;
            }
            if (SM.FromStateSlot < NumSlots)
            {
                State.StateSlots[SM.FromStateSlot] = -1.0f;
            }
        }

        // One inertialization record per state machine (transition smoothing; rebuilt each init).
        State.Inertializers.assign(Graph->StateMachines.size(), FAnimInertializer());

        State.SourceGraph  = Graph;
        State.bInitialized = true;
    }

    void FAnimationGraphVM::Execute(const CAnimationGraph* Graph, FSkeletonResource* Skeleton, float DeltaTime, FAnimGraphVMState& State, TVector<FMatrix4>& OutMatrices, bool bLockRoot, int32 RootBoneIndex)
    {
        LUMINA_PROFILE_SCOPE();

        if (Graph == nullptr || Skeleton == nullptr || Skeleton->GetNumBones() == 0)
        {
            OutMatrices.clear();
            return;
        }

        // Re-initialize when the state is stale or sized for a different graph asset.
        if (!State.bInitialized ||
            State.SourceGraph != Graph ||
            (int32)State.ScalarRegisters.size() != Graph->NumScalarRegisters ||
            (int32)State.PoseRegisters.size() != Graph->NumPoseRegisters ||
            (int32)State.StateSlots.size() != Graph->NumStateSlots ||
            State.Parameters.size() != Graph->Parameters.size())
        {
            InitState(Graph, State);
        }

        const SIZE_T NumScalar = State.ScalarRegisters.size();
        const SIZE_T NumPose   = State.PoseRegisters.size();
        const SIZE_T NumState  = State.StateSlots.size();
        const SIZE_T NumClips  = Graph->Clips.size();
        const SIZE_T NumParams = State.Parameters.size();

        float* RESTRICT Scalars = State.ScalarRegisters.data();

        Detail::FByteReader Reader;
        Reader.Data = Graph->Bytecode.data();
        Reader.Size = Graph->Bytecode.size();

        bool bOutputWritten = false;

        while (!Reader.AtEnd())
        {
            const EAnimOp Op = (EAnimOp)Reader.Read<uint8>();

            switch (Op)
            {
            case EAnimOp::Halt:
            {
                Reader.Cursor = Reader.Size;
                break;
            }

            case EAnimOp::LoadConst:
            {
                const float Imm  = Reader.Read<float>();
                const uint16 Dst = Reader.Read<uint16>();
                if (Dst < NumScalar)
                {
                    Scalars[Dst] = Imm;
                }
                break;
            }

            case EAnimOp::LoadParam:
            {
                const uint16 ParamIdx = Reader.Read<uint16>();
                const uint16 Dst      = Reader.Read<uint16>();
                if (Dst < NumScalar)
                {
                    Scalars[Dst] = ParamIdx < NumParams ? State.Parameters[ParamIdx] : 0.0f;
                }
                break;
            }

            case EAnimOp::ScalarOp:
            {
                const EAnimScalarOp SubOp = (EAnimScalarOp)Reader.Read<uint8>();
                const uint16 A   = Reader.Read<uint16>();
                const uint16 B   = Reader.Read<uint16>();
                const uint16 Dst = Reader.Read<uint16>();
                if (A < NumScalar && B < NumScalar && Dst < NumScalar)
                {
                    Scalars[Dst] = Detail::ApplyScalarOp(SubOp, Scalars[A], Scalars[B]);
                }
                break;
            }

            case EAnimOp::AdvanceClock:
            {
                const uint16 StateIdx     = Reader.Read<uint16>();
                const uint16 SpeedReg     = Reader.Read<uint16>();
                const uint16 ClipIdx      = Reader.Read<uint16>();
                const uint16 LoopModeReg  = Reader.Read<uint16>();
                const uint16 DstClock     = Reader.Read<uint16>();
                const uint16 DstFinished  = Reader.Read<uint16>();

                const EClipLoopMode Mode = Detail::ReadEnumReg<EClipLoopMode>(Scalars, NumScalar, LoopModeReg, (int32)EClipLoopMode::PlayOnce);

                if (StateIdx < NumState)
                {
                    const float Speed = SpeedReg < NumScalar ? Scalars[SpeedReg] : 1.0f;
                    float Clock = State.StateSlots[StateIdx] + DeltaTime * Speed;
                    float Finished = 0.0f;

                    if (ClipIdx < NumClips && Graph->Clips[ClipIdx].IsValid())
                    {
                        const float Duration = Graph->Clips[ClipIdx]->GetDuration();
                        if (Duration > 0.0f)
                        {
                            if (Mode == EClipLoopMode::Loop)
                            {
                                Clock = fmodf(Clock, Duration);
                                if (Clock < 0.0f)
                                {
                                    Clock += Duration;
                                }
                            }
                            else // PlayOnce -- clamp at the end and signal finished.
                            {
                                if (Clock >= Duration)
                                {
                                    Clock    = Duration;
                                    Finished = 1.0f;
                                }
                                else if (Clock < 0.0f)
                                {
                                    Clock = 0.0f;
                                }
                            }
                        }
                    }

                    State.StateSlots[StateIdx] = Clock;
                    if (DstClock < NumScalar)
                    {
                        Scalars[DstClock] = Clock;
                    }
                    if (DstFinished < NumScalar)
                    {
                        Scalars[DstFinished] = Finished;
                    }
                }
                break;
            }

            case EAnimOp::MakeAdditive:
            {
                const uint16 Src = Reader.Read<uint16>();
                const uint16 Dst = Reader.Read<uint16>();
                if (Src < NumPose && Dst < NumPose)
                {
                    AnimPose::MakeAdditive(State.PoseRegisters[Src], Skeleton, State.PoseRegisters[Dst]);
                }
                break;
            }

            case EAnimOp::ApplyAdditive:
            {
                const uint16 Base  = Reader.Read<uint16>();
                const uint16 Delta = Reader.Read<uint16>();
                const uint16 Alpha = Reader.Read<uint16>();
                const uint16 Dst   = Reader.Read<uint16>();
                if (Base < NumPose && Delta < NumPose && Dst < NumPose)
                {
                    const float AlphaValue = Alpha < NumScalar ? Scalars[Alpha] : 0.0f;
                    AnimPose::ApplyAdditive(State.PoseRegisters[Base], State.PoseRegisters[Delta], AlphaValue, State.PoseRegisters[Dst]);
                }
                break;
            }

            case EAnimOp::SampleAnim:
            {
                const uint16 ClipIdx = Reader.Read<uint16>();
                const uint16 TimeReg = Reader.Read<uint16>();
                const uint16 Dst     = Reader.Read<uint16>();

                if (Dst < NumPose)
                {
                    FPose& OutPose = State.PoseRegisters[Dst];
                    const float Time = TimeReg < NumScalar ? Scalars[TimeReg] : 0.0f;

                    if (ClipIdx < NumClips && Graph->Clips[ClipIdx].IsValid())
                    {
                        Graph->Clips[ClipIdx]->SampleLocalPose(Time, Skeleton, OutPose);
                    }
                    else
                    {
                        OutPose.ResetToBindPose(Skeleton);
                    }
                }
                break;
            }

            case EAnimOp::RefPose:
            {
                const uint16 Dst = Reader.Read<uint16>();
                if (Dst < NumPose)
                {
                    State.PoseRegisters[Dst].ResetToBindPose(Skeleton);
                }
                break;
            }

            case EAnimOp::Blend:
            {
                const uint16 A     = Reader.Read<uint16>();
                const uint16 B     = Reader.Read<uint16>();
                const uint16 Alpha = Reader.Read<uint16>();
                const uint16 Dst   = Reader.Read<uint16>();

                if (A < NumPose && B < NumPose && Dst < NumPose)
                {
                    const float AlphaValue = Alpha < NumScalar ? Scalars[Alpha] : 0.0f;
                    AnimPose::Blend(State.PoseRegisters[A], State.PoseRegisters[B], AlphaValue, State.PoseRegisters[Dst]);
                }
                break;
            }

            case EAnimOp::BlendMasked:
            {
                const uint16 A       = Reader.Read<uint16>();
                const uint16 B       = Reader.Read<uint16>();
                const uint16 Alpha   = Reader.Read<uint16>();
                const uint16 MaskIdx = Reader.Read<uint16>();
                const uint16 Dst     = Reader.Read<uint16>();

                if (A < NumPose && B < NumPose && Dst < NumPose)
                {
                    const float AlphaValue = Alpha < NumScalar ? Scalars[Alpha] : 0.0f;

                    // Out-of-range mask index falls back to a whole-skeleton blend.
                    if (MaskIdx < Graph->BoneMasks.size())
                    {
                        AnimPose::BlendMasked(State.PoseRegisters[A], State.PoseRegisters[B], AlphaValue, Graph->BoneMasks[MaskIdx].Weights, State.PoseRegisters[Dst]);
                    }
                    else
                    {
                        AnimPose::Blend(State.PoseRegisters[A], State.PoseRegisters[B], AlphaValue, State.PoseRegisters[Dst]);
                    }
                }
                break;
            }

            case EAnimOp::EvalStateMachine:
            {
                const uint16 SmIdx = Reader.Read<uint16>();
                const uint16 Dst   = Reader.Read<uint16>();

                if (SmIdx >= Graph->StateMachines.size() || Dst >= NumPose)
                {
                    break;
                }

                const FAnimGraphStateMachine& SM = Graph->StateMachines[SmIdx];
                const int32 NumStates = (int32)SM.StatePoseRegisters.size();

                if (NumStates == 0)
                {
                    State.PoseRegisters[Dst].ResetToBindPose(Skeleton);
                    break;
                }

                // Out-of-range slot = corrupt/version-mismatched bytecode; fall back to bind pose.
                if (SM.CurrentStateSlot >= NumState || SM.FromStateSlot >= NumState ||
                    SmIdx >= State.Inertializers.size())
                {
                    State.PoseRegisters[Dst].ResetToBindPose(Skeleton);
                    break;
                }

                FAnimInertializer& Inert = State.Inertializers[SmIdx];

                int32 Current = Math::Clamp((int32)State.StateSlots[SM.CurrentStateSlot], 0, NumStates - 1);
                int32 From    = (int32)State.StateSlots[SM.FromStateSlot];

                // Pick a transition: any matching edge when stable; only interruptible edges mid-transition.
                // The first passing edge (author order) wins.
                const bool bTransitioning = From >= 0;
                bool bStart = false;
                for (const FAnimGraphTransition& Transition : SM.Transitions)
                {
                    if (bTransitioning && !Transition.bCanInterrupt)
                    {
                        continue;
                    }
                    const bool bFromMatches = (Transition.FromState == Current) || (Transition.FromState < 0);
                    if (!bFromMatches ||
                        Transition.ToState == Current ||
                        Transition.ToState < 0 ||
                        Transition.ToState >= NumStates)
                    {
                        continue;
                    }
                    if (Detail::EvalTransitionCondition(Transition, Graph, State.Parameters))
                    {
                        From           = Current;
                        Current        = Transition.ToState;
                        Inert.Duration = Math::Max(Transition.BlendDuration, 0.0f);
                        bStart         = true;
                        break;
                    }
                }

                const uint16 CurReg = SM.StatePoseRegisters[Current];

                if (CurReg >= NumPose)
                {
                    State.PoseRegisters[Dst].ResetToBindPose(Skeleton);
                    From = -1;
                }
                else
                {
                    // Inertialize the seam: capture the offset from the last shown pose (PrevOutput) to the
                    // new target, then decay it onto the freshly-evaluated target each frame. Pop-free, and
                    // only the target state contributes -- no cross-fade of two trees. An interrupt re-captures
                    // from the currently-shown (already-inertializing) pose, so velocity stays continuous.
                    if (bStart)
                    {
                        Detail::InertCapture(Inert, Inert.PrevOutput, Inert.PrevPrevOutput,
                                             State.PoseRegisters[CurReg], Inert.Duration, DeltaTime,
                                             Inert.HistoryCount >= 2);
                        Inert.Elapsed = 0.0f;
                    }

                    if (Inert.bActive)
                    {
                        Detail::InertApply(Inert, State.PoseRegisters[CurReg], State.PoseRegisters[Dst]);
                        Inert.Elapsed += DeltaTime;
                        if (Inert.Elapsed >= Inert.Duration)
                        {
                            Inert.bActive = false;
                            From = -1;
                        }
                    }
                    else
                    {
                        State.PoseRegisters[Dst] = State.PoseRegisters[CurReg];
                        From = -1;
                    }
                }

                State.StateSlots[SM.CurrentStateSlot] = (float)Current;
                State.StateSlots[SM.FromStateSlot]    = (float)From;

                // 2-frame output history for the next seam's velocity estimate.
                Inert.PrevPrevOutput = Inert.PrevOutput;
                Inert.PrevOutput     = State.PoseRegisters[Dst];
                Inert.HistoryCount   = Math::Min(Inert.HistoryCount + 1, 2);
                break;
            }

            case EAnimOp::BoneTransform:
            {
                const uint16 Src      = Reader.Read<uint16>();
                const uint16 AlphaReg = Reader.Read<uint16>();
                const uint16 BoneIdx  = Reader.Read<uint16>();
                const uint16 SpaceReg = Reader.Read<uint16>();
                const uint16 ModeReg  = Reader.Read<uint16>();
                const FVector3 T      = Reader.Read<FVector3>();
                const FQuat R         = Reader.Read<FQuat>();
                const FVector3 S      = Reader.Read<FVector3>();
                const uint16 Dst      = Reader.Read<uint16>();

                if (Src < NumPose && Dst < NumPose)
                {
                    const float AlphaValue = AlphaReg < NumScalar ? Scalars[AlphaReg] : 1.0f;

                    const EBoneTransformSpace Space = Detail::ReadEnumReg<EBoneTransformSpace>(Scalars, NumScalar, SpaceReg, (int32)EBoneTransformSpace::ComponentSpace);
                    const EBoneTransformMode  Mode  = Detail::ReadEnumReg<EBoneTransformMode>(Scalars, NumScalar, ModeReg, (int32)EBoneTransformMode::Replace);

                    if (Dst != Src)
                    {
                        State.PoseRegisters[Dst] = State.PoseRegisters[Src];
                    }

                    AnimPose::ApplyBoneTransform(State.PoseRegisters[Dst],
                                                 Skeleton,
                                                 (int32)BoneIdx,
                                                 Space == EBoneTransformSpace::LocalBone ? AnimPose::EBoneSpace::LocalBone : AnimPose::EBoneSpace::ComponentSpace,
                                                 Mode == EBoneTransformMode::Add ? AnimPose::EBoneApplyMode::Add : AnimPose::EBoneApplyMode::Replace,
                                                 T, R, S, AlphaValue);
                }
                break;
            }

            case EAnimOp::TwoBoneIK:
            {
                const uint16 Src      = Reader.Read<uint16>();
                const uint16 AlphaReg = Reader.Read<uint16>();
                const uint16 TX       = Reader.Read<uint16>();
                const uint16 TY       = Reader.Read<uint16>();
                const uint16 TZ       = Reader.Read<uint16>();
                const uint16 RootIdx  = Reader.Read<uint16>();
                const uint16 MidIdx   = Reader.Read<uint16>();
                const uint16 EndIdx   = Reader.Read<uint16>();
                const FVector3 Pole   = Reader.Read<FVector3>();
                const uint16 Dst      = Reader.Read<uint16>();

                if (Src < NumPose && Dst < NumPose)
                {
                    const float AlphaValue = AlphaReg < NumScalar ? Scalars[AlphaReg] : 1.0f;
                    const FVector3 Target(
                        TX < NumScalar ? Scalars[TX] : 0.0f,
                        TY < NumScalar ? Scalars[TY] : 0.0f,
                        TZ < NumScalar ? Scalars[TZ] : 0.0f);

                    if (Dst != Src)
                    {
                        State.PoseRegisters[Dst] = State.PoseRegisters[Src];
                    }

                    AnimPose::TwoBoneIK(State.PoseRegisters[Dst], Skeleton,
                                        (int32)RootIdx, (int32)MidIdx, (int32)EndIdx,
                                        Target, Pole, AlphaValue);
                }
                break;
            }

            case EAnimOp::Output:
            {
                const uint16 Src = Reader.Read<uint16>();
                if (Src < NumPose && State.PoseRegisters[Src].IsValid())
                {
                    if (bLockRoot && RootBoneIndex != INDEX_NONE)
                    {
                        RootMotion::PinRootToBindPose(State.PoseRegisters[Src], Skeleton, RootBoneIndex);
                    }
                    AnimPose::ToSkinningMatrices(State.PoseRegisters[Src], Skeleton, OutMatrices);
                    bOutputWritten = true;
                }
                Reader.Cursor = Reader.Size;
                break;
            }

            default:
            {
                // Unknown opcode, so the stream is corrupt or version-mismatched.
                Reader.Cursor = Reader.Size;
                break;
            }
            }
        }

        if (!bOutputWritten)
        {
            FPose BindPose;
            BindPose.ResetToBindPose(Skeleton);
            AnimPose::ToSkinningMatrices(BindPose, Skeleton, OutMatrices);
        }
    }
}
