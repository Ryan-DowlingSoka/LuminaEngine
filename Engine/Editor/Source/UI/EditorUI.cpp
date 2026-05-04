#include "EditorUI.h"
#include <cfloat>
#include <filesystem>
#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_internal.h>
#include <Lumina.h>
#include <string.h>
#include <Windows.h>
#include <Assets/AssetRegistry/AssetData.h>
#include <Containers/Array.h>
#include <Containers/Function.h>
#include <Containers/String.h>
#include <Core/UpdateContext.h>
#include <Core/Assertions/Assert.h>
#include <Core/Engine/Engine.h>
#include <Core/Math/Math.h>
#include <Core/Math/Transform.h>
#include <Core/Object/ObjectArray.h>
#include <Core/Object/ObjectCore.h>
#include <Core/Object/ObjectFlags.h>
#include <Core/Templates/LuminaTemplate.h>
#include <EASTL/algorithm.h>
#include <EASTL/fixed_string.h>
#include <EASTL/string.h>
#include <Events/Event.h>
#include <FileSystem/FileSystem.h>
#include <GLFW/glfw3.h>
#include <glm/fwd.hpp>
#include <GUID/GUID.h>
#include <Memory/SmartPtr.h>
#include <Paths/Paths.h>
#include <Platform/GenericPlatform.h>
#include <Renderer/Shader.h>
#include <Tools/Screenshot/ScreenshotCapture.h>
#include <World/World.h>
#include "implot.h"
#include "lstate.h"
#include "LuminaEditor.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/Material/Material.h"
#include "Assets/AssetTypes/Material/MaterialInstance.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "Assets/AssetTypes/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Assets/AssetTypes/Mesh/Skeleton/Skeleton.h"
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Assets/AssetTypes/ParticleSystem/ParticleSystem.h"
#include "Assets/AssetTypes/Prefabs/Prefab.h"
#include "Assets/AssetTypes/Textures/Texture.h"
#include "Config/Config.h"
#include "Core/Application/Application.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Class.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectIterator.h"
#include "Core/Object/Package/Package.h"
#include "Core/Profiler/Profile.h"
#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"
#include "EASTL/sort.h"
#include "Input/InputProcessor.h"
#include "Memory/Memory.h"
#include "Platform/Process/PlatformProcess.h"
#include "Properties/Customizations/CoreTypeCustomization.h"
#include "Properties/Customizations/CustomPrimitiveDataCustomization.h"
#include "Tools/AssetEditors/ParticleSystemEditor/ParticleParameterCustomization.h"
#include "Properties/Customizations/ScriptComponentCustomization.h"
#include "Renderer/CustomPrimitiveData.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderDocImpl.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/ShaderCompiler.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "Thumbnails/ThumbnailManager.h"
#include "Tools/ConsoleLogEditorTool.h"
#include "Tools/ContentBrowserEditorTool.h"
#include "Tools/EditorTool.h"
#include "Tools/LuaDebuggerEditorTool.h"
#include "Tools/CPUProfilerEditorTool.h"
#include "Tools/GPUProfilerEditorTool.h"
#include "Tools/ShadowAtlasEditorTool.h"
#include "Tools/EditorToolModal.h"
#include "Tools/GamePreviewTool.h"
#include "Tools/ToolFlags.h"
#include "Tools/WorldEditorTool.h"
#include "Tools/Debug/AboutEditorTool.h"
#include "Tools/Debug/MemoryProfilerEditorTool.h"
#include "Tools/Debug/ObjectBrowserEditorTool.h"
#include "Tools/Debug/InputActionEditorTool.h"
#include "Tools/Debug/ProjectPackagerEditorTool.h"
#include "Tools/Debug/ProjectSettingsEditorTool.h"
#include "Tools/Debug/RendererInfoEditorTool.h"
#include "Tools/Debug/ScriptsInfoEditorTool.h"
#include "Tools/AssetEditors/Animation/AnimationEditorTool.h"
#include "Tools/AssetEditors/MaterialEditor/MaterialEditorTool.h"
#include "Tools/AssetEditors/MaterialEditor/MaterialInstanceEditorTool.h"
#include "Tools/AssetEditors/MeshEditor/MeshEditorTool.h"
#include "Tools/AssetEditors/MeshEditor/SkeletalMeshEditorTool.h"
#include "Tools/AssetEditors/MeshEditor/SkeletonEditorTool.h"
#include "Tools/AssetEditors/ParticleSystemEditor/ParticleSystemEditorTool.h"
#include "Tools/AssetEditors/PrefabEditor/PrefabEditorTool.h"
#include "Tools/AssetEditors/LuaEditor/LuaEditorTool.h"
#include "Tools/AssetEditors/RmlUiEditor/RmlUiEditorTool.h"
#include "Tools/AssetEditors/TextureEditor/TextureEditorTool.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiRenderer.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "World/Scene/RenderScene/RenderScene.h"

namespace Lumina
{
    bool FEditorUI::OnEvent(FEvent& Event)
    {
        // Consume input ImGui owns so it doesn't fall through; pass everything
        // else to tools (file drops, etc.).
        const bool bIsMouseEvent =
               Event.IsA<FMouseMovedEvent>()
            || Event.IsA<FMouseButtonPressedEvent>()
            || Event.IsA<FMouseButtonReleasedEvent>()
            || Event.IsA<FMouseScrolledEvent>();

        const bool bIsKeyEvent =
               Event.IsA<FKeyPressedEvent>()
            || Event.IsA<FKeyReleasedEvent>()
            || Event.IsA<FCharInputEvent>();

        if (bIsMouseEvent || bIsKeyEvent)
        {
            const ImGuiIO& IO = ImGui::GetIO();
            if (bIsMouseEvent && IO.WantCaptureMouse)
            {
                return true;
            }
            if (bIsKeyEvent && IO.WantCaptureKeyboard)
            {
                return true;
            }
        }

        for (FEditorTool* Tool : EditorTools)
        {
            if (Tool->OnEvent(Event))
            {
                return true;
            }
        }

        return false;
    }

    void FEditorUI::Initialize(const FUpdateContext& UpdateContext)
    {
        ImGuiContext* Context = GRenderManager->GetImGuiRenderer()->GetImGuiContext();
        ImPlotContext* PlotContext = GRenderManager->GetImGuiRenderer()->GetImPlotContext();
        ImGui::SetCurrentContext(Context);
        ImPlot::SetCurrentContext(PlotContext);

        // Force ThumbnailManager init before any world load so engine primitive
        // meshes (Cube/Sphere/etc.) are already in the transient package and
        // resolvable when a saved world's mesh imports are deserialized.
        (void)CThumbnailManager::Get();

        PropertyCustomizationRegistry = Memory::New<FPropertyCustomizationRegistry>();
        PropertyCustomizationRegistry->RegisterPropertyCustomization(TBaseStructure<glm::vec2>::Get()->GetName(), []
        {
            return FVec2PropertyCustomization::MakeInstance();
        });
        PropertyCustomizationRegistry->RegisterPropertyCustomization(TBaseStructure<glm::vec3>::Get()->GetName(), []
        {
            return FVec3PropertyCustomization::MakeInstance();
        });
        PropertyCustomizationRegistry->RegisterPropertyCustomization(TBaseStructure<glm::vec4>::Get()->GetName(), []
        {
            return FVec4PropertyCustomization::MakeInstance();
        });
        PropertyCustomizationRegistry->RegisterPropertyCustomization(TBaseStructure<glm::quat>::Get()->GetName(), []
        {
            return FVec3PropertyCustomization::MakeInstance();
        });
        PropertyCustomizationRegistry->RegisterPropertyCustomization(TBaseStructure<FTransform>::Get()->GetName(), []
        {
            return FTransformPropertyCustomization::MakeInstance();
        });
        PropertyCustomizationRegistry->RegisterPropertyCustomization(SScriptComponent::StaticStruct()->GetName(), []
        {
           return FScriptComponentPropertyCustomization::MakeInstance(); 
        });
        
        PropertyCustomizationRegistry->RegisterPropertyCustomization(SCustomPrimitiveData::StaticStruct()->GetName(), []
        {
           return FCustomPrimDataPropertyCustomization::MakeInstance();
        });

        PropertyCustomizationRegistry->RegisterPropertyCustomization(FParticleParameter::StaticStruct()->GetName(), []
        {
           return FParticleParameterCustomization::MakeInstance();
        });
        
        EditorWindowClass.ClassId                       = ImHashStr("EditorWindowClass");
        EditorWindowClass.DockingAllowUnclassed         = false;
        EditorWindowClass.ViewportFlagsOverrideSet      = ImGuiViewportFlags_NoAutoMerge;
        EditorWindowClass.ViewportFlagsOverrideClear    = ImGuiViewportFlags_NoTaskBarIcon;
        EditorWindowClass.ParentViewportId              = 0; // Top level window
        EditorWindowClass.DockingAlwaysTabBar           = true;

        WorldEditorTool = CreateTool<FWorldEditorTool>(this, NewObject<CWorld>(nullptr, "Transient World", FGuid::New(), OF_Transient));
        ConsoleLogTool = CreateTool<FConsoleLogEditorTool>(this);
        ContentBrowser = CreateTool<FContentBrowserEditorTool>(this);
        
        if (GEditorEngine->GetProjectName().empty())
        {
            OpenProjectDialog();
        }
    }

