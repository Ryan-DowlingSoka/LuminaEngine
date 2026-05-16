#include "AnimGraphNode_Output.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphCompiler.h"

namespace Lumina
{
    void CAnimGraphNode_Output::BuildNode()
    {
        ResultPosePin = CreateAnimPin("Result Pose", ENodePinDirection::Input, EAnimPinType::Pose);
    }

    void CAnimGraphNode_Output::GenerateBytecode(FAnimationGraphCompiler& Compiler)
    {
        // Resolve the pose feeding the graph but do not emit the Output opcode
        // here -- the owning graph decides whether this is the final frame
        // output (top-level) or a state's sub-graph result (state machine).
        ResolvedPoseRegister = ResolvePoseInput(ResultPosePin, Compiler);
    }
}
