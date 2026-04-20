#include "MaterialNodeExpression.h"

#include "Core/Object/Class.h"
#include "Core/Object/Cast.h"
#include "glm/gtc/type_ptr.hpp"

#include "UI/Tools/NodeGraph/Material/MaterialOutput.h"
#include "UI/Tools/NodeGraph/Material/MaterialCompiler.h"


namespace Lumina
{
    
    void CMaterialExpression::BuildNode()
    {
        Output = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "Material Output", ENodePinDirection::Output));
        Output->SetShouldDrawEditor(false);
    }

    void CMaterialExpression_Math::BuildNode()
    {
        Super::BuildNode();
    }

    void CMaterialExpression_ComponentMask::BuildNode()
    {
        Super::BuildNode();

        InputPin = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Input", ENodePinDirection::Input));
        
        InputPin->SetInputType(EMaterialInputType::Float4);
        InputPin->SetComponentMask(EComponentMask::RGBA);
    }

    FString CMaterialExpression_ComponentMask::GetNodeDisplayName() const
    {
        FString Builder = "ComponentMask_";
        if (R)
        {
            Builder.append("R");
        }

        if (G)
        {
            Builder.append("G");
        }

        if (B)
        {
            Builder.append("B");
        }

        if (A)
        {
            Builder.append("A");
        }
        
        return Builder;
    }

    
    void CMaterialExpression_ComponentMask::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.ComponentMask(InputPin);
    }

    ImVec2 CMaterialExpression_ComponentMask::GetMinNodeTitleBarSize() const
    {
        return ImVec2(22.0f, Super::GetMinNodeTitleBarSize().y);
    }
    
    void CMaterialExpression_Append::BuildNode()
    {
        CMaterialExpression::BuildNode();

        InputA = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "A", ENodePinDirection::Input));
        InputA->SetInputType(EMaterialInputType::Float4);
        InputA->SetComponentMask(EComponentMask::RGBA);
    
        InputB = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "B", ENodePinDirection::Input));
        InputB->SetInputType(EMaterialInputType::Float4);
        InputB->SetComponentMask(EComponentMask::RGBA);
    
        Output->SetComponentMask(EComponentMask::RGBA);
    }

    void CMaterialExpression_Append::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        
    }

    void CMaterialExpression_MakeFloat2::BuildNode()
    {
        Super::BuildNode();
        
        Output->SetInputType(EMaterialInputType::Float2);
        Output->SetComponentMask(EComponentMask::RG);
        
        R = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "R", ENodePinDirection::Input));
        R->SetInputType(EMaterialInputType::Float);
        R->SetComponentMask(EComponentMask::R);
    
        G = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "G", ENodePinDirection::Input));
        G->SetInputType(EMaterialInputType::Float);
        G->SetComponentMask(EComponentMask::G);
    
    }

    void CMaterialExpression_MakeFloat2::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.MakeFloat2(R, G);
    }

    void CMaterialExpression_MakeFloat3::BuildNode()
    {
        Super::BuildNode();
        
        Output->SetInputType(EMaterialInputType::Float3);
        Output->SetComponentMask(EComponentMask::RGB);
        
        R = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "R", ENodePinDirection::Input));
        R->SetInputType(EMaterialInputType::Float);
        R->SetComponentMask(EComponentMask::R);
    
        G = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "G", ENodePinDirection::Input));
        G->SetInputType(EMaterialInputType::Float);
        G->SetComponentMask(EComponentMask::G);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "B", ENodePinDirection::Input));
        B->SetInputType(EMaterialInputType::Float);
        B->SetComponentMask(EComponentMask::B);
    
    }

    void CMaterialExpression_MakeFloat3::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.MakeFloat3(R, G, B);
    }

    void CMaterialExpression_MakeFloat4::BuildNode()
    {
        Super::BuildNode();
        
        Output->SetInputType(EMaterialInputType::Float4);
        Output->SetComponentMask(EComponentMask::RGBA);
        
        R = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "R", ENodePinDirection::Input));
        R->SetInputType(EMaterialInputType::Float);
        R->SetComponentMask(EComponentMask::R);
    
        G = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "G", ENodePinDirection::Input));
        G->SetInputType(EMaterialInputType::Float);
        G->SetComponentMask(EComponentMask::G);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "B", ENodePinDirection::Input));
        B->SetInputType(EMaterialInputType::Float);
        B->SetComponentMask(EComponentMask::B);
        
        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "A", ENodePinDirection::Input));
        A->SetInputType(EMaterialInputType::Float);
        A->SetComponentMask(EComponentMask::A);
    
    }

    void CMaterialExpression_MakeFloat4::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.MakeFloat4(R, G, B, A);
    }

    void CMaterialExpression_WorldPos::BuildNode()
    {
        Super::BuildNode();
        
        Output->SetInputType(EMaterialInputType::Float3);
        Output->SetComponentMask(EComponentMask::RGB);
    }

    void CMaterialExpression_WorldPos::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.WorldPos(FullName);
    }

    void CMaterialExpression_CameraPos::BuildNode()
    {
        Super::BuildNode();
    }

    void CMaterialExpression_CameraPos::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.CameraPos(FullName);
    }

    void CMaterialExpression_EntityID::BuildNode()
    {
        Super::BuildNode();
    }

    void CMaterialExpression_EntityID::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.EntityID(FullName);
    }

    void CMaterialExpression_VertexNormal::BuildNode()
    {
        CMaterialExpression::BuildNode();
    }

    void CMaterialExpression_VertexNormal::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.VertexNormal(FullName);
    }

    void CMaterialExpression_Panner::BuildNode()
    {
        CMaterialExpression::BuildNode();
        
        UV = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "UV", ENodePinDirection::Input));
        UV->SetPinColor(IM_COL32(255, 10, 10, 255));
        UV->SetHideDuringConnection(false);
        UV->SetPinName("UV");
        UV->SetInputType(EMaterialInputType::Float2);
                
        Time = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Time", ENodePinDirection::Input));
        Time->SetHideDuringConnection(false);
        Time->SetPinName("Time");
        Time->SetInputType(EMaterialInputType::Float);
        
        Speed = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Speed", ENodePinDirection::Input));
        Speed->SetHideDuringConnection(false);
        Speed->SetPinName("Speed");
        Speed->SetInputType(EMaterialInputType::Float2);
        
        Output->SetInputType(EMaterialInputType::Float2);
    }

    void CMaterialExpression_Panner::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Panner(UV, Time, Speed);
    }
    
    void CMaterialExpression_TexCoords::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.TexCoords(FullName, TextureIndex, UTiling, VTiling);
    }

    //========================================================================================
    //  Terrain
    //========================================================================================

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
        Layer0->SetPinName("Layer0");
        Layer0->SetInputType(EMaterialInputType::Float3);
        Layer0->SetComponentMask(EComponentMask::RGB);
        Layer0->SetIndex(0);

        Layer1 = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Layer1", ENodePinDirection::Input));
        Layer1->SetPinName("Layer1");
        Layer1->SetInputType(EMaterialInputType::Float3);
        Layer1->SetComponentMask(EComponentMask::RGB);
        Layer1->SetIndex(1);

        Layer2 = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Layer2", ENodePinDirection::Input));
        Layer2->SetPinName("Layer2");
        Layer2->SetInputType(EMaterialInputType::Float3);
        Layer2->SetComponentMask(EComponentMask::RGB);
        Layer2->SetIndex(2);

        Layer3 = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Layer3", ENodePinDirection::Input));
        Layer3->SetPinName("Layer3");
        Layer3->SetInputType(EMaterialInputType::Float3);
        Layer3->SetComponentMask(EComponentMask::RGB);
        Layer3->SetIndex(3);
    }

    void CMaterialExpression_TerrainLayerBlend::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.TerrainLayerBlend(Layer0, Layer1, Layer2, Layer3);
    }

    void CMaterialExpression_Constant::DrawContextMenu()
    {
        const char* MenuItem = bDynamic ? "Make Constant" : "Make Parameter";
        if (ImGui::MenuItem(MenuItem))
        {
            bDynamic = !bDynamic;
            if (bDynamic)
            {
                ParameterName = GetNodeDisplayName() + "_Param";
            }
        }
    }

    void CMaterialExpression_Constant::DrawNodeTitleBar()
    {
        if (bDynamic)
        {
            ImGui::SetNextItemWidth(125);
            if (ImGui::InputText("##", const_cast<char*>(ParameterName.c_str()), 256))
            {
                
            }
        }
        else
        {
            CMaterialExpression::DrawNodeTitleBar();
        }
    }

    void CMaterialExpression_Constant::BuildNode()
    {
        switch (ValueType)
        {
        case EMaterialInputType::Float:
            {
                CMaterialOutput* ValuePin = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "X", ENodePinDirection::Output));
                ValuePin->SetShouldDrawEditor(true);
                ValuePin->SetHideDuringConnection(false);
                ValuePin->SetPinName("X");
                ValuePin->SetInputType(EMaterialInputType::Float);
            }
            break;
        case EMaterialInputType::Float2:
            {
                CMaterialOutput* ValuePin = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "XY", ENodePinDirection::Output));
                ValuePin->SetShouldDrawEditor(true);
                ValuePin->SetHideDuringConnection(false);
                ValuePin->SetPinName("XY");
                ValuePin->SetComponentMask(EComponentMask::RG);
                ValuePin->SetInputType(EMaterialInputType::Float2);
                
                CMaterialOutput* R = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "X", ENodePinDirection::Output));
                R->SetPinColor(IM_COL32(255, 10, 10, 255));
                R->SetHideDuringConnection(false);
                R->SetPinName("X");
                R->SetComponentMask(EComponentMask::R);

                CMaterialOutput* G = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "Y", ENodePinDirection::Output));
                G->SetPinColor(IM_COL32(10, 255, 10, 255));
                G->SetHideDuringConnection(false);
                G->SetPinName("Y");
                G->SetComponentMask(EComponentMask::G);

            }
            break;
        case EMaterialInputType::Float3:
            {
                CMaterialOutput* ValuePin = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "RGB", ENodePinDirection::Output));
                ValuePin->SetShouldDrawEditor(true);
                ValuePin->SetHideDuringConnection(false);
                ValuePin->SetPinName("RGB");
                ValuePin->SetComponentMask(EComponentMask::RGB);
                ValuePin->SetInputType(EMaterialInputType::Float3);

                CMaterialOutput* R = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "R", ENodePinDirection::Output));
                R->SetPinColor(IM_COL32(255, 10, 10, 255));
                R->SetHideDuringConnection(false);
                R->SetPinName("R");
                R->SetComponentMask(EComponentMask::R);

                CMaterialOutput* G = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "G", ENodePinDirection::Output));
                G->SetPinColor(IM_COL32(10, 255, 10, 255));
                G->SetHideDuringConnection(false);
                G->SetPinName("G");
                G->SetComponentMask(EComponentMask::G);

                CMaterialOutput* B = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "B", ENodePinDirection::Output));
                B->SetPinColor(IM_COL32(10, 10, 255, 255));
                B->SetHideDuringConnection(false);
                B->SetPinName("B");
                B->SetComponentMask(EComponentMask::B);

            }
            break;
        case EMaterialInputType::Float4:
            {
                CMaterialOutput* ValuePin = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "RGBA", ENodePinDirection::Output));
                ValuePin->SetShouldDrawEditor(true);
                ValuePin->SetHideDuringConnection(false);
                ValuePin->SetPinName("RGBA");
                ValuePin->SetComponentMask(EComponentMask::RGBA);
                ValuePin->SetInputType(EMaterialInputType::Float4);
                
                CMaterialOutput* R = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "R", ENodePinDirection::Output));
                R->SetPinColor(IM_COL32(255, 10, 10, 255));
                R->SetHideDuringConnection(false);
                R->SetPinName("R");
                R->SetComponentMask(EComponentMask::R);

                CMaterialOutput* G = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "G", ENodePinDirection::Output));
                G->SetPinColor(IM_COL32(10, 255, 10, 255));
                G->SetHideDuringConnection(false);
                G->SetPinName("G");
                G->SetComponentMask(EComponentMask::G);

                CMaterialOutput* B = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "B", ENodePinDirection::Output));
                B->SetPinColor(IM_COL32(10, 10, 255, 255));
                B->SetHideDuringConnection(false);
                B->SetPinName("B");
                B->SetComponentMask(EComponentMask::B);

                CMaterialOutput* A = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "A", ENodePinDirection::Output));
                A->SetHideDuringConnection(false);
                A->SetPinName("A");
                A->SetComponentMask(EComponentMask::A);

            }
            break;
        }
    }
    
    void CMaterialExpression_ConstantFloat::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        if (bDynamic)
        {
            Compiler.DefineFloatParameter(FullName, ParameterName, Value.r);
        }
        else
        {
            Compiler.DefineConstantFloat(FullName, Value.r);
        }
    }

    void CMaterialExpression_ConstantFloat::DrawNodeBody()
    {
        ImGui::SetNextItemWidth(126.0f);
        ImGui::DragFloat("##", glm::value_ptr(Value), 0.01f);
    }
    
    void CMaterialExpression_ConstantFloat2::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        if (bDynamic)
        {
            Compiler.DefineFloat2Parameter(FullName, ParameterName, &Value.r);
        }
        else
        {
            Compiler.DefineConstantFloat2(FullName, &Value.r);
        }
    }

    void CMaterialExpression_ConstantFloat2::DrawNodeBody()
    {
        ImGui::SetNextItemWidth(126.0f);
        ImGui::DragFloat2("##", glm::value_ptr(Value), 0.01f);
    }

    void CMaterialExpression_ConstantFloat3::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        if (bDynamic)
        {
            Compiler.DefineFloat3Parameter(FullName, ParameterName, &Value.r);
        }
        else
        {
            Compiler.DefineConstantFloat3(FullName, &Value.r);
        }
    }

    void CMaterialExpression_ConstantFloat3::DrawNodeBody()
    {
        ImGui::SetNextItemWidth(126.0f);
        ImGui::ColorPicker3("##", glm::value_ptr(Value));
    }
    
    void CMaterialExpression_ConstantFloat4::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        if (bDynamic)
        {
            Compiler.DefineFloat4Parameter(FullName, ParameterName, &Value.r);
        }
        else
        {
            Compiler.DefineConstantFloat4(FullName, &Value.r);
        }
    }

    void CMaterialExpression_ConstantFloat4::DrawNodeBody()
    {
        ImGui::SetNextItemWidth(126.0f);
        ImGui::ColorPicker4("##", glm::value_ptr(Value));
    }


    void CMaterialExpression_Addition::BuildNode()
    {
        CMaterialExpression_Math::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y");
        B->SetShouldDrawEditor(true);
        B->SetIndex(1);

    }

    void CMaterialExpression_Addition::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Add(A, B);
    }

    void CMaterialExpression_Clamp::BuildNode()
    {
        CMaterialExpression_Math::BuildNode();

        X = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        X->SetPinName("X");
        X->SetShouldDrawEditor(true);
        X->SetIndex(0);
        
        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "A", ENodePinDirection::Input));
        A->SetPinName("A");
        A->SetShouldDrawEditor(true);
        A->SetIndex(1);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "B", ENodePinDirection::Input));
        B->SetPinName("B");
        B->SetShouldDrawEditor(true);
        B->SetIndex(2);
    }

    void CMaterialExpression_Clamp::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Clamp(A, B, X);
    }

    void CMaterialExpression_Saturate::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
    }

    void CMaterialExpression_Saturate::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Saturate(A, B);
    }

    void CMaterialExpression_Normalize::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
    }

    void CMaterialExpression_Normalize::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Normalize(A, B);
    }

    void CMaterialExpression_Distance::BuildNode()
    {
        Super::BuildNode();
        
        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y");
        B->SetShouldDrawEditor(true);
        B->SetIndex(1);
    }

    void CMaterialExpression_Distance::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Distance(A, B);
    }

    void CMaterialExpression_Abs::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
    }

    void CMaterialExpression_Abs::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Abs(A, B);
    }

    void CMaterialExpression_SmoothStep::BuildNode()
    {
        CMaterialExpression_Math::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Edge0", ENodePinDirection::Input));
        A->SetPinName("Edge0");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Edge1", ENodePinDirection::Input));
        B->SetPinName("Edge1");
        B->SetShouldDrawEditor(true);
        B->SetIndex(1);

        C = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        C->SetPinName("X");
        C->SetShouldDrawEditor(true);
        C->SetIndex(2);
    }

    void CMaterialExpression_SmoothStep::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.SmoothStep(A, B, C);
    }

    void CMaterialExpression_Subtraction::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y");
        B->SetShouldDrawEditor(true);
        B->SetIndex(1);
    }


    void CMaterialExpression_Subtraction::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Subtract(A, B);
    }

    void CMaterialExpression_Multiplication::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y");
        B->SetShouldDrawEditor(true);
        B->SetIndex(1);
    }
    
    void CMaterialExpression_Multiplication::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Multiply(A, B);
    }

    void CMaterialExpression_Sin::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
    }

    void CMaterialExpression_Sin::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Sin(A, B);
    }

    void CMaterialExpression_Cosin::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
    }

    void CMaterialExpression_Cosin::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Cos(A, B);
    }

    void CMaterialExpression_Floor::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
    }

    void CMaterialExpression_Floor::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Floor(A, B);
    }
    
    void CMaterialExpression_Fract::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
    }

    void CMaterialExpression_Fract::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Fract(A);
    }

    void CMaterialExpression_Ceil::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        
    }

    void CMaterialExpression_Ceil::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Ceil(A, B);
    }

    void CMaterialExpression_Power::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y");
        B->SetShouldDrawEditor(true);
        B->SetIndex(3);
        
    }

    void CMaterialExpression_Power::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Power(A, B);
    }

    void CMaterialExpression_Mod::BuildNode()
    {
        Super::BuildNode();
        
        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y");
        B->SetShouldDrawEditor(true);
        B->SetIndex(1);
    }

    void CMaterialExpression_Min::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y");
        B->SetShouldDrawEditor(true);
        B->SetIndex(1);
    }

    void CMaterialExpression_Max::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y");
        B->SetShouldDrawEditor(true);
        B->SetIndex(1);
    }

    void CMaterialExpression_Step::BuildNode()
    {
        Super::BuildNode();
        
        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);

        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y");
        B->SetShouldDrawEditor(true);
        B->SetIndex(1);

    }

    void CMaterialExpression_Step::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Step(A, B);
    }

    void CMaterialExpression_Lerp::BuildNode()
    {
        Super::BuildNode();

        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y");
        B->SetShouldDrawEditor(true);
        B->SetIndex(1);

        C = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "A", ENodePinDirection::Input));
        C->SetPinName("A");
        C->SetShouldDrawEditor(true);
        C->SetIndex(2);
    }

    void CMaterialExpression_Lerp::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Lerp(A, B, C);
    }

    void CMaterialExpression_Max::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Max(A, B);
    }

    void CMaterialExpression_Min::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Min(A, B);
    }

    void CMaterialExpression_Mod::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Mod(A, B);
    }

    void CMaterialExpression_Division::BuildNode()
    {
        Super::BuildNode();
        
        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X");
        A->SetShouldDrawEditor(true);
        A->SetIndex(0);
        
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y");
        B->SetShouldDrawEditor(true);
        B->SetIndex(1);
    }

    void CMaterialExpression_Division::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.Divide(A, B);
    }
    
    void CMaterialExpression_BreakFloat2::BuildNode()
    {
        InputPin = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Vec2", ENodePinDirection::Input));
        InputPin->SetPinName("Vec2");
        InputPin->SetShouldDrawEditor(true);
        InputPin->SetIndex(0);

        R = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "R", ENodePinDirection::Output));
        R->SetShouldDrawEditor(true);
        R->SetHideDuringConnection(false);
        R->SetPinName("R");
        R->SetPinColor(IM_COL32(255, 10, 10, 255));
        R->SetComponentMask(EComponentMask::R);
        R->InputType = EMaterialInputType::Float;

        G = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "G", ENodePinDirection::Output));
        G->SetShouldDrawEditor(true);
        G->SetHideDuringConnection(false);
        G->SetPinName("G");
        G->SetPinColor(IM_COL32(10, 255, 10, 255));
        G->SetComponentMask(EComponentMask::G);
        G->InputType = EMaterialInputType::Float;
    }
    
    void CMaterialExpression_BreakFloat2::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.BreakFloat2(InputPin);
    }
    void CMaterialExpression_BreakFloat3::BuildNode()
    {
        InputPin = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Vec3", ENodePinDirection::Input));
        InputPin->SetPinName("Vec3");
        InputPin->SetShouldDrawEditor(true);
        InputPin->SetIndex(0);

        R = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "R", ENodePinDirection::Output));
        R->SetShouldDrawEditor(true);
        R->SetHideDuringConnection(false);
        R->SetPinName("R");
        R->SetPinColor(IM_COL32(255, 10, 10, 255));
        R->SetComponentMask(EComponentMask::R);
        R->InputType = EMaterialInputType::Float;

        G = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "G", ENodePinDirection::Output));
        G->SetShouldDrawEditor(true);
        G->SetHideDuringConnection(false);
        G->SetPinName("G");
        G->SetPinColor(IM_COL32(10, 255, 10, 255));
        G->SetComponentMask(EComponentMask::G);
        G->InputType = EMaterialInputType::Float;

        B = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "B", ENodePinDirection::Output));
        B->SetShouldDrawEditor(true);
        B->SetHideDuringConnection(false);
        B->SetPinName("B");
        B->SetPinColor(IM_COL32(10, 10, 255, 255));
        B->SetComponentMask(EComponentMask::B);
        B->InputType = EMaterialInputType::Float;
    }
    void CMaterialExpression_BreakFloat3::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.BreakFloat3(InputPin);
    }
    void CMaterialExpression_BreakFloat4::BuildNode()
    {
        InputPin = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Vec4", ENodePinDirection::Input));
        InputPin->SetPinName("Vec4");
        InputPin->SetShouldDrawEditor(true);
        InputPin->SetIndex(0);

        R = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "R", ENodePinDirection::Output));
        R->SetShouldDrawEditor(true);
        R->SetHideDuringConnection(false);
        R->SetPinName("R");
        R->SetPinColor(IM_COL32(255, 10, 10, 255)); 
        R->SetComponentMask(EComponentMask::R);
        R->InputType = EMaterialInputType::Float;

        G = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "G", ENodePinDirection::Output));
        G->SetShouldDrawEditor(true);
        G->SetHideDuringConnection(false);
        G->SetPinName("G");
        G->SetPinColor(IM_COL32(10, 255, 10, 255));
        G->SetComponentMask(EComponentMask::G);
        G->InputType = EMaterialInputType::Float;

        B = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "B", ENodePinDirection::Output));
        B->SetShouldDrawEditor(true);
        B->SetHideDuringConnection(false);
        B->SetPinName("B");
        B->SetPinColor(IM_COL32(10, 10, 255, 255));
        B->SetComponentMask(EComponentMask::B);
        B->InputType = EMaterialInputType::Float;

        A = Cast<CMaterialOutput>(CreatePin(CMaterialOutput::StaticClass(), "A", ENodePinDirection::Output));
        A->SetShouldDrawEditor(true);
        A->SetHideDuringConnection(false);
        A->SetPinName("A");
        A->SetComponentMask(EComponentMask::A);
        A->InputType = EMaterialInputType::Float;
    }
    void CMaterialExpression_BreakFloat4::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        Compiler.BreakFloat4(InputPin);
    }
}
