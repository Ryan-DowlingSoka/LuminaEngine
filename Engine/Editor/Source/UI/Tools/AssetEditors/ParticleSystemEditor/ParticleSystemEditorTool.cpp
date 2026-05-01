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
            DrawPropertyBindings();
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

    namespace
    {
        struct FBindableProperty
        {
            const char*             PropertyName;
            const char*             Category;
            EParticleParameterType  ExpectedType;
        };

        static const FBindableProperty GBindableProperties[] =
        {
            { "SpawnRate",              "Simulation",     EParticleParameterType::Float },
            { "BurstCount",             "Simulation",     EParticleParameterType::Int   },
            { "Duration",               "Simulation",     EParticleParameterType::Float },
            { "bLooping",               "Simulation",     EParticleParameterType::Bool  },

            { "ShapeSize",              "Emitter Shape",  EParticleParameterType::Vec3  },
            { "ShapeAngle",             "Emitter Shape",  EParticleParameterType::Float },

            { "VelocityMin",            "Velocity",       EParticleParameterType::Vec3  },
            { "VelocityMax",            "Velocity",       EParticleParameterType::Vec3  },
            { "SpeedRange",             "Velocity",       EParticleParameterType::Vec2  },

            { "LifetimeRange",          "Lifetime",       EParticleParameterType::Vec2  },

            { "Gravity",                "Physics",        EParticleParameterType::Vec3  },
            { "Drag",                   "Physics",        EParticleParameterType::Float },
            { "InheritEmitterVelocity", "Physics",        EParticleParameterType::Float },

            { "StartColor",             "Color",          EParticleParameterType::Color },
            { "EndColor",               "Color",          EParticleParameterType::Color },

            { "StartSizeRange",         "Size",           EParticleParameterType::Vec2  },
            { "EndSizeRange",           "Size",           EParticleParameterType::Vec2  },

            { "RotationRange",          "Rotation",       EParticleParameterType::Vec2  },
            { "RotationSpeedRange",     "Rotation",       EParticleParameterType::Vec2  },

            { "NoiseStrength",          "Noise",          EParticleParameterType::Vec3  },
            { "NoiseScale",             "Noise",          EParticleParameterType::Float },
            { "NoiseSpeed",             "Noise",          EParticleParameterType::Float },

            { "bBillboardToCamera",     "Render",         EParticleParameterType::Bool  },
            { "bWriteDepth",            "Render",         EParticleParameterType::Bool  },
        };

        static const char* ParameterTypeLabel(EParticleParameterType T)
        {
            switch (T)
            {
            case EParticleParameterType::Float: return "Float";
            case EParticleParameterType::Int:   return "Int";
            case EParticleParameterType::Bool:  return "Bool";
            case EParticleParameterType::Vec2:  return "Vec2";
            case EParticleParameterType::Vec3:  return "Vec3";
            case EParticleParameterType::Vec4:  return "Vec4";
            case EParticleParameterType::Color: return "Color";
            }
            return "?";
        }

        static bool IsCompatible(EParticleParameterType Expected, EParticleParameterType Actual)
        {
            if (Expected == Actual) return true;
            // Color and Vec4 share storage and are interchangeable as drivers.
            if ((Expected == EParticleParameterType::Color && Actual == EParticleParameterType::Vec4) ||
                (Expected == EParticleParameterType::Vec4  && Actual == EParticleParameterType::Color))
            {
                return true;
            }
            return false;
        }
    }

    void FParticleSystemEditorTool::DrawPropertyBindings()
    {
        CParticleSystem* PS = Cast<CParticleSystem>(Asset.Get());
        if (PS == nullptr)
        {
            return;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

        ImGui::TextDisabled("Bind any built-in property to a user parameter so scripts and C++ can drive it at runtime.");
        ImGui::Separator();

        const char* CurrentCategory = nullptr;
        for (const FBindableProperty& Prop : GBindableProperties)
        {
            if (CurrentCategory == nullptr || strcmp(CurrentCategory, Prop.Category) != 0)
            {
                CurrentCategory = Prop.Category;
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 222, 255));
                ImGui::TextUnformatted(Prop.Category);
                ImGui::PopStyleColor();
                ImGui::Separator();
            }

            ImGui::PushID(Prop.PropertyName);

            const FName PropName(Prop.PropertyName);
            const FName BoundParam = PS->GetPropertyBinding(PropName);

            const float NameWidth = 180.0f;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Prop.PropertyName);
            ImGui::SameLine(NameWidth);

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(140, 140, 145, 255));
            ImGui::TextUnformatted(ParameterTypeLabel(Prop.ExpectedType));
            ImGui::PopStyleColor();
            ImGui::SameLine(NameWidth + 60.0f);

            const char* PreviewLabel = BoundParam.IsNone() ? "(literal)" : BoundParam.c_str();
            const float ComboWidth = ImGui::GetContentRegionAvail().x - 28.0f;
            ImGui::PushItemWidth(ComboWidth);
            if (ImGui::BeginCombo("##Bind", PreviewLabel))
            {
                if (ImGui::Selectable("(literal)", BoundParam.IsNone()))
                {
                    PS->ClearPropertyBinding(PropName);
                    PS->GetPackage()->MarkDirty();
                }

                for (const FParticleParameter& Param : PS->UserParameters)
                {
                    if (Param.Name.IsNone()) continue;
                    const bool bCompat = IsCompatible(Prop.ExpectedType, Param.Type);
                    if (!bCompat) continue;

                    const bool bSelected = (BoundParam == Param.Name);
                    if (ImGui::Selectable(Param.Name.c_str(), bSelected))
                    {
                        PS->SetPropertyBinding(PropName, Param.Name);
                        PS->GetPackage()->MarkDirty();
                    }
                }

                bool bAnyMismatch = false;
                for (const FParticleParameter& Param : PS->UserParameters)
                {
                    if (!Param.Name.IsNone() && !IsCompatible(Prop.ExpectedType, Param.Type))
                    {
                        bAnyMismatch = true;
                        break;
                    }
                }
                if (bAnyMismatch)
                {
                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(140, 140, 145, 255));
                    ImGui::TextUnformatted("Incompatible:");
                    ImGui::PopStyleColor();
                    for (const FParticleParameter& Param : PS->UserParameters)
                    {
                        if (Param.Name.IsNone() || IsCompatible(Prop.ExpectedType, Param.Type)) continue;
                        ImGui::BeginDisabled();
                        char Label[160];
                        snprintf(Label, sizeof(Label), "%s (%s)", Param.Name.c_str(), ParameterTypeLabel(Param.Type));
                        ImGui::Selectable(Label, false);
                        ImGui::EndDisabled();
                    }
                }

                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();

            ImGui::SameLine();
            ImGui::BeginDisabled(BoundParam.IsNone());
            if (ImGui::Button("X", ImVec2(22, 0)))
            {
                PS->ClearPropertyBinding(PropName);
                PS->GetPackage()->MarkDirty();
            }
            ImGui::EndDisabled();

            ImGui::PopID();
        }

        ImGui::PopStyleVar(2);
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
