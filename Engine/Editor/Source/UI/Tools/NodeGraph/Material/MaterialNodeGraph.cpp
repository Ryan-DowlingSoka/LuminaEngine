#include "MaterialNodeGraph.h"
#include "MaterialCompiler.h"
#include "Core/Object/Cast.h"
#include "Nodes/MaterialNodes.h"
#include "UI/Tools/NodeGraph/EdNodeGraphPin.h"
#include "UI/Tools/NodeGraph/EdNode_Reroute.h"
#include "UI/Tools/NodeGraph/GraphAlgorithms.h"


namespace Lumina
{
    void CMaterialNodeGraph::Initialize()
    {
        Super::Initialize();

        bool bHasOutputNode = false;
        for (const TObjectPtr<CEdGraphNode>& Node : Nodes)
        {
            if (Node.IsValid() && Node->IsA<CMaterialOutputNode>())
            {
                bHasOutputNode = true;
                break;
            }
        }

        if (!bHasOutputNode)
        {
            CreateNode(CMaterialOutputNode::StaticClass());
        }

        // ---- Math (binary) ----
        RegisterGraphNode(CMaterialExpression_Addition::StaticClass());
        RegisterGraphNode(CMaterialExpression_Subtraction::StaticClass());
        RegisterGraphNode(CMaterialExpression_Multiplication::StaticClass());
        RegisterGraphNode(CMaterialExpression_Division::StaticClass());
        RegisterGraphNode(CMaterialExpression_Power::StaticClass());
        RegisterGraphNode(CMaterialExpression_Mod::StaticClass());
        RegisterGraphNode(CMaterialExpression_Min::StaticClass());
        RegisterGraphNode(CMaterialExpression_Max::StaticClass());
        RegisterGraphNode(CMaterialExpression_Step::StaticClass());
        RegisterGraphNode(CMaterialExpression_Atan2::StaticClass());

        // ---- Math (unary) ----
        RegisterGraphNode(CMaterialExpression_Sin::StaticClass());
        RegisterGraphNode(CMaterialExpression_Cosin::StaticClass());
        RegisterGraphNode(CMaterialExpression_Tan::StaticClass());
        RegisterGraphNode(CMaterialExpression_Asin::StaticClass());
        RegisterGraphNode(CMaterialExpression_Acos::StaticClass());
        RegisterGraphNode(CMaterialExpression_Atan::StaticClass());
        RegisterGraphNode(CMaterialExpression_Sinh::StaticClass());
        RegisterGraphNode(CMaterialExpression_Cosh::StaticClass());
        RegisterGraphNode(CMaterialExpression_Tanh::StaticClass());
        RegisterGraphNode(CMaterialExpression_Sqrt::StaticClass());
        RegisterGraphNode(CMaterialExpression_Rsqrt::StaticClass());
        RegisterGraphNode(CMaterialExpression_Log::StaticClass());
        RegisterGraphNode(CMaterialExpression_Log2::StaticClass());
        RegisterGraphNode(CMaterialExpression_Log10::StaticClass());
        RegisterGraphNode(CMaterialExpression_Exp::StaticClass());
        RegisterGraphNode(CMaterialExpression_Exp2::StaticClass());
        RegisterGraphNode(CMaterialExpression_Sign::StaticClass());
        RegisterGraphNode(CMaterialExpression_OneMinus::StaticClass());
        RegisterGraphNode(CMaterialExpression_Reciprocal::StaticClass());
        RegisterGraphNode(CMaterialExpression_Round::StaticClass());
        RegisterGraphNode(CMaterialExpression_Truncate::StaticClass());
        RegisterGraphNode(CMaterialExpression_Negate::StaticClass());
        RegisterGraphNode(CMaterialExpression_Square::StaticClass());
        RegisterGraphNode(CMaterialExpression_DegreesToRadians::StaticClass());
        RegisterGraphNode(CMaterialExpression_RadiansToDegrees::StaticClass());
        RegisterGraphNode(CMaterialExpression_Floor::StaticClass());
        RegisterGraphNode(CMaterialExpression_Fract::StaticClass());
        RegisterGraphNode(CMaterialExpression_Ceil::StaticClass());
        RegisterGraphNode(CMaterialExpression_Abs::StaticClass());
        RegisterGraphNode(CMaterialExpression_Saturate::StaticClass());

        // ---- Math (ternary) ----
        RegisterGraphNode(CMaterialExpression_Lerp::StaticClass());
        RegisterGraphNode(CMaterialExpression_Clamp::StaticClass());
        RegisterGraphNode(CMaterialExpression_SmoothStep::StaticClass());
        RegisterGraphNode(CMaterialExpression_Remap::StaticClass());

        // ---- Vector ops ----
        RegisterGraphNode(CMaterialExpression_ComponentMask::StaticClass());
        RegisterGraphNode(CMaterialExpression_Append::StaticClass());
        RegisterGraphNode(CMaterialExpression_MakeFloat2::StaticClass());
        RegisterGraphNode(CMaterialExpression_MakeFloat3::StaticClass());
        RegisterGraphNode(CMaterialExpression_MakeFloat4::StaticClass());
        RegisterGraphNode(CMaterialExpression_BreakFloat2::StaticClass());
        RegisterGraphNode(CMaterialExpression_BreakFloat3::StaticClass());
        RegisterGraphNode(CMaterialExpression_BreakFloat4::StaticClass());
        RegisterGraphNode(CMaterialExpression_Normalize::StaticClass());
        RegisterGraphNode(CMaterialExpression_Distance::StaticClass());
        RegisterGraphNode(CMaterialExpression_Length::StaticClass());
        RegisterGraphNode(CMaterialExpression_Dot::StaticClass());
        RegisterGraphNode(CMaterialExpression_Cross::StaticClass());
        RegisterGraphNode(CMaterialExpression_Reflect::StaticClass());
        RegisterGraphNode(CMaterialExpression_Refract::StaticClass());
        RegisterGraphNode(CMaterialExpression_RotateAboutAxis::StaticClass());

        // ---- Inputs ----
        RegisterGraphNode(CMaterialExpression_TexCoords::StaticClass());
        RegisterGraphNode(CMaterialExpression_Panner::StaticClass());
        RegisterGraphNode(CMaterialExpression_VertexNormal::StaticClass());
        RegisterGraphNode(CMaterialExpression_VertexTangent::StaticClass());
        RegisterGraphNode(CMaterialExpression_VertexBitangent::StaticClass());
        RegisterGraphNode(CMaterialExpression_VertexColor::StaticClass());
        RegisterGraphNode(CMaterialExpression_WorldPos::StaticClass());
        RegisterGraphNode(CMaterialExpression_CameraPos::StaticClass());
        RegisterGraphNode(CMaterialExpression_EntityID::StaticClass());
        RegisterGraphNode(CMaterialNodeGetTime::StaticClass());
        RegisterGraphNode(CMaterialExpression_CustomPrimitiveData::StaticClass());

        // ---- Constants ----
        RegisterGraphNode(CMaterialExpression_ConstantFloat::StaticClass());
        RegisterGraphNode(CMaterialExpression_ConstantFloat2::StaticClass());
        RegisterGraphNode(CMaterialExpression_ConstantFloat3::StaticClass());
        RegisterGraphNode(CMaterialExpression_ConstantFloat4::StaticClass());
        RegisterGraphNode(CMaterialExpression_NumericConstant::StaticClass());

        // ---- Textures ----
        RegisterGraphNode(CMaterialExpression_TextureSample::StaticClass());

        // ---- Color ----
        RegisterGraphNode(CMaterialExpression_Luminance::StaticClass());
        RegisterGraphNode(CMaterialExpression_Desaturate::StaticClass());
        RegisterGraphNode(CMaterialExpression_RGBToHSV::StaticClass());
        RegisterGraphNode(CMaterialExpression_HSVToRGB::StaticClass());
        RegisterGraphNode(CMaterialExpression_Posterize::StaticClass());
        RegisterGraphNode(CMaterialExpression_GammaCorrection::StaticClass());
        RegisterGraphNode(CMaterialExpression_Contrast::StaticClass());
        RegisterGraphNode(CMaterialExpression_Brightness::StaticClass());
        RegisterGraphNode(CMaterialExpression_Tint::StaticClass());
        RegisterGraphNode(CMaterialExpression_LinearToSRGB::StaticClass());
        RegisterGraphNode(CMaterialExpression_SRGBToLinear::StaticClass());

        // ---- Noise ----
        RegisterGraphNode(CMaterialExpression_Hash11::StaticClass());
        RegisterGraphNode(CMaterialExpression_Hash21::StaticClass());
        RegisterGraphNode(CMaterialExpression_Hash22::StaticClass());
        RegisterGraphNode(CMaterialExpression_Hash33::StaticClass());
        RegisterGraphNode(CMaterialExpression_ValueNoise::StaticClass());
        RegisterGraphNode(CMaterialExpression_GradientNoise::StaticClass());
        RegisterGraphNode(CMaterialExpression_PerlinNoise::StaticClass());
        RegisterGraphNode(CMaterialExpression_VoronoiNoise::StaticClass());
        RegisterGraphNode(CMaterialExpression_SimpleNoise::StaticClass());
        RegisterGraphNode(CMaterialExpression_Checkerboard::StaticClass());

        // ---- UV ----
        RegisterGraphNode(CMaterialExpression_RotateUV::StaticClass());
        RegisterGraphNode(CMaterialExpression_TilingAndOffset::StaticClass());
        RegisterGraphNode(CMaterialExpression_FlipBook::StaticClass());
        RegisterGraphNode(CMaterialExpression_PolarCoordinates::StaticClass());
        RegisterGraphNode(CMaterialExpression_TwirlUV::StaticClass());

        // ---- Scene ----
        RegisterGraphNode(CMaterialExpression_ScreenPosition::StaticClass());
        RegisterGraphNode(CMaterialExpression_ViewDirection::StaticClass());
        RegisterGraphNode(CMaterialExpression_ReflectionVector::StaticClass());
        RegisterGraphNode(CMaterialExpression_FragmentDepth::StaticClass());
        RegisterGraphNode(CMaterialExpression_ViewportSize::StaticClass());
        RegisterGraphNode(CMaterialExpression_AspectRatio::StaticClass());

        // ---- Conditional ----
        RegisterGraphNode(CMaterialExpression_If::StaticClass());
        RegisterGraphNode(CMaterialExpression_Compare::StaticClass());

        // ---- Shading ----
        RegisterGraphNode(CMaterialExpression_Fresnel::StaticClass());
        RegisterGraphNode(CMaterialExpression_DepthFade::StaticClass());
        RegisterGraphNode(CMaterialExpression_NormalFromHeight::StaticClass());
        RegisterGraphNode(CMaterialExpression_DeriveNormalZ::StaticClass());
        RegisterGraphNode(CMaterialExpression_BlendNormals::StaticClass());

        // ---- Terrain ----
        RegisterGraphNode(CMaterialExpression_TerrainLayerWeight::StaticClass());
        RegisterGraphNode(CMaterialExpression_TerrainLayerWeights::StaticClass());
        RegisterGraphNode(CMaterialExpression_TerrainLayerBlend::StaticClass());

        ValidateGraph();
    }

