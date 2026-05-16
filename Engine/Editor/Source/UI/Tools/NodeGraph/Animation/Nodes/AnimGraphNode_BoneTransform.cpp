#include "AnimGraphNode_BoneTransform.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphCompiler.h"

namespace Lumina
{
    void CAnimGraphNode_BoneTransform::BuildNode()
    {
        PoseInPin  = CreateAnimPin("Pose", ENodePinDirection::Input, EAnimPinType::Pose);
        AlphaPin   = CreateAnimPin("Alpha", ENodePinDirection::Input, EAnimPinType::Value, 1.0f);
        PoseOutPin = CreateAnimPin("Result", ENodePinDirection::Output, EAnimPinType::Pose);
    }

    void CAnimGraphNode_BoneTransform::GenerateBytecode(FAnimationGraphCompiler& Compiler)
    {
        const int32 BoneIndex = Compiler.ResolveBoneIndex(BoneName);
        if (BoneIndex == INDEX_NONE)
        {
            EdNodeGraph::FError Error;
            Error.Name        = "Unknown Bone";
            Error.Description = BoneName.IsNone()
                ? FString("Bone Transform has no Bone Name set; pose will be passed through unchanged.")
                : FString("Bone Transform references bone '") + BoneName.ToString() + "' which doesn't exist on the graph's skeleton.";
            Error.Node        = this;
            Compiler.AddError(Error);

            const uint16 SrcReg = ResolvePoseInput(PoseInPin, Compiler);
            Compiler.SetPinRegister(PoseOutPin, SrcReg);
            return;
        }

        const uint16 SrcReg   = ResolvePoseInput(PoseInPin, Compiler);
        const uint16 AlphaReg = ResolveValueInput(AlphaPin, Compiler);

        const glm::vec3 RadEuler = glm::radians(Rotation);
        const glm::quat Quat     = glm::normalize(glm::quat(RadEuler));

        const uint16 ResultReg = Compiler.EmitBoneTransform(SrcReg, AlphaReg, (uint16)BoneIndex,
                                                            Space, Mode,
                                                            Translation, Quat, Scale);

        Compiler.SetPinRegister(PoseOutPin, ResultReg);
    }
}
