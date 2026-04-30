#include "MaterialNodeExpression.h"

#include "Core/Object/Cast.h"
#include "UI/Tools/NodeGraph/Material/MaterialOutput.h"

namespace Lumina
{
    void CMaterialExpression::BuildNode()
    {
        Output = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "Material Output", ENodePinDirection::Output));
        Output->SetShouldDrawEditor(false);
    }

    void CMaterialExpression_Math::BuildNode()
    {
        Super::BuildNode();
    }
}
