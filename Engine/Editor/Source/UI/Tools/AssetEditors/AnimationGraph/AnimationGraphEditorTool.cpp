#include "AnimationGraphEditorTool.h"

#include "Assets/AssetTypes/Animation/AnimationGraph/AnimationGraph.h"
#include "Assets/AssetTypes/Mesh/Skeleton/Skeleton.h"
#include "Core/Math/Math.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Class.h"
#include "Core/Object/Package/Package.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "UI/Properties/Customizations/BonePickerContext.h"
#include "UI/Properties/Customizations/ParameterPickerContext.h"
#include "UI/Tools/NodeGraph/EdNodeGraphPin.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphCompiler.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphNodeGraph.h"
#include "UI/Tools/NodeGraph/Animation/AnimStateMachineGraph.h"
#include "UI/Tools/NodeGraph/Animation/AnimStateTransition.h"
#include "UI/Tools/NodeGraph/Animation/Nodes/AnimGraphNode_State.h"
#include "World/Entity/Components/EnvironmentComponent.h"
#include "World/Entity/Components/LightComponent.h"
#include "World/Entity/Components/AnimationGraphComponent.h"
#include "World/Entity/Components/SkeletalMeshComponent.h"

namespace Lumina
{
    static const char* AnimationGraphWindowName = "Animation Graph";
    static const char* GraphPropertiesWindowName = "Graph Properties";
    static const char* GraphParametersWindowName = "Parameters";

    FAnimationGraphEditorTool::FAnimationGraphEditorTool(IEditorToolContext* Context, CObject* InAsset)
        : FAssetEditorTool(Context, InAsset->GetName().c_str(), InAsset, NewObject<CWorld>())
        , NodeGraph(nullptr)
    {
    }

    void FAnimationGraphEditorTool::OnInitialize()
    {
        FAssetEditorTool::OnInitialize();

        CreateToolWindow(AnimationGraphWindowName, [this](bool /*bFocused*/)
        {
            DrawGraphWindow();
        });

        CreateToolWindow(GraphPropertiesWindowName, [this](bool /*bFocused*/)
        {
            DrawPropertiesWindow();
        });

        CreateToolWindow(GraphParametersWindowName, [this](bool /*bFocused*/)
        {
            DrawParametersWindow();
        });

        // The editor node graph lives as a sibling sub-object inside the asset's
        // package, mirroring the material editor. It is created on first open
        // and reloaded thereafter.
        FString GraphName = "AssetAnimationGraph";
        NodeGraph = Cast<CAnimationGraphNodeGraph>(Asset->GetPackage()->LoadObjectByName(GraphName));

        if (NodeGraph == nullptr)
        {
            NodeGraph = NewObject<CAnimationGraphNodeGraph>(Asset->GetPackage(), GraphName);
        }

        NodeGraph->SetAnimationGraph(Cast<CAnimationGraph>(Asset.Get()));

        // Seed the navigation stack with the top-level graph. EnterGraph readies
        // it (creates its context, wires callbacks) and pushes it.
        EnterGraph(NodeGraph, "Animation Graph");

        // Seed the runtime asset with bytecode so the preview viewport has
        // something to evaluate on the very first frame.
        Compile(false);
    }

    void FAnimationGraphEditorTool::OnDeinitialize(const FUpdateContext& /*UpdateContext*/)
    {
        // Tear down every node-editor context this tool created (the top graph
        // plus any nested state machine / blend-tree canvases that were opened).
        for (CEdNodeGraph* Graph : InitializedGraphs)
        {
            if (Graph != nullptr)
            {
                Graph->Shutdown();
            }
        }
        InitializedGraphs.clear();
        GraphStack.clear();
        NodeGraph = nullptr;
    }

