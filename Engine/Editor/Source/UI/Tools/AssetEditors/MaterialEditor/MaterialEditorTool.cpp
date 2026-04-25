#include "MaterialEditorTool.h"
#include "Assets/AssetTypes/Material/Material.h"
#include "Assets/AssetTypes/Textures/Texture.h"
#include "Core/Engine/Engine.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Class.h"
#include "Core/Object/Package/Package.h"
#include "Paths/Paths.h"
#include "Platform/Filesystem/FileHelper.h"
#include "Renderer/MaterialTypes.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/ShaderCompiler.h"
#include "Thumbnails/ThumbnailManager.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "UI/Tools/NodeGraph/Material/MaterialCompiler.h"
#include "UI/Tools/NodeGraph/Material/MaterialNodeGraph.h"
#include "world/entity/components/environmentcomponent.h"
#include "World/entity/components/lightcomponent.h"
#include "World/entity/components/staticmeshcomponent.h"

namespace Lumina
{
    static const char* MaterialGraphName           = "Material Graph";
    static const char* MaterialPropertiesName      = "Material Properties";
    static const char* GLSLPreviewName             = "GLSL Preview";

    FMaterialEditorTool::FMaterialEditorTool(IEditorToolContext* Context, CObject* InAsset)
        : FAssetEditorTool(Context, InAsset->GetName().c_str(), InAsset, NewObject<CWorld>())
        , MeshEntity()
        , DirectionalLightEntity()
        , CompilationResult()
        , NodeGraph(nullptr)
    {
    }

    
    void FMaterialEditorTool::OnInitialize()
    {
        FAssetEditorTool::OnInitialize();
        
        CreateToolWindow(MaterialGraphName, [&](bool bFocused)
        {
            DrawMaterialGraph();
        });

        CreateToolWindow(MaterialPropertiesName, [&](bool bFocused)
        {
            DrawMaterialProperties();
        });

        CreateToolWindow(GLSLPreviewName, [&](bool bFocused)
        {
            DrawGLSLPreview();
        });

        FString GraphName = "AssetMaterialGraph";
        NodeGraph = Cast<CMaterialNodeGraph>(Asset->GetPackage()->LoadObjectByName(GraphName));
        
        if (NodeGraph == nullptr)
        {
            NodeGraph = NewObject<CMaterialNodeGraph>(Asset->GetPackage(), GraphName);
        }
        
        NodeGraph->SetMaterial(Cast<CMaterial>(Asset.Get()));
        NodeGraph->Initialize();
        NodeGraph->SetNodeSelectedCallback( [this] (CEdGraphNode* Node)
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
    
    void FMaterialEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
        if (NodeGraph)
        {
            NodeGraph->Shutdown();
            NodeGraph = nullptr;
        }
    }

    void FMaterialEditorTool::SetupWorldForTool()
    {
        FAssetEditorTool::SetupWorldForTool();

        DirectionalLightEntity = World->ConstructEntity("Directional Light");
        World->GetEntityRegistry().emplace<SDirectionalLightComponent>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SEnvironmentComponent>(DirectionalLightEntity);
        
        MeshEntity = World->ConstructEntity("MeshEntity");
        SStaticMeshComponent& StaticMeshComponent = World->GetEntityRegistry().emplace<SStaticMeshComponent>(MeshEntity);
        StaticMeshComponent.StaticMesh = CThumbnailManager::Get().SphereMesh;

        STransformComponent& MeshTransform = World->GetEntityRegistry().get<STransformComponent>(MeshEntity);
        
        STransformComponent& EditorTransform = World->GetEntityRegistry().get<STransformComponent>(EditorEntity);
        glm::quat Rotation = Math::FindLookAtRotation(MeshTransform.GetLocation(), EditorTransform.GetLocation());
        EditorTransform.SetRotation(Rotation);
        
        StaticMeshComponent.MaterialOverrides.push_back(CastAsserted<CMaterialInterface>(Asset.Get()));
    }

    void FMaterialEditorTool::DrawHelpMenu()
    {
        ImGui::TableNextRow();
        
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Debug Node IDs");

        ImGui::TableNextColumn();
        ImGui::Checkbox("##DebugID", &NodeGraph->bDebug);
    }

    bool FMaterialEditorTool::DrawViewport(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture)
    {
        const ImVec2 ViewportSize(eastl::max(ImGui::GetContentRegionAvail().x, 64.0f), eastl::max(ImGui::GetContentRegionAvail().y, 64.0f));
        const ImVec2 WindowPosition = ImGui::GetWindowPos();
        const ImVec2 WindowBottomRight = { WindowPosition.x + ViewportSize.x, WindowPosition.y + ViewportSize.y };
        float AspectRatio = (ViewportSize.x / ViewportSize.y);
        
        SCameraComponent* CameraComponent =  World->GetActiveCamera();
        CameraComponent->SetAspectRatio(AspectRatio);
        CameraComponent->SetFOV(60.0f);
        
        /** Mostly for debug, so we can easily see if there's some transparency issue */
        ImGui::GetWindowDrawList()->AddRectFilled(WindowPosition, WindowBottomRight, IM_COL32(0, 0, 0, 255));
        
        if (bViewportHovered)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            {
                ImGui::SetWindowFocus();
                bViewportFocused = true;
            }
        }

        ImVec2 CursorScreenPos = ImGui::GetCursorScreenPos();
        
        ImGui::GetWindowDrawList()->AddImage(
            ViewportTexture,
            CursorScreenPos,
            ImVec2(CursorScreenPos.x + ViewportSize.x, CursorScreenPos.y + ViewportSize.y),
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32_WHITE
        );

        const ImGuiStyle& ImStyle = ImGui::GetStyle();

        ImGui::Dummy(ImStyle.ItemSpacing);
        ImGui::SetCursorPos(ImStyle.ItemSpacing);
        DrawViewportOverlayElements(UpdateContext, ViewportTexture, ViewportSize);

        ImGui::Dummy(ImStyle.ItemSpacing);
        ImGui::SetCursorPos(ImStyle.ItemSpacing);
        DrawViewportToolbar(UpdateContext);
        
        if (ImGuiDockNode* pDockNode = ImGui::GetWindowDockNode())
        {
           pDockNode->LocalFlags = 0;
           pDockNode->LocalFlags |= ImGuiDockNodeFlags_NoDockingOverMe;
           pDockNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        }

        return false;
    }

