#include "EdNodeGraph.h"

#include "EdGraphNode.h"
#include "Core/Object/Class.h"
#include <Core/Reflection/Type/LuminaTypes.h>
#include "Drawing.h"
#include "imgui_internal.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Package/Package.h"
#include "Core/Profiler/Profile.h"
#include "EASTL/sort.h"
#include "imgui-node-editor/imgui_node_editor_internal.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{
    static void DrawPinIcon(bool bConnected, int Alpha, ImVec4 Color)
    {
        EIconType iconType = EIconType::Circle;
        Color.w = Alpha / 255.0f;
        
        Icon(ImVec2(24.f, 24.0f), iconType, bConnected, Color, ImColor(32, 32, 32, Alpha));
    }
    
    CEdNodeGraph::CEdNodeGraph()
    {
    }

    CEdNodeGraph::~CEdNodeGraph()
    {
    }

    bool CEdNodeGraph::GraphSaveSettings(const char* data, size_t size, ax::NodeEditor::SaveReasonFlags reason, void* userPointer)
    {
        CEdNodeGraph* ThisGraph = (CEdNodeGraph*)userPointer;
        
        if (reason != ax::NodeEditor::SaveReasonFlags::None && !ThisGraph->bFirstDraw)
        {
            ThisGraph->GetPackage()->MarkDirty();
            ThisGraph->GraphSaveData.assign(data, size);
        }
        
        return true;
    }
    
    size_t CEdNodeGraph::GraphLoadSettings(char* Data, void* UserPointer)
    {
        CEdNodeGraph* ThisGraph = (CEdNodeGraph*)UserPointer;
        if (Data)
        {
            Memory::Memcpy(Data, ThisGraph->GraphSaveData.data(), ThisGraph->GraphSaveData.size());
        }
        return ThisGraph->GraphSaveData.size();
    }
    

    void CEdNodeGraph::Initialize()
    {
        ax::NodeEditor::Config Config;
        Config.EnableSmoothZoom = true;
        Config.UserPointer = this;
        Config.SaveSettings = GraphSaveSettings;
        Config.LoadSettings = GraphLoadSettings;
        Config.SettingsFile = nullptr;
        Context = ax::NodeEditor::CreateEditor(&Config);
    }

    void CEdNodeGraph::Shutdown()
    {
        ax::NodeEditor::DestroyEditor(Context);
        Context = nullptr;
    }

    void CEdNodeGraph::Serialize(FArchive& Ar)
    {
        Super::Serialize(Ar);
    }

    void CEdNodeGraph::PostLoad()
    {
        Super::PostLoad();
        
        TVector<TObjectPtr<CEdGraphNode>> SavedNodes = Move(Nodes);
        TVector<uint16> SavedConnections = Move(Connections);
        Nodes.clear();
        Connections.clear();

        for (const TObjectPtr<CEdGraphNode>& Node : SavedNodes)
        {
            if (Node.IsValid())
            {
                AddNode(Node.Get());
            }
        }

        for (size_t i = 0; i + 1 < SavedConnections.size(); i += 2)
        {
            uint16 InputID = SavedConnections[i];
            uint16 OutputID = SavedConnections[i + 1];

            CEdNodeGraphPin* InputPin = nullptr;
            CEdNodeGraphPin* OutputPin = nullptr;

            for (CEdGraphNode* Node : Nodes)
            {
                if (!InputPin)
                {
                    InputPin = Node->GetPin(InputID, ENodePinDirection::Input);
                }
                if (!OutputPin)
                {
                    OutputPin = Node->GetPin(OutputID, ENodePinDirection::Output);
                }
                if (InputPin && OutputPin)
                {
                    break;
                }
            }

            if (!InputPin || !OutputPin || InputPin->OwningNode == OutputPin->OwningNode)
            {
                continue;
            }

            if (InputPin->HasConnection() && !GetSchema().AllowsMultipleConnections(InputPin))
            {
                continue;
            }

            OutputPin->AddConnection(InputPin);
            InputPin->AddConnection(OutputPin);
        }
    }

    CPackage* CEdNodeGraph::GetNodeOuter()
    {
        return GetPackage();
    }

    void CEdNodeGraph::DrawGraph()
    {
        LUMINA_PROFILE_SCOPE();
        using namespace ax;
        
        NodeEditor::SetCurrentEditor(Context);
        NodeEditor::Begin(GetName().c_str());
    
        Graph::GraphNodeBuilder NodeBuilder;
        
        TVector<TPair<CEdNodeGraphPin*, CEdNodeGraphPin*>> Links;
        Links.reserve(40);
    
        uint32 Index = 0;
        for (CEdGraphNode* Node : Nodes)
        {
            NodeBuilder.Begin(Node->GetNodeID());
            
            ImVec2 Position = NodeEditor::GetNodePosition(Node->GetNodeID());
            Node->GridX = Position.x;
            Node->GridY = Position.y;
            
            if (!Node->WantsTitlebar())
            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            }
            
            NodeBuilder.Header(ImGui::ColorConvertU32ToFloat4(Node->GetNodeTitleColor()));
            
            if (!Node->WantsTitlebar())
            {
                ImGui::PopStyleVar();
            }
            
            ImGui::Spring(0);
            Node->DrawNodeTitleBar();
            
            if (bDebug)
            {
                ImGui::Text("(ID - %lld)", Node->GetNodeID());
            }
            
            ImGui::Spring(1);
            ImGui::Dummy(ImVec2(Node->GetMinNodeTitleBarSize()));
            ImGui::Spring(0);
            NodeBuilder.EndHeader();
    
            if (Node->GetInputPins().empty())
            {
                ImGui::BeginVertical("inputs", ImVec2(0,0), 0.0f);
                ImGui::Dummy(ImVec2(0,0));
                ImGui::EndVertical();
            }
            
            for (CEdNodeGraphPin* InputPin : Node->GetInputPins())
            {
                for (CEdNodeGraphPin* Connection : InputPin->GetConnections())
                {
                    Links.emplace_back(InputPin, Connection);
                }
                
                NodeBuilder.Input(InputPin->GetPinGUID());
    
                ImGui::PushID(InputPin);
                {
                    ImVec4 PinColor = ImGui::ColorConvertU32ToFloat4(InputPin->GetPinColor());
                    if (Node->HasError())
                    {
                        PinColor = ImVec4(255.0f, 0.0f, 0.0f, 255.0f);
                    }
                    
                    DrawPinIcon(InputPin->HasConnection(), 255.0f, PinColor);
                    ImGui::Spring(0);
    
                    ImGui::TextUnformatted(InputPin->GetPinName().c_str());
                    
                    if (bDebug)
                    {
                        ImGui::Text("(ID - %i)", InputPin->GetPinGUID());
                    }
                    
                    ImGui::Spring(0);
                }
                ImGui::PopID();
                
                NodeBuilder.EndInput();
            }
    
            NodeBuilder.Middle();
            Node->DrawNodeBody();
            
            for (CEdNodeGraphPin* OutputPin : Node->GetOutputPins())
            {
                NodeBuilder.Output(OutputPin->GetPinGUID());
                
                ImGui::PushID(OutputPin);
                {
                    ImGui::Spring(0);
    
                    ImGui::Spring(1, 1);
                    ImGui::TextUnformatted(OutputPin->GetPinName().c_str());
                    ImGui::Spring(0);
                    
                    ImVec4 PinColor = ImGui::ColorConvertU32ToFloat4(OutputPin->GetPinColor());
                    if (Node->HasError())
                    {
                        PinColor = ImVec4(255.0f, 0.0f, 0.0f, 255.0f);
                    }
                    DrawPinIcon(OutputPin->HasConnection(), 255.0f, PinColor);
                    
                    if (bDebug)
                    {
                        ImGui::Text("(ID - %i)", OutputPin->GetPinGUID());
                    }
                    
                }
                ImGui::PopID();
    
                NodeBuilder.EndOutput();
            }
            
            NodeBuilder.End(Node->WantsTitlebar());
            Index++;
        }
    
        NodeEditor::Suspend();
        {
            
            NodeEditor::NodeId NodeId;
            if (NodeEditor::ShowNodeContextMenu(&NodeId))
            {
                ImGui::OpenPopup("Node Context Menu");
            }
            
            NodeEditor::PinId PinId;
            if (NodeEditor::ShowPinContextMenu(&PinId))
            {
                ImGui::OpenPopup("Pin Context Menu");
            }
            
            NodeEditor::LinkId LinkId;
            if (NodeEditor::ShowLinkContextMenu(&LinkId))
            {
                ImGui::OpenPopup("Link Context Menu");
            }
            
            if (NodeEditor::ShowBackgroundContextMenu())
            {
                ActionMenu.Reset();
                ImGui::OpenPopup("Create New Node");
            }

            if (ImGui::BeginPopup("Create New Node"))
            {
                DrawGraphContextMenu();
                ImGui::EndPopup();
            }
        
            if (NodeEditor::BeginShortcut())
            {
                static ImVec2 CopiedPivot;
                if (NodeEditor::AcceptCopy())
                {
                    CopiedNodes.clear();
                
                    NodeEditor::NodeId Selections[12];
                    int Num = NodeEditor::GetSelectedNodes(Selections, std::size(Selections));
                
                    ImVec2 Min(FLT_MAX, FLT_MAX);
                    ImVec2 Max(-FLT_MAX, -FLT_MAX);
                
                    for (int i = 0; i < Num; ++i)
                    {
                        NodeEditor::NodeId Selection = Selections[i];
                        auto NodeItr = eastl::find_if(Nodes.begin(), Nodes.end(), [&] (const TObjectPtr<CEdGraphNode>& A)
                        {
                            return A->GetNodeID() == Selection.Get() && A->IsDeletable();
                        });
                    
                        if (NodeItr == Nodes.end())
                        {
                            continue;
                        }
                    
                        CopiedNodes.push_back(NodeItr->Get());
                    
                        ImVec2 Pos = NodeEditor::GetNodePosition(Selection);
                        ImVec2 Size = NodeEditor::GetNodeSize(Selection);

                        Min.x = eastl::min(Min.x, Pos.x);
                        Min.y = eastl::min(Min.y, Pos.y);

                        Max.x = eastl::max(Max.x, Pos.x + Size.x);
                        Max.y = eastl::max(Max.y, Pos.y + Size.y);
                    }
                
                    CopiedPivot = (Min + Max) * 0.5f;
                }
            
                if (NodeEditor::AcceptPaste())
                {
                    NodeEditor::ClearSelection();
                
                    ImVec2 PasteLocation = NodeEditor::ScreenToCanvas(ImGui::GetMousePos());

                    ImVec2 Delta = PasteLocation - CopiedPivot;
                
                    for (CEdGraphNode* Node : CopiedNodes)
                    {
                        ImVec2 CopiedCanvasPosition = NodeEditor::GetNodePosition(Node->GetNodeID());
                    
                        CEdGraphNode* NewNode = CreateNode(Node->GetClass());
                        Node->CopyPropertiesTo(NewNode);
                    
                        ImVec2 NewPosition = CopiedCanvasPosition + Delta;
                        NodeEditor::SetNodePosition(NewNode->GetNodeID(), NewPosition);
                        NodeEditor::SelectNode(NewNode->GetNodeID(), true);
                    }
                }
            
                if (NodeEditor::AcceptDuplicate())
                {
                    TVector<CEdGraphNode*> DupNodes;
                
                    NodeEditor::NodeId Selections[12];
                    int Num = NodeEditor::GetSelectedNodes(Selections, std::size(Selections));
                
                    ImVec2 Min(FLT_MAX, FLT_MAX);
                    ImVec2 Max(-FLT_MAX, -FLT_MAX);
                
                    for (int i = 0; i < Num; ++i)
                    {
                        NodeEditor::NodeId Selection = Selections[i];
                        auto NodeItr = eastl::find_if(Nodes.begin(), Nodes.end(), [&] (const TObjectPtr<CEdGraphNode>& A)
                        {
                            return A->GetNodeID() == Selection.Get() && A->IsDeletable();
                        });
                    
                        if (NodeItr == Nodes.end())
                        {
                            continue;
                        }
                    
                        DupNodes.push_back(NodeItr->Get());
                    
                        ImVec2 Pos = NodeEditor::GetNodePosition(Selection);
                        ImVec2 Size = NodeEditor::GetNodeSize(Selection);

                        Min.x = eastl::min(Min.x, Pos.x);
                        Min.y = eastl::min(Min.y, Pos.y);

                        Max.x = eastl::max(Max.x, Pos.x + Size.x);
                        Max.y = eastl::max(Max.y, Pos.y + Size.y);
                    }
                
                    CopiedPivot = (Min + Max) * 0.5f;
                
                    NodeEditor::ClearSelection();
                    ImVec2 PasteLocation = NodeEditor::ScreenToCanvas(ImGui::GetMousePos());
                    ImVec2 Delta = PasteLocation - CopiedPivot;
                
                    for (CEdGraphNode* Node : DupNodes)
                    {
                        ImVec2 CopiedCanvasPosition = NodeEditor::GetNodePosition(Node->GetNodeID());
                    
                        CEdGraphNode* NewNode = CreateNode(Node->GetClass());
                        Node->CopyPropertiesTo(NewNode);

                        ImVec2 NewPosition = CopiedCanvasPosition + Delta;
                        NodeEditor::SetNodePosition(NewNode->GetNodeID(), NewPosition);
                        NodeEditor::SelectNode(NewNode->GetNodeID(), true);
                    }
                }
            }
        }
        NodeEditor::Resume();

        NodeEditor::EndShortcut();
        
        bool bAnyNodeSelected = false;
        for (CEdGraphNode* Node : Nodes)
        {
            if (NodeEditor::IsNodeSelected(Node->GetNodeID()))
            {
                bAnyNodeSelected = true;
                if (NodeSelectedCallback)
                {
                    NodeSelectedCallback(Node);
                }
            }
        }
    
        if (!bAnyNodeSelected && NodeSelectedCallback)
        {
            NodeSelectedCallback(nullptr);
        }
        
        uint32 LinkID = 1;
        for (auto& [Start, End] : Links)
        {
            NodeEditor::Link(LinkID++, Start->GetPinGUID(), End->GetPinGUID());
        }
        
        if (NodeEditor::BeginCreate())
        {
            NodeEditor::PinId StartPinID, EndPinID;
            if (NodeEditor::QueryNewLink(&StartPinID, &EndPinID))
            {
                if (StartPinID && EndPinID && StartPinID != EndPinID)
                {
                    if (NodeEditor::AcceptNewItem())
                    {
                        CEdNodeGraphPin* StartPin = nullptr;
                        CEdNodeGraphPin* EndPin = nullptr;
    
                        for (CEdGraphNode* Node : Nodes)
                        {
                            StartPin = Node->GetPin(static_cast<uint16>(StartPinID.Get()), ENodePinDirection::Output);
                            if (StartPin)
                            {
                                break;
                            }
                        }
    
                        for (CEdGraphNode* Node : Nodes)
                        {
                            EndPin = Node->GetPin(static_cast<uint16>(EndPinID.Get()), ENodePinDirection::Input);
                            if (EndPin)
                            {
                                break;
                            }
                        }
                        
                        const FEdGraphSchema& Schema = GetSchema();
                        if (Schema.CanCreateConnection(StartPin, EndPin))
                        {
                            if (EndPin->HasConnection() && !Schema.AllowsMultipleConnections(EndPin))
                            {
                                TVector<CEdNodeGraphPin*> Existing = EndPin->GetConnections();
                                for (CEdNodeGraphPin* ConnectedPin : Existing)
                                {
                                    EndPin->DisconnectFrom(ConnectedPin);
                                }
                            }

                            StartPin->AddConnection(EndPin);
                            EndPin->AddConnection(StartPin);
                            ValidateGraph();
                        }
                    }
                }
            }
        }

        NodeEditor::EndCreate();
        
        if (NodeEditor::BeginDelete())
        {
            NodeEditor::NodeId NodeId = 0;
            while (NodeEditor::QueryDeletedNode(&NodeId))
            {
                // Unfortunately the way we do this now is a bit gross, it's too much of a pain to keep these nodes and the internal NodeEditor nodes in sync.
                // This is the way it's done in the examples, and even though it's essentially O(n^2), it seems to be working correctly.
                // Realistically it shouldn't matter too much.
                auto NodeItr = eastl::find_if(Nodes.begin(), Nodes.end(), [NodeId] (const TObjectPtr<CEdGraphNode>& A)
                {
                    return std::cmp_equal(A->GetNodeID(), NodeId.Get()) && A->IsDeletable();
                });

                if (NodeItr != Nodes.end())
                {
                    CEdGraphNode* Node = *NodeItr;
                    if (!NodeEditor::AcceptDeletedItem())
                    {
                        continue;
                    }

                    if (PreNodeDeletedCallback)
                    {
                        PreNodeDeletedCallback(Node);
                    }
                    
                    for (CEdNodeGraphPin* Pin : Node->GetInputPins())
                    {
                        if (Pin->HasConnection())
                        {
                            TVector<CEdNodeGraphPin*> PinConnections = Pin->GetConnections();
                            for (CEdNodeGraphPin* ConnectedPin : PinConnections)
                            {
                                ConnectedPin->DisconnectFrom(Pin);
                            }
                            Pin->ClearConnections();
                        }
                    }
        
                    for (CEdNodeGraphPin* Pin : Node->GetOutputPins())
                    {
                        if (Pin->HasConnection())
                        {
                            TVector<CEdNodeGraphPin*> PinConnections = Pin->GetConnections();
                            for (CEdNodeGraphPin* ConnectedPin : PinConnections)
                            {
                                ConnectedPin->DisconnectFrom(Pin);
                            }
                            Pin->ClearConnections();
                        }
                    }
                    
                    Nodes.erase(NodeItr);

                    Node->ConditionalBeginDestroy();
                    Node = nullptr;
                    
                    ValidateGraph();
                }
            }
            
            
            NodeEditor::LinkId DeletedLinkId;
            while (NodeEditor::QueryDeletedLink(&DeletedLinkId))
            {
                if (NodeEditor::AcceptDeletedItem())
                {
                    uint64 LinkIndex = DeletedLinkId.Get() - 1u;
                    if (LinkIndex < Links.size())
                    {
                        const TPair<CEdNodeGraphPin*, CEdNodeGraphPin*>& Pair = Links[LinkIndex];
                        Pair.first->RemoveConnection(Pair.second);
                        Pair.second->RemoveConnection(Pair.first);
                        ValidateGraph();
                    }
                }
            }
        }
        
        NodeEditor::EndDelete();
        
        NodeEditor::End();
        NodeEditor::SetCurrentEditor(nullptr);

        bFirstDraw = false;
    }


    void CEdNodeGraph::DrawGraphContextMenu()
    {
        if (ActionMenu.Draw(this))
        {
            ImGui::CloseCurrentPopup();
        }
    }

    void CEdNodeGraph::DrawNodeContextMenu(CEdGraphNode* Node)
    {
        constexpr ImVec2 PopupSize(320, 450);
        constexpr float CategorySpacing = 4.0f;

        ImGui::SetNextWindowSize(PopupSize, ImGuiCond_Always);
        
        
        constexpr float HeaderHeight = 54;
        constexpr float FooterHeight = 32;
        ImVec2 ChildSize = ImVec2(PopupSize.x, PopupSize.y - HeaderHeight - FooterHeight);
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, CategorySpacing));
        
        if (ImGui::BeginChild("##NodeList", ChildSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.08f, 0.08f, 0.10f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.25f, 0.25f, 0.27f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.35f, 0.35f, 0.37f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(0.45f, 0.45f, 0.47f, 1.0f));
            
            if (ImGui::BeginChild("##ScrollRegion", ImVec2(0, 0), false))
            {
                ImGuiX::Text("Node: {}", Node->GetNodeDisplayName());
                
                ImGui::EndChild();
            }
            
            ImGui::PopStyleColor(4);
            ImGui::EndChild();
        }
        
        ImGui::PopStyleVar(2);
    }

    void CEdNodeGraph::DrawPinContextMenu(CEdNodeGraphPin* Pin)
    {
    }

    CEdGraphNode* CEdNodeGraph::CreateNode(CClass* NodeClass)
    {
        CEdGraphNode* NewNode = NewObject<CEdGraphNode>(NodeClass, GetNodeOuter());
        AddNode(NewNode);
        return NewNode;
    }
    
    void CEdNodeGraph::RegisterGraphNode(CClass* InClass)
    {
        if (SupportedNodes.find(InClass) == SupportedNodes.end())
        {
            SupportedNodes.emplace(InClass);
        }
    }

    uint64 CEdNodeGraph::AddNode(CEdGraphNode* InNode)
    {
        int64 NodeID;
        
        if (InNode->NodeID != 0)
        {
            NodeID = InNode->GetNodeID();
        }
        else
        {
            NodeID = Math::RandRange(0u, UINT32_MAX);
        }
        
        InNode->FullName = InNode->GetNodeDisplayName() + "_" + eastl::to_string(NodeID);
        InNode->NodeID = NodeID;

        Nodes.push_back(InNode);

        if (!InNode->bWasBuild)
        {
            InNode->BuildNode();
            InNode->bWasBuild = true;
        }
        
        ValidateGraph();

        return NodeID;
    }
    
}