    void CMaterialNodeGraph::Shutdown()
    {
        CEdNodeGraph::Shutdown();
    }

    // Reverse-BFS the input-edge closure starting at any nodes feeding the
    // given pin. Used to partition the graph into "nodes that contribute to
    // the pixel stage" vs. "nodes that contribute to the vertex stage (WPO)".
    static void CollectInputClosure(CEdNodeGraphPin* StartPin, THashSet<CEdGraphNode*>& OutSet)
    {
        if (StartPin == nullptr || !StartPin->HasConnection())
        {
            return;
        }

        TQueue<CEdGraphNode*> Q;
        for (CEdNodeGraphPin* Conn : StartPin->GetConnections())
        {
            CEdGraphNode* N = Conn->GetOwningNode();
            if (OutSet.insert(N).second)
            {
                Q.push(N);
            }
        }

        while (!Q.empty())
        {
            CEdGraphNode* Node = Q.front();
            Q.pop();
            for (CEdNodeGraphPin* InputPin : Node->GetInputPins())
            {
                for (CEdNodeGraphPin* Conn : InputPin->GetConnections())
                {
                    CEdGraphNode* Up = Conn->GetOwningNode();
                    if (OutSet.insert(Up).second)
                    {
                        Q.push(Up);
                    }
                }
            }
        }
    }