    void FEditorUI::Deinitialize(const FUpdateContext& UpdateContext)
    {
        while (!EditorTools.empty())
        {
            // Pops internally.
            DestroyTool(UpdateContext, EditorTools[0]);
        }
        
        WorldEditorTool = nullptr;
        ConsoleLogTool = nullptr;
        ImGui::SetCurrentContext(nullptr);
    }

    void FEditorUI::OnStartFrame(const FUpdateContext& UpdateContext)
    {
        LUMINA_PROFILE_SCOPE();
        ImGuizmo::BeginFrame();

        auto TitleBarLeftContents = [this, &UpdateContext] ()
        {
            DrawTitleBarMenu(UpdateContext);
        };

        auto TitleBarRightContents = [this, &UpdateContext] ()
        {
            DrawTitleBarInfoStats(UpdateContext);
        };

        TitleBar.Draw(TitleBarLeftContents, 400, TitleBarRightContents, 230);
        
        const ImGuiID DockspaceID = ImGui::GetID("EditorDockSpace");

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        constexpr ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("EditorDockSpaceWindow", nullptr, WindowFlags);
        
        ImGui::PopStyleVar(3);
        {
            if (!ImGui::DockBuilderGetNode(DockspaceID))
            {
                ImGui::DockBuilderAddNode(DockspaceID, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(DockspaceID, ImGui::GetContentRegionAvail());

                ImGuiID TopDockID = 0, BottomDockID = 0;
                ImGui::DockBuilderSplitNode(DockspaceID, ImGuiDir_Down, 0.3f, &BottomDockID, &TopDockID);
                
                ImGui::DockBuilderFinish(DockspaceID);

                ImGui::DockBuilderDockWindow(WorldEditorTool->GetToolName().c_str(), TopDockID);
                ImGui::DockBuilderDockWindow(ContentBrowser->GetToolName().c_str(), BottomDockID);
                ImGui::DockBuilderDockWindow(ConsoleLogTool->GetToolName().c_str(), BottomDockID);
            }

            // Create the actual dock space
            ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0);
            ImGui::DockSpace(DockspaceID, viewport->WorkSize, 0, &EditorWindowClass);
            ImGui::PopStyleVar();
        }
        
        
        ImGui::End();
        
        if (ImGui::IsKeyPressed(ImGuiKey_F5))
        {
            CMaterial::CreateDefaultMaterial();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_F9, false))
        {
            const auto Source = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)
                ? Screenshot::ECaptureSource::SceneHDR
                : Screenshot::ECaptureSource::FinalLDR;
            Screenshot::CaptureActiveWorld(Source);
        }


        if (!FocusTargetWindowName.empty())
        {
            ImGuiWindow* Window = ImGui::FindWindowByName(FocusTargetWindowName.c_str());
            if (Window == nullptr || Window->DockNode == nullptr || Window->DockNode->TabBar == nullptr)
            {
                FocusTargetWindowName.clear();
                return;
            }

            ImGuiID TabID = 0;
            for (int i = 0; i < Window->DockNode->TabBar->Tabs.size(); ++i)
            {
                ImGuiTabItem* pTab = &Window->DockNode->TabBar->Tabs[i];
                if (pTab->Window->ID == Window->ID)
                {
                    TabID = pTab->ID;
                    break;
                }
            }

            if (TabID != 0)
            {
                Window->DockNode->TabBar->NextSelectedTabId = TabID;
                ImGui::SetWindowFocus(FocusTargetWindowName.c_str());
            }

            FocusTargetWindowName.clear();
            
        }
        
        if (bShowDearImGuiDemoWindow)
        {
            ImGui::ShowDemoWindow(&bShowDearImGuiDemoWindow);
        }

        if (bShowImGuiStyleEditor)
        {
            ImGui::ShowStyleEditor();
        }

        if (bShowImPlotDemoWindow)
        {
            ImPlot::ShowDemoWindow(&bShowImPlotDemoWindow);
        }

        if (GEngine->IsCloseRequested())
        {
            static bool IsVerifyingPackages = false;
            if (IsVerifyingPackages == false)
            {
                IsVerifyingPackages = true;
                VerifyDirtyPackages();
            }

            if (ModalManager.HasModal())
            {
                GEngine->SetEngineReadyToClose(false);
            }
        }
        
        FEditorTool* ToolToClose = nullptr;
        
        for (FEditorTool* Tool : EditorTools)
        {
            if (!SubmitToolMainWindow(UpdateContext, Tool, DockspaceID))
            {
                ToolToClose = Tool;
            }
        }

        for (FEditorTool* Tool : EditorTools)
        {
            if (Tool == ToolToClose)
            {
                continue;
            }
            
            DrawToolContents(UpdateContext, Tool);
        }

        
        if (ToolToClose)
        {
            ToolsPendingDestroy.push(ToolToClose);
        }

        while (!ToolsPendingDestroy.empty())
        {
            FEditorTool* Tool = ToolsPendingDestroy.front();
            ToolsPendingDestroy.pop();

            DestroyTool(UpdateContext, Tool);
        }
        
        while (!ToolsPendingAdd.empty())
        {
            FEditorTool* NewTool = ToolsPendingAdd.front();
            ToolsPendingAdd.pop();

            EditorTools.push_back(NewTool);
        }
        
        ModalManager.DrawDialogue();
    }

    void FEditorUI::OnUpdate(const FUpdateContext& UpdateContext)
    {
        LUMINA_PROFILE_SCOPE();

        // Auto-open + focus the script editor whenever the debugger pauses
        // at a new source. Reacting to source changes (rather than just the
        // running→paused transition) means a Step Into that crosses into a
        // different file follows the user with a tab switch. The inline
        // debugger panel lives inside FLuaEditorTool, so opening the file
        // is enough to get the toolbar, call stack, and locals on screen.
        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
        if (Debugger.IsPaused())
        {
            const FStringView Source = Debugger.GetPausedSource();
            const FStringView Last(LuaDebuggerLastOpenedSource.c_str(), LuaDebuggerLastOpenedSource.size());
            if (!Source.empty() && Source != Last)
            {
                // OpenFileEditor — not OpenScriptEditor — routes .lua to the
                // in-engine FLuaEditorTool. OpenScriptEditor calls LaunchURL
                // and would shell out to VS Code instead.
                OpenFileEditor(Source);

                // Drive a real tab switch via the FocusTargetWindowName
                // pipeline that runs in OnStartFrame. Plain ImGui::SetWindowFocus
                // doesn't always raise a docked tab to the foreground, but the
                // pipeline explicitly sets DockNode->TabBar->NextSelectedTabId.
                FString Key(Source.data(), Source.size());
                auto Itr = ActiveFileTools.find(Key);
                if (Itr != ActiveFileTools.end())
                {
                    FocusTargetWindowName = Itr->second->GetToolName();
                }

                LuaDebuggerLastOpenedSource.assign(Source.data(), Source.size());
            }
        }
        else
        {
            // Reset on resume so the next pause — even on the same file —
            // re-fires the focus.
            LuaDebuggerLastOpenedSource.clear();
        }

        for (FEditorTool* Tool : EditorTools)
        {
            if (Tool->HasWorld())
            {
                Tool->WorldUpdate(UpdateContext);
            }
        }
    }

    void FEditorUI::OnEndFrame(const FUpdateContext& UpdateContext)
    {
        LUMINA_PROFILE_SCOPE();

        for (FEditorTool* Tool : EditorTools)
        {
            Tool->EndFrame();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_LeftShift) && ImGui::IsKeyPressed(ImGuiKey_F1))
        {
            FInputProcessor::Get().SetMouseMode(EMouseMode::Normal);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && GamePreviewTool != nullptr)
        {
            WorldEditorTool->GetOnPreviewStopRequestedDelegate().Broadcast();
        }
    }
    
    void FEditorUI::DestroyTool(const FUpdateContext& UpdateContext, FEditorTool* Tool)
    {
        auto Itr = eastl::find(EditorTools.begin(), EditorTools.end(), Tool);
        ASSERT(Itr != EditorTools.end());

        EditorTools.erase(Itr);
        
        for (auto MapItr = ActiveAssetTools.begin(); MapItr != ActiveAssetTools.end(); ++MapItr)
        {
            if (MapItr->second == Tool)
            {
                ActiveAssetTools.erase(MapItr);
                break;
            }
        }

        for (auto MapItr = ActiveFileTools.begin(); MapItr != ActiveFileTools.end(); ++MapItr)
        {
            if (MapItr->second == Tool)
            {
                ActiveFileTools.erase(MapItr);
                break;
            }
        }

        if (Tool == GamePreviewTool)
        {
            WorldEditorTool->NotifyPlayInEditorStop();
            GamePreviewTool = nullptr;
        }
        
        Tool->Deinitialize(UpdateContext);
        Memory::Delete(Tool);
    }

    void FEditorUI::PushModal(const FString& Title, ImVec2 Size, TMoveOnlyFunction<bool()> DrawFunction)
    {
        ModalManager.CreateDialogue(Title, Size, Move(DrawFunction));
    }

    void FEditorUI::OpenScriptEditor(FStringView ScriptPath)
    {
        Platform::LaunchURL(StringUtils::ToWideString(ScriptPath.data()).c_str());
    }

    void FEditorUI::OpenAssetEditor(const FGuid& AssetGUID)
    {
        CObject* Asset = LoadObject<CObject>(AssetGUID);
        
        if (Asset == nullptr)
        {
            return;
        }
        
        auto Itr = ActiveAssetTools.find(Asset);
        if (Itr != ActiveAssetTools.end())
        {
            const char* Name = Itr->second->GetToolName().c_str();
            ImGui::SetWindowFocus(Name);
            return;
        }

        if (WorldEditorTool->GetWorld() == Asset)
        {
            const char* Name = WorldEditorTool->GetToolName().c_str();
            ImGui::SetWindowFocus(Name);
            return;
        }
        
        FEditorTool* NewTool = nullptr;
        if (Asset->IsA<CParticleSystem>())
        {
            NewTool = CreateTool<FParticleSystemEditorTool>(this, Asset);
        }
        else if (Asset->IsA<CMaterial>())
        {
            NewTool = CreateTool<FMaterialEditorTool>(this, Asset);
        }
        else if (Asset->IsA<CTexture>())
        {
            NewTool = CreateTool<FTextureEditorTool>(this, Asset);
        }
        else if (Asset->IsA<CStaticMesh>())
        {
            NewTool = CreateTool<FStaticMeshEditorTool>(this, Asset);
        }
        else if (Asset->IsA<CSkeleton>())
        {
            NewTool = CreateTool<FSkeletonEditorTool>(this, Asset);
        }
        else if (Asset->IsA<CAnimation>())
        {
            NewTool = CreateTool<FAnimationEditorTool>(this, Asset);
        }
        else if (Asset->IsA<CSkeletalMesh>())
        {
            NewTool = CreateTool<FSkeletalMeshEditorTool>(this, Asset);
        }
        else if (Asset->IsA<CMaterialInstance>())
        {
            NewTool = CreateTool<FMaterialInstanceEditorTool>(this, Asset);
        }
        else if (Asset->IsA<CPrefab>())
        {
            NewTool = CreateTool<FPrefabEditorTool>(this, Asset);
        }
        else if (Asset->IsA<CWorld>())
        {
            if (WorldEditorTool->HasSimulatingWorld())
            {
                WorldEditorTool->StopAllSimulations();
            }
            
            WorldEditorTool->SetWorld(Cast<CWorld>(Asset));
        }

        if (NewTool)
        {
            ActiveAssetTools.insert_or_assign(Asset, NewTool);
        }
    }

    void FEditorUI::OpenFileEditor(FStringView VirtualPath)
    {
        if (VirtualPath.empty())
        {
            return;
        }

        FString Key(VirtualPath.data(), VirtualPath.size());

        auto Itr = ActiveFileTools.find(Key);
        if (Itr != ActiveFileTools.end())
        {
            const char* Name = Itr->second->GetToolName().c_str();
            ImGui::SetWindowFocus(Name);
            return;
        }

        const FStringView Ext = VFS::Extension(VirtualPath);

        // Lua scripts can be routed to the platform editor via a config toggle.
        // Default is the in-engine FLuaEditorTool.
        const bool bIsLuaScript = (Ext == ".lua" || Ext == ".luau");
        if (bIsLuaScript && GConfig->GetBool("Editor.LuaEditor.UsePlatformEditor"))
        {
            Platform::LaunchURL(StringUtils::ToWideString(VirtualPath.data()).c_str());
            return;
        }

        FEditorTool* NewTool = nullptr;
        if (Ext == ".rml")
        {
            NewTool = CreateTool<FRmlUiEditorTool>(this, VirtualPath);
        }
        else if (bIsLuaScript)
        {
            NewTool = CreateTool<FLuaEditorTool>(this, VirtualPath);
        }

        if (NewTool == nullptr)
        {
            // No registered editor for this extension; fall back to OS default.
            Platform::LaunchURL(StringUtils::ToWideString(VirtualPath.data()).c_str());
            return;
        }

        ActiveFileTools.insert_or_assign(Move(Key), NewTool);
    }

    void FEditorUI::OnDestroyAsset(CObject* InAsset)
    {
        if (ActiveAssetTools.find(InAsset) != ActiveAssetTools.end())
        {
            ToolsPendingDestroy.push(ActiveAssetTools.at(InAsset));
        }
    }

    FEditorTool* FEditorUI::FindToolByTypeID(uint32 TypeID) const
    {
        // Linear scan is fine — the tool list is small (< ~20 in normal use)
        // and lookups happen at menu-draw frequency, not per-frame hot paths.
        for (FEditorTool* Tool : EditorTools)
        {
            if (Tool->GetUniqueTypeID() == TypeID)
            {
                return Tool;
            }
        }
        return nullptr;
    }

    void FEditorUI::EditorToolLayoutCopy(FEditorTool* SourceTool)
    {
        LUMINA_PROFILE_SCOPE();

        ImGuiID sourceToolID = SourceTool->GetPrevDockspaceID();
        ImGuiID destinationToolID = SourceTool->GetCurrDockspaceID();
        ASSERT(sourceToolID != 0 && destinationToolID != 0);
        
        // Helper to build an array of strings pointer into the same contiguous memory buffer.
        struct ContiguousStringArrayBuilder
        {
            void AddEntry(const char* data, size_t dataLength)
            {
                const int32 bufferSize = (int32_t) m_buffer.size();
                m_offsets.push_back( bufferSize );
                const int32 offset = bufferSize;
                m_buffer.resize( bufferSize + (int32_t) dataLength );
                memcpy( m_buffer.data() + offset, data, dataLength );
            }

            void BuildPointerArray( ImVector<const char*>& outArray )
            {
                outArray.resize( (int32_t) m_offsets.size() );
                for (int32 n = 0; n < (int32) m_offsets.size(); n++)
                {
                    outArray[n] = m_buffer.data() + m_offsets[n];
                }
            }

            TFixedVector<char, 100>       m_buffer;
            TFixedVector<int32, 100>    m_offsets;
        };

        ContiguousStringArrayBuilder namePairsBuilder;

        for (auto& Window : SourceTool->ToolWindows)
        {
            const FFixedString sourceToolWindowName = FEditorTool::GetToolWindowName(Window->Name.c_str(), sourceToolID);
            const FFixedString destinationToolWindowName = FEditorTool::GetToolWindowName(Window->Name.c_str(), destinationToolID);
            namePairsBuilder.AddEntry( sourceToolWindowName.c_str(), sourceToolWindowName.length() + 1 );
            namePairsBuilder.AddEntry( destinationToolWindowName.c_str(), destinationToolWindowName.length() + 1 );
        }

        // Perform the cloning
        if (ImGui::DockContextFindNodeByID( ImGui::GetCurrentContext(), sourceToolID))
        {
            // Build the same array with char* pointers at it is the input of DockBuilderCopyDockspace() (may change its signature?)
            ImVector<const char*> windowRemapPairs;
            namePairsBuilder.BuildPointerArray(windowRemapPairs);

            ImGui::DockBuilderCopyDockSpace(sourceToolID, destinationToolID, &windowRemapPairs);
            ImGui::DockBuilderFinish(destinationToolID);
        }
    }

    bool FEditorUI::SubmitToolMainWindow(const FUpdateContext& UpdateContext, FEditorTool* EditorTool, ImGuiID TopLevelDockspaceID)
    {
        LUMINA_PROFILE_SCOPE();
        ASSERT(EditorTool != nullptr);
        ASSERT(TopLevelDockspaceID != 0);

        bool bIsToolStillOpen = true;
        bool* bIsToolOpen = (EditorTool == WorldEditorTool || EditorTool == ContentBrowser || EditorTool == ConsoleLogTool) ? nullptr : &bIsToolStillOpen; // Prevent closing the map-editor editor tool
        
        // Top level editors can only be docked with each others
        ImGui::SetNextWindowClass(&EditorWindowClass);
        if (EditorTool->GetDesiredDockID() != 0)
        {
            ImGui::SetNextWindowDockID(EditorTool->GetDesiredDockID());
            EditorTool->DesiredDockID = 0;
        }
        else
        {
            ImGui::SetNextWindowDockID(TopLevelDockspaceID, ImGuiCond_FirstUseEver);
        }
        
        ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar;
        if (EditorTool->IsUnsavedDocument())
        {
            WindowFlags |= ImGuiWindowFlags_UnsavedDocument;
        }
        
        ImGuiWindow* CurrentWindow = ImGui::FindWindowByName(EditorTool->GetToolName().c_str());
        const bool bVisible = CurrentWindow != nullptr && !CurrentWindow->Hidden;
        
        ImVec4 VisibleColor   = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImVec4 NotVisibleColor = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Text, bVisible ? VisibleColor : NotVisibleColor);
        ImGui::SetNextWindowSizeConstraints(ImVec2(128, 128), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(1024, 768), ImGuiCond_FirstUseEver);

        // Undock detection: when a tool tears off a dock node it inherits the node's
        // bounds, which is often a tall sliver or a wide stripe. Reset to a sensible
        // floating size + center on the main viewport for one frame.
        // CurrDockID still holds last frame's value at this point; CurrentWindow->DockId
        // is what ImGui has assigned for the upcoming frame.
        const ImGuiID PrevFrameDockID = EditorTool->CurrDockID;
        const ImGuiID NextFrameDockID = CurrentWindow ? CurrentWindow->DockId : 0;
        if (PrevFrameDockID != 0 && NextFrameDockID == 0 && CurrentWindow != nullptr)
        {
            constexpr ImVec2 UndockedSize(1177.6f, 883.2f);
            const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
            const ImVec2 UndockedPos
            {
                MainViewport->Pos.x + (MainViewport->Size.x - UndockedSize.x) * 0.5f,
                MainViewport->Pos.y + (MainViewport->Size.y - UndockedSize.y) * 0.5f,
            };
            ImGui::SetNextWindowSize(UndockedSize, ImGuiCond_Always);
            ImGui::SetNextWindowPos(UndockedPos, ImGuiCond_Always);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.5f);
        ImGui::Begin(EditorTool->GetToolName().c_str(), bIsToolOpen, WindowFlags);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_DockHierarchy))
        {
            LastActiveTool = EditorTool;
        }
        
        // Set WindowClass based on per-document ID, so tabs from Document A are not dockable in Document B etc. We could be using any ID suiting us, e.g. &doc
        // We also set ParentViewportId to request the platform back-end to set parent/child relationship at the windowing level
        EditorTool->ToolWindowsClass.ClassId = EditorTool->GetID();
        EditorTool->ToolWindowsClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoTaskBarIcon | ImGuiViewportFlags_NoDecoration;
        EditorTool->ToolWindowsClass.ParentViewportId = ImGui::GetWindowViewport()->ID;
        EditorTool->ToolWindowsClass.DockingAllowUnclassed = true;

        // Track LocationID change so we can fork/copy the layout data according to where the window is going + reference count
        // LocationID ~~ (DockId != 0 ? DockId : DocumentID) // When we are in a loose floating window we use our own document id instead of the dock id
        EditorTool->CurrDockID = ImGui::GetWindowDockID();
        EditorTool->PrevLocationID = EditorTool->CurrLocationID;
        EditorTool->CurrLocationID = EditorTool->CurrDockID != 0 ? EditorTool->CurrDockID : EditorTool->GetID();

        // Dockspace ID ~~ Hash of LocationID + DocType
        // So all editors of a same type inside a same tab-bar will share the same layout.
        // We will also use this value as a suffix to create window titles, but we could perfectly have an indirection to allocate and use nicer names for window names (e.g. 0001, 0002).
        EditorTool->PrevDockspaceID = EditorTool->CurrDockspaceID;
        EditorTool->CurrDockspaceID = EditorTool->CalculateDockspaceID();
        ASSERT(EditorTool->CurrDockspaceID != 0);
        

        ImGui::End();

        return bIsToolStillOpen;
    }

    void FEditorUI::DrawToolContents(const FUpdateContext& UpdateContext, FEditorTool* Tool)
    {
        LUMINA_PROFILE_SCOPE();

        // This is the second Begin(), as SubmitToolMainWindow() has already done one
        // (Therefore only the p_open and flags of the first call to Begin() applies)
        ImGui::Begin(Tool->GetToolName().c_str());
        
        ASSERT(ImGui::GetCurrentWindow()->BeginCount == 2);
        
        const ImGuiID dockspaceID = Tool->GetCurrentDockspaceID();
        const ImVec2 DockspaceSize = ImGui::GetContentRegionAvail();

        if (Tool->PrevLocationID != 0 && Tool->PrevLocationID != Tool->CurrLocationID)
        {
            int PrevDockspaceRefCount = 0;
            int CurrDockspaceRefCount = 0;
            for (FEditorTool* OtherTool : EditorTools)
            {
                if (OtherTool->CurrDockspaceID == Tool->PrevDockspaceID)
                {
                    PrevDockspaceRefCount++;
                }
                else if (OtherTool->CurrDockspaceID == Tool->CurrDockspaceID)
                {
                    CurrDockspaceRefCount++;
                }
            }

            // Fork or overwrite settings
            // FIXME: should be able to do a "move window but keep layout" if CurrDockspaceRefCount > 1.
            // FIXME: when moving, delete settings of old windows
            EditorToolLayoutCopy(Tool);

            if (PrevDockspaceRefCount == 0)
            {
                ImGui::DockBuilderRemoveNode(Tool->PrevDockspaceID);

                // Delete settings of old windows
                // Rely on window name to ditch their .ini settings forever.
                char windowSuffix[16];
                ImFormatString(windowSuffix, IM_ARRAYSIZE(windowSuffix), "##%08X", Tool->PrevDockspaceID);
                size_t windowSuffixLength = strlen(windowSuffix);
                ImGuiContext& g = *GImGui;
                for (ImGuiWindowSettings* settings = g.SettingsWindows.begin(); settings != nullptr; settings = g.SettingsWindows.next_chunk(settings))
                {
                    if ( settings->ID == 0 )
                    {
                        continue;
                    }
                    
                    
                    char const* pWindowName = settings->GetName();
                    size_t windowNameLength = strlen(pWindowName);
                    if (windowNameLength >= windowSuffixLength)
                    {
                        if (strcmp(pWindowName + windowNameLength - windowSuffixLength, windowSuffix) == 0) // Compare suffix
                        {
                            ImGui::ClearWindowSettings(pWindowName);
                        }
                    }
                }
            }
        }
        else if (ImGui::DockBuilderGetNode(Tool->GetCurrentDockspaceID()) == nullptr)
        {
            ImVec2 dockspaceSize = ImGui::GetContentRegionAvail();
            dockspaceSize.x = eastl::max(dockspaceSize.x, 1.0f);
            dockspaceSize.y = eastl::max(dockspaceSize.y, 1.0f);

            ImGui::DockBuilderAddNode(Tool->GetCurrentDockspaceID(), ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(Tool->GetCurrentDockspaceID(), dockspaceSize);
            if (!Tool->IsSingleWindowTool())
            {
                Tool->InitializeDockingLayout(Tool->GetCurrentDockspaceID(), dockspaceSize);
            }
            ImGui::DockBuilderFinish(Tool->GetCurrentDockspaceID());
        }

        // FIXME-DOCK: This is a little tricky to explain, but we currently need this to use the pattern of sharing a same dockspace between tabs of a same tab bar
        bool bVisible = true;
        if (ImGui::GetCurrentWindow()->Hidden)
        {
            bVisible = false;
        }
        
        const bool bIsLastFocusedTool = (LastActiveTool == Tool);

        // Ctrl+S routes to the focused tool only — checking IsKeyPressed inside
        // each tool's Update fires for every open tool simultaneously.
        if (bIsLastFocusedTool && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S, false))
        {
            Tool->OnSave();
        }

        Tool->Update(UpdateContext);
        Tool->bViewportFocused = false;
        Tool->bViewportHovered = false;

        if (Tool->HasWorld())
        {
            Tool->GetWorld()->SetActive(bVisible);
        }
        
        if (!bVisible)
        {
            if (!Tool->IsSingleWindowTool())
            {
                // Keep alive document dockspace so windows that are docked into it but which visibility are not linked to the dockspace visibility won't get undocked.
                ImGui::DockSpace(dockspaceID, DockspaceSize, ImGuiDockNodeFlags_KeepAliveOnly, &Tool->ToolWindowsClass);
            }
            
            ImGui::End();
            
            return;
        }

        
        if (Tool->HasFlag(EEditorToolFlags::Tool_WantsToolbar))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 16));
            if (ImGui::BeginMenuBar())
            {
                Tool->DrawMainToolbar(UpdateContext);
                ImGui::EndMenuBar();
            }
            ImGui::PopStyleVar();
        }

        if (Tool->IsSingleWindowTool())
        {
            ASSERT(Tool->ToolWindows.size() == 1);
            Tool->ToolWindows[0]->DrawFunction(bIsLastFocusedTool);
        }
        else
        {
            ImGui::DockSpace(dockspaceID, DockspaceSize, ImGuiDockNodeFlags_None, &Tool->ToolWindowsClass);
        }
    
        ImGui::End();


        if (!Tool->IsSingleWindowTool())
        {
            for (auto& Window : Tool->ToolWindows)
            {
                LUMINA_PROFILE_SECTION("Setup and Draw Tool Window");

                const FFixedString ToolWindowName = FEditorTool::GetToolWindowName(Window->Name.c_str(), Tool->GetCurrentDockspaceID());

                // When multiple documents are open, floating tools only appear for focused one
                if (!bIsLastFocusedTool)
                {
                    if (ImGuiWindow* pWindow = ImGui::FindWindowByName(ToolWindowName.c_str()))
                    {
                        ImGuiDockNode* pWindowDockNode = pWindow->DockNode;
                        if (pWindowDockNode == nullptr && pWindow->DockId != 0)
                        {
                            pWindowDockNode = ImGui::DockContextFindNodeByID(ImGui::GetCurrentContext(), pWindow->DockId);
                        }
                       
                        if (pWindowDockNode == nullptr || ImGui::DockNodeGetRootNode(pWindowDockNode)->ID != dockspaceID)
                        {
                            continue;
                        }
                    }
                }
            
                if (Window->bViewport)
                {
                    LUMINA_PROFILE_SECTION("Draw Viewport");

                    constexpr ImGuiWindowFlags ViewportWindowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus;

                    if (Tool->IsViewportFullscreen())
                    {
                        // Begin/End the docked viewport empty so its dock-node slot stays
                        // alive — letting the window snap back into place when we exit
                        // fullscreen instead of orphaning at the last fullscreen rect.
                        ImGui::SetNextWindowClass(&Tool->ToolWindowsClass);
                        ImGui::SetNextWindowSizeConstraints(ImVec2(128, 128), ImVec2(FLT_MAX, FLT_MAX));
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                        ImGui::Begin(ToolWindowName.c_str(), nullptr, ViewportWindowFlags);
                        ImGui::PopStyleVar();
                        ImGui::End();

                        // Now draw the actual viewport into a separate fullscreen overlay window.
                        // Different name so the docked window's position/dock state is untouched.
                        const FFixedString FullscreenName(FFixedString::CtorSprintf(), "%s##Fullscreen_%08X", FEditorTool::ViewportWindowName, Tool->GetCurrentDockspaceID());

                        const ImGuiViewport* MainVP = ImGui::GetMainViewport();
                        ImGui::SetNextWindowPos(MainVP->WorkPos);
                        ImGui::SetNextWindowSize(MainVP->WorkSize);
                        ImGui::SetNextWindowViewport(MainVP->ID);

                        constexpr ImGuiWindowFlags FullscreenFlags =
                            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoNavInputs |
                            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse;

                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                        bool const DrawViewportWindow = ImGui::Begin(FullscreenName.c_str(), nullptr, FullscreenFlags);
                        ImGui::PopStyleVar(3);

                        if (DrawViewportWindow)
                        {
                            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

                            IRenderScene* SceneRenderer = Tool->GetWorld()->GetRenderer();
                            ImTextureRef ViewportTexture = ImGuiX::ToImTextureRef(SceneRenderer->GetRenderTarget());

                            Tool->bViewportFocused = ImGui::IsWindowFocused();
                            Tool->bViewportHovered = ImGui::IsWindowHovered();
                            Tool->DrawViewport(UpdateContext, ViewportTexture);
                        }

                        ImGui::End();
                    }
                    else
                    {
                        ImGui::SetNextWindowClass(&Tool->ToolWindowsClass);
                        ImGui::SetNextWindowSizeConstraints(ImVec2(128, 128), ImVec2(FLT_MAX, FLT_MAX));
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                        bool const DrawViewportWindow = ImGui::Begin(ToolWindowName.c_str(), nullptr, ViewportWindowFlags);
                        ImGui::PopStyleVar();

                        if (DrawViewportWindow)
                        {
                            IRenderScene* SceneRenderer = Tool->GetWorld()->GetRenderer();
                            ImTextureRef ViewportTexture = ImGuiX::ToImTextureRef(SceneRenderer->GetRenderTarget());

                            Tool->bViewportFocused = ImGui::IsWindowFocused();
                            Tool->bViewportHovered = ImGui::IsWindowHovered();
                            Tool->DrawViewport(UpdateContext, ViewportTexture);
                        }

                        ImGui::End();
                    }
                }
                else
                {
                    LUMINA_PROFILE_SECTION("Draw Tool Window");

                    ImGuiWindowFlags ToolWindowFlags = ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoCollapse;

                    ImGui::SetNextWindowClass(&Tool->ToolWindowsClass);

                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImGui::GetStyle().WindowPadding);
                    bool const DrawToolWindow = ImGui::Begin(ToolWindowName.c_str(), nullptr, ToolWindowFlags);
                    ImGui::PopStyleVar();

                    if (DrawToolWindow)
                    {
                        const bool bToolWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_DockHierarchy);
                        Window->DrawFunction(bToolWindowFocused);
                    }
                    
                    ImGui::End();
                }
            }
        }
    }

    void FEditorUI::CreateGameViewportTool(const FUpdateContext& UpdateContext)
    {
    }

    void FEditorUI::DestroyGameViewportTool(const FUpdateContext& UpdateContext)
    {
        
    }

    void FEditorUI::HandleUserInput(const FUpdateContext& UpdateContext)
    {
        
    }

    void FEditorUI::VerifyDirtyPackages()
    {
        TVector<CPackage*> DirtyPackages;
        DirtyPackages.reserve(4);
        for (TObjectIterator<CPackage> Itr; Itr; ++Itr)
        {
            CPackage* Package = *Itr;

            if (Package->IsDirty())
            {
                DirtyPackages.push_back(Package);
            }
        }

        if (DirtyPackages.empty())
        {
            return;
        }
        
        TVector<bool> PackageSelection;
        PackageSelection.resize(DirtyPackages.size(), true);
        
        enum class ESaveState { Idle, Saving, Success, Failed };
        TVector<ESaveState> SaveStates;
        SaveStates.resize(DirtyPackages.size(), ESaveState::Idle);
        
        ModalManager.CreateDialogue("Save Modified Packages", ImVec2(450, 600), [&, Packages = Move(DirtyPackages), PackageSelection, SaveStates] () mutable
        {
            bool bShouldClose = false;
            
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 12));
            
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), LE_ICON_EXCLAMATION_THICK " Unsaved Changes Detected");
            
            ImGui::Spacing();
            ImGui::TextWrapped("The following packages have unsaved changes. Select which packages you would like to save before continuing.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::BeginGroup();
            {
                if (ImGui::Button(LE_ICON_SELECT_ALL " Select All", ImVec2(140, 0)))
                {
                    for (bool& Selected : PackageSelection)
                    {
                        Selected = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(LE_ICON_SQUARE_OUTLINE " Deselect All", ImVec2(140, 0)))
                {
                    for (bool& Selected : PackageSelection)
                    {
                        Selected = false;
                    }
                }
                
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                
                int32 SelectedCount = 0;
                for (bool Selected : PackageSelection)
                {
                    if (Selected)
                    {
                        SelectedCount++;
                    }
                }

                ImGui::Text("%d of %d selected", SelectedCount, (int32)Packages.size());
            }
            ImGui::EndGroup();
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            ImVec2 ListSize = ImVec2(-1, -80);
            if (ImGui::BeginChild("PackageList", ListSize, true, ImGuiWindowFlags_AlwaysVerticalScrollbar))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
                
                for (size_t i = 0; i < Packages.size(); ++i)
                {
                    CPackage* Package = Packages[i];
                    
                    ImGui::PushID((int)i);
                    
                    ImVec2 ItemStart = ImGui::GetCursorScreenPos();
                    ImVec2 ItemSize = ImVec2(ImGui::GetContentRegionAvail().x, 64);
                    
                    bool bIsHovered = ImGui::IsMouseHoveringRect(ItemStart, ImVec2(ItemStart.x + ItemSize.x, ItemStart.y + ItemSize.y));
                    
                    ImU32 BgColor = bIsHovered ? 
                        IM_COL32(50, 50, 55, 180) : 
                        IM_COL32(35, 35, 40, 180);
                    
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ItemStart, 
                        ImVec2(ItemStart.x + ItemSize.x, ItemStart.y + ItemSize.y),
                        BgColor,
                        4.0f
                    );
                    
                    ImGui::BeginGroup();
                    {
                        ImGui::Dummy(ImVec2(0, 4));
                        
                        ImGui::Checkbox("##select", &PackageSelection[i]);
                        ImGui::SameLine();
                        
                        ImGui::BeginGroup();
                        {
                            ImGui::Text(LE_ICON_FILE " %s", Package->GetName().c_str());
                            
                            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", Package->GetPackagePath().c_str());
                            
                            switch (SaveStates[i])
                            {
                                case ESaveState::Saving:
                                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), LE_ICON_WATCH_VIBRATE " Saving...");
                                    break;
                                case ESaveState::Success:
                                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), LE_ICON_CHECK " Saved");
                                    break;
                                case ESaveState::Failed:
                                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), LE_ICON_EXCLAMATION_THICK " Failed to save");
                                    break;
                            }
                        }
                        ImGui::EndGroup();
                        
                        ImGui::Dummy(ImVec2(0, 4));
                    }
                    ImGui::EndGroup();
                    
                    ImGui::PopID();
                    
                    ImGui::Spacing();
                }
                
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::BeginGroup();
            {
                float ButtonWidth = 150.0f;
                float Spacing = 8.0f;
                float TotalWidth = (ButtonWidth * 2) + (Spacing * 2);
                float OffsetX = (ImGui::GetContentRegionAvail().x - TotalWidth) * 0.5f;
                
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + OffsetX);
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
                
                if (ImGui::Button(LE_ICON_CONTENT_SAVE " Save Selected", ImVec2(ButtonWidth, 35)))
                {
                    for (size_t i = 0; i < Packages.size(); ++i)
                    {
                        if (PackageSelection[i])
                        {
                            SaveStates[i] = ESaveState::Saving;
                            
                            bool bSaveSuccess = CPackage::SavePackage(Packages[i], Packages[i]->GetPackagePath());
                            SaveStates[i] = bSaveSuccess ? ESaveState::Success : ESaveState::Failed;
                        }
                    }
                    
                    bShouldClose = true;
                }
                ImGui::PopStyleColor(3);
                
                ImGui::SameLine();
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.5f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.3f, 0.15f, 1.0f));
                
                if (ImGui::Button(LE_ICON_SQUARE " Don't Save", ImVec2(ButtonWidth, 35)))
                {
                    bShouldClose = true;
                }
                ImGui::PopStyleColor(3);
                
                //ImGui::SameLine();
                //
                //if (ImGui::Button(LE_ICON_CANCEL " Cancel", ImVec2(ButtonWidth, 35)))
                //{
                //    
                //    bShouldClose = true;
                //}
            }
            ImGui::EndGroup();
            
            ImGui::PopStyleVar();
            
            return bShouldClose;
        });
    }
    
    void FEditorUI::DrawTitleBarMenu(const FUpdateContext& UpdateContext)
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
        ImGui::Image(ImGuiX::ToImTextureRef(Paths::GetEngineResourceDirectory() + "/Textures/Lumina.png"), ImVec2(24.0f, 24.0f));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
    
        // Styled menu bar
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.08f, 0.1f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.25f, 0.27f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.92f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.35f, 0.45f, 0.8f));
    
        ImGui::SetNextWindowSizeConstraints(ImVec2(220, 1), ImVec2(280, 1000));
    
        DrawFileMenu();
        DrawProjectMenu();
        DrawToolsMenu();
        DrawHelpMenu();
    
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(3);

    }
    
    void FEditorUI::DrawTitleBarInfoStats(const FUpdateContext& UpdateContext)
    {
        ImGui::SameLine();

        float CurrentFPS = UpdateContext.GetFPS();
        float CurrentFrameTime = UpdateContext.GetDeltaTime() * 1000.0f;
        
        SmoothedFPS = SmoothedFPS + (CurrentFPS - SmoothedFPS) * FPSSmoothingFactor;
        SmoothedFrameTime = SmoothedFrameTime + (CurrentFrameTime - SmoothedFrameTime) * FPSSmoothingFactor;

        const TFixedString<100> PerfStats(TFixedString<100>::CtorSprintf(), "FPS: %3.0f / %.2f ms", SmoothedFPS, SmoothedFrameTime);
        ImGui::TextUnformatted(PerfStats.c_str());

        ImGui::SameLine();

        const TFixedString<100> ObjectStats(TFixedString<100>::CtorSprintf(), "CObjects: %i", GObjectArray.GetNumAliveObjects());
        ImGui::TextUnformatted(ObjectStats.c_str());
    }

    void FEditorUI::DrawFileMenu()
    {
        if (!ImGui::BeginMenu(LE_ICON_FILE " File"))
        {
            return;
        }
        if (ImGui::MenuItem(LE_ICON_ZIP_DISK " Save", "Ctrl+S"))
        {
            // Save action
        }

        if (ImGui::MenuItem(LE_ICON_ZIP_DISK " Save All", "Ctrl+Shift+S"))
        {
            // Save all action
        }

        ImGui::Separator();

        DrawToolMenuItem<FProjectSettingsEditorTool>(LE_ICON_SETTINGS_HELPER " Project Settings", this);
        
        if (ImGui::BeginMenu(LE_ICON_ROTATE_LEFT " Recent"))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.62f, 1.0f));
         
            auto Recents = GConfig->Get<std::vector<std::string>>("Editor.RecentProjects");
            for (const auto& Item : Recents)
            {
                ImGui::TextUnformatted(Item.c_str());
            }
            ImGui::PopStyleColor();
            
            ImGui::EndMenu();
        }

        ImGui::Separator();
        

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.6f, 0.4f, 1.0f));
        if (ImGui::BeginMenu(LE_ICON_HAMMER " Shaders"))
        {
            if (ImGui::MenuItem(LE_ICON_HAMMER " Recompile All", "F5"))
            {
                CMaterial::CreateDefaultMaterial();
            }
            
            if (ImGui::MenuItem(LE_ICON_MATERIAL_DESIGN " Recompile Default Material"))
            {
                CMaterial::CreateDefaultMaterial();
            }

            if (ImGui::MenuItem(LE_ICON_FOLDER " Open Shaders Directory", "F6"))
            {
                Platform::LaunchURL(StringUtils::ToWideString(Paths::GetEngineShadersDirectory()).c_str());
            }

            ImGui::Separator();

            for (auto& Directory : std::filesystem::recursive_directory_iterator(Paths::GetEngineShadersDirectory().c_str()))
            {
                if (Directory.is_regular_file())
                {
                    FString FileName = Directory.path().filename().string().c_str();
                    if (ImGui::BeginMenu(FileName.c_str()))
                    {
                        if (ImGui::MenuItem(LE_ICON_HAMMER " Recompile"))
                        {
                            GRenderContext->GetShaderCompiler()->CompileShaderPath(Directory.path().string().c_str(), {}, [&](const FShaderHeader& Header)
                            {
                                GRenderContext->GetShaderLibrary()->CreateAndAddShader(Header.DebugName, Header, true);
                            });
                        }

                        if (ImGui::MenuItem(LE_ICON_FOLDER " Open"))
                        {
                            Platform::LaunchURL(Directory.path().c_str());
                        }

                        ImGui::EndMenu();
                    }
                }
            }

            ImGui::EndMenu();
        }
        ImGui::PopStyleColor();

        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
        if (ImGui::MenuItem(LE_ICON_DOOR_OPEN " Exit", "Alt+F4"))
        {
            // ...
			FApplication::RequestExit();
        }
        ImGui::PopStyleColor();

        ImGui::EndMenu();
    }

    void FEditorUI::DrawProjectMenu()
    {
        if (!ImGui::BeginMenu(LE_ICON_FOLDER " Project"))
        {
            return;
        }
        
        if (GEngine->HasLoadedProject())
        {
            if (ImGui::MenuItem(LE_ICON_LANGUAGE_LUA " Reload Project Module"))
            {
                FString ModuleFile = GConfig->Get<std::string>("Project.LuaModuleFile").c_str();
                GEngine->LoadProjectScript(ModuleFile);
            }
        }
        
        //if (ImGui::MenuItem(LE_ICON_FOLDER_OPEN " Open Project...", "Ctrl+O"))
        //{
        //    OpenProjectDialog();
        //}
    
        if (ImGui::MenuItem(LE_ICON_FOLDER_PLUS " New Project...", "Ctrl+N"))
        {
            NewProjectDialog();
        }
    
        ImGui::Separator();
    
        if (ImGui::MenuItem(LE_ICON_DATABASE " Asset Registry"))
        {
            AssetRegistryDialog();
        }
    
        ImGui::Separator();
    
        DrawToolMenuItem<FProjectPackagerEditorTool>(LE_ICON_PACKAGE_VARIANT " Package Project...", this);

        ImGui::EndMenu();
    }

    void FEditorUI::DrawToolsMenu()
    {
        if (!ImGui::BeginMenu(LE_ICON_WRENCH " Tools"))
        {
            return;
        }

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.62f, 1.0f), "Debug Windows");
        ImGui::Separator();

        DrawToolMenuItem<FInputActionEditorTool>(LE_ICON_KEYBOARD " Input Actions", this);
        DrawToolMenuItem<FScriptsInfoEditorTool>(LE_ICON_LANGUAGE_LUA " Scripts Info", this);
        DrawToolMenuItem<FLuaDebuggerEditorTool>(LE_ICON_BUG " Lua Debugger", this);
        DrawToolMenuItem<FRendererInfoEditorTool>(LE_ICON_CHART_LINE " Renderer Info", this);
        DrawToolMenuItem<FGPUProfilerEditorTool>(LE_ICON_CHART_TIMELINE " GPU Profiler", this);
        DrawToolMenuItem<FCPUProfilerEditorTool>(LE_ICON_CHART_BAR " CPU Profiler", this);
        DrawToolMenuItem<FShadowAtlasEditorTool>(LE_ICON_GRID " Shadow Atlas", this);
        DrawToolMenuItem<FMemoryProfilerEditorTool>(LE_ICON_MEMORY " Memory Profiler", this);
        DrawToolMenuItem<FObjectBrowserEditorTool>(LE_ICON_LIST_BOX " Object Browser", this);
        
        ImGui::Spacing();
        
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.62f, 1.0f), "ImGui Tools");
        ImGui::Separator();
        
        ImGui::MenuItem(LE_ICON_WINDOW_OPEN " ImGui Style Editor", nullptr, &bShowImGuiStyleEditor);
        ImGui::MenuItem(LE_ICON_WINDOW_OPEN " ImGui Demo", nullptr, &bShowDearImGuiDemoWindow);
        ImGui::MenuItem(LE_ICON_CHART_BAR " ImPlot Demo", nullptr, &bShowImPlotDemoWindow);
        
        ImGui::Spacing();
        
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.62f, 1.0f), "External Tools");
        ImGui::Separator();
        
        if (ImGui::MenuItem(LE_ICON_WATCH" Tracy Profiler", "Ctrl+P"))
        {
            FString LuminaDirEnv = std::getenv("LUMINA_DIR");
            FString FullPath = LuminaDirEnv + "/External/Tracy/tracy-profiler.exe";
            
            Platform::LaunchURL(StringUtils::ToWideString(FullPath).c_str());
        }
        
        if (ImGui::MenuItem(LE_ICON_CAMERA " RenderDoc Capture", "F11"))
        {
            FRenderDoc::Get().TriggerCapture();
        }

        if (ImGui::BeginMenu(LE_ICON_CAMERA " Screenshot"))
        {
            if (ImGui::MenuItem("Save PNG (Tonemapped)", "F9"))
            {
                Screenshot::CaptureActiveWorld(Screenshot::ECaptureSource::FinalLDR);
            }
            if (ImGui::MenuItem("Save HDR (Linear)", "Shift+F9"))
            {
                Screenshot::CaptureActiveWorld(Screenshot::ECaptureSource::SceneHDR);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(LE_ICON_FOLDER " Open Screenshots Folder"))
            {
                FString Folder = Paths::GetEngineDirectory() + "/Saved/Screenshots";
                Paths::CreateDirectories(FStringView(Folder.c_str(), Folder.size()));
                Platform::LaunchURL(StringUtils::ToWideString(Folder).c_str());
            }
            ImGui::EndMenu();
        }

        ImGui::Spacing();
        
        // Settings
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.62f, 1.0f), "Settings");
        ImGui::Separator();
        
        bool bVSyncEnabled = GRenderContext->IsVSyncEnabled();
        if (ImGui::MenuItem(LE_ICON_DISC_PLAYER " V-Sync", nullptr, bVSyncEnabled))
        {
            GRenderContext->SetVSyncEnabled(!bVSyncEnabled);
        }
        
        if (ImGui::BeginMenu(LE_ICON_PALETTE " Theme"))
        {
            if (ImGui::MenuItem("Dark", nullptr, true))  // Currently selected
            {
                // Apply dark theme
            }
            
            if (ImGui::MenuItem("Light", nullptr, false))
            {
                // Apply light theme
            }
            
            if (ImGui::MenuItem("Custom...", nullptr, false))
            {
                // Open theme editor
            }
            
            ImGui::EndMenu();
        }
        
        ImGui::EndMenu();
    }

    void FEditorUI::DrawHelpMenu()
    {
        if (!ImGui::BeginMenu(LE_ICON_HELP " Help"))
        {
            return;
        }

        if (ImGui::MenuItem(LE_ICON_GROUP " Discord"))
        {
            Platform::LaunchURL(TEXT("https://discord.gg/UhTmzB8UdY"));
        }

        if (ImGui::BeginMenu(LE_ICON_BOOK " Documentation"))
        {
            if (ImGui::MenuItem(LE_ICON_GROUP " Lumina"))
            {
                Platform::LaunchURL(TEXT("https://discord.gg/UhTmzB8UdY"));
            }
            
            if (ImGui::MenuItem(LE_ICON_LANGUAGE_LUA " Luau"))
            {
                Platform::LaunchURL(TEXT("https://luau.org/getting-started/"));
            }
            
            ImGui::EndMenu();
        }
    
        if (ImGui::MenuItem(LE_ICON_ACCOUNT_QUESTION " Tutorials"))
        {
            Platform::LaunchURL(TEXT("https://discord.gg/UhTmzB8UdY"));
        }
    
        ImGui::Separator();

        if (ImGui::MenuItem(LE_ICON_GITHUB " GitHub Repository"))
        {
            Platform::LaunchURL(TEXT("https://github.com/MrDrElliot/LuminaEngine"));
        }
    
        if (ImGui::MenuItem(LE_ICON_BUG " Report Issue"))
        {
            Platform::LaunchURL(TEXT("https://github.com/MrDrElliot/LuminaEngine/issues"));
        }
    
        ImGui::Separator();
        
        // About + Contributors are now tabs of the same tool, so a single menu entry covers both.
        DrawToolMenuItem<FAboutEditorTool>(LE_ICON_CIRCLE " About Lumina", this);

        ImGui::EndMenu();
    }

    void FEditorUI::OpenProjectDialog()
    {
        ModalManager.CreateDialogue("Open Project", ImVec2(1000, 650), [this] () -> bool
        {
            bool bShouldClose = false;

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
            ImGui::TextWrapped(LE_ICON_FOLDER_OPEN " Select a project to open or browse for an existing project");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::BeginChild("ProjectContent", ImVec2(0, -50), false);
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                ImGui::Text(LE_ICON_FOLDER_OPEN " Example Projects");
                ImGui::PopStyleColor();
                ImGui::Spacing();

                ImGui::BeginChild("ProjectCards", ImVec2(0, 0), false);
                {
                    const float CardWidth = 280.0f;
                    const float CardHeight = 200.0f;
                    const float Padding = 16.0f;

                    float availWidth = ImGui::GetContentRegionAvail().x;
                    int CardsPerRow = Math::Max(1, (int)((availWidth + Padding) / (CardWidth + Padding)));

                    ImGui::BeginGroup();
                    {
                        ImVec2 CursorPos = ImGui::GetCursorScreenPos();
                        ImDrawList* drawList = ImGui::GetWindowDrawList();

                        ImVec4 cardBgColor = ImVec4(0.15f, 0.15f, 0.16f, 1.0f);
                        ImVec4 cardBgHoverColor = ImVec4(0.18f, 0.18f, 0.19f, 1.0f);
                        ImVec4 accentColor = ImVec4(0.3f, 0.6f, 1.0f, 1.0f);

                        ImGui::PushStyleColor(ImGuiCol_Button, cardBgColor);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cardBgHoverColor);
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.21f, 1.0f));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18, 18));

                        if (ImGui::Button("##SandboxCard", ImVec2(CardWidth, CardHeight)))
                        {
                            FString SandboxProjectDirectory = Paths::GetEngineDirectory() + "/Sandbox/Sandbox.lproject";
                            GEditorEngine->LoadProject(SandboxProjectDirectory);
                            OnProjectLoaded();
                            bShouldClose = true;
                        }

                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor(3);

                        drawList->AddRectFilled(
                            CursorPos,
                            ImVec2(CursorPos.x + CardWidth, CursorPos.y + 4),
                            ImGui::GetColorU32(accentColor)
                        );

                        ImGui::SetCursorScreenPos(ImVec2(CursorPos.x + 16, CursorPos.y + 20));
                        ImGui::Dummy(ImVec2(0, 0));

                        ImGui::BeginGroup();
                        {
                            ImVec2 iconPos = ImGui::GetCursorScreenPos();
                            drawList->AddCircleFilled(
                                ImVec2(iconPos.x + 20, iconPos.y + 20),
                                20.0f,
                                ImGui::GetColorU32(ImVec4(0.3f, 0.6f, 1.0f, 0.2f))
                            );

                            ImGui::PushStyleColor(ImGuiCol_Text, accentColor);
                            ImGui::SetCursorScreenPos(ImVec2(iconPos.x + 10, iconPos.y + 10));
                            ImGui::Text(LE_ICON_FOLDER_OPEN);
                            ImGui::PopStyleColor();

                            ImGui::SetCursorScreenPos(ImVec2(CursorPos.x + 16, iconPos.y + 50));

                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                            ImGui::Text("Sandbox Project");
                            ImGui::PopStyleColor();

                            ImGui::Spacing();

                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                            ImGui::BeginChild("##SandboxDesc", ImVec2(CardWidth - 32, 60), false, ImGuiWindowFlags_NoScrollbar);
                            ImGui::TextWrapped("A basic sandbox environment for testing and experimentation. Perfect for learning the engine basics.");
                            ImGui::EndChild();
                            ImGui::PopStyleColor();

                            ImGui::SetCursorScreenPos(ImVec2(CursorPos.x + 16, CursorPos.y + CardHeight - 30));

                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 0.5f, 1.0f));
                            ImGui::Text("Example Project");
                            ImGui::PopStyleColor();

                            ImGui::EndGroup();
                        }
                    }
                    ImGui::EndGroup();


                    ImGui::EndChild();
                }

                ImGui::EndChild();
            }

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::BeginGroup();
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));

                if (ImGui::Button(LE_ICON_FOLDER_OPEN " Browse for Project...", ImVec2(200, 32)))
                {
                    FFixedString Project;
                    if (Platform::OpenFileDialogue(
                        Project,
                        "Open Project",
                        "Lumina Project (*.lproject)\0*.lproject\0All Files (*.*)\0*.*\0",
                        nullptr
                    ))
                    {
                        GEditorEngine->LoadProject(Project);
                        OnProjectLoaded();
                        bShouldClose = true;
                    }
                }

                ImGui::PopStyleColor(3);

                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

                if (ImGui::Button("Cancel", ImVec2(120, 32)))
                {
                    bShouldClose = true;
                }

                ImGui::PopStyleColor(3);

                ImGui::EndGroup();
            }

            return bShouldClose;
        }, true, false);
    }

    void FEditorUI::NewProjectDialog()
    {
        ModalManager.CreateDialogue("New Project", ImVec2(900, 600), [this] () -> bool
        {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), LE_ICON_FOLDER_PLUS " Create a new Lumina project");
            ImGui::Separator();
            ImGui::Spacing();
            
            static char NewProjectName[256] = "MyProject";
            static char NewProjectPath[512] = "";
            
            ImGui::Text("Project Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ProjectName", NewProjectName, sizeof(NewProjectName));
        
            ImGui::Spacing();
        
            ImGui::Text("Project Location:");
            ImGui::SetNextItemWidth(-120);
            ImGui::InputText("##ProjectPath", NewProjectPath, sizeof(NewProjectPath));
            ImGui::SameLine();
            if (ImGui::Button("Browse...", ImVec2(110, 0)))
            {
                FFixedString File;
                if (Platform::OpenFileDialogue(File,"Browse..."))
                {
                    strncpy_s(NewProjectPath, sizeof(NewProjectPath), File.c_str(), _TRUNCATE);
                }
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::Text("Project Template:");
            ImGui::BeginChild("Templates", ImVec2(0, -40), true);
            {
                if (ImGui::Selectable(LE_ICON_CUBE " Blank Project"))
                {
                    
                }
            }
            ImGui::EndChild();
            
            ImGui::Spacing();
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 0.3f, 1.0f));
            if (ImGui::Button(LE_ICON_CHECK " Create Project", ImVec2(140, 0)))
            {
                GEditorEngine->CreateProject(NewProjectName, NewProjectPath);
                ImGui::PopStyleColor();
                
                ImGuiX::Notifications::NotifySuccess("Successfully created project, please close the engine and relaunch");
                return true;
            }
            ImGui::PopStyleColor();
            
            ImGui::SameLine();
            
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                return true;
            }
            
            return false;
        });
    }

    void FEditorUI::OnProjectLoaded()
    {
        ContentBrowser->RefreshContentBrowser();
        
        //@TODO TEMP, maybe just wait until finished to load startup.
        GTaskSystem->WaitForAll();
        
        const FString RawEditorStartupMap = GConfig->Get<std::string>("Project.EditorStartupMap").c_str();
        const FFixedString EditorStartupMapFixed = VFS::ResolveToVirtualPath(RawEditorStartupMap);
        const FString EditorStartupMap(EditorStartupMapFixed.c_str(), EditorStartupMapFixed.size());
        if (FAssetData* Data = FAssetRegistry::Get().GetAssetByPath(EditorStartupMap))
        {
            OpenAssetEditor(Data->AssetGUID);
        }
        
        auto Recents = GConfig->Get<std::vector<std::string>>("Editor.RecentProjects");
        bool bDoesNotContains = eastl::none_of(Recents.begin(), Recents.end(), [&](const std::string& Item)
        {
            return Item == std::string(GEngine->GetProjectName().data());
        });
        
        if (bDoesNotContains)
        {
            Recents.emplace_back(GEngine->GetProjectName().data());
            GConfig->Set("Editor.RecentProjects", Recents);
        }
    }

    void FEditorUI::AssetRegistryDialog()
    {
        struct FAssetDialogueState
        {
            FAssetData* SelectedData = nullptr;
        };
        
        auto DialogueState = MakeUnique<FAssetDialogueState>();
        
        ModalManager.CreateDialogue("Asset Registry", ImVec2(1000, 700), [DialogueState = Move(DialogueState)] () -> bool
        {
            ImGui::BeginChild("SettingsCategories", ImVec2(200, 0), true);
            {
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Assets");
                ImGui::Separator();
                ImGui::Spacing();
                
                const FAssetDataMap& Assets = FAssetRegistry::Get().GetAssets();
                
                if (ImGui::BeginTable("##AssetList", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableHeadersRow();
    
                    for (const TUniquePtr<FAssetData>& Asset : Assets)
                    {
                        ImGui::TableNextRow();
                        
                        ImGui::TableNextColumn();
                        bool bIsSelected = (DialogueState->SelectedData == Asset.get());
                        if (ImGui::Selectable(Asset->AssetName.c_str(), bIsSelected))
                        {
                            DialogueState->SelectedData = Asset.get();
                        }
                    }
                    
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
            
            ImGui::SameLine();
            
            ImGui::BeginChild("SettingsContent", ImVec2(0, -40), true);
            {
                if (DialogueState->SelectedData)
                {
                    FAssetData* Asset = DialogueState->SelectedData;
                    
                    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Asset Details");
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    ImGui::Columns(2, nullptr, false);
                    ImGui::SetColumnWidth(0, 100);
                    
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Name:");
                    ImGui::NextColumn();
                    ImGui::TextUnformatted(Asset->AssetName.c_str());
                    ImGui::NextColumn();
                    ImGui::Spacing();
                    
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Class:");
                    ImGui::NextColumn();
                    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s", Asset->AssetClass.c_str());
                    ImGui::NextColumn();
                    ImGui::Spacing();
                    
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Path:");
                    ImGui::NextColumn();
                    ImGui::TextWrapped("%s", Asset->Path.c_str());
                    ImGui::NextColumn();
                    ImGui::Spacing();
                    
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "GUID:");
                    ImGui::NextColumn();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", Asset->AssetGUID.ToString().c_str());
                    ImGui::NextColumn();
                    ImGui::Spacing();

                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Size On Disk:");
                    ImGui::NextColumn();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", ImGuiX::FormatSize(VFS::Size(Asset->Path)).c_str());
                    ImGui::NextColumn();
                    
                    ImGui::Columns(1);
                }
                else
                {
                    ImGui::TextDisabled("Select an asset to view details");
                }
            }
            ImGui::EndChild();
            
            ImGui::Spacing();
            
            if (ImGui::Button("Close", ImVec2(120, 0)))
            {
                return true;
            }
            
            return false;
        }, false);
    }
    
}
