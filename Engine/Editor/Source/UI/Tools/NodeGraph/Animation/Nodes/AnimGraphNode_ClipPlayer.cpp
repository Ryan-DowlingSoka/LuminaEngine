#include "AnimGraphNode_ClipPlayer.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphCompiler.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"

namespace Lumina
{
    void CAnimGraphNode_ClipPlayer::BuildNode()
    {
        SpeedPin    = CreateAnimPin("Speed", ENodePinDirection::Input, EAnimPinType::Value, 1.0f);
        PosePin     = CreateAnimPin("Pose", ENodePinDirection::Output, EAnimPinType::Pose);
        FinishedPin = CreateAnimPin("Finished", ENodePinDirection::Output, EAnimPinType::Value);
    }

    void CAnimGraphNode_ClipPlayer::GenerateBytecode(FAnimationGraphCompiler& Compiler)
    {
        if (!Clip.IsValid())
        {
            EdNodeGraph::FError Error;
            Error.Name        = "Missing Clip";
            Error.Description = "Play Animation Clip node has no clip assigned; it will evaluate to the bind pose.";
            Error.Node        = this;
            Compiler.AddError(Error);
        }

        const uint16 ClipIndex = Compiler.AddClip(Clip.Get());
        const uint16 StateSlot = Compiler.AllocStateSlot();
        const uint16 SpeedReg  = ResolveValueInput(SpeedPin, Compiler);

        uint16 FinishedReg = 0;
        const uint16 TimeReg = Compiler.EmitAdvanceClock(StateSlot, SpeedReg, ClipIndex, LoopMode, FinishedReg);
        const uint16 PoseReg = Compiler.EmitSampleAnim(ClipIndex, TimeReg);

        Compiler.SetPinRegister(PosePin, PoseReg);
        Compiler.SetPinRegister(FinishedPin, FinishedReg);
    }
}
