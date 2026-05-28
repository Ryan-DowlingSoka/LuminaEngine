#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectMacros.h"
#include "Core/Math/Math.h"
#include "MaterialFunction.generated.h"

namespace Lumina
{
    // Value type for a material function's input/output pins. Mirrors the editor-side
    // EMaterialInputType ordering (Float..Float4) so the editor can map 1:1 without a table.
    // Texture-typed function I/O is intentionally not supported yet.
    REFLECT()
    enum class EMaterialValueType : uint8
    {
        Float,
        Float2,
        Float3,
        Float4,
    };

    // One declared input of a material function. Authored in the function editor as a
    // FunctionInput node; the editor mirrors the node set into these on save so a call
    // node can build its pins without loading the function's editor graph.
    REFLECT()
    struct RUNTIME_API FMaterialFunctionInput
    {
        GENERATED_BODY()

        PROPERTY()
        FName Name;

        PROPERTY()
        EMaterialValueType Type = EMaterialValueType::Float;

        /** Used when a call node leaves this input pin unconnected. */
        PROPERTY()
        FVector4 DefaultValue = FVector4(0.0f);

        PROPERTY()
        FString Description;
    };

    // One declared output of a material function.
    REFLECT()
    struct RUNTIME_API FMaterialFunctionOutput
    {
        GENERATED_BODY()

        PROPERTY()
        FName Name;

        PROPERTY()
        EMaterialValueType Type = EMaterialValueType::Float;

        PROPERTY()
        FString Description;
    };

    // A reusable material subgraph (like Unreal's Material Functions). It owns no compiled
    // shader of its own: at material-compile time the editor inlines the function's graph into
    // the host material's generated shader. Only the input/output signature is needed at
    // runtime, so this asset is little more than that signature plus a description; the editable
    // node graph lives as a child object in this asset's package (see the editor tool).
    REFLECT()
    class RUNTIME_API CMaterialFunction : public CObject
    {
        GENERATED_BODY()

    public:

        bool IsAsset() const override { return true; }

        const TVector<FMaterialFunctionInput>&  GetInputs() const  { return Inputs; }
        const TVector<FMaterialFunctionOutput>& GetOutputs() const { return Outputs; }

        /** Human-readable summary shown in the call node tooltip / library. */
        PROPERTY(Editable, Category = "Material Function")
        FString Description;

        /** Derived from the function graph's FunctionInput nodes on save; do not hand-edit. */
        PROPERTY()
        TVector<FMaterialFunctionInput> Inputs;

        /** Derived from the function graph's FunctionOutput nodes on save; do not hand-edit. */
        PROPERTY()
        TVector<FMaterialFunctionOutput> Outputs;
    };
}