    void FAnimationGraphEditorTool::WireGraphCallbacks(CEdNodeGraph* Graph)
    {
        Graph->SetNodeSelectedCallback([this](CEdGraphNode* Node)
        {
            if (Node != nullptr)
            {
                if (Node != SelectedNode || SelectedTransition != nullptr)
                {
                    SelectedNode = Node;
                    SelectedTransition = nullptr;
                    GetPropertyTable()->SetObject(Node, Node->GetClass());
                }
            }
            else if (SelectedNode != nullptr)
            {
                SelectedNode = nullptr;
                // A transition may be (re)selected later this same frame by the
                // link callback; only fall back to the asset if not.
                if (SelectedTransition == nullptr)
                {
                    GetPropertyTable()->SetObject(Asset, Asset->GetClass());
                }
            }
        });

        Graph->SetPreNodeDeletedCallback([this](const CEdGraphNode* Node)
        {
            // Deleting a State node also drops its transitions, so clear any
            // inspected transition defensively rather than risk a stale pointer.
            if (Node == SelectedNode || SelectedTransition != nullptr)
            {
                SelectedNode = nullptr;
                SelectedTransition = nullptr;
                GetPropertyTable()->SetObject(Asset, Asset->GetClass());
            }
        });

        Graph->SetNodeDoubleClickedCallback([this](CEdGraphNode* Node)
        {
            if (Node == nullptr)
            {
                return;
            }
            if (CEdNodeGraph* SubGraph = Node->GetEnterableSubGraph())
            {
                FString Label = Node->GetNodeDisplayName();
                if (CAnimGraphNode_State* StateNode = Cast<CAnimGraphNode_State>(Node))
                {
                    Label = StateNode->StateName.IsNone() ? FString("State") : StateNode->StateName.ToString();
                }
                EnterGraph(SubGraph, Label);
            }
        });

        Graph->SetLinkSelectedCallback([this](CEdNodeGraphPin* PinA, CEdNodeGraphPin* PinB)
        {
            // No single link selected -> drop a previously inspected transition.
            if (PinA == nullptr || PinB == nullptr)
            {
                if (SelectedTransition != nullptr)
                {
                    SelectedTransition = nullptr;
                    GetPropertyTable()->SetObject(Asset, Asset->GetClass());
                }
                return;
            }

            // Links are emitted as (input pin, connected output pin); be order
            // agnostic anyway.
            CEdNodeGraphPin* InPin  = PinA->bInputPin ? PinA : PinB;
            CEdNodeGraphPin* OutPin = PinA->bInputPin ? PinB : PinA;

            CAnimGraphNode_State* ToState   = Cast<CAnimGraphNode_State>(InPin->GetOwningNode());
            CAnimGraphNode_State* FromState = Cast<CAnimGraphNode_State>(OutPin->GetOwningNode());
            if (ToState == nullptr || FromState == nullptr)
            {
                // Not a transition wire (e.g. the Entry link) -- leave as-is.
                return;
            }

            CAnimStateMachineGraph* SMGraph = GraphStack.empty()
                ? nullptr
                : Cast<CAnimStateMachineGraph>(GraphStack.back().Graph);
            if (SMGraph == nullptr)
            {
                return;
            }

            CAnimStateTransition* Transition = SMGraph->FindTransition(FromState->GetNodeID(), ToState->GetNodeID());
            if (Transition != nullptr && Transition != SelectedTransition)
            {
                SelectedTransition = Transition;
                SelectedNode = nullptr;
                GetPropertyTable()->SetObject(Transition, Transition->GetClass());
            }
        });
    }

    void FAnimationGraphEditorTool::EnsureGraphReady(CEdNodeGraph* Graph)
    {
        if (Graph == nullptr || InitializedGraphs.find(Graph) != InitializedGraphs.end())
        {
            return;
        }
        InitializedGraphs.insert(Graph);
        Graph->Initialize();          // guarded internally; creates the context
        WireGraphCallbacks(Graph);
    }

    void FAnimationGraphEditorTool::EnterGraph(CEdNodeGraph* Graph, const FString& Label)
    {
        if (Graph == nullptr)
        {
            return;
        }

        EnsureGraphReady(Graph);
        GraphStack.push_back({ Graph, Label });

        // Reset the inspector on descent so the panel isn't showing a node from
        // the graph we just left. The cached transition tables are tied to the
        // outer canvas's transitions and would dangle on return.
        SelectedNode = nullptr;
        SelectedTransition = nullptr;
        TransitionTables.clear();
        GetPropertyTable()->SetObject(Asset, Asset->GetClass());
    }

