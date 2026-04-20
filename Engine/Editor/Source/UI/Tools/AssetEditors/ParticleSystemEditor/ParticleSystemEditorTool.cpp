#include "ParticleSystemEditorTool.h"
#include "Assets/AssetTypes/ParticleSystem/ParticleSystem.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Package/Package.h"
#include "Paths/Paths.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/ShaderCompiler.h"
#include "UI/Tools/NodeGraph/Particle/ParticleCompiler.h"
#include "UI/Tools/NodeGraph/Particle/ParticleNodeGraph.h"
#include "World/entity/components/environmentcomponent.h"
#include "World/entity/components/lightcomponent.h"
#include "World/Entity/Components/ParticleSystemComponent.h"
#include "World/Entity/Components/TransformComponent.h"

namespace Lumina
{
    static const char* ParticleGraphName       = "Particle Graph";
    static const char* ParticlePropertiesName  = "Properties";
    static const char* ShaderPreviewName       = "Shader Preview";

    FParticleSystemEditorTool::FParticleSystemEditorTool(IEditorToolContext* Context, CObject* InAsset)
        : FAssetEditorTool(Context, InAsset->GetName().c_str(), InAsset, NewObject<CWorld>())
        , ParticleEntity()
        , DirectionalLightEntity(), NodeGraph(nullptr)
    {
    }

    void FParticleSystemEditorTool::SetupWorldForTool()
    {
        FAssetEditorTool::SetupWorldForTool();

        DirectionalLightEntity = World->ConstructEntity("Directional Light");
        World->GetEntityRegistry().emplace<SDirectionalLightComponent>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SEnvironmentComponent>(DirectionalLightEntity);

        ParticleEntity = World->ConstructEntity("Particle System");
        SParticleSystemComponent& ParticleComponent = World->GetEntityRegistry().emplace<SParticleSystemComponent>(ParticleEntity);
        ParticleComponent.ParticleSystem = Cast<CParticleSystem>(Asset.Get());

        STransformComponent& ParticleTransform = World->GetEntityRegistry().get<STransformComponent>(ParticleEntity);
        STransformComponent& EditorTransform   = World->GetEntityRegistry().get<STransformComponent>(EditorEntity);
        const glm::quat LookRotation = Math::FindLookAtRotation(ParticleTransform.GetLocation(), EditorTransform.GetLocation());
        EditorTransform.SetRotation(LookRotation);
    }

    void FParticleSystemEditorTool::OnInitialize()
    {
        FAssetEditorTool::OnInitialize();

        CreateToolWindow(ParticleGraphName, [&](bool bFocused)
        {
            DrawParticleGraph();
        });

        CreateToolWindow(ParticlePropertiesName, [&](bool bFocused)
        {
        });

        CreateToolWindow(ShaderPreviewName, [&](bool bFocused)
        {
            DrawParticleProperties();
            //DrawShaderPreview();
        });

        FString GraphName = "AssetParticleGraph";
        NodeGraph = Cast<CParticleNodeGraph>(Asset->GetPackage()->LoadObjectByName(GraphName));

        if (NodeGraph == nullptr)
        {
            NodeGraph = NewObject<CParticleNodeGraph>(Asset->GetPackage(), GraphName);
        }

        NodeGraph->SetParticleSystem(Cast<CParticleSystem>(Asset.Get()));
        NodeGraph->Initialize();

        NodeGraph->SetNodeSelectedCallback([this](CEdGraphNode* Node)
        {
            if (Node != SelectedNode)
            {
                SelectedNode = Node;

                if (SelectedNode == nullptr)
                {
                    GetPropertyTable()->SetObject(Asset, Asset->GetClass());
                }
                else
                {
                    GetPropertyTable()->SetObject(Node, Node->GetClass());
                }
            }
        });

        NodeGraph->SetPreNodeDeletedCallback([this](const CEdGraphNode* Node)
        {
            if (Node == SelectedNode)
            {
                GetPropertyTable()->SetObject(nullptr, nullptr);
            }
        });
    }

