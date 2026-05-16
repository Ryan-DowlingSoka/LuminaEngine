#pragma once

#include "UI/Tools/NodeGraph/Animation/AnimGraphNode.h"
#include "AnimGraphNode_Output.generated.h"

namespace Lumina
{
    // Terminal node of the graph. Whatever pose is wired into its single input
    // becomes the final pose evaluated for the frame. Exactly one exists per
    // graph and it cannot be deleted.
    REFLECT()
    class CAnimGraphNode_Output : public CAnimGraphNode
    {
        GENERATED_BODY()
    public:

        FString GetNodeDisplayName() const override { return "Output Pose"; }
        FString GetNodeTooltip() const override { return "Final pose evaluated by the animation graph."; }
        uint32 GetNodeTitleColor() const override { return IM_COL32(35, 35, 200, 255); }
        bool IsDeletable() const override { return false; }

        void BuildNode() override;
        void GenerateBytecode(FAnimationGraphCompiler& Compiler) override;

        // Pose register resolved from the Result Pose input during the last
        // compile. The owning graph emits the final Output opcode (top-level)
        // or threads this register into a state machine (sub-graph).
        uint16 GetResolvedPoseRegister() const { return ResolvedPoseRegister; }

        CAnimGraphPin* ResultPosePin = nullptr;

    private:

        uint16 ResolvedPoseRegister = 0;
    };
}
