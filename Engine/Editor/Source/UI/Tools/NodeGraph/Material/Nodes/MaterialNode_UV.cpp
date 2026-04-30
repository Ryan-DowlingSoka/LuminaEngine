#include "MaterialNode_UV.h"

#include "Core/Object/Cast.h"
#include "UI/Tools/NodeGraph/Material/MaterialOutput.h"
#include "UI/Tools/NodeGraph/Material/MaterialCompiler.h"

namespace Lumina
{
    static CMaterialInput* MakeIn(CMaterialExpression* Self, const char* Name, EMaterialInputType Type = EMaterialInputType::Float)
    {
        CMaterialInput* P = Cast<CMaterialInput>(Self->CreatePin(CMaterialInput::StaticClass(), Name, ENodePinDirection::Input));
        P->SetPinName(Name);
        P->SetInputType(Type);
        return P;
    }

    void CMaterialExpression_RotateUV::BuildNode()
    {
        Super::BuildNode();
        UV       = MakeIn(this, "UV", EMaterialInputType::Float2);
        Center   = MakeIn(this, "Center", EMaterialInputType::Float2);
        Rotation = MakeIn(this, "Rotation", EMaterialInputType::Float);
        Output->SetInputType(EMaterialInputType::Float2);
    }
    void CMaterialExpression_RotateUV::GenerateDefinition(FMaterialCompiler& C) { C.RotateUV(UV, Center, Rotation); }

    void CMaterialExpression_TilingAndOffset::BuildNode()
    {
        Super::BuildNode();
        UV     = MakeIn(this, "UV", EMaterialInputType::Float2);
        Tiling = MakeIn(this, "Tiling", EMaterialInputType::Float2);
        Offset = MakeIn(this, "Offset", EMaterialInputType::Float2);
        Output->SetInputType(EMaterialInputType::Float2);
    }
    void CMaterialExpression_TilingAndOffset::GenerateDefinition(FMaterialCompiler& C) { C.TilingAndOffset(UV, Tiling, Offset); }

    void CMaterialExpression_FlipBook::BuildNode()
    {
        Super::BuildNode();
        UV         = MakeIn(this, "UV", EMaterialInputType::Float2);
        NumColumns = MakeIn(this, "NumColumns");
        NumRows    = MakeIn(this, "NumRows");
        Time       = MakeIn(this, "Time");
        FPS        = MakeIn(this, "FPS");
        Output->SetInputType(EMaterialInputType::Float2);
    }
    void CMaterialExpression_FlipBook::GenerateDefinition(FMaterialCompiler& C) { C.FlipBookUV(UV, NumColumns, NumRows, Time, FPS); }

    void CMaterialExpression_PolarCoordinates::BuildNode()
    {
        Super::BuildNode();
        UV     = MakeIn(this, "UV", EMaterialInputType::Float2);
        Center = MakeIn(this, "Center", EMaterialInputType::Float2);
        Output->SetInputType(EMaterialInputType::Float2);
    }
    void CMaterialExpression_PolarCoordinates::GenerateDefinition(FMaterialCompiler& C) { C.PolarCoordinates(UV, Center); }

    void CMaterialExpression_TwirlUV::BuildNode()
    {
        Super::BuildNode();
        UV       = MakeIn(this, "UV", EMaterialInputType::Float2);
        Center   = MakeIn(this, "Center", EMaterialInputType::Float2);
        Strength = MakeIn(this, "Strength");
        Output->SetInputType(EMaterialInputType::Float2);
    }
    void CMaterialExpression_TwirlUV::GenerateDefinition(FMaterialCompiler& C) { C.TwirlUV(UV, Center, Strength); }
}
