#pragma once

#include "MaterialNodeGraph.h"
#include "MaterialFunctionGraph.generated.h"

namespace Lumina
{
    class CMaterialFunction;

    // Name of the function graph child object inside a CMaterialFunction's package. Shared by the
    // function editor (which creates/loads it) and the call node's inliner (which loads it).
    inline constexpr const char* GMaterialFunctionGraphObjectName = "MaterialFunctionGraph";

    // The editable graph of a material function. Same node library as a material graph, minus the
    // material output node (it has none) and plus FunctionInput / FunctionOutput nodes that declare
    // the function's signature. It produces no shader of its own; the call node inlines it into a host
    // material at compile time. CompileForValidation only exists so the function editor can surface
    // node errors without building a real shader.
    REFLECT()
    class CMaterialFunctionGraph : public CMaterialNodeGraph
    {
        GENERATED_BODY()

    public:

        // Runs each node's GenerateDefinition into a throwaway compiler to surface type errors. Walks
        // from every FunctionOutput node; FunctionInput nodes emit their preview-default constants.
        void CompileForValidation(FMaterialCompiler& Compiler);

    protected:

        // Function graphs have no material output node; the author adds FunctionInput / FunctionOutput.
        void EnsureRootNodes() override {}

        void RegisterGraphTypeNodes() override;
    };
}
