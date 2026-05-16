#include "AnimGraphPin.h"

namespace Lumina
{
    void CAnimGraphPin::SetPinType(EAnimPinType InType)
    {
        PinType = InType;
        switch (InType)
        {
        case EAnimPinType::Pose:      PinColor = IM_COL32(120, 220, 130, 255); break;  // green
        case EAnimPinType::Value:     PinColor = IM_COL32(110, 170, 240, 255); break;  // blue
        case EAnimPinType::StateFlow: PinColor = IM_COL32(240, 200, 100, 255); break;  // amber
        }
    }
}