    void FMaterialEditorTool::OnAssetLoadFinished()
    {
    }
    
    void FMaterialEditorTool::DrawToolMenu(const FUpdateContext& UpdateContext)
    {
        if (ImGui::MenuItem(LE_ICON_RECEIPT_TEXT" Compile"))
        {
            Compile();
        }
    }

    void FMaterialEditorTool::DrawMaterialGraph()
    {
        NodeGraph->DrawGraph();
    }

    void FMaterialEditorTool::DrawMaterialProperties()
    {
        GetPropertyTable()->DrawTree();
    }
    
    void FMaterialEditorTool::DrawGLSLPreview()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
    
        if (CompilationResult.bIsError)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
            ImGui::BeginChild("##error_preview", ImVec2(0, 0), true);
            
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f)); // Red for errors
            ImGui::TextUnformatted(CompilationResult.CompilationLog.c_str());
            ImGui::PopStyleColor();
    
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }
    
        if (Tree.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
            ImGui::BeginChild("##empty_preview", ImVec2(0, 0), true);
    
            ImVec2 available = ImGui::GetContentRegionAvail();
            ImVec2 textSize = ImGui::CalcTextSize("Compile to see preview");
            ImGui::SetCursorPos(ImVec2(
                (available.x - textSize.x) * 0.5f,
                (available.y - textSize.y) * 0.5f
            ));
    
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.65f, 1.0f));
            ImGui::TextUnformatted("Compile to see preview");
            ImGui::PopStyleColor();
    
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }
    
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
    
        ImGui::BeginChild("##glsl_preview", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
        ImGui::TextUnformatted("GLSL Shader Tree");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        
        if (ReplacementStart != std::string::npos && ReplacementEnd != std::string::npos)
        {
            FString BeforeReplacement = Tree.substr(0, ReplacementStart);
            FString ReplacedCode = Tree.substr(ReplacementStart, ReplacementEnd - ReplacementStart);
            FString AfterReplacement = Tree.substr(ReplacementEnd);
    
            if (!BeforeReplacement.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.92f, 1.0f));
                ImGui::TextUnformatted(BeforeReplacement.c_str());
                ImGui::PopStyleColor();
            }
    
            if (!ReplacedCode.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.5f, 1.0f)); // Bright green
                ImGui::TextUnformatted(ReplacedCode.c_str());
                ImGui::PopStyleColor();
            }
    
            if (!AfterReplacement.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.92f, 1.0f));
                ImGui::TextUnformatted(AfterReplacement.c_str());
                ImGui::PopStyleColor();
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.92f, 1.0f));
            ImGui::TextUnformatted(Tree.c_str());
            ImGui::PopStyleColor();
        }
    
        ImGui::PopFont();
    
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    
        ImGui::PopStyleVar(2);
    }

    void FMaterialEditorTool::Compile()
    {
        CompilationResult = FCompilationResultInfo();
        CMaterial* Material = Cast<CMaterial>(Asset.Get());

        FMaterialCompiler Compiler;
        Compiler.SetMaterialType(Material->GetMaterialType());
        NodeGraph->CompileGraph(Compiler);
        Material->SetReadyForRender(false);

        if (Compiler.HasErrors())
        {
            for (const EdNodeGraph::FError& Error : Compiler.GetErrors())
            {
                CompilationResult.CompilationLog += "ERROR - [" + Error.Name + "]: " + Error.Description + "\n";
            }
                
            CompilationResult.bIsError = true;
            bGLSLPreviewDirty = true;
        }
        else
        {
            Tree = Compiler.BuildTree(ReplacementStart, ReplacementEnd, Material->GetMaterialType());
            CompilationResult.CompilationLog = "Generated GLSL: \n \n \n";
            CompilationResult.bIsError = false;
            bGLSLPreviewDirty = true;

            IShaderCompiler* ShaderCompiler = GRenderContext->GetShaderCompiler();

            const bool bIsTerrainMaterial = Material->GetMaterialType() == EMaterialType::Terrain;
            FString VertexPath = Paths::GetEngineResourceDirectory() + "/Shaders/MaterialShader/"
                + (bIsTerrainMaterial ? "TerrainBaseVertexPass.slang" : "BaseVertexPass.slang");
            FString LoadedVertexString;
            if (!FileHelper::LoadFileIntoString(LoadedVertexString, VertexPath))
            {
                LOG_ERROR("Failed to find BaseVertPass.slang!");
                return;
            }
            
            ShaderCompiler->CompilerShaderRaw(LoadedVertexString, {}, [this](const FShaderHeader& Header) mutable 
            {
                CMaterial* Material = Cast<CMaterial>(Asset.Get());
                FRHIVertexShaderRef VertexShader = GRenderContext->CreateVertexShader(Header);
                Material->VertexShaderBinaries.assign(Header.Binaries.begin(), Header.Binaries.end());
                Material->VertexShader = VertexShader;
                GRenderContext->OnShaderCompiled(VertexShader, false, true);
            });
            
            FShaderCompileOptions Options;
            if (Material->GetBlendMode() == EBlendMode::Translucent)
            {
                Options.MacroDefinitions.emplace_back("TRANSLUCENT");
            }
            if (Material->GetShadingModel() == EMaterialShadingModel::Unlit)
            {
                Options.MacroDefinitions.emplace_back("UNLIT");
            }
            
            ShaderCompiler->CompilerShaderRaw(Tree, Move(Options), [this](const FShaderHeader& Header) mutable 
            {
                CMaterial* Material = Cast<CMaterial>(Asset.Get());
                FRHIPixelShaderRef PixelShader = GRenderContext->CreatePixelShader(Header);
                Material->PixelShaderBinaries.assign(Header.Binaries.begin(), Header.Binaries.end());
                Material->PixelShader = PixelShader;
                GRenderContext->OnShaderCompiled(PixelShader, false, true);
            });
            
            
            ShaderCompiler->Flush();

            Compiler.GetBoundTextures(Material->Textures);

            Memory::Memzero(&Material->MaterialUniforms, sizeof(FMaterialUniforms));
            Material->Parameters.clear();

            Compiler.GetParameters(Material->Parameters, Material->MaterialUniforms);

            Material->PostLoad();
            Material->GetPackage()->MarkDirty();
        }
    }

    void FMaterialEditorTool::OnSave()
    {
        FAssetEditorTool::OnSave();
    }

    void FMaterialEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGuiID leftDockID = 0, rightDockID = 0;
        ImGuiID rightBottomDockID = 0;

        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.3f, &rightDockID, &leftDockID);

        ImGui::DockBuilderSplitNode(rightDockID, ImGuiDir_Down, 0.3f, &rightBottomDockID, &rightDockID);

        ImGui::DockBuilderDockWindow(GetToolWindowName(MaterialGraphName).c_str(), leftDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(), rightDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(GLSLPreviewName).c_str(), rightBottomDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(MaterialPropertiesName).c_str(), rightBottomDockID);
    }
}
