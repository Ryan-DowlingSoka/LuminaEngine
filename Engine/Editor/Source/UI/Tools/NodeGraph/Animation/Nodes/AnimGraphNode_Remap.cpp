#include "AnimGraphNode_Remap.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphCompiler.h"

namespace Lumina
{
    void CAnimGraphNode_Remap::BuildNode()
    {
        ValuePin  = CreateAnimPin("Value", ENodePinDirection::Input, EAnimPinType::Value, 0.0f);
        ResultPin = CreateAnimPin("Result", ENodePinDirection::Output, EAnimPinType::Value);
    }

    void CAnimGraphNode_Remap::GenerateBytecode(FAnimationGraphCompiler& Compiler)
    {
        const uint16 ValueReg  = ResolveValueInput(ValuePin, Compiler);
        const uint16 InMinReg  = Compiler.EmitLoadConst(InMin);
        const uint16 InMaxReg  = Compiler.EmitLoadConst(InMax);
        const uint16 OutMinReg = Compiler.EmitLoadConst(OutMin);
        const uint16 OutMaxReg = Compiler.EmitLoadConst(OutMax);

        // Normalize: (Value - InMin) / (InMax - InMin). Divide-by-zero on a
        // degenerate input range resolves to 0 inside the VM.
        const uint16 Numerator   = Compiler.EmitScalarOp(EAnimScalarOp::Sub, ValueReg, InMinReg);
        const uint16 Denominator = Compiler.EmitScalarOp(EAnimScalarOp::Sub, InMaxReg, InMinReg);
        uint16 NormalizedReg     = Compiler.EmitScalarOp(EAnimScalarOp::Div, Numerator, Denominator);

        if (bClampToRange)
        {
            NormalizedReg = Compiler.EmitScalarOp(EAnimScalarOp::Clamp01, NormalizedReg, NormalizedReg);
        }

        // Scale into the output range: OutMin + Normalized * (OutMax - OutMin).
        const uint16 OutRangeReg = Compiler.EmitScalarOp(EAnimScalarOp::Sub, OutMaxReg, OutMinReg);
        const uint16 ScaledReg   = Compiler.EmitScalarOp(EAnimScalarOp::Mul, NormalizedReg, OutRangeReg);
        const uint16 ResultReg   = Compiler.EmitScalarOp(EAnimScalarOp::Add, ScaledReg, OutMinReg);

        Compiler.SetPinRegister(ResultPin, ResultReg);
    }
}