    void FAnimationGraphEditorTool::PopToLevel(int32 Index)
    {
        if (Index < 0 || Index >= (int32)GraphStack.size() - 1)
        {
            return;
        }
        GraphStack.resize(Index + 1);

        SelectedNode = nullptr;
        SelectedTransition = nullptr;
        GetPropertyTable()->SetObject(Asset, Asset->GetClass());
    }

    void FAnimationGraphEditorTool::SetupWorldForTool()
    {
        FEditorTool::SetupWorldForTool();

        CreateFloorPlane();

        DirectionalLightEntity = World->ConstructEntity("Directional Light");
        World->GetEntityRegistry().emplace<SDirectionalLightComponent>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SEnvironmentComponent>(DirectionalLightEntity);

        CameraState.Speed = 5.0f;

        // The skeleton may already be set (reopening a configured asset) or not
        // (a fresh graph). SyncPreviewMesh handles both, and re-runs every frame.
        SyncPreviewMesh();
    }

    void FAnimationGraphEditorTool::SyncPreviewMesh()
    {
        if (!World.IsValid())
        {
            return;
        }

        CAnimationGraph* Graph = Cast<CAnimationGraph>(Asset.Get());
        const bool bHasPreview = Graph != nullptr
            && Graph->Skeleton.IsValid()
            && Graph->Skeleton->PreviewMesh.IsValid();

        auto& Registry = World->GetEntityRegistry();
        const bool bMeshEntityValid = MeshEntity != entt::null && Registry.valid(MeshEntity);

        // Skeleton cleared while the tool is open -> tear the preview down.
        if (!bHasPreview)
        {
            if (bMeshEntityValid)
            {
                World->DestroyEntity(MeshEntity);
            }
            MeshEntity = entt::null;
            return;
        }

        CSkeletalMesh* PreviewMesh = Graph->Skeleton->PreviewMesh;

        if (!bMeshEntityValid)
        {
            MeshEntity = World->ConstructEntity("Preview Mesh");
            Registry.emplace<SSkeletalMeshComponent>(MeshEntity).SkeletalMesh = PreviewMesh;
            Registry.emplace<SAnimationGraphComponent>(MeshEntity).Graph = Graph;

            STransformComponent& MeshTransform   = Registry.get<STransformComponent>(MeshEntity);
            STransformComponent& EditorTransform = Registry.get<STransformComponent>(EditorEntity);

            glm::quat Rotation = Math::FindLookAtRotation(MeshTransform.GetLocation() + glm::vec3(0.0f, 0.85f, 0.0f), EditorTransform.GetLocation());
            EditorTransform.SetRotation(Rotation);
            return;
        }

        // Entity exists -> keep its mesh / graph references current in case the
        // skeleton's preview mesh or the graph asset changed underneath us.
        SSkeletalMeshComponent& MeshComp = Registry.get<SSkeletalMeshComponent>(MeshEntity);
        if (MeshComp.SkeletalMesh.Get() != PreviewMesh)
        {
            MeshComp.SkeletalMesh = PreviewMesh;
        }

        SAnimationGraphComponent& GraphComp = Registry.get<SAnimationGraphComponent>(MeshEntity);
        if (GraphComp.Graph.Get() != Graph)
        {
            GraphComp.Graph = Graph;
        }
    }

    void FAnimationGraphEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        FAssetEditorTool::Update(UpdateContext);

        // The skeleton is frequently assigned after the tool is already open;
        // pick that up here rather than only at world setup.
        SyncPreviewMesh();

        // Live preview: keep the runtime asset's bytecode in sync with the node
        // graph so edits resolve in the viewport without a manual compile.
        if (bAutoCompile)
        {
            Compile(false);
        }

