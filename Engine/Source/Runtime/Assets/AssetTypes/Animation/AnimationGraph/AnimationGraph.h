#pragma once

#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "AnimationGraph.generated.h"

namespace Lumina
{
    class CSkeleton;
    class CAnimation;
    class CBlackboard;

    REFLECT()
    enum class EAnimGraphParamType : uint8
    {
        Float,
        Bool,
    };

    // Comparison applied by a state-machine transition condition: the named
    // parameter's current value is tested against the transition's CompareValue.
    REFLECT()
    enum class EAnimTransitionCompare : uint8
    {
        Greater,
        GreaterEqual,
        Less,
        LessEqual,
        Equal,
        NotEqual,
    };

    // A graph parameter: a named value the editor and Lua scripts can tweak at
    // runtime to drive blend weights, playback speeds, etc.
    struct FAnimGraphParameter
    {
        FName Name;
        EAnimGraphParamType Type = EAnimGraphParamType::Float;
        float DefaultValue = 0.0f;

        friend FArchive& operator << (FArchive& Ar, FAnimGraphParameter& Data)
        {
            Ar << Data.Name;
            Ar << Data.Type;
            Ar << Data.DefaultValue;
            return Ar;
        }
    };

    // A single state-machine edge. Resolved from the editor's name-based
    // transition definitions into state indices at compile time. The VM checks
    // the condition each frame the FromState is active and, when it passes,
    // cross-fades to ToState over BlendDuration seconds.
    struct FAnimGraphTransition
    {
        // Index into the owning state machine's state list. -1 means "any
        // state" -- the transition is checked regardless of the active state.
        int32 FromState = -1;
        int32 ToState = 0;

        // Parameter compared against CompareValue to gate the transition. A
        // parameter the graph does not declare evaluates as 0.
        FName ConditionParameter;
        EAnimTransitionCompare Compare = EAnimTransitionCompare::Greater;
        float CompareValue = 0.0f;

        // Cross-fade length in seconds. 0 snaps instantly.
        float BlendDuration = 0.2f;

        // When true, the VM re-evaluates this transition's condition every
        // frame *during* an ongoing cross-fade, so it can pre-empt the
        // transition in flight. Default off: once a transition starts, it
        // runs to completion.
        bool bCanInterrupt = false;

        friend FArchive& operator << (FArchive& Ar, FAnimGraphTransition& Data)
        {
            Ar << Data.FromState;
            Ar << Data.ToState;
            Ar << Data.ConditionParameter;
            Ar << Data.Compare;
            Ar << Data.CompareValue;
            Ar << Data.BlendDuration;
            Ar << Data.bCanInterrupt;
            return Ar;
        }
    };

    // One bone of a bone mask: a per-bone weight applied during a masked blend.
    // Bone names are resolved to skeleton bone indices at compile time and
    // baked into FAnimGraphBoneMask::Weights for cheap runtime access.
    REFLECT()
    struct FAnimGraphBoneMaskBone
    {
        GENERATED_BODY()

        /** Name of the bone to weight. Must exist in the graph's skeleton. */
        PROPERTY(Editable, Category = "Bone Mask", BonePicker)
        FName BoneName;

        /** Blend weight applied to this bone (0 = base, 1 = overlay). */
        PROPERTY(Editable, Category = "Bone Mask", ClampMin = 0.0f, ClampMax = 1.0f)
        float Weight = 1.0f;
    };

    // Editor-authored bone mask: a named list of (bone, weight) entries. The
    // compiler resolves these against the skeleton and writes the per-bone
    // weight array into the runtime FAnimGraphBoneMask table.
    REFLECT()
    struct FAnimGraphBoneMaskDef
    {
        GENERATED_BODY()

        /** Identifier referenced by Layered Blend Per Bone nodes. */
        PROPERTY(Editable, Category = "Bone Mask")
        FName Name;

        /** Per-bone weight entries; unlisted bones default to zero. */
        PROPERTY(Editable, Category = "Bone Mask")
        TVector<FAnimGraphBoneMaskBone> Bones;
    };

