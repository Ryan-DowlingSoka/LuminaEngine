#pragma once

#include "UI/Tools/NodeGraph/EdGraphNode.h"
#include "Animation/AnimationGraphVM.h"
#include "Assets/AssetTypes/Animation/AnimationGraph/AnimationGraph.h"
#include "Containers/Array.h"
#include "Core/Object/ObjectHandleTyped.h"

namespace Lumina
{
    class CAnimation;
    class CAnimationGraph;
    struct FSkeletonResource;

    // Bytecode-emission backend for the animation node graph. Nodes call the
    // Emit* / Alloc* / Add* API during CAnimationGraphNodeGraph::CompileGraph;
    // BuildGraph then stamps the accumulated program, resource tables, and
    // register-file sizing into the runtime CAnimationGraph asset.
    //
    // Mirrors FMaterialCompiler in spirit: the graph walk / topo-sort lives in
    // the node graph, this class only knows how to emit a valid instruction
    // stream and hand back the register indices that wire instructions together.
    class FAnimationGraphCompiler
    {
    public:

        FAnimationGraphCompiler() = default;

        // -- Resource registration ------------------------------------------

        // Registers a clip and returns its index; dedups identical clips.
        uint16 AddClip(CAnimation* Clip);

        // Registers an exposed parameter and returns its index; dedups by name.
        // A name collision with a different type reports an error and returns
        // the existing index.
        int32 AddParameter(const FName& Name, EAnimGraphParamType Type, float DefaultValue);

        // -- Register / persistent-state allocation -------------------------

        uint16 AllocScalarReg() { return NextScalarReg++; }
        uint16 AllocPoseReg()   { return NextPoseReg++; }
        uint16 AllocStateSlot() { return NextStateSlot++; }

        // -- Bytecode emission ----------------------------------------------
        // Emitters that produce a value return the destination register they
        // allocated; callers thread that index into downstream emitters.

        uint16 EmitLoadConst(float Value);
        uint16 EmitLoadParam(uint16 ParameterIndex);
        uint16 EmitScalarOp(EAnimScalarOp Op, uint16 RegA, uint16 RegB);

        // Advances a persistent playback clock and emits two scalar outputs:
        // the clock (returned) and a "finished" flag (written to OutFinishedReg).
        // For Loop mode the finished flag stays at 0; PlayOnce sets it to 1 once
        // the clip's duration is reached.
        uint16 EmitAdvanceClock(uint16 StateSlot, uint16 SpeedReg, uint16 ClipIndex, EClipLoopMode LoopMode, uint16& OutFinishedReg);

        uint16 EmitSampleAnim(uint16 ClipIndex, uint16 TimeReg);
        uint16 EmitRefPose();
        uint16 EmitBlend(uint16 PoseRegA, uint16 PoseRegB, uint16 AlphaReg);
        uint16 EmitBlendMasked(uint16 PoseRegA, uint16 PoseRegB, uint16 AlphaReg, uint16 MaskIndex);

        // Additive blending: MakeAdditive produces a pose-delta against the
        // skeleton's bind pose; ApplyAdditive layers that delta onto a base pose.
        uint16 EmitMakeAdditive(uint16 SrcPoseReg);
        uint16 EmitApplyAdditive(uint16 BasePoseReg, uint16 DeltaPoseReg, uint16 AlphaReg);

        // Registers a compiled state machine and emits the opcode that evaluates
        // it. The machine's StatePoseRegisters / *Slot fields must already be
        // filled in by the caller (the StateMachine node). Returns the pose
        // register the machine's resolved pose is written to.
        uint16 EmitEvalStateMachine(FAnimGraphStateMachine&& StateMachine);

        void   EmitOutput(uint16 PoseReg);
        void   EmitHalt();

        // -- Pin -> register wiring -----------------------------------------
        // Nodes record the register their output pin resolved to; downstream
        // nodes look it up to thread the value through. Keyed on the pin
        // pointer, valid for the duration of one CompileGraph pass.

        void SetPinRegister(const CEdNodeGraphPin* Pin, uint16 Register);
        bool TryGetPinRegister(const CEdNodeGraphPin* Pin, uint16& OutRegister) const;

        // Modifies a single bone of an incoming pose. Returns the destination
        // pose register; (T, R, S) are baked into the bytecode at compile time.
        uint16 EmitBoneTransform(uint16 SrcPoseReg, uint16 AlphaReg, uint16 BoneIndex,
                                 EBoneTransformSpace Space, EBoneTransformMode Mode,
                                 const glm::vec3& Translation, const glm::quat& Rotation, const glm::vec3& Scale);

        // Two-bone analytical IK. Target X/Y/Z come from scalar registers so they
        // can be driven dynamically; Pole is baked at compile time.
        uint16 EmitTwoBoneIK(uint16 SrcPoseReg, uint16 AlphaReg,
                             uint16 TargetXReg, uint16 TargetYReg, uint16 TargetZReg,
                             uint16 RootIndex, uint16 MidIndex, uint16 EndIndex,
                             const glm::vec3& Pole);

        // -- Bone masks / skeleton -------------------------------------------
        // The editor tool calls SetSkeleton + ResolveBoneMasks once, up front,
        // so individual nodes can look up bones / masks by name in their
        // GenerateBytecode without re-fetching the asset.

        void SetSkeleton(const FSkeletonResource* InSkeleton) { Skeleton = InSkeleton; }
        const FSkeletonResource* GetSkeleton() const { return Skeleton; }

        int32 ResolveBoneIndex(const FName& BoneName) const;

        void ResolveBoneMasks(const TVector<FAnimGraphBoneMaskDef>& Defs, const FSkeletonResource* InSkeleton);
        int32 FindBoneMaskIndex(const FName& Name) const;

        // -- Errors ----------------------------------------------------------

        FORCEINLINE bool HasErrors() const { return !Errors.empty(); }
        FORCEINLINE void AddError(const EdNodeGraph::FError& Error) { Errors.push_back(Error); }
        FORCEINLINE const TVector<EdNodeGraph::FError>& GetErrors() const { return Errors; }

        // -- Finalize --------------------------------------------------------

        // Writes the compiled program, clip / parameter tables, and register
        // sizing into OutGraph. Appends a Halt if the program did not end in
        // an Output. Safe to call once per compile.
        void BuildGraph(CAnimationGraph* OutGraph);

    private:

        template <typename T>
        void Write(const T& Value)
        {
            const uint8* Bytes = reinterpret_cast<const uint8*>(&Value);
            Bytecode.insert(Bytecode.end(), Bytes, Bytes + sizeof(T));
        }

        void WriteOp(EAnimOp Op) { Bytecode.push_back((uint8)Op); }

        TVector<uint8>                          Bytecode;
        TVector<TObjectPtr<CAnimation>>         Clips;
        TVector<FAnimGraphParameter>            Parameters;
        TVector<FAnimGraphBoneMask>             BoneMasks;
        THashMap<FName, int32>                  BoneMaskNameToIndex;
        TVector<FAnimGraphStateMachine>         StateMachines;
        TVector<EdNodeGraph::FError>            Errors;
        THashMap<const CEdNodeGraphPin*, uint16> PinRegisters;
        const FSkeletonResource*                Skeleton = nullptr;

        uint16 NextScalarReg = 0;
        uint16 NextPoseReg   = 0;
        uint16 NextStateSlot = 0;

        bool bEmittedOutput = false;
    };
}
