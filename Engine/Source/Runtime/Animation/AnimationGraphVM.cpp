#include "pch.h"
#include "AnimationGraphVM.h"

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
            const int32 ParamIdx = Graph->FindParameterIndex(Transition.ConditionParameter);
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
            }
            return 0.0f;
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

        State.SourceGraph  = Graph;
        State.bInitialized = true;
    }

    void FAnimationGraphVM::Execute(const CAnimationGraph* Graph, FSkeletonResource* Skeleton, float DeltaTime, FAnimGraphVMState& State, TVector<FMatrix4>& OutMatrices)
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
                    SM.TimerSlot >= NumState || SM.DurationSlot >= NumState)
                {
                    State.PoseRegisters[Dst].ResetToBindPose(Skeleton);
                    break;
                }

                int32 Current  = (int32)State.StateSlots[SM.CurrentStateSlot];
                int32 From     = (int32)State.StateSlots[SM.FromStateSlot];
                float Timer    = State.StateSlots[SM.TimerSlot];
                float Duration = State.StateSlots[SM.DurationSlot];

                Current = Math::Clamp(Current, 0, NumStates - 1);

                if (From < 0)
                {
                    // Not transitioning: take the first outgoing edge whose
                    // condition passes. Edges are checked in author order.
                    for (const FAnimGraphTransition& Transition : SM.Transitions)
                    {
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
                            From     = Current;
                            Current  = Transition.ToState;
                            Timer    = 0.0f;
                            Duration = Math::Max(Transition.BlendDuration, 0.0f);
                            break;
                        }
                    }
                }
                else
                {
                    // Mid-transition: bCanInterrupt transitions are re-checked each frame so a
                    // higher-priority condition can pre-empt the cross-fade in flight.
                    From   = Math::Clamp(From, 0, NumStates - 1);
                    Timer += DeltaTime;

                    bool bInterrupted = false;
                    for (const FAnimGraphTransition& Transition : SM.Transitions)
                    {
                        if (!Transition.bCanInterrupt)
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
                            // Without per-frame pose caching there's a small pop at the seam; longer BlendDurations hide it.
                            From     = Current;
                            Current  = Transition.ToState;
                            Timer    = 0.0f;
                            Duration = Math::Max(Transition.BlendDuration, 0.0f);
                            bInterrupted = true;
                            break;
                        }
                    }

                    if (!bInterrupted && Timer >= Duration)
                    {
                        From  = -1;
                        Timer = 0.0f;
                    }
                }

                State.StateSlots[SM.CurrentStateSlot] = (float)Current;
                State.StateSlots[SM.FromStateSlot]    = (float)From;
                State.StateSlots[SM.TimerSlot]        = Timer;
                State.StateSlots[SM.DurationSlot]     = Duration;

                const uint16 CurReg = SM.StatePoseRegisters[Current];

                if (From >= 0 && From < NumStates)
                {
                    const uint16 FromReg = SM.StatePoseRegisters[From];
                    const float Alpha = Duration > 0.0f ? Math::Clamp(Timer / Duration, 0.0f, 1.0f) : 1.0f;
                    if (CurReg < NumPose && FromReg < NumPose)
                    {
                        AnimPose::Blend(State.PoseRegisters[FromReg], State.PoseRegisters[CurReg], Alpha, State.PoseRegisters[Dst]);
                    }
                }
                else if (CurReg < NumPose)
                {
                    State.PoseRegisters[Dst] = State.PoseRegisters[CurReg];
                }
                break;
            }

            case EAnimOp::BoneTransform:
            {
                const uint16 Src      = Reader.Read<uint16>();
                const uint16 AlphaReg = Reader.Read<uint16>();
                const uint16 BoneIdx  = Reader.Read<uint16>();
                const uint16 SpaceReg = Reader.Read<uint16>();
                const uint16 ModeReg  = Reader.Read<uint16>();
                const FVector3 T     = Reader.Read<FVector3>();
                const FQuat R     = Reader.Read<FQuat>();
                const FVector3 S     = Reader.Read<FVector3>();
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
                const FVector3 Pole  = Reader.Read<FVector3>();
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
                    AnimPose::ToSkinningMatrices(State.PoseRegisters[Src], Skeleton, OutMatrices);
                    bOutputWritten = true;
                }
                Reader.Cursor = Reader.Size;
                break;
            }

            default:
            {
                // Unknown opcode -- the stream is corrupt or version-mismatched.
                // Stop rather than walk off into garbage.
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