    void FParticleSystemEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
        if (NodeGraph)
        {
            NodeGraph->Shutdown();
            NodeGraph = nullptr;
        }
    }

    void FParticleSystemEditorTool::DrawHelpMenu()
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Debug Node IDs");
        ImGui::TableNextColumn();
        ImGui::Checkbox("##DebugID", &NodeGraph->bDebug);
    }

    void FParticleSystemEditorTool::DrawToolMenu(const FUpdateContext& UpdateContext)
    {
        if (ImGui::MenuItem(LE_ICON_RECEIPT_TEXT" Compile"))
        {
            Compile();
        }
    }

    void FParticleSystemEditorTool::DrawParticleGraph()
    {
        NodeGraph->DrawGraph();
    }

    void FParticleSystemEditorTool::DrawParticleProperties()
    {
        GetPropertyTable()->DrawTree();
    }

    void FParticleSystemEditorTool::DrawShaderPreview()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

        if (CompilationResult.bIsError)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
            ImGui::BeginChild("##error_preview", ImVec2(0, 0), true);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextUnformatted(CompilationResult.CompilationLog.c_str());
            ImGui::PopStyleColor();
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }

        if (CompiledSource.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
            ImGui::BeginChild("##empty_preview", ImVec2(0, 0), true);
            ImVec2 Available = ImGui::GetContentRegionAvail();
            ImVec2 TextSize  = ImGui::CalcTextSize("Compile to see shader");
            ImGui::SetCursorPos(ImVec2((Available.x - TextSize.x) * 0.5f, (Available.y - TextSize.y) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.65f, 1.0f));
            ImGui::TextUnformatted("Compile to see shader");
            ImGui::PopStyleColor();
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
        ImGui::BeginChild("##shader_preview", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
        ImGui::TextUnformatted("Compute Shader Source");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted(CompiledSource.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    void FParticleSystemEditorTool::Compile()
    {
        CompilationResult = FCompilationResultInfo();
        CParticleSystem* PS = Cast<CParticleSystem>(Asset.Get());

        FParticleCompiler Compiler;
        NodeGraph->CompileGraph(Compiler);

        if (Compiler.HasErrors())
        {
            for (const EdNodeGraph::FError& Error : Compiler.GetErrors())
            {
                CompilationResult.CompilationLog += "ERROR - [" + Error.Name + "]: " + Error.Description + "\n";
            }
            CompilationResult.bIsError = true;
            return;
        }

        CompiledSource = Compiler.BuildShader();

        if (CompiledSource.empty())
        {
            CompilationResult.CompilationLog = "Failed to build shader source.";
            CompilationResult.bIsError = true;
            return;
        }

        IShaderCompiler* ShaderCompiler = GRenderContext->GetShaderCompiler();

        ShaderCompiler->CompilerShaderRaw(CompiledSource, {}, [this](const FShaderHeader& Header) mutable
        {
            CParticleSystem* PS = Cast<CParticleSystem>(Asset.Get());
            PS->ComputeShader = GRenderContext->CreateComputeShader(Header);
            PS->ComputeShaderBinaries.assign(Header.Binaries.begin(), Header.Binaries.end());
            GRenderContext->OnShaderCompiled(PS->ComputeShader, false, true);
        });

        ShaderCompiler->Flush();

        PS->PostLoad();
        PS->GetPackage()->MarkDirty();
    }

    void FParticleSystemEditorTool::OnSave()
    {
        FAssetEditorTool::OnSave();
    }

    void FParticleSystemEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGuiID LeftDockID = 0, RightDockID = 0, RightBottomDockID = 0;
        
        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.3f, &RightDockID, &LeftDockID);
        ImGui::DockBuilderSplitNode(RightDockID, ImGuiDir_Down, 0.3f, &RightBottomDockID, &RightDockID);
        
        ImGui::DockBuilderDockWindow(GetToolWindowName(ParticleGraphName).c_str(), LeftDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(), RightDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(ShaderPreviewName).c_str(), LeftDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(ParticlePropertiesName).c_str(), RightBottomDockID);
    }
}
