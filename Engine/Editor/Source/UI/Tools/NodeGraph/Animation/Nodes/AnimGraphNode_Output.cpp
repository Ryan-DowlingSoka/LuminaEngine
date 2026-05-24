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
        // Don't emit Output opcode here; the owning graph decides top-level vs sub-graph output.
        ResolvedPoseRegister = ResolvePoseInput(ResultPosePin, Compiler);
    }
}
