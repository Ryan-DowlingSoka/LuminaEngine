#include "AnimGraphNode_FloatConstant.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphCompiler.h"

namespace Lumina
{
    void CAnimGraphNode_FloatConstant::BuildNode()
    {
        ValuePin = CreateAnimPin("Value", ENodePinDirection::Output, EAnimPinType::Value);
    }

    void CAnimGraphNode_FloatConstant::GenerateBytecode(FAnimationGraphCompiler& Compiler)
    {
        const uint16 ValueReg = Compiler.EmitLoadConst(Value);
        Compiler.SetPinRegister(ValuePin, ValueReg);
    }
}
