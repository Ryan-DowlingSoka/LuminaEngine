#include "AnimGraphNode.h"
#include "AnimationGraphCompiler.h"
#include "Core/Object/Cast.h"
#include "UI/Tools/NodeGraph/EdNodeGraphPin.h"

namespace Lumina
{
    CAnimGraphPin* CAnimGraphNode::CreateAnimPin(const FString& Name, ENodePinDirection Direction, EAnimPinType Type, float DefaultValue)
    {
        CEdNodeGraphPin* Pin = CreatePin(CAnimGraphPin::StaticClass(), Name, Direction);
        CAnimGraphPin* AnimPin = static_cast<CAnimGraphPin*>(Pin);
        AnimPin->SetPinName(Name);
        AnimPin->SetPinType(Type);
        AnimPin->DefaultValue = DefaultValue;
        return AnimPin;
    }

    uint16 CAnimGraphNode::ResolvePoseInput(CEdNodeGraphPin* InputPin, FAnimationGraphCompiler& Compiler)
    {
        if (InputPin != nullptr && InputPin->HasConnection())
        {
            CEdNodeGraphPin* Source = InputPin->GetConnection(0);
            uint16 Register;
            if (Compiler.TryGetPinRegister(Source, Register))
            {
                return Register;
            }
        }

        // Unconnected pose inputs resolve to the skeleton bind pose so partially
        // wired graphs still compile and evaluate.
        return Compiler.EmitRefPose();
    }

    uint16 CAnimGraphNode::ResolveValueInput(CEdNodeGraphPin* InputPin, FAnimationGraphCompiler& Compiler)
    {
        if (InputPin != nullptr && InputPin->HasConnection())
        {
            CEdNodeGraphPin* Source = InputPin->GetConnection(0);
            uint16 Register;
            if (Compiler.TryGetPinRegister(Source, Register))
            {
                return Register;
            }
        }

        float Default = 0.0f;
        if (CAnimGraphPin* AnimPin = Cast<CAnimGraphPin>(InputPin))
        {
            Default = AnimPin->DefaultValue;
        }
        return Compiler.EmitLoadConst(Default);
    }
}
