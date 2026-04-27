
#include "EditorTool.h"
#include "imgui_internal.h"
#include "ToolFlags.h"
#include "Core/Object/Package/Package.h"
#include "Thumbnails/ThumbnailManager.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "Core/Application/Application.h"
#include "UI/RmlUiBridge.h"
#include <RmlUi/Core/Input.h>
#include "World/WorldManager.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/EditorComponent.h"
#include "World/Entity/Components/InputComponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Entity/Components/VelocityComponent.h"
#include "World/Entity/Systems/EditorEntityMovementSystem.h"

namespace Lumina
{
    
    FEditorTool::FEditorTool(IEditorToolContext* Context, const FString& DisplayName, CWorld* InWorld)
        : ToolContext(Context)
        , ToolName(DisplayName)
        , World(InWorld)
        , EditorEntity(entt::null)
    {
        ToolFlags |= EEditorToolFlags::Tool_WantsToolbar;
    }

    void FEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);
    }

    void FEditorTool::GenerateThumbnail(CPackage* Package)
    {
        if (!World)
        {
            return;
        }
        
        FRHIImageRef RenderTarget = World->GetRenderer()->GetRenderTarget();
    
        FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
        CommandList->Open();
    
        FRHIStagingImageRef StagingImage = GRenderContext->CreateStagingImage(RenderTarget->GetDescription(), ERHIAccess::HostRead);
        CommandList->CopyImage(RenderTarget, FTextureSlice(), StagingImage, FTextureSlice());
    
        CommandList->Close();
        GRenderContext->ExecuteCommandList(CommandList);
    
        size_t RowPitch = 0;
        void* MappedMemory = GRenderContext->MapStagingTexture(StagingImage, FTextureSlice(), ERHIAccess::HostRead, &RowPitch);
        if (!MappedMemory)
        {
            return;
        }
        
        Package->GetPackageThumbnail()->LoadState.store(FPackageThumbnail::EState::None, std::memory_order_relaxed);
    
        const uint32 SourceWidth  = RenderTarget->GetDescription().Extent.x;
        const uint32 SourceHeight = RenderTarget->GetDescription().Extent.y;
        
    
        // Thumbnail dimensions
        constexpr uint32 ThumbWidth = 256;
        constexpr uint32 ThumbHeight = 256;
        
        Package->GetPackageThumbnail()->ImageWidth = ThumbWidth;
        Package->GetPackageThumbnail()->ImageHeight = ThumbHeight;

        constexpr size_t BytesPerPixel = 4;
        constexpr size_t TotalBytes = ThumbWidth * ThumbHeight * BytesPerPixel;
        
        Package->GetPackageThumbnail()->ImageData.resize(TotalBytes);
        
        const uint8* SourceData = static_cast<const uint8*>(MappedMemory);
        uint8* DestData = Package->GetPackageThumbnail()->ImageData.data();
        
        // Downsample with bilinear filtering
        const float ScaleX = static_cast<float>(SourceWidth) / ThumbWidth;
        const float ScaleY = static_cast<float>(SourceHeight) / ThumbHeight;
        
        for (uint32 DestY = 0; DestY < ThumbHeight; ++DestY)
        {
            const uint32 FlippedDestY = ThumbHeight - 1 - DestY;
    
            for (uint32 DestX = 0; DestX < ThumbWidth; ++DestX)
            {
                const float SrcX = DestX * ScaleX;
                const float SrcY = DestY * ScaleY;

                const uint32 X0 = static_cast<uint32>(SrcX);
                const uint32 Y0 = static_cast<uint32>(SrcY);
                const uint32 X1 = Math::Min(X0 + 1, SourceWidth - 1);
                const uint32 Y1 = Math::Min(Y0 + 1, SourceHeight - 1);

                const float FracX = SrcX - X0;
                const float FracY = SrcY - Y0;

                const uint8* P00 = SourceData + (Y0 * RowPitch) + (X0 * BytesPerPixel);
                const uint8* P10 = SourceData + (Y0 * RowPitch) + (X1 * BytesPerPixel);
                const uint8* P01 = SourceData + (Y1 * RowPitch) + (X0 * BytesPerPixel);
                const uint8* P11 = SourceData + (Y1 * RowPitch) + (X1 * BytesPerPixel);

                uint8* DestPixel = DestData + (FlippedDestY * ThumbWidth * BytesPerPixel) + (DestX * BytesPerPixel);

                for (uint32 Channel = 0; Channel < BytesPerPixel; ++Channel)
                {
                    const float Top     = Math::Lerp(static_cast<float>(P00[Channel]), static_cast<float>(P10[Channel]), FracX);
                    const float Bottom  = Math::Lerp(static_cast<float>(P01[Channel]), static_cast<float>(P11[Channel]), FracX);
                    const float Result  = Math::Lerp(Top, Bottom, FracY);

                    DestPixel[Channel] = static_cast<uint8>(Result + 0.5f);
                }
            }
        }
        
        GRenderContext->UnMapStagingTexture(StagingImage);
    }

    void FEditorTool::Initialize()
    {
        ToolName = std::format("{0} {1}", GetTitlebarIcon(), GetToolName().c_str()).c_str();

        if (HasWorld())
        {
            if (World->GetPhysicsScene() == nullptr)
            {
                GWorldManager->CreateWorldContext(World, EWorldType::Editor);
            }

            SetupWorldForTool();

            Internal_CreateViewportTool();
        }

        OnInitialize();
    }

    void FEditorTool::Deinitialize(const FUpdateContext& UpdateContext)
    {
        OnDeinitialize(UpdateContext);
        
        ToolWindows.clear();
        
        if (HasWorld())
        {
            GWorldManager->DestroyWorldContext(World);
            World->ForceDestroyNow();
            World = nullptr;
        }

        ToolWindows.clear();
    }

    ImGuiID FEditorTool::CalculateDockspaceID() const
    {
        uint32 DockspaceID = CurrLocationID;
        char const* const EditorToolTypeName = GetUniqueTypeName();
        DockspaceID = ImHashData(EditorToolTypeName, strlen(EditorToolTypeName), DockspaceID);
        return DockspaceID;
    }

    void FEditorTool::SetWorld(CWorld* InWorld)
    {
        if (World == InWorld)
        {
            return;
        }
        
        if (World.IsValid())
        {
            GWorldManager->DestroyWorldContext(World);
            World->ForceDestroyNow();
            World = nullptr;
        }

        World = InWorld;

        if (World->GetPhysicsScene() == nullptr)
        {
            GWorldManager->CreateWorldContext(World, EWorldType::Editor);
        }
        
        SetupWorldForTool();
    }

    void FEditorTool::SetupWorldForTool()
    {
        EditorEntity = World->ConstructEntity("Editor Entity");
        World->GetEntityRegistry().emplace<FHideInSceneOutliner>(EditorEntity);
        World->GetEntityRegistry().emplace<SCameraComponent>(EditorEntity);
        World->GetEntityRegistry().emplace<SInputComponent>(EditorEntity);
        World->GetEntityRegistry().emplace<FEditorComponent>(EditorEntity);
        World->GetEntityRegistry().emplace<SVelocityComponent>(EditorEntity).Speed = 50.0f;
        World->GetEntityRegistry().get<STransformComponent>(EditorEntity).SetLocation(glm::vec3(0.0f, 1.25f, 3.25f));

        World->SetActiveCamera(EditorEntity);
    }

    entt::entity FEditorTool::CreateFloorPlane(float YOffset, float ScaleX, float ScaleY)
    {
        FTransform Transform;
        Transform.Rotate({-90.0f, 0.0f, 0.0f});
        Transform.SetScale(glm::vec3(ScaleX, ScaleY, 1.0f));
        Transform.Translate(glm::vec3(0.0f, YOffset, 0.0f));
        
        entt::entity FloorEntity = World->ConstructEntity("FloorPlane", Transform);
        World->GetEntityRegistry().emplace<FHideInSceneOutliner>(FloorEntity);
        SStaticMeshComponent& MeshComponent = World->GetEntityRegistry().emplace<SStaticMeshComponent>(FloorEntity);
        MeshComponent.StaticMesh = CThumbnailManager::Get().PlaneMesh;
        
        return FloorEntity;
    }

    void FEditorTool::DrawMainToolbar(const FUpdateContext& UpdateContext)
    {
        if (ImGui::MenuItem(LE_ICON_FILE_PLUS_OUTLINE"##New"))
        {
            OnNew();
        }

        if (IsAssetEditorTool())
        {
            if (ImGui::MenuItem(LE_ICON_CONTENT_SAVE"##Save"))
            {
                OnSave();
            }
        }

        ImGui::BeginDisabled(UndoStack.empty());
        
        if (ImGui::MenuItem(LE_ICON_UNDO_VARIANT"##Undo"))
        {
            Undo();
        }
        ImGui::EndDisabled();
        
        ImGuiX::TextTooltip("Undo last transaction");

        ImGui::BeginDisabled(RedoStack.empty());
        
        if (ImGui::MenuItem(LE_ICON_REDO_VARIANT"##Redo"))
        {
            Redo();
        }
        ImGui::EndDisabled();
        
        ImGuiX::TextTooltip("Redo last undo");
        

        if (ImGui::BeginMenu(LE_ICON_HELP_CIRCLE_OUTLINE" Help"))
        {
            if (ImGui::BeginTable("HelpTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                DrawHelpMenu();
                ImGui::EndTable();
            }
            ImGui::EndMenu();
        }

        DrawToolMenu(UpdateContext);
    }

    void FEditorTool::DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize)
    {
        ImGui::Dummy(ImVec2(0, 0));
    }

    bool FEditorTool::DrawViewport(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture)
    {
        const ImVec2 ContentRegion = ImGui::GetContentRegionAvail();
        const ImVec2 ViewportSize(eastl::max(ContentRegion.x, 64.0f), eastl::max(ContentRegion.y, 64.0f));
        const ImVec2 WindowPosition = ImGui::GetCursorScreenPos();
        const ImVec2 WindowBottomRight = { WindowPosition.x + ViewportSize.x, WindowPosition.y + ViewportSize.y };
        float AspectRatio = (ViewportSize.x / ViewportSize.y);
        float t = (ViewportSize.x - 500) / (1200 - 500);
        t = glm::clamp(t, 0.0f, 1.0f);
        float NewFOV = glm::mix(120.0f, 50.0f, t);

        if (SCameraComponent* CameraComponent =  World->GetActiveCamera())
        {
            CameraComponent->SetAspectRatio(AspectRatio);
            CameraComponent->SetFOV(NewFOV);
        }
        
        /** Mostly for debug, so we can easily see if there's some transparency issue */
        ImGui::GetWindowDrawList()->AddRectFilled(WindowPosition, WindowBottomRight, IM_COL32(255, 0, 0, 255));
        
        
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

        // RmlUi mouse forwarding. Translate screen mouse to panel-local
        // fraction to world-RT pixel space. ImGui would otherwise eat events
        // before our auto handler sees them. Suppressed in Game-only mode.
        const EInputMode CurrentInputMode = GApp ? GApp->GetEventProcessor().GetInputMode() : EInputMode::Game;
        const bool bForwardToUi = (CurrentInputMode == EInputMode::UI || CurrentInputMode == EInputMode::GameAndUI);
        if (World != nullptr && bForwardToUi)
        {
            const ImVec2 PanelMin = CursorScreenPos;
            const ImVec2 PanelMax(PanelMin.x + ViewportSize.x, PanelMin.y + ViewportSize.y);
            const bool bHoveringPanel = ImGui::IsMouseHoveringRect(PanelMin, PanelMax, /*clip=*/false);

            if (bHoveringPanel)
            {
                int RTW = int(ViewportSize.x);
                int RTH = int(ViewportSize.y);
                if (IRenderScene* Scene = World->GetRenderer())
                {
                    if (FRHIImage* RT = Scene->GetRenderTarget())
                    {
                        RTW = int(RT->GetSizeX());
                        RTH = int(RT->GetSizeY());
                    }
                }

                const ImVec2 Mouse = ImGui::GetMousePos();
                const float U = (Mouse.x - PanelMin.x) / std::max(1.0f, ViewportSize.x);
                const float V = (Mouse.y - PanelMin.y) / std::max(1.0f, ViewportSize.y);
                const int CtxX = int(U * float(RTW));
                const int CtxY = int(V * float(RTH));

                int Mods = 0;
                using namespace Rml::Input;
                if (ImGui::GetIO().KeyCtrl)  Mods |= KM_CTRL;
                if (ImGui::GetIO().KeyShift) Mods |= KM_SHIFT;
                if (ImGui::GetIO().KeyAlt)   Mods |= KM_ALT;
                if (ImGui::GetIO().KeySuper) Mods |= KM_META;

                RmlUi::ForwardMouseMove(World, CtxX, CtxY, Mods);

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))    RmlUi::ForwardMouseButton(World, 0, true,  Mods);
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))   RmlUi::ForwardMouseButton(World, 0, false, Mods);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))   RmlUi::ForwardMouseButton(World, 1, true,  Mods);
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))  RmlUi::ForwardMouseButton(World, 1, false, Mods);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))  RmlUi::ForwardMouseButton(World, 2, true,  Mods);
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) RmlUi::ForwardMouseButton(World, 2, false, Mods);

                const float Wheel = ImGui::GetIO().MouseWheel;
                if (Wheel != 0.0f)
                {
                    RmlUi::ForwardMouseWheel(World, -Wheel, Mods);
                }
            }
            else
            {
                RmlUi::ForwardMouseLeave(World);
            }
        }

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

    void FEditorTool::DrawViewportToolbar(const FUpdateContext& UpdateContext)
    {
        ImGui::Dummy(ImVec2(0, 0));
    }

    void FEditorTool::FocusViewportToEntity(entt::entity Entity)
    {
        if (!HasWorld())
        {
            return;
        }
    
        if (!World->GetEntityRegistry().valid(Entity))
        {
            return;
        }
    
        const STransformComponent& EntityTransform = World->GetEntityRegistry().get<STransformComponent>(Entity);
        STransformComponent& EditorTransform = World->GetEntityRegistry().get<STransformComponent>(EditorEntity);
        
        float FocusDistance = 10.0f;
    
        glm::vec3 CurrentForward = EditorTransform.GetForward();
        glm::vec3 NewPosition = EntityTransform.GetLocation() - CurrentForward * FocusDistance;
        EditorTransform.SetLocation(NewPosition);
        
        glm::quat Rotation = Math::FindLookAtRotation(EntityTransform.GetLocation(), NewPosition);
        EditorTransform.SetRotation(Rotation);
    }

    void FEditorTool::DrawWorldGrid(int Scale, int Spacing)
    {
        if (World && !World->IsGameWorld() && bWorldGridEnabled)
        {
            for (int i = -Scale; i <= Scale; ++i)
            {
                const float Coord = static_cast<float>(i * Spacing);
                
                const glm::vec4 ZAxisColor  = (i == 0) ? glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) : glm::vec4(0.05f);
                const glm::vec4 XAxisColor  = (i == 0) ? glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) : glm::vec4(0.05f);
                const float AxisThickness   = (i == 0) ? 5.0f : 3.5f;

                World->DrawLine(
                    glm::vec3(Coord, 0, -Scale * Spacing),
                    glm::vec3(Coord, 0,  Scale * Spacing),
                    ZAxisColor,
                    AxisThickness);
                

                World->DrawLine(
                    glm::vec3(-Scale * Spacing, 0, Coord),
                    glm::vec3( Scale * Spacing, 0, Coord),
                    XAxisColor,
                    AxisThickness);
            }
            
            World->DrawLine(
            glm::vec3(0, -Scale, 0),
            glm::vec3(0,  Scale, 0),
            glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),
            8.0f);
        }
    }

    bool FEditorTool::BeginViewportToolbarGroup(char const* GroupID, ImVec2 GroupSize, const ImVec2& Padding)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, 0xFF2C2C2C);
        ImGui::PushStyleColor(ImGuiCol_Header, 0xFF2C2C2C);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, 0xFF2C2C2C);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, 0xFF303030);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, 0xFF3A3A3A);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Padding);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);

        // Adjust "use available" height to default toolbar height
        if (GroupSize.y <= 0)
        {
            GroupSize.y = ImGui::GetFrameHeight();
        }

        return ImGui::BeginChild(GroupID, GroupSize, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    }

    void FEditorTool::EndViewportToolbarGroup()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);
    }

    void FEditorTool::Internal_CreateViewportTool()
    {
        FToolWindow* Tool = CreateToolWindow(ViewportWindowName, nullptr);
        Tool->bViewport = true;
    }

    FEditorTool::FToolWindow* FEditorTool::CreateToolWindow(FName InName, const TFunction<void(bool)>& DrawFunction, const ImVec2& WindowPadding, bool DisableScrolling)
    {
        DEBUG_ASSERT(eastl::none_of(ToolWindows.begin(), ToolWindows.end(), [&](const TUniquePtr<FToolWindow>& W)
        {
            return W->Name == InName;
        }));
        
        auto ToolWindow = MakeUnique<FToolWindow>(InName, DrawFunction, WindowPadding, DisableScrolling); 
        return ToolWindows.emplace_back(Move(ToolWindow)).get();
    }
    
    void FEditorTool::DrawHelpTextRow(const char* Label, const char* Text) const
    {
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        {
            ImGui::TextUnformatted(Label);
        }

        ImGui::TableNextColumn();
        {
            ImGui::TextUnformatted(Text);
        }
    }
}
