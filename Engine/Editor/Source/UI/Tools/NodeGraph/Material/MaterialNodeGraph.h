#pragma once

#include "UI/Tools/NodeGraph/EdNodeGraph.h"
#include "MaterialNodeGraph.generated.h"

namespace Lumina
{
    class CMaterial;
    class FMaterialCompiler;
}

namespace Lumina
{
    REFLECT()
    class CMaterialNodeGraph : public CEdNodeGraph
    {
        GENERATED_BODY()
        
    public:
        
        CMaterialNodeGraph();

        void Initialize() override;
        void Shutdown() override;
        
        void CompileGraph(FMaterialCompiler& Compiler);

        void ValidateGraph() override;

        void SetMaterial(CMaterial* InMaterial);
        CMaterial* GetMaterial() const { return Material; }

    private:

        TObjectPtr<CMaterial> Material;
    };
}
