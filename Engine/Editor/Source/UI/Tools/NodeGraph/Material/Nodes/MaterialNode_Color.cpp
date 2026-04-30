#include "MaterialNode_Color.h"

#include "Core/Object/Cast.h"
#include "UI/Tools/NodeGraph/Material/MaterialOutput.h"
#include "UI/Tools/NodeGraph/Material/MaterialCompiler.h"

namespace Lumina
{
    static CMaterialInput* MakeInput(CMaterialExpression* Self, const char* Name)
    {
        CMaterialInput* Pin = Cast<CMaterialInput>(Self->CreatePin(CMaterialInput::StaticClass(), Name, ENodePinDirection::Input));
        Pin->SetPinName(Name);
        return Pin;
    }

    void CMaterialExpression_Luminance::BuildNode()
    {
        Super::BuildNode();
        Color = MakeInput(this, "Color");
        Output->SetInputType(EMaterialInputType::Float);
    }
    void CMaterialExpression_Luminance::GenerateDefinition(FMaterialCompiler& C) { C.Luminance(Color); }

    void CMaterialExpression_Desaturate::BuildNode()
    {
        Super::BuildNode();
        Color  = MakeInput(this, "Color");
        Amount = MakeInput(this, "Amount");
        Output->SetInputType(EMaterialInputType::Float3);
    }
    void CMaterialExpression_Desaturate::GenerateDefinition(FMaterialCompiler& C) { C.Desaturate(Color, Amount); }

    void CMaterialExpression_RGBToHSV::BuildNode()
    {
        Super::BuildNode();
        RGB = MakeInput(this, "RGB");
        Output->SetInputType(EMaterialInputType::Float3);
    }
    void CMaterialExpression_RGBToHSV::GenerateDefinition(FMaterialCompiler& C) { C.RGBToHSV(RGB); }

    void CMaterialExpression_HSVToRGB::BuildNode()
    {
        Super::BuildNode();
        HSV = MakeInput(this, "HSV");
        Output->SetInputType(EMaterialInputType::Float3);
    }
    void CMaterialExpression_HSVToRGB::GenerateDefinition(FMaterialCompiler& C) { C.HSVToRGB(HSV); }

    void CMaterialExpression_Posterize::BuildNode()
    {
        Super::BuildNode();
        Color = MakeInput(this, "Color");
        Steps = MakeInput(this, "Steps");
    }
    void CMaterialExpression_Posterize::GenerateDefinition(FMaterialCompiler& C) { C.Posterize(Color, Steps); }

    void CMaterialExpression_GammaCorrection::BuildNode()
    {
        Super::BuildNode();
        Color = MakeInput(this, "Color");
        Gamma = MakeInput(this, "Gamma");
    }
    void CMaterialExpression_GammaCorrection::GenerateDefinition(FMaterialCompiler& C) { C.GammaCorrection(Color, Gamma); }

    void CMaterialExpression_Contrast::BuildNode()
    {
        Super::BuildNode();
        Color  = MakeInput(this, "Color");
        Amount = MakeInput(this, "Amount");
    }
    void CMaterialExpression_Contrast::GenerateDefinition(FMaterialCompiler& C) { C.Contrast(Color, Amount); }

    void CMaterialExpression_Brightness::BuildNode()
    {
        Super::BuildNode();
        Color  = MakeInput(this, "Color");
        Amount = MakeInput(this, "Amount");
    }
    void CMaterialExpression_Brightness::GenerateDefinition(FMaterialCompiler& C) { C.Brightness(Color, Amount); }

    void CMaterialExpression_Tint::BuildNode()
    {
        Super::BuildNode();
        Color     = MakeInput(this, "Color");
        TintColor = MakeInput(this, "Tint");
        Amount    = MakeInput(this, "Amount");
        Output->SetInputType(EMaterialInputType::Float3);
    }
    void CMaterialExpression_Tint::GenerateDefinition(FMaterialCompiler& C) { C.Tint(Color, TintColor, Amount); }

    void CMaterialExpression_LinearToSRGB::BuildNode()
    {
        Super::BuildNode();
        Color = MakeInput(this, "Color");
        Output->SetInputType(EMaterialInputType::Float3);
    }
    void CMaterialExpression_LinearToSRGB::GenerateDefinition(FMaterialCompiler& C) { C.LinearToSRGB(Color); }

    void CMaterialExpression_SRGBToLinear::BuildNode()
    {
        Super::BuildNode();
        Color = MakeInput(this, "Color");
        Output->SetInputType(EMaterialInputType::Float3);
    }
    void CMaterialExpression_SRGBToLinear::GenerateDefinition(FMaterialCompiler& C) { C.SRGBToLinear(Color); }
}
