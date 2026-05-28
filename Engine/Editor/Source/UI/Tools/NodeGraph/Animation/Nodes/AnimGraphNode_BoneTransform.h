#pragma once

#include "UI/Tools/NodeGraph/Animation/AnimGraphNode.h"
#include "Animation/AnimationGraphVM.h"
#include "AnimGraphNode_BoneTransform.generated.h"

namespace Lumina
{
    // Procedural per-bone transform. Drops a (Translation, Rotation, Scale) onto
    // a named bone of the input pose, either in the bone's local frame or in
    // component space. Useful for hand-IK style overrides (place the hand on a
    // weapon grip), aim offsets, lean, look-at, and similar.
    REFLECT()
    class CAnimGraphNode_BoneTransform : public CAnimGraphNode
    {
        GENERATED_BODY()
    public:

        FString GetNodeDisplayName() const override { return "Bone Transform"; }
        FString GetNodeTooltip() const override { return "Applies a translation/rotation/scale to a named bone, in the bone's local space or in component space."; }
        FFixedString GetNodeCategory() const override { return "Animation"; }
        uint32 GetNodeTitleColor() const override { return IM_COL32(150, 90, 70, 255); }

        void BuildNode() override;
        void GenerateBytecode(FAnimationGraphCompiler& Compiler) override;

        /** Name of the bone to drive. Must exist in the graph's skeleton. */
        PROPERTY(Editable, Category = "Bone", BonePicker)
        FName BoneName;

        /** Frame the offset is interpreted in.
         *  - Local Bone: the bone's local frame (relative to its parent). Cheap.
         *  - Component: the entity-root frame. The offset is applied to the bone's
         *    global transform via FK and converted back to local. */
        PROPERTY(Editable, Category = "Bone")
        EBoneTransformSpace Space = EBoneTransformSpace::LocalBone;

        /** How the offset is applied.
         *  - Add: layered on top of the existing bone transform (additive).
         *  - Replace: lerps the bone toward the configured target by Alpha. */
        PROPERTY(Editable, Category = "Bone")
        EBoneTransformMode Mode = EBoneTransformMode::Add;

        /** Translation offset (or target, in Replace mode), in the selected space. */
        PROPERTY(Editable, Category = "Transform")
        FVector3 Translation = FVector3(0.0f);

        /** Euler rotation in degrees (X = pitch, Y = yaw, Z = roll). Converted to
         *  a quaternion at compile time. */
        PROPERTY(Editable, Category = "Transform")
        FVector3 Rotation = FVector3(0.0f);

        /** Scale. In Add mode, 1.0 leaves the bone unchanged. */
        PROPERTY(Editable, Category = "Transform")
        FVector3 Scale = FVector3(1.0f);

        CAnimGraphPin* PoseInPin = nullptr;
        CAnimGraphPin* AlphaPin = nullptr;
        CAnimGraphPin* SpacePin = nullptr;
        CAnimGraphPin* ModePin = nullptr;
        CAnimGraphPin* PoseOutPin = nullptr;
    };
}
