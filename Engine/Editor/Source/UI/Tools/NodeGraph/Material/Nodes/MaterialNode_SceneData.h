#pragma once
#include "MaterialNodeExpression.h"
#include "MaterialNode_SceneData.generated.h"

namespace Lumina
{
    REFLECT()
    class CMaterialExpression_ScreenPosition : public CMaterialExpression
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FFixedString GetNodeCategory() const override { return "Scene"; }
        FString GetNodeDisplayName() const override { return "ScreenPosition"; }
        FString GetNodeTooltip() const override { return "Returns the fragment's screen position. Normalized [0,1] by default; toggle Raw for pixels."; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        PROPERTY(Editable) bool bRaw = false;
    };

    REFLECT()
    class CMaterialExpression_ViewDirection : public CMaterialExpression
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FFixedString GetNodeCategory() const override { return "Scene"; }
        FString GetNodeDisplayName() const override { return "ViewDirection"; }
        FString GetNodeTooltip() const override { return "Returns the unit vector from the fragment toward the camera (float3)."; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_ReflectionVector : public CMaterialExpression
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FFixedString GetNodeCategory() const override { return "Scene"; }
        FString GetNodeDisplayName() const override { return "ReflectionVector"; }
        FString GetNodeTooltip() const override { return "Returns the world-space reflection vector of the view direction about the surface normal."; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_FragmentDepth : public CMaterialExpression
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FFixedString GetNodeCategory() const override { return "Scene"; }
        FString GetNodeDisplayName() const override { return "FragmentDepth"; }
        FString GetNodeTooltip() const override { return "Returns fragment depth. Linear by default (view-space Z), toggle for raw NDC z."; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        PROPERTY(Editable) bool bLinear = true;
    };

    REFLECT()
    class CMaterialExpression_ViewportSize : public CMaterialExpression
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FFixedString GetNodeCategory() const override { return "Scene"; }
        FString GetNodeDisplayName() const override { return "ViewportSize"; }
        FString GetNodeTooltip() const override { return "Returns the current viewport size in pixels (float2)."; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_AspectRatio : public CMaterialExpression
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FFixedString GetNodeCategory() const override { return "Scene"; }
        FString GetNodeDisplayName() const override { return "AspectRatio"; }
        FString GetNodeTooltip() const override { return "Returns viewport width / height."; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };
}
