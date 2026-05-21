#pragma once

#include "UI/Tools/NodeGraph/Animation/AnimGraphNode.h"
#include "AnimGraphNode_Remap.generated.h"

namespace Lumina
{
    // Remaps a scalar value from an input range to an output range. Built
    // purely from existing scalar opcodes, so it needs no dedicated VM
    // instruction. Useful for turning a raw parameter (speed, distance) into a
    // normalized blend alpha.
    REFLECT()
    class CAnimGraphNode_Remap : public CAnimGraphNode
    {
        GENERATED_BODY()
    public:

        FString GetNodeDisplayName() const override { return "Remap"; }
        FString GetNodeTooltip() const override { return "Remaps a value from [In Min, In Max] to [Out Min, Out Max]."; }

        void BuildNode() override;
        void GenerateBytecode(FAnimationGraphCompiler& Compiler) override;

        /** When true, the normalized input is clamped to [0, 1] before scaling. */
        PROPERTY(Editable, Category = "Remap")
        bool bClampToRange = true;

        CAnimGraphPin* ValuePin = nullptr;
        CAnimGraphPin* InMinPin = nullptr;
        CAnimGraphPin* InMaxPin = nullptr;
        CAnimGraphPin* OutMinPin = nullptr;
        CAnimGraphPin* OutMaxPin = nullptr;
        CAnimGraphPin* ResultPin = nullptr;
    };
}
