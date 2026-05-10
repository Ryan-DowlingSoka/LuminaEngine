#include "EdNode_Reroute.h"

#include "EdNodeGraphPin.h"
#include "Core/Object/Class.h"

namespace Lumina
{
    void CEdNode_Reroute::BuildNode()
    {
        Output = CreatePin(GetOutputPinClass(), "", ENodePinDirection::Output);
        Output->SetShouldDrawEditor(false);

        Input = CreatePin(GetInputPinClass(), "", ENodePinDirection::Input);
        Input->SetShouldDrawEditor(false);
    }

    CClass* CEdNode_Reroute::GetInputPinClass() const
    {
        return CEdNodeGraphPin::StaticClass();
    }

    CClass* CEdNode_Reroute::GetOutputPinClass() const
    {
        return CEdNodeGraphPin::StaticClass();
    }
}