        // Drive the preview mesh's parameters from the Parameters panel so
        // transition conditions and Get Parameter nodes actually do something.
        PushParameterOverrides();
    }

    void FAnimationGraphEditorTool::PushParameterOverrides()
    {
        if (!World.IsValid() || MeshEntity == entt::null)
        {
            return;
        }

        auto& Registry = World->GetEntityRegistry();
        if (!Registry.valid(MeshEntity))
        {
            return;
        }

        SAnimationGraphComponent* GraphComp = Registry.try_get<SAnimationGraphComponent>(MeshEntity);
        if (GraphComp == nullptr || !GraphComp->Graph.IsValid())
        {
            return;
        }

        // Write straight into the VM state's parameter array. Going through the
        // component's SetFloot would pull an un-exported Runtime symbol into the
        // editor; FindParameterIndex is part of the exported CAnimationGraph API.
        // Unknown names resolve to INDEX_NONE; an un-sized VMState (system hasn't
        // ticked yet) is simply skipped until the next frame.
        CAnimationGraph* Graph = GraphComp->Graph.Get();
        for (const auto& [Name, Value] : ParameterOverrides)
        {
            const int32 Index = Graph->FindParameterIndex(Name);
            if (Index >= 0 && Index < (int32)GraphComp->VMState.Parameters.size())
            {
                GraphComp->VMState.Parameters[Index] = Value;
            }
        }
    }

    void FAnimationGraphEditorTool::DrawToolMenu(const FUpdateContext& /*UpdateContext*/)
    {
        if (ImGui::MenuItem(LE_ICON_COG " Compile"))
        {
            Compile();
        }
    }

    void FAnimationGraphEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& /*InDockspaceSize*/) const
    {
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);

        ImGuiID leftDockID = 0, rightDockID = 0, bottomDockID = 0;

        // Right pane: properties. Left pane splits into a preview viewport on
        // top and the node graph canvas below it.
        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.25f, &rightDockID, &leftDockID);
        ImGui::DockBuilderSplitNode(leftDockID, ImGuiDir_Up, 0.45f, &leftDockID, &bottomDockID);

        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(),            leftDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(AnimationGraphWindowName).c_str(),      bottomDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(GraphPropertiesWindowName).c_str(),     rightDockID);
        // Parameters share the right pane, tabbed behind Properties.
        ImGui::DockBuilderDockWindow(GetToolWindowName(GraphParametersWindowName).c_str(),     rightDockID);
    }

    void FAnimationGraphEditorTool::DrawBreadcrumbBar()
    {
        for (int32 i = 0; i < (int32)GraphStack.size(); ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextUnformatted(">");
                ImGui::SameLine(0.0f, 4.0f);
            }

            const bool bIsCurrent = (i == (int32)GraphStack.size() - 1);
            ImGui::BeginDisabled(bIsCurrent);
            if (ImGui::Button(GraphStack[i].Label.c_str()))
            {
                PopToLevel(i);
            }
            ImGui::EndDisabled();
        }

        ImGui::Separator();
    }

    void FAnimationGraphEditorTool::DrawGraphWindow()
    {
        DrawBreadcrumbBar();

        if (!GraphStack.empty() && GraphStack.back().Graph != nullptr)
        {
            GraphStack.back().Graph->DrawGraph();
        }
    }

    void FAnimationGraphEditorTool::DrawPreviewControls()
    {
        CAnimationGraph* Graph = Cast<CAnimationGraph>(Asset.Get());
        if (Graph == nullptr)
        {
            return;
        }

        if (!Graph->Skeleton.IsValid())
        {
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.35f, 1.0f),
                "No skeleton assigned -- set one on the graph asset to see a preview mesh.");
        }
        else if (!Graph->Skeleton->PreviewMesh.IsValid())
        {
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.35f, 1.0f),
                "Skeleton has no preview mesh assigned.");
        }
    }

    void FAnimationGraphEditorTool::DrawPropertiesWindow()
    {
        CAnimationGraph* Graph = Cast<CAnimationGraph>(Asset.Get());
        const FSkeletonResource* ActiveSkeleton = (Graph != nullptr && Graph->Skeleton.IsValid())
            ? Graph->Skeleton->GetSkeletonResource()
            : nullptr;
        BonePickerContext::FScope      BonePickerScope(ActiveSkeleton);
        ParameterPickerContext::FScope ParamPickerScope(Graph);

        if (ImGui::Button(LE_ICON_COG " Compile"))
        {
            Compile();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Auto Compile", &bAutoCompile);

        ImGui::SameLine();
        if (bHasCompilationErrors)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Compile failed");
        }
        else if (!CompilationLog.empty())
        {
            ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.45f, 1.0f), "Compiled");
        }

        ImGui::Separator();

        DrawPreviewControls();

        ImGui::Separator();

        // Contextual hint: on the state machine canvas, transitions are edited
        // by selecting their wire -- not obvious without a nudge.
        if (SelectedNode == nullptr && SelectedTransition == nullptr &&
            !GraphStack.empty() && Cast<CAnimStateMachineGraph>(GraphStack.back().Graph) != nullptr)
        {
            ImGui::TextWrapped("Tip: click a transition wire between two States to edit its "
                "condition here. Click a State node to rename it; double-click to edit its blend tree.");
            ImGui::Separator();
        }

        GetPropertyTable()->DrawTree();

        // When a State node is selected, inline-list its outgoing transitions
        // so the user can edit conditions without having to click each wire.
        if (CAnimGraphNode_State* StateNode = Cast<CAnimGraphNode_State>(SelectedNode))
        {
            DrawOutgoingTransitionsForState(StateNode);
        }

        if (!CompilationLog.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted(CompilationLog.c_str());
        }
    }

    void FAnimationGraphEditorTool::DrawOutgoingTransitionsForState(CAnimGraphNode_State* State)
    {
        if (State == nullptr || GraphStack.empty())
        {
            return;
        }

        CAnimStateMachineGraph* SMGraph = Cast<CAnimStateMachineGraph>(GraphStack.back().Graph);
        if (SMGraph == nullptr)
        {
            return;
        }

        TVector<CAnimStateTransition*> Outgoing;
        for (const TObjectPtr<CAnimStateTransition>& T : SMGraph->GetTransitions())
        {
            if (T.IsValid() && T->FromStateNodeID == State->GetNodeID())
            {
                Outgoing.push_back(T.Get());
            }
        }

        // Drop cached property tables whose backing transition was removed.
        for (auto It = TransitionTables.begin(); It != TransitionTables.end(); )
        {
            const bool bAlive = eastl::find(Outgoing.begin(), Outgoing.end(), It->first) != Outgoing.end();
            if (!bAlive)
            {
                It = TransitionTables.erase(It);
            }
            else
            {
                ++It;
            }
        }

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 222, 255));
        ImGui::TextUnformatted(LE_ICON_ARROW_RIGHT_BOLD " Outgoing Transitions");
        ImGui::PopStyleColor();

        if (Outgoing.empty())
        {
            ImGui::TextDisabled("Drag from this State's Out pin to another State to create one.");
            return;
        }

        for (CAnimStateTransition* Transition : Outgoing)
        {
            ImGui::PushID(Transition);

            CAnimGraphNode_State* ToState = nullptr;
            for (CEdGraphNode* N : SMGraph->Nodes)
            {
                CAnimGraphNode_State* S = Cast<CAnimGraphNode_State>(N);
                if (S != nullptr && S->GetNodeID() == Transition->ToStateNodeID)
                {
                    ToState = S;
                    break;
                }
            }

            const FString ToLabel = (ToState != nullptr && !ToState->StateName.IsNone())
                ? ToState->StateName.ToString()
                : FString("(unnamed)");
            const FString Header = FString("\xE2\x86\x92 ") + ToLabel;

            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::CollapsingHeader(Header.c_str()))
            {
                auto It = TransitionTables.find(Transition);
                if (It == TransitionTables.end())
                {
                    TUniquePtr<FPropertyTable> NewTable = MakeUnique<FPropertyTable>(Transition);
                    It = TransitionTables.emplace(Transition, Move(NewTable)).first;
                }
                It->second->DrawTree();
            }

            ImGui::PopID();
        }
    }

    void FAnimationGraphEditorTool::DrawParametersWindow()
    {
        CAnimationGraph* Graph = Cast<CAnimationGraph>(Asset.Get());
        if (Graph == nullptr)
        {
            return;
        }

        ImGui::TextWrapped("Parameters drive blend weights, playback speeds, and state-machine "
            "transition conditions. Set values here to live-test in the preview viewport.");
        ImGui::Separator();

        // Inline create-parameter row. The graph's Parameters list is overwritten
        // on every compile from discovered usage; to make a name "exist" across
        // compiles we seed an editor override -- the picker dropdown also reads
        // the (transient) list, and the compiler always re-adds names referenced
        // by Get Parameter nodes / transition conditions.
        ImGui::PushItemWidth(180.0f);
        ImGui::InputTextWithHint("##NewParam", "New parameter name...", NewParameterBuffer, sizeof(NewParameterBuffer));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_PLUS " Add"))
        {
            const FName Name(NewParameterBuffer);
            if (!Name.IsNone())
            {
                if (Graph->FindParameterIndex(Name) == INDEX_NONE)
                {
                    FAnimGraphParameter Param;
                    Param.Name = Name;
                    Param.Type = EAnimGraphParamType::Float;
                    Param.DefaultValue = 0.0f;
                    Graph->Parameters.push_back(Param);
                }
                ParameterOverrides[Name] = 0.0f;
                NewParameterBuffer[0] = '\0';
                Asset->GetPackage()->MarkDirty();
            }
        }
        ImGui::Separator();

        if (Graph->Parameters.empty())
        {
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.35f, 1.0f),
                "No parameters yet. Add one above, drop in a Get Parameter node,");
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.35f, 1.0f),
                "or fill in a transition's Condition Parameter.");
            return;
        }

        FName PendingRemoval;
        for (const FAnimGraphParameter& Param : Graph->Parameters)
        {
            // Lazily seed the editor override from the compiled default value.
            auto It = ParameterOverrides.find(Param.Name);
            if (It == ParameterOverrides.end())
            {
                It = ParameterOverrides.emplace(Param.Name, Param.DefaultValue).first;
            }

            float& Value = It->second;
            const char* Name = Param.Name.c_str();

            ImGui::PushID(Name);

            const float RemoveButtonW = 24.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - RemoveButtonW - 4.0f);
            if (Param.Type == EAnimGraphParamType::Bool)
            {
                bool bValue = Value != 0.0f;
                if (ImGui::Checkbox(Name, &bValue))
                {
                    Value = bValue ? 1.0f : 0.0f;
                }
            }
            else
            {
                ImGui::DragFloat(Name, &Value, 0.01f);
            }

            ImGui::SameLine();
            if (ImGui::Button(LE_ICON_TRASH_CAN, ImVec2(RemoveButtonW, 0)))
            {
                PendingRemoval = Param.Name;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGuiX::TextTooltip_Internal("Remove parameter (will be re-discovered if any node still references it)");
            }

            ImGui::PopID();
        }

        if (!PendingRemoval.IsNone())
        {
            for (auto It = Graph->Parameters.begin(); It != Graph->Parameters.end(); ++It)
            {
                if (It->Name == PendingRemoval)
                {
                    Graph->Parameters.erase(It);
                    break;
                }
            }
            ParameterOverrides.erase(PendingRemoval);
            Asset->GetPackage()->MarkDirty();
        }
    }

    void FAnimationGraphEditorTool::Compile(bool bMarkPackageDirty)
    {
        CompilationLog.clear();
        bHasCompilationErrors = false;

        CAnimationGraph* Graph = Cast<CAnimationGraph>(Asset.Get());
        if (Graph == nullptr || NodeGraph == nullptr)
        {
            return;
        }

        FAnimationGraphCompiler Compiler;

        // Resolve bone-mask names to per-bone weight arrays once, up front, so
        // Layered Blend Per Bone nodes can just look up by name during their
        // GenerateBytecode pass.
        if (Graph->Skeleton.IsValid())
        {
            Compiler.ResolveBoneMasks(Graph->BoneMaskDefs, Graph->Skeleton->GetSkeletonResource());
        }

        // Seed the compiler with any parameters already declared on the asset
        // (e.g. user-added through the Parameters panel) so they persist across
        // compiles even if no node/transition references them yet.
        for (const FAnimGraphParameter& Param : Graph->Parameters)
        {
            Compiler.AddParameter(Param.Name, Param.Type, Param.DefaultValue);
        }

        NodeGraph->CompileGraph(Compiler);

        if (Compiler.HasErrors())
        {
            bHasCompilationErrors = true;
            for (const EdNodeGraph::FError& Error : Compiler.GetErrors())
            {
                CompilationLog += "ERROR - [" + Error.Name + "]: " + Error.Description + "\n";
            }
            return;
        }

        Compiler.BuildGraph(Graph);

        if (bMarkPackageDirty)
        {
            Asset->GetPackage()->MarkDirty();
        }

        CompilationLog = "Animation graph compiled successfully.\n";
    }

    void FAnimationGraphEditorTool::OnSave()
    {
        Compile();
        FAssetEditorTool::OnSave();
    }
}
