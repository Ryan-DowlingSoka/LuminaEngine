#pragma once

#include "UI/Tools/NodeGraph/EdNodeGraphPin.h"
#include "AnimGraphPin.generated.h"

namespace Lumina
{
    enum class EAnimPinType : uint8
    {
        Pose,       // a blended skeletal pose flowing between nodes
        Value,      // a scalar float (blend alphas, playback speeds, parameter outputs)
        StateFlow,  // a state-machine edge: connects State nodes on the state machine canvas
    };

    // Typed pin for the animation node graph. The graph schema only permits
    // connections between pins of the same EAnimPinType.
    REFLECT()
    class CAnimGraphPin : public CEdNodeGraphPin
    {
        GENERATED_BODY()
    public:

        void SetPinType(EAnimPinType InType);
        EAnimPinType GetPinType() const { return PinType; }

        /** Fed into the compiler as a LoadConst when a Value input is left unconnected. */
        float DefaultValue = 0.0f;

        /** When true, this input pin accepts more than one incoming wire (e.g. the
         *  State Machine node's "States" pin). The anim graph schema honors this. */
        bool bAllowMultipleConnections = false;

        EAnimPinType PinType = EAnimPinType::Value;
    };
}
