#pragma once

#include "Animation/Pose.h"
#include "Containers/Array.h"
#include "Core/Object/ObjectMacros.h"
#include "Core/Serialization/Archiver.h"
#include "AnimationGraphVM.generated.h"

namespace Lumina
{
    class CAnimationGraph;
    struct FSkeletonResource;

    // What AdvanceClock does when the playback clock reaches a clip's duration.
    REFLECT()
    enum class EClipLoopMode : uint8
    {
        Loop,
        PlayOnce,
    };

    // Frame the BoneTransform op interprets its (T, R, S) offsets in.
    REFLECT()
    enum class EBoneTransformSpace : uint8
    {
        // The bone's local frame (relative to its parent). Cheap; no FK walk.
        LocalBone,
        // The component (entity-root) space. The offset is applied to the bone's
        // global transform after FK; the result is converted back into local space.
        ComponentSpace,
    };

    // Whether the BoneTransform op layers the (T, R, S) onto the bone's existing
    // transform or replaces it.
    REFLECT()
    enum class EBoneTransformMode : uint8
    {
        // Additive: T offsets translation, R post-multiplies rotation, S
        // multiplies scale. Alpha scales how strongly the offset is applied.
        Add,
        // Override: lerp the bone's transform toward (T, R, S) by alpha.
        Replace,
    };

    enum class EAnimOp : uint8
    {
        Halt = 0,
        LoadConst,       // imm:float, dst:sReg
        LoadParam,       // paramIdx:uint16, dst:sReg
        ScalarOp,        // op:uint8, a:sReg, b:sReg, dst:sReg
        AdvanceClock,    // stateIdx:uint16, speed:sReg, clipIdx:uint16, loopMode:sReg, dstClock:sReg, dstFinished:sReg
        SampleAnim,      // clipIdx:uint16, time:sReg, dst:pReg
        RefPose,         // dst:pReg
        Blend,           // a:pReg, b:pReg, alpha:sReg, dst:pReg
        BlendMasked,     // a:pReg, b:pReg, alpha:sReg, maskIdx:uint16, dst:pReg
        MakeAdditive,    // src:pReg, dst:pReg
        ApplyAdditive,   // base:pReg, delta:pReg, alpha:sReg, dst:pReg
        EvalStateMachine,// smIdx:uint16, dst:pReg
        BoneTransform,   // src:pReg, alpha:sReg, boneIdx:uint16, space:sReg, mode:sReg, T:vec3, R:quat, S:vec3, dst:pReg
        TwoBoneIK,       // src:pReg, alpha:sReg, tx:sReg, ty:sReg, tz:sReg, rootIdx:uint16, midIdx:uint16, endIdx:uint16, pole:vec3, dst:pReg
        Output,          // src:pReg
    };

    REFLECT()
    enum class EAnimScalarOp : uint8
    {
        Add,
        Sub,
        Mul,
        Div,
        Min,
        Max,
        // Unary ops below ignore operand B.
        Clamp01,
        OneMinus,    // 1 - A
        Abs,         // |A|
        Sin,         // sin(A)
        Cos,         // cos(A)
    };

    // Per-instance mutable execution state. Persists across frames so playback
    // clocks keep advancing; owned by SAnimationGraphComponent.
    struct FAnimGraphVMState
    {
        TVector<float> ScalarRegisters;
        TVector<FPose> PoseRegisters;
        TVector<float> StateSlots;     // persistent playback clocks
        TVector<float> Parameters;     // current parameter values (editor / Lua driven)

        // Graph this state was sized against; the VM re-initializes the state
        // when the component's graph asset changes underneath it.
        const void* SourceGraph = nullptr;

        bool bInitialized = false;
    };

    // Stateless executor.
    class RUNTIME_API FAnimationGraphVM
    {
    public:

        // Sizes register files / state slots / parameters from the graph; call when the graph asset changes.
        static void InitState(const CAnimationGraph* Graph, FAnimGraphVMState& State);

        // Executes the bytecode for one frame.
        static void Execute(const CAnimationGraph* Graph,
                            FSkeletonResource* Skeleton,
                            float DeltaTime,
                            FAnimGraphVMState& State,
                            TVector<FMatrix4>& OutMatrices,
                            bool bLockRoot = false,
                            int32 RootBoneIndex = INDEX_NONE);
    };
}
