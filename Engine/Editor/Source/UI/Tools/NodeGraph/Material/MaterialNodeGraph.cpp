#include "MaterialNodeGraph.h"
#include "MaterialCompiler.h"
#include "Core/Object/Cast.h"
#include "Nodes/MaterialNodeExpression.h"
#include "Nodes/MaterialNodeGetTime.h"
#include "Nodes/MaterialNode_PrimitiveData.h"
#include "Nodes/MaterialNode_TextureSample.h"
#include "Nodes/MaterialOutputNode.h"
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

        //RegisterGraphNode(CEdNode_Reroute::StaticClass());

        RegisterGraphNode(CMaterialExpression_SmoothStep::StaticClass());
        RegisterGraphNode(CMaterialExpression_Saturate::StaticClass());
        RegisterGraphNode(CMaterialExpression_Normalize::StaticClass());
        RegisterGraphNode(CMaterialExpression_Distance::StaticClass());
        RegisterGraphNode(CMaterialExpression_Abs::StaticClass());

        RegisterGraphNode(CMaterialExpression_Addition::StaticClass());
        RegisterGraphNode(CMaterialExpression_Subtraction::StaticClass());
        RegisterGraphNode(CMaterialExpression_Division::StaticClass());
        RegisterGraphNode(CMaterialExpression_Multiplication::StaticClass());
        RegisterGraphNode(CMaterialExpression_Sin::StaticClass());
        RegisterGraphNode(CMaterialExpression_Cosin::StaticClass());
        RegisterGraphNode(CMaterialExpression_Floor::StaticClass());
        RegisterGraphNode(CMaterialExpression_Fract::StaticClass());
        RegisterGraphNode(CMaterialExpression_Ceil::StaticClass());
        RegisterGraphNode(CMaterialExpression_Power::StaticClass());
        RegisterGraphNode(CMaterialExpression_Mod::StaticClass());
        RegisterGraphNode(CMaterialExpression_Min::StaticClass());
        RegisterGraphNode(CMaterialExpression_Max::StaticClass());
        RegisterGraphNode(CMaterialExpression_Step::StaticClass());
        RegisterGraphNode(CMaterialExpression_Lerp::StaticClass());
        RegisterGraphNode(CMaterialExpression_Clamp::StaticClass());

        //RegisterGraphNode(CMaterialExpression_Append::StaticClass());
        RegisterGraphNode(CMaterialExpression_ComponentMask::StaticClass());
        RegisterGraphNode(CMaterialExpression_VertexNormal::StaticClass());
        RegisterGraphNode(CMaterialExpression_TexCoords::StaticClass());
        RegisterGraphNode(CMaterialExpression_Panner::StaticClass());
        RegisterGraphNode(CMaterialNodeGetTime::StaticClass());
        RegisterGraphNode(CMaterialExpression_CameraPos::StaticClass());
        RegisterGraphNode(CMaterialExpression_WorldPos::StaticClass());
        RegisterGraphNode(CMaterialExpression_EntityID::StaticClass());
        RegisterGraphNode(CMaterialExpression_CustomPrimitiveData::StaticClass());

        RegisterGraphNode(CMaterialExpression_ConstantFloat::StaticClass());
        RegisterGraphNode(CMaterialExpression_ConstantFloat2::StaticClass());
        RegisterGraphNode(CMaterialExpression_ConstantFloat3::StaticClass());
        RegisterGraphNode(CMaterialExpression_ConstantFloat4::StaticClass());

        RegisterGraphNode(CMaterialExpression_BreakFloat2::StaticClass());
        RegisterGraphNode(CMaterialExpression_BreakFloat3::StaticClass());
        RegisterGraphNode(CMaterialExpression_BreakFloat4::StaticClass());

        RegisterGraphNode(CMaterialExpression_MakeFloat2::StaticClass());
        RegisterGraphNode(CMaterialExpression_MakeFloat3::StaticClass());
        RegisterGraphNode(CMaterialExpression_MakeFloat4::StaticClass());
        
        RegisterGraphNode(CMaterialExpression_TextureSample::StaticClass());

        ValidateGraph();
        
    }
    
    void CMaterialNodeGraph::Shutdown()
    {
        CEdNodeGraph::Shutdown();
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
        
        Compiler.NewLine();
        Compiler.NewLine();

        for (size_t i = 0; i < SortedNodes.size(); ++i)
        {
            CEdGraphNode* Node = SortedNodes[i];
            
            Node->SetDebugExecutionOrder((uint32)i);
            if (Node->GetClass() == CMaterialOutputNode::StaticClass())
            {
                continue; 
            }

            CMaterialGraphNode* MaterialGraphNode = static_cast<CMaterialGraphNode*>(Node);
            MaterialGraphNode->GenerateDefinition(Compiler);
        }

        // Start off the compilation process using the MaterialOutput node as the kick-off.
        CMaterialGraphNode* MaterialOutputNode = static_cast<CMaterialGraphNode*>(Nodes[0].Get());
        MaterialOutputNode->GenerateDefinition(Compiler);

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

}
