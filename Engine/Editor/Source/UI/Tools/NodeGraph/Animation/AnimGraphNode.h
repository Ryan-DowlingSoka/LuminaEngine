#pragma once

#include "AnimGraphPin.h"
#include "UI/Tools/NodeGraph/EdGraphNode.h"
#include "AnimGraphNode.generated.h"

namespace Lumina
{
    class FAnimationGraphCompiler;

    // Base class for every animation node-graph node. Subclasses build their
    // pins in BuildNode() and emit bytecode in GenerateBytecode(); the node
    // graph walks them in topological order during compile.
    REFLECT()
    class CAnimGraphNode : public CEdGraphNode
    {
        GENERATED_BODY()
    public:

        virtual void GenerateBytecode(FAnimationGraphCompiler& Compiler) { UNREACHABLE(); }

        FFixedString GetNodeCategory() const override { return "Animation"; }
        uint32 GetNodeTitleColor() const override { return IM_COL32(60, 110, 70, 255); }

    protected:

        // Creates a typed animation pin, sets its name / color / default, and
        // returns it already cast to CAnimGraphPin.
        CAnimGraphPin* CreateAnimPin(const FString& Name, ENodePinDirection Direction, EAnimPinType Type, float DefaultValue = 0.0f);

        // Resolves the pose register feeding InputPin, or emits a bind-pose
        // RefPose when the pin is unconnected.
        static uint16 ResolvePoseInput(CEdNodeGraphPin* InputPin, FAnimationGraphCompiler& Compiler);

        // Resolves the scalar register feeding InputPin, or emits a LoadConst of
        // the pin's DefaultValue when the pin is unconnected.
        static uint16 ResolveValueInput(CEdNodeGraphPin* InputPin, FAnimationGraphCompiler& Compiler);
    };
}
