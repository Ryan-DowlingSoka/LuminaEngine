#pragma once

#include "UI/Tools/NodeGraph/Animation/AnimGraphNode.h"
#include "AnimGraphNode_LayeredBlendPerBone.generated.h"

namespace Lumina
{
    // Blends Base and Overlay per-bone using a bone mask. Each bone's effective
    // alpha is the global Alpha multiplied by that bone's mask weight, so an
    // upper-body mask leaves the lower body fully on the base while letting the
    // overlay take over the spine / arms. The mask is defined on the graph
    // asset's Bone Masks list and referenced here by name.
    REFLECT()
    class CAnimGraphNode_LayeredBlendPerBone : public CAnimGraphNode
    {
        GENERATED_BODY()
    public:

        FString GetNodeDisplayName() const override { return "Layered Blend Per Bone"; }
        FString GetNodeTooltip() const override { return "Per-bone blend of Overlay onto Base using a named bone mask."; }
        FFixedString GetNodeCategory() const override { return "Animation"; }

        void BuildNode() override;
        void GenerateBytecode(FAnimationGraphCompiler& Compiler) override;

        /** Name of a bone mask declared on the graph asset's Bone Masks list. */
        PROPERTY(Editable, Category = "Bone Mask")
        FName MaskName;

        CAnimGraphPin* BasePin = nullptr;
        CAnimGraphPin* OverlayPin = nullptr;
        CAnimGraphPin* AlphaPin = nullptr;
        CAnimGraphPin* ResultPin = nullptr;
    };
}