    void CMaterialNodeGraph::CompileGraph(FMaterialCompiler& Compiler)
    {
        if (Nodes.empty())
        {
            return;
        }

        for (CEdGraphNode* Node : Nodes)
        {
            Node->ClearError();
        }

        TVector<CEdGraphNode*> SortedNodes;
        CEdGraphNode* CyclicNode = GraphAlgorithms::TopologicalSortFromRoot(Nodes, SortedNodes, [](CEdGraphNode* Node)
        {
            return Cast<CMaterialOutputNode>(Node) != nullptr;
        });

        if (CyclicNode != nullptr)
        {
            EdNodeGraph::FError Error;
            Error.Name          = "Cyclic";
            Error.Description   = "Cycle detected in material node graph! Graph must be acyclic!";
            Error.Node          = CyclicNode;
            Compiler.AddError(Error);

            return;
        }

        // Find the output node and partition the graph into vertex- and
        // pixel-reachable sets. Nodes appearing in both sets get walked twice
        // (once per stage) so they emit into both chunk buffers.
        CMaterialOutputNode* OutputNode = nullptr;
        for (const TObjectPtr<CEdGraphNode>& N : Nodes)
        {
            if (N.IsValid() && N->IsA<CMaterialOutputNode>())
            {
                OutputNode = static_cast<CMaterialOutputNode*>(N.Get());
                break;
            }
        }

        THashSet<CEdGraphNode*> VertexSet;
        THashSet<CEdGraphNode*> PixelSet;
        if (OutputNode)
        {
            // Vertex stage: only WPO contributes for now.
            CollectInputClosure(OutputNode->WorldPositionOffsetPin, VertexSet);

            // Pixel stage: every other output pin contributes.
            CEdNodeGraphPin* PixelPins[] = {
                OutputNode->BaseColorPin, OutputNode->MetallicPin,  OutputNode->RoughnessPin,
                OutputNode->SpecularPin,  OutputNode->EmissivePin,  OutputNode->AOPin,
                OutputNode->NormalPin,    OutputNode->OpacityPin
            };
            for (CEdNodeGraphPin* P : PixelPins)
            {
                CollectInputClosure(P, PixelSet);
            }
        }

        Compiler.NewLine();
        Compiler.NewLine();

        // Walk pixel set in topo order with stage = Pixel.
        Compiler.SetStage(EMaterialShaderStage::Pixel);
        for (size_t i = 0; i < SortedNodes.size(); ++i)
        {
            CEdGraphNode* Node = SortedNodes[i];
            Node->SetDebugExecutionOrder((uint32)i);

            if (Node->GetClass() == CMaterialOutputNode::StaticClass())
            {
                continue;
            }

            if (PixelSet.find(Node) == PixelSet.end())
            {
                continue;
            }

            static_cast<CMaterialGraphNode*>(Node)->GenerateDefinition(Compiler);
        }

        // Walk vertex set in topo order with stage = Vertex. Skipped entirely
        // when WPO is unconnected (VertexSet empty), so unmodified materials
        // pay zero compile-time cost.
        if (!VertexSet.empty())
        {
            Compiler.SetStage(EMaterialShaderStage::Vertex);
            for (CEdGraphNode* Node : SortedNodes)
            {
                if (Node->GetClass() == CMaterialOutputNode::StaticClass())
                {
                    continue;
                }
                if (VertexSet.find(Node) == VertexSet.end())
                {
                    continue;
                }
                static_cast<CMaterialGraphNode*>(Node)->GenerateDefinition(Compiler);
            }
        }

        // Output node: emits per-stage assignment chunks via AddPixelOutput /
        // AddVertexOutput regardless of cursor.
        if (OutputNode)
        {
            OutputNode->GenerateDefinition(Compiler);
        }

        // Restore default cursor.
        Compiler.SetStage(EMaterialShaderStage::Pixel);

        for (auto& Error : Compiler.GetErrors())
        {
            if (Error.Node)
            {
                Error.Node->SetError(Error);
            }
        }
    }

