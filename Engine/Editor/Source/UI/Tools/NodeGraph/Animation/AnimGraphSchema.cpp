#include "AnimGraphSchema.h"
#include "AnimGraphPin.h"
#include "Core/Object/Cast.h"

namespace Lumina
{
    bool FAnimGraphSchema::CanCreateConnection(CEdNodeGraphPin* From, CEdNodeGraphPin* To) const
    {
        if (!FEdGraphSchema::CanCreateConnection(From, To))
        {
            return false;
        }

        CAnimGraphPin* FromAnim = Cast<CAnimGraphPin>(From);
        CAnimGraphPin* ToAnim   = Cast<CAnimGraphPin>(To);
        if (FromAnim == nullptr || ToAnim == nullptr)
        {
            return false;
        }

        return FromAnim->GetPinType() == ToAnim->GetPinType();
    }

    bool FAnimGraphSchema::AllowsMultipleConnections(CEdNodeGraphPin* InputPin) const
    {
        CAnimGraphPin* AnimPin = Cast<CAnimGraphPin>(InputPin);
        return AnimPin != nullptr && AnimPin->bAllowMultipleConnections;
    }

    const FAnimGraphSchema& GetAnimGraphSchema()
    {
        static FAnimGraphSchema Schema;
        return Schema;
    }
}
