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
        
        void Initialize() override;
        void Shutdown() override;
        
        void CompileGraph(FMaterialCompiler& Compiler);

        void ValidateGraph() override;

        // Hold-and-click quick-place: 1..4 -> ConstantFloat..ConstantFloat4,
        // 5 -> Time, 6 -> WorldPos, 7 -> TexCoords, 8 -> VertexNormal,
        // 9 -> Multiply, 0 -> Add. Future digits / chord support can layer on
        // top by extending the dispatch table in HandleQuickPlace.
        void HandleQuickPlace(int Digit, ImVec2 CanvasPos) override;

        // Reroute support: enables the double-click-on-wire UX in the editor and tells the graph
        // which class to spawn. Material reroutes carry typed CMaterialInput / CMaterialOutput
        // pins so the existing compiler casts keep working.
        CClass* GetRerouteNodeClass() const override;

        void SetMaterial(CMaterial* InMaterial);
        CMaterial* GetMaterial() const { return Material; }

    private:

        TObjectPtr<CMaterial> Material;
    };
}
