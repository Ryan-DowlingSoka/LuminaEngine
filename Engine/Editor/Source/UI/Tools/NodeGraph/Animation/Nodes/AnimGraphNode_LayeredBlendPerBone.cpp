#include "AnimGraphNode_LayeredBlendPerBone.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphCompiler.h"

namespace Lumina
{
    void CAnimGraphNode_LayeredBlendPerBone::BuildNode()
    {
        BasePin    = CreateAnimPin("Base", ENodePinDirection::Input, EAnimPinType::Pose);
        OverlayPin = CreateAnimPin("Overlay", ENodePinDirection::Input, EAnimPinType::Pose);
        AlphaPin   = CreateAnimPin("Alpha", ENodePinDirection::Input, EAnimPinType::Value, 1.0f);
        ResultPin  = CreateAnimPin("Result", ENodePinDirection::Output, EAnimPinType::Pose);
    }

    void CAnimGraphNode_LayeredBlendPerBone::GenerateBytecode(FAnimationGraphCompiler& Compiler)
    {
        const int32 MaskIndex = Compiler.FindBoneMaskIndex(MaskName);
        if (MaskIndex == INDEX_NONE)
        {
            EdNodeGraph::FError Error;
            Error.Name        = "Unknown Bone Mask";
            Error.Description = MaskName.IsNone()
                ? FString("Layered Blend Per Bone has no Mask Name set; falling back to a whole-skeleton blend.")
                : FString("Layered Blend Per Bone references mask '") + MaskName.ToString() + "' which isn't defined on the graph asset's Bone Masks list.";
            Error.Node        = this;
            Compiler.AddError(Error);
        }

        const uint16 Base    = ResolvePoseInput(BasePin, Compiler);
        const uint16 Overlay = ResolvePoseInput(OverlayPin, Compiler);
        const uint16 Alpha   = ResolveValueInput(AlphaPin, Compiler);

        // An unknown mask index falls back to a plain Blend inside the VM, so
        // an unconfigured node still produces a sensible pose.
        const uint16 ResolvedMaskIdx = (MaskIndex == INDEX_NONE) ? 0xFFFFu : (uint16)MaskIndex;
        const uint16 Result = Compiler.EmitBlendMasked(Base, Overlay, Alpha, ResolvedMaskIdx);

        Compiler.SetPinRegister(ResultPin, Result);
    }
}