    // Compiled bone mask: a dense per-bone weight array sized to the skeleton's
    // bone count. The VM's BlendMasked op indexes this directly.
    struct FAnimGraphBoneMask
    {
        TVector<float> Weights;

        friend FArchive& operator << (FArchive& Ar, FAnimGraphBoneMask& Data)
        {
            Ar << Data.Weights;
            return Ar;
        }
    };

    // A compiled state machine. Each state owns a pose register holding its
    // (already evaluated) blend-tree pose; the VM picks the active state, runs
    // transition conditions, and cross-fades between states. Mutable per-frame
    // bookkeeping (active state, transition timer) lives in the per-instance
    // FAnimGraphVMState.StateSlots, addressed by the *Slot indices below.
    struct FAnimGraphStateMachine
    {
        // State entered when the VM state is first initialized.
        int32 EntryState = 0;

        // One pose register per state, in state-index order.
        TVector<uint16> StatePoseRegisters;

        // Outgoing edges, checked in list order; the first passing one wins.
        TVector<FAnimGraphTransition> Transitions;

        // Indices into FAnimGraphVMState.StateSlots for this machine's
        // persistent bookkeeping. CurrentState / FromState hold state indices
        // (FromState is -1 when not mid-transition); Timer is elapsed transition
        // time; Duration is the active transition's length.
        uint16 CurrentStateSlot = 0;
        uint16 FromStateSlot = 0;
        uint16 TimerSlot = 0;
        uint16 DurationSlot = 0;

        friend FArchive& operator << (FArchive& Ar, FAnimGraphStateMachine& Data)
        {
            Ar << Data.EntryState;
            Ar << Data.StatePoseRegisters;
            Ar << Data.Transitions;
            Ar << Data.CurrentStateSlot;
            Ar << Data.FromStateSlot;
            Ar << Data.TimerSlot;
            Ar << Data.DurationSlot;
            return Ar;
        }
    };

    // Runtime animation-graph asset: a compiled bytecode program plus the
    // resources and metadata its instructions reference. The editor authors a
    // node graph (CAnimationGraphNodeGraph) and compiles it into this asset;
    // SAnimationGraphComponent / SAnimationGraphSystem execute it each frame
    // through FAnimationGraphVM.
    REFLECT()
    class RUNTIME_API CAnimationGraph : public CObject
    {
        GENERATED_BODY()

        friend class CAnimationGraphNodeGraph;

    public:

        void Serialize(FArchive& Ar) override;
        bool IsAsset() const override { return true; }

        int32 FindParameterIndex(const FName& Name) const;

        bool IsCompiled() const { return !Bytecode.empty(); }

        /** Skeleton every pose produced by this graph is authored against. */
        PROPERTY(Editable, Category = "Animation")
        TObjectPtr<CSkeleton> Skeleton;

        /** Blackboard schema this graph reads parameters from. Get Parameter
         *  nodes and transition conditions pick keys from it; at runtime the
         *  values come from the entity's SBlackboardComponent. */
        PROPERTY(Editable, Category = "Animation")
        TObjectPtr<CBlackboard> Blackboard;

        /** Animation clips referenced by SampleAnim opcodes, indexed by clip index. */
        PROPERTY()
        TVector<TObjectPtr<CAnimation>> Clips;

        /** Lua- and editor-tweakable parameters that drive the graph. */
        TVector<FAnimGraphParameter> Parameters;

        /** Editor-authored bone mask definitions; resolved into BoneMasks at compile. */
        PROPERTY(Editable, Category = "Bone Masks")
        TVector<FAnimGraphBoneMaskDef> BoneMaskDefs;

        /** Compiled per-bone weight arrays referenced by BlendMasked opcodes. */
        TVector<FAnimGraphBoneMask> BoneMasks;

        /** State machines referenced by EvalStateMachine opcodes, indexed by machine index. */
        TVector<FAnimGraphStateMachine> StateMachines;

        /** Compiled instruction stream consumed by FAnimationGraphVM. */
        TVector<uint8> Bytecode;

        /** Register-file and persistent-state sizing produced by the compiler. */
        uint16 NumScalarRegisters = 0;
        uint16 NumPoseRegisters = 0;
        uint16 NumStateSlots = 0;
    };
}
