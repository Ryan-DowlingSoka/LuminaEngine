#include "AnimGraphNode_ScalarOps.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphCompiler.h"

namespace Lumina
{
    void CAnimGraphNode_ScalarBinaryOp::BuildNode()
    {
        APin      = CreateAnimPin("A", ENodePinDirection::Input, EAnimPinType::Value, GetDefaultA());
        BPin      = CreateAnimPin("B", ENodePinDirection::Input, EAnimPinType::Value, GetDefaultB());
        ResultPin = CreateAnimPin("Result", ENodePinDirection::Output, EAnimPinType::Value);
    }

    void CAnimGraphNode_ScalarBinaryOp::GenerateBytecode(FAnimationGraphCompiler& Compiler)
    {
        const uint16 RegA   = ResolveValueInput(APin, Compiler);
        const uint16 RegB   = ResolveValueInput(BPin, Compiler);
        const uint16 Result = Compiler.EmitScalarOp(GetScalarOp(), RegA, RegB);

        Compiler.SetPinRegister(ResultPin, Result);
    }

    void CAnimGraphNode_ScalarUnaryOp::BuildNode()
    {
        APin      = CreateAnimPin("A", ENodePinDirection::Input, EAnimPinType::Value, GetDefaultA());
        ResultPin = CreateAnimPin("Result", ENodePinDirection::Output, EAnimPinType::Value);
    }

    void CAnimGraphNode_ScalarUnaryOp::GenerateBytecode(FAnimationGraphCompiler& Compiler)
    {
        // Unary ops ignore operand B; pass the A register twice so the opcode
        // layout stays uniform with the binary ops.
        const uint16 RegA   = ResolveValueInput(APin, Compiler);
        const uint16 Result = Compiler.EmitScalarOp(GetScalarOp(), RegA, RegA);

        Compiler.SetPinRegister(ResultPin, Result);
    }
}
