#include "MaterialNode_SceneData.h"

#include "UI/Tools/NodeGraph/Material/MaterialOutput.h"
#include "UI/Tools/NodeGraph/Material/MaterialCompiler.h"

namespace Lumina
{
    void CMaterialExpression_ScreenPosition::BuildNode()    { Super::BuildNode(); Output->SetInputType(EMaterialInputType::Float2); Output->SetComponentMask(EComponentMask::RG); }
    void CMaterialExpression_ScreenPosition::GenerateDefinition(FMaterialCompiler& C)
    {
        if (!C.RequirePixelStage(this, "ScreenPosition")) return;
        C.ScreenPosition(FullName, bRaw);
    }

    void CMaterialExpression_ViewDirection::BuildNode()     { Super::BuildNode(); Output->SetInputType(EMaterialInputType::Float3); Output->SetComponentMask(EComponentMask::RGB); }
    void CMaterialExpression_ViewDirection::GenerateDefinition(FMaterialCompiler& C) { C.ViewDirection(FullName); }

    void CMaterialExpression_ReflectionVector::BuildNode()  { Super::BuildNode(); Output->SetInputType(EMaterialInputType::Float3); Output->SetComponentMask(EComponentMask::RGB); }
    void CMaterialExpression_ReflectionVector::GenerateDefinition(FMaterialCompiler& C) { C.ReflectionVector(FullName); }

    void CMaterialExpression_FragmentDepth::BuildNode()     { Super::BuildNode(); Output->SetInputType(EMaterialInputType::Float); }
    void CMaterialExpression_FragmentDepth::GenerateDefinition(FMaterialCompiler& C)
    {
        if (!C.RequirePixelStage(this, "FragmentDepth")) return;
        C.FragmentDepth(FullName, bLinear);
    }

    void CMaterialExpression_ViewportSize::BuildNode()      { Super::BuildNode(); Output->SetInputType(EMaterialInputType::Float2); Output->SetComponentMask(EComponentMask::RG); }
    void CMaterialExpression_ViewportSize::GenerateDefinition(FMaterialCompiler& C) { C.ViewportSize(FullName); }

    void CMaterialExpression_AspectRatio::BuildNode()       { Super::BuildNode(); Output->SetInputType(EMaterialInputType::Float); }
    void CMaterialExpression_AspectRatio::GenerateDefinition(FMaterialCompiler& C) { C.AspectRatio(FullName); }
}
