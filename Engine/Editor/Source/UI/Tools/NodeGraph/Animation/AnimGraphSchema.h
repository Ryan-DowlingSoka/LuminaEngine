#pragma once

#include "UI/Tools/NodeGraph/EdGraphSchema.h"

namespace Lumina
{
    // Connection rules for the animation node graph: a pin may only connect to
    // another pin of the same EAnimPinType (pose-to-pose, value-to-value).
    struct FAnimGraphSchema : public FEdGraphSchema
    {
        bool CanCreateConnection(CEdNodeGraphPin* From, CEdNodeGraphPin* To) const override;

        // Honors CAnimGraphPin::bAllowMultipleConnections so the State Machine
        // node's "States" pin can collect every state in one input.
        bool AllowsMultipleConnections(CEdNodeGraphPin* InputPin) const override;
    };

    const FAnimGraphSchema& GetAnimGraphSchema();
}