    void CMaterialNodeGraph::ValidateGraph()
    {
        Connections.clear();
        Connections.reserve(16);

        for (CEdGraphNode* Node : Nodes)
        {
            for (CEdNodeGraphPin* InputPin : Node->GetInputPins())
            {
                for (CEdNodeGraphPin* Connection : InputPin->GetConnections())
                {
                    Connections.push_back(InputPin->PinID);
                    Connections.push_back(Connection->PinID);
                }
            }
        }
    }

    void CMaterialNodeGraph::SetMaterial(CMaterial* InMaterial)
    {
        Material = InMaterial;
    }

    void CMaterialNodeGraph::HandleQuickPlace(int Digit, ImVec2 CanvasPos)
    {
        CClass* NodeClass = nullptr;
        switch (Digit)
        {
            case 1: NodeClass = CMaterialExpression_ConstantFloat::StaticClass();   break;
            case 2: NodeClass = CMaterialExpression_ConstantFloat2::StaticClass();  break;
            case 3: NodeClass = CMaterialExpression_ConstantFloat3::StaticClass();  break;
            case 4: NodeClass = CMaterialExpression_ConstantFloat4::StaticClass();  break;
            case 5: NodeClass = CMaterialNodeGetTime::StaticClass();                break;
            case 6: NodeClass = CMaterialExpression_WorldPos::StaticClass();        break;
            case 7: NodeClass = CMaterialExpression_TexCoords::StaticClass();       break;
            case 8: NodeClass = CMaterialExpression_VertexNormal::StaticClass();    break;
            case 9: NodeClass = CMaterialExpression_Multiplication::StaticClass();  break;
            case 0: NodeClass = CMaterialExpression_Addition::StaticClass();        break;
            default: return;
        }

        if (CEdGraphNode* NewNode = CreateNode(NodeClass))
        {
            ax::NodeEditor::SetNodePosition(NewNode->GetNodeID(), CanvasPos);
            ax::NodeEditor::SelectNode(NewNode->GetNodeID(), false);
        }
    }
}
