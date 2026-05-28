#pragma once
#include "MaterialNodeExpression.h"
#include "MaterialGraphNode.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Assets/AssetTypes/MaterialFunction/MaterialFunction.h"
#include "UI/Tools/NodeGraph/Material/MaterialGraphTypes.h"
#include "MaterialNode_Function.generated.h"

namespace Lumina
{
    class CMaterialInput;

    // Runtime EMaterialValueType and editor EMaterialInputType share Float..Float4 ordering, so the
    // two conversions are just casts. Texture-typed function I/O is not supported, so a Texture input
    // type clamps to Float4 on the way back.
    inline EMaterialInputType ToMaterialInputType(EMaterialValueType Type)
    {
        return static_cast<EMaterialInputType>(Type);
    }

    inline EMaterialValueType ToMaterialValueType(EMaterialInputType Type)
    {
        return (Type == EMaterialInputType::Texture) ? EMaterialValueType::Float4 : static_cast<EMaterialValueType>(Type);
    }

    // Declares one input of the owning material function. Lives only inside a function graph. Its
    // single output pin feeds the function body; when the function is inlined into a host material the
    // call node binds this output to the caller's argument, so GenerateDefinition (a constant emit) only
    // runs in the function editor's standalone validation compile.
    REFLECT()
    class CMaterialExpression_FunctionInput : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;
        FFixedString GetNodeCategory() const override { return "Function"; }
        FString GetNodeDisplayName() const override { return "FunctionInput"; }
        FString GetNodeTooltip() const override { return "An input parameter of this material function."; }
        uint32 GetNodeTitleColor() const override { return IM_COL32(35, 140, 90, 255); }
        void DrawNodeBody() override;
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        /** Identifier for this input. Must be unique within the function; drives the call node's pin. */
        PROPERTY(Editable, Category = "Function Input")
        FName InputName = "In";

        PROPERTY(Editable, Category = "Function Input")
        EMaterialValueType InputType = EMaterialValueType::Float;

        /** Value used when a call node leaves this input unconnected (and for in-editor preview). */
        PROPERTY(Editable, Category = "Function Input")
        FVector4 DefaultValue = FVector4(0.0f);

        /** Lower values sort earlier in the call node's pin list. */
        PROPERTY(Editable, Category = "Function Input")
        int32 SortPriority = 0;

        PROPERTY(Editable, Category = "Function Input")
        FString Description;

        // Refreshes the output pin's type from InputType so downstream nodes see the right width.
        void DrawNodeTitleBar() override;
    };

    // Declares one output of the owning material function. Lives only inside a function graph; one node
    // per output. Its single input pin is what the function body drives. The call node reads the
    // resolved value during inlining, so GenerateDefinition only validates the type in the editor.
    REFLECT()
    class CMaterialFunctionOutput : public CMaterialGraphNode
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;
        FFixedString GetNodeCategory() const override { return "Function"; }
        FString GetNodeDisplayName() const override { return "FunctionOutput"; }
        FString GetNodeTooltip() const override { return "An output of this material function."; }
        uint32 GetNodeTitleColor() const override { return IM_COL32(140, 70, 35, 255); }
        bool IsDeletable() const override { return true; }
        void DrawNodeBody() override;
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        /** Identifier for this output. Must be unique within the function; drives the call node's pin. */
        PROPERTY(Editable, Category = "Function Output")
        FName OutputName = "Out";

        PROPERTY(Editable, Category = "Function Output")
        EMaterialValueType OutputType = EMaterialValueType::Float;

        /** Lower values sort earlier in the call node's pin list. */
        PROPERTY(Editable, Category = "Function Output")
        int32 SortPriority = 0;

        PROPERTY(Editable, Category = "Function Output")
        FString Description;

        CMaterialInput* Input = nullptr;
    };

    // Calls a material function: a placeholder node in a host material (or another function) graph that,
    // at compile time, inlines the referenced function's body with this call's argument pins bound to
    // the function's inputs and the function's outputs exposed as this node's output pins.
    REFLECT()
    class CMaterialExpression_MaterialFunctionCall : public CMaterialGraphNode
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;
        FFixedString GetNodeCategory() const override { return "Function"; }
        FString GetNodeDisplayName() const override { return "MaterialFunction"; }
        FString GetNodeTooltip() const override { return "Inlines a Material Function asset, exposing its inputs and outputs as pins."; }
        uint32 GetNodeTitleColor() const override { return IM_COL32(80, 110, 170, 255); }
        ImVec2 GetMinNodeBodySize() const override { return ImVec2(120, 60); }

        void DrawNodeBody() override;
        void DrawNodeTitleBar() override;
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        /** The material function asset to inline. Changing it rebuilds this node's pins. */
        PROPERTY(Editable, Category = "Material Function")
        TObjectPtr<CMaterialFunction> Function;

        // (Re)creates input/output pins from the current Function's signature, preserving any existing
        // connections that still match by pin name. Pin IDs are name-hashed so they survive save/load.
        void RebuildPins();

        // The input/output pins, in signature order. Parallel to Function's Inputs/Outputs.
        TVector<CMaterialInput*>  FunctionInputPins;
        TVector<CMaterialOutput*> FunctionOutputPins;

    private:

        // Detects a Function change (or first build) so DrawNodeTitleBar can rebuild pins lazily.
        CMaterialFunction* CachedFunction = nullptr;
        bool               bPinsBuilt = false;
    };
}
