#pragma once
#include "EdGraphNode.h"
#include "Core/Object/ObjectMacros.h"
#include "ednode_reroute.generated.h"

namespace Lumina
{
    class CClass;
    class CEdNodeGraphPin;
}

namespace Lumina
{
    // Wire-passthrough node. Renders as a small dot in the graph editor and contributes nothing to
    // execution -- graph compilers / closure walks skip it and chase through to the connected source.
    // The pin classes are virtualised so derived graphs (e.g. material) can swap in their typed pin
    // subclass; the base implementation creates plain CEdNodeGraphPin instances.
    REFLECT()
    class CEdNode_Reroute : public CEdGraphNode
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        bool WantsTitlebar() const override { return false; }
        bool IsRerouteNode() const override { return true; }

        FString GetNodeDisplayName() const override { return "Reroute"; }
        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeTooltip() const override { return "Reroute -- visual wire passthrough."; }

        ImVec2 GetMinNodeBodySize() const override { return ImVec2(0, 0); }
        ImVec2 GetMinNodeTitleBarSize() const override { return ImVec2(0, 0); }

        // Override to use a typed pin subclass (e.g. CMaterialInput). Called from BuildNode at
        // construction time, so changing the return value after the node is built has no effect.
        virtual CClass* GetInputPinClass() const;
        virtual CClass* GetOutputPinClass() const;

        CEdNodeGraphPin* GetInputPin() const { return Input; }
        CEdNodeGraphPin* GetOutputPin() const { return Output; }

    protected:

        CEdNodeGraphPin* Input = nullptr;
        CEdNodeGraphPin* Output = nullptr;
    };
}
