#include "MaterialNode_Terrain.h"

#include "Core/Object/Cast.h"
#include "UI/Tools/NodeGraph/Material/MaterialOutput.h"
#include "UI/Tools/NodeGraph/Material/MaterialCompiler.h"

namespace Lumina
{
    void CMaterialExpression_TerrainLayerWeight::BuildNode()
    {
        Super::BuildNode();
        Output->SetInputType(EMaterialInputType::Float);
        Output->SetComponentMask(EComponentMask::R);
    }
    void CMaterialExpression_TerrainLayerWeight::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.TerrainLayerWeight(FullName, LayerIndex, this);
    }

    void CMaterialExpression_TerrainLayerWeights::BuildNode()
    {
        Super::BuildNode();
        Output->SetInputType(EMaterialInputType::Float4);
        Output->SetComponentMask(EComponentMask::RGBA);
    }
    void CMaterialExpression_TerrainLayerWeights::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.TerrainLayerWeights(FullName, this);
    }

    void CMaterialExpression_TerrainLayerBlend::BuildNode()
    {
        Super::BuildNode();
        Output->SetInputType(EMaterialInputType::Float3);
        Output->SetComponentMask(EComponentMask::RGB);

        Layer0 = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Layer0", ENodePinDirection::Input));
        Layer0->SetPinName("Layer0"); Layer0->SetInputType(EMaterialInputType::Float3); Layer0->SetComponentMask(EComponentMask::RGB); Layer0->SetIndex(0);

        Layer1 = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Layer1", ENodePinDirection::Input));
        Layer1->SetPinName("Layer1"); Layer1->SetInputType(EMaterialInputType::Float3); Layer1->SetComponentMask(EComponentMask::RGB); Layer1->SetIndex(1);

        Layer2 = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Layer2", ENodePinDirection::Input));
        Layer2->SetPinName("Layer2"); Layer2->SetInputType(EMaterialInputType::Float3); Layer2->SetComponentMask(EComponentMask::RGB); Layer2->SetIndex(2);

        Layer3 = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Layer3", ENodePinDirection::Input));
        Layer3->SetPinName("Layer3"); Layer3->SetInputType(EMaterialInputType::Float3); Layer3->SetComponentMask(EComponentMask::RGB); Layer3->SetIndex(3);
    }
    void CMaterialExpression_TerrainLayerBlend::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.TerrainLayerBlend(Layer0, Layer1, Layer2, Layer3);
    }
}
