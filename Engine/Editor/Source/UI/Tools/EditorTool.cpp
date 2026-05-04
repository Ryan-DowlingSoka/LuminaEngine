
#include "EditorTool.h"

#include <Tools/PrimitiveManager/PrimitiveManager.h>

#include "imgui_internal.h"
#include "ToolFlags.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Core/Object/Package/Package.h"
#include "Core/Windows/Window.h"
#include "Core/Serialization/MemoryArchiver.h"
#include "Core/Serialization/ObjectArchiver.h"
#include "Thumbnails/ThumbnailManager.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "UI/Tools/EditorAssetDropHandlers.h"
#include "Core/Application/Application.h"
#include "Core/UpdateContext.h"
#include "Input/InputContext.h"
#include "Input/InputProcessor.h"
#include "Input/InputViewport.h"
#include "World/WorldManager.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/EditorComponent.h"
#include "World/Entity/Components/InputComponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Entity/Components/TransformComponent.h"

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

    FEditorTool::~FEditorTool() = default;

    void FEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);
    }

    void FEditorTool::GenerateThumbnail(CPackage* Package)
    {
        if (!World || !World->GetRenderer())
        {
            return;
        }
        
        FRHIImageRef RenderTarget = World->GetRenderer()->GetRenderTarget();
        if (!RenderTarget)
        {
            return;
        }
    
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

            InputViewport = MakeUnique<FInputViewport>();
            InputViewport->SetWorld(World);
            InputViewport->GetContext().SetInputMode(EInputMode::Game);
            FInputViewportRegistry::Get().Register(InputViewport.get());
        }

        OnInitialize();
    }

    void FEditorTool::Deinitialize(const FUpdateContext& UpdateContext)
    {
        OnDeinitialize(UpdateContext);

        ToolWindows.clear();

        if (InputViewport)
        {
            FInputViewportRegistry::Get().Unregister(InputViewport.get());
            InputViewport.reset();
        }

        if (HasWorld())
        {
            GWorldManager->DestroyWorldContext(World);
            World->ForceDestroyNow();
            World = nullptr;
        }

        ToolWindows.clear();
    }

    void FEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        TickEditorCamera(UpdateContext.GetDeltaTime());
        TickEditorActions();
    }

    void FEditorTool::TickEditorActions()
    {
        if (EditorActions.empty())
        {
            return;
        }

        // Don't fire shortcuts while a text input field is active.
        const ImGuiIO& IO = ImGui::GetIO();
        if (IO.WantTextInput)
        {
            return;
        }

        for (const FEditorAction& Action : EditorActions)
        {
            const FInputChord& Chord = Action.DefaultChord;
            if (!Chord.IsValid() || !Action.Callback)
            {
                continue;
            }
            if (Chord.bCtrl  != IO.KeyCtrl)  continue;
            if (Chord.bShift != IO.KeyShift) continue;
            if (Chord.bAlt   != IO.KeyAlt)   continue;

            const bool bTriggered = ImGui::IsKeyPressed(Chord.Key, Action.bRepeatOnHold);
            if (!bTriggered)
            {
                continue;
            }
            if (Action.CanExecute && !Action.CanExecute())
            {
                continue;
            }
            Action.Callback();
        }
    }

    FString FInputChord::ToDisplayString() const
    {
        if (!IsValid())
        {
            return FString();
        }
        FString Out;
        if (bCtrl)  Out += "Ctrl+";
        if (bShift) Out += "Shift+";
        if (bAlt)   Out += "Alt+";
        Out += ImGui::GetKeyName(Key);
        return Out;
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

        if (InputViewport)
        {
            InputViewport->SetWorld(World);
        }

        SetupWorldForTool();
    }

    void FEditorTool::RebindToWorld(CWorld* InWorld)
    {
        World = InWorld;
        if (InputViewport)
        {
            InputViewport->SetWorld(InWorld);
        }
    }

    void FEditorTool::SetupWorldForTool()
    {
        EditorEntity = World->ConstructEntity("Editor Entity");
        World->GetEntityRegistry().emplace<FHideInSceneOutliner>(EditorEntity);
        World->GetEntityRegistry().emplace<SCameraComponent>(EditorEntity);
        World->GetEntityRegistry().emplace<SInputComponent>(EditorEntity);
        World->GetEntityRegistry().emplace<FEditorComponent>(EditorEntity);
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
        MeshComponent.StaticMesh = CPrimitiveManager::Get().PlaneMesh;
        
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
            DrawKeybindsMenu();

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
        if ((bViewportFocused || bViewportHovered) && ImGui::IsKeyPressed(ImGuiKey_F11, false))
        {
            ToggleViewportFullscreen();
        }

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

        if (InputViewport)
        {
            // ImGui multi-viewport returns absolute monitor coords; GLFW events use
            // window-relative coords. Subtract the window origin so they match.
            int WindowX = 0;
            int WindowY = 0;
            Windowing::GetPrimaryWindowHandle()->GetWindowPosition(WindowX, WindowY);

            const ImVec2 PanelMin(CursorScreenPos.x - float(WindowX), CursorScreenPos.y - float(WindowY));
            const ImVec2 PanelMax(PanelMin.x + ViewportSize.x, PanelMin.y + ViewportSize.y);
            InputViewport->SetWindowRect(int(PanelMin.x), int(PanelMin.y), int(PanelMax.x), int(PanelMax.y));

            uint32 RTW = uint32(eastl::max(ViewportSize.x, 1.0f));
            uint32 RTH = uint32(eastl::max(ViewportSize.y, 1.0f));
            if (World != nullptr)
            {
                if (IRenderScene* Scene = World->GetRenderer())
                {
                    if (FRHIImage* RT = Scene->GetRenderTarget())
                    {
                        RTW = RT->GetSizeX();
                        RTH = RT->GetSizeY();
                    }
                }
            }
            InputViewport->SetRenderTargetSize(RTW, RTH);

            InputViewport->SetHovered(bViewportHovered);
            InputViewport->SetFocused(bViewportFocused);

            if (bViewportHovered)
            {
                FInputViewportRegistry::Get().SetHoveredViewport(InputViewport.get());
            }
            else if (FInputViewportRegistry::Get().GetHoveredViewport() == InputViewport.get())
            {
                FInputViewportRegistry::Get().SetHoveredViewport(nullptr);
            }

            if (bViewportFocused)
            {
                FInputViewportRegistry::Get().SetFocusedViewport(InputViewport.get());
                FInputViewportRegistry::Get().SetActiveViewport(InputViewport.get());
            }
        }

        const ImGuiStyle& ImStyle = ImGui::GetStyle();

        ImVec2 Origin = ImGui::GetCursorStartPos();

        ImGui::Dummy(ImStyle.ItemSpacing);
        ImGui::SetCursorPos(Origin + ImStyle.ItemSpacing);
        DrawViewportOverlayElements(UpdateContext, ViewportTexture, ViewportSize);

        Origin = ImGui::GetCursorStartPos();

        ImGui::Dummy(ImStyle.ItemSpacing);
        ImGui::SetCursorPos(Origin + ImStyle.ItemSpacing);
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

        // Resolve to world space — local would mis-frame any entity parented under another.
        const glm::vec3 EntityWorldLocation = EntityTransform.GetWorldLocation();
        const float FocusDistance = (CameraState.Mode == EEditorCameraMode::Orbit) ? CameraState.OrbitDistance : 10.0f;

        if (CameraState.Mode == EEditorCameraMode::Orbit)
        {
            // Re-anchor on the focused entity; the orbit-target lerp in TickEditorCamera
            // drives toward this position over a few frames. Anchor snaps so a later
            // ResetOrbitPan returns to the focused entity.
            CameraState.OrbitAnchor      = EntityWorldLocation;
            CameraState.FocusOrbitTarget = EntityWorldLocation;
            CameraState.FocusOrbitDistance = FocusDistance;
            CameraState.bFocusInterp     = true;
            return;
        }

        glm::vec3 CurrentForward = EditorTransform.GetForward();
        CameraState.FocusFreePosition = EntityWorldLocation - CurrentForward * FocusDistance;
        CameraState.FocusFreeRotation = Math::FindLookAtRotation(EntityWorldLocation, CameraState.FocusFreePosition);
        CameraState.bFocusInterp      = true;
    }

    void FEditorTool::SetCameraMode(EEditorCameraMode Mode)
    {
        if (CameraState.Mode == Mode)
        {
            return;
        }

        // When entering Orbit, derive yaw/pitch/distance from the current camera so the
        // first frame doesn't snap. If the camera was looking through OrbitTarget already,
        // distance falls out naturally; otherwise it's just the magnitude of the offset.
        if (Mode == EEditorCameraMode::Orbit && HasWorld() && EditorEntity != entt::null
            && World->GetEntityRegistry().valid(EditorEntity))
        {
            const STransformComponent& Transform = World->GetEntityRegistry().get<STransformComponent>(EditorEntity);
            const glm::vec3 Position = Transform.GetLocation();
            const glm::vec3 Offset   = Position - CameraState.OrbitTarget;
            const float Distance = glm::length(Offset);

            CameraState.OrbitDistance = glm::max(Distance, 0.1f);
            // Yaw is around world-Y measured from +Z; pitch is the elevation above the XZ plane.
            CameraState.OrbitYaw   = glm::degrees(std::atan2(Offset.x, Offset.z));
            CameraState.OrbitPitch = glm::degrees(std::asin(glm::clamp(Offset.y / CameraState.OrbitDistance, -1.0f, 1.0f)));
        }

        CameraState.Mode = Mode;
        CameraState.Velocity = glm::vec3(0.0f);

        // Snap the entity transform now so the first frame after SetupWorldForTool sees the
        // mesh framed correctly — TickEditorCamera otherwise wouldn't run until the viewport
        // gets focus, leaving the camera at FEditorTool::SetupWorldForTool's default origin.
        if (Mode == EEditorCameraMode::Orbit)
        {
            ApplyOrbitTransform();
        }
    }

    void FEditorTool::SetOrbitTarget(const glm::vec3& Target, float Distance)
    {
        CameraState.OrbitTarget = Target;
        CameraState.OrbitAnchor = Target;
        if (Distance > 0.0f)
        {
            CameraState.OrbitDistance = Distance;
        }

        if (CameraState.Mode == EEditorCameraMode::Orbit)
        {
            ApplyOrbitTransform();
        }
    }

    void FEditorTool::ResetOrbitPan()
    {
        CameraState.OrbitTarget = CameraState.OrbitAnchor;
        if (CameraState.Mode == EEditorCameraMode::Orbit)
        {
            ApplyOrbitTransform();
        }
    }

    void FEditorTool::ApplyOrbitTransform()
    {
        if (!HasWorld() || EditorEntity == entt::null)
        {
            return;
        }
        if (!World->GetEntityRegistry().valid(EditorEntity))
        {
            return;
        }

        const float YawRad   = glm::radians(CameraState.OrbitYaw);
        const float PitchRad = glm::radians(CameraState.OrbitPitch);
        const glm::vec3 Offset(
            CameraState.OrbitDistance * std::cos(PitchRad) * std::sin(YawRad),
            CameraState.OrbitDistance * std::sin(PitchRad),
            CameraState.OrbitDistance * std::cos(PitchRad) * std::cos(YawRad));

        const glm::vec3 NewPosition = CameraState.OrbitTarget + Offset;
        STransformComponent& Transform = World->GetEntityRegistry().get<STransformComponent>(EditorEntity);
        Transform.SetLocation(NewPosition);
        Transform.SetRotation(Math::FindLookAtRotation(CameraState.OrbitTarget, NewPosition));
    }

    void FEditorTool::DrawCameraModeSelector(float ItemWidth)
    {
        const char* Label = (CameraState.Mode == EEditorCameraMode::Orbit) ? "Orbit" : "Free";
        ImGui::PushItemWidth(ItemWidth);
        if (ImGui::BeginCombo("##CameraMode", Label, ImGuiComboFlags_HeightLarge))
        {
            if (ImGui::Selectable("Free", CameraState.Mode == EEditorCameraMode::Free))
            {
                SetCameraMode(EEditorCameraMode::Free);
            }
            if (ImGui::Selectable("Orbit", CameraState.Mode == EEditorCameraMode::Orbit))
            {
                SetCameraMode(EEditorCameraMode::Orbit);
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        // Reset-pan button: only meaningful in orbit mode, and only when MMB-drag has actually
        // moved OrbitTarget off its anchor. Hidden otherwise so it doesn't add noise.
        if (CameraState.Mode == EEditorCameraMode::Orbit)
        {
            const bool bPanned = glm::distance(CameraState.OrbitTarget, CameraState.OrbitAnchor) > 1e-4f;
            ImGui::SameLine();
            ImGui::BeginDisabled(!bPanned);
            if (ImGui::Button(LE_ICON_HOME "##ResetPan"))
            {
                ResetOrbitPan();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Reset Pan (return to anchor)");
            }
        }
    }

    void FEditorTool::TickEditorCamera(double DeltaTime)
    {
        if (!HasWorld() || EditorEntity == entt::null)
        {
            return;
        }
        if (!World->GetEntityRegistry().valid(EditorEntity))
        {
            return;
        }

        FInputProcessor& Input = FInputProcessor::Get();
        const bool bWantLook = bViewportFocused && Input.IsMouseButtonDown(EMouseKey::ButtonRight);
        const bool bWantPan  = bViewportFocused
                            && CameraState.Mode == EEditorCameraMode::Orbit
                            && Input.IsMouseButtonDown(EMouseKey::ButtonMiddle);
        const bool bWantsCaptured = bWantLook || bWantPan;
        
        if (CameraState.bWasLooking && !bWantsCaptured)
        {
            Input.SetMouseMode(EMouseMode::Normal);
        }
        CameraState.bWasLooking = bWantsCaptured;

        STransformComponent& Transform = World->GetEntityRegistry().get<STransformComponent>(EditorEntity);

        // Advance any in-flight focus lerp before reading user input. Movement input
        // from the focused viewport cancels the lerp so the user can take over mid-flight.
        if (CameraState.bFocusInterp)
        {
            const bool bWheel = bViewportFocused && Input.GetMouseZ() != 0.0;
            bool bMoveInput = false;
            if (bViewportFocused)
            {
                if (CameraState.Mode == EEditorCameraMode::Free)
                {
                    bMoveInput = bWantLook
                        || Input.IsKeyDown(EKey::W) || Input.IsKeyDown(EKey::A)
                        || Input.IsKeyDown(EKey::S) || Input.IsKeyDown(EKey::D)
                        || Input.IsKeyDown(EKey::E) || Input.IsKeyDown(EKey::Q);
                }
                else
                {
                    bMoveInput = bWantLook || bWantPan;
                }
            }

            if (bMoveInput || bWheel)
            {
                CameraState.bFocusInterp = false;
            }
            else
            {
                const float Alpha = 1.0f - std::exp(-CameraState.FocusInterpRate * static_cast<float>(DeltaTime));
                if (CameraState.Mode == EEditorCameraMode::Free)
                {
                    const glm::vec3 NewLoc = glm::mix(Transform.GetLocation(), CameraState.FocusFreePosition, Alpha);
                    const glm::quat NewRot = glm::slerp(Transform.GetRotation(), CameraState.FocusFreeRotation, Alpha);
                    Transform.SetLocation(NewLoc);
                    Transform.SetRotation(NewRot);

                    if (glm::distance(NewLoc, CameraState.FocusFreePosition) < 1e-3f)
                    {
                        Transform.SetLocation(CameraState.FocusFreePosition);
                        Transform.SetRotation(CameraState.FocusFreeRotation);
                        CameraState.bFocusInterp = false;
                    }
                }
                else
                {
                    CameraState.OrbitTarget   = glm::mix(CameraState.OrbitTarget, CameraState.FocusOrbitTarget, Alpha);
                    CameraState.OrbitDistance = glm::mix(CameraState.OrbitDistance, CameraState.FocusOrbitDistance, Alpha);

                    if (glm::distance(CameraState.OrbitTarget, CameraState.FocusOrbitTarget) < 1e-3f)
                    {
                        CameraState.OrbitTarget   = CameraState.FocusOrbitTarget;
                        CameraState.OrbitDistance = CameraState.FocusOrbitDistance;
                        CameraState.bFocusInterp = false;
                    }
                }
            }
        }

        if (CameraState.Mode == EEditorCameraMode::Free)
        {
            // Free-cam consumes input directly into the entity transform; no derived state
            // to apply if the viewport isn't focused, so just bail. Skip input while the
            // focus lerp is still running so the camera doesn't fight the lerp.
            if (!bViewportFocused || CameraState.bFocusInterp)
            {
                return;
            }

            const glm::vec3 Forward = Transform.GetForward();
            const glm::vec3 Right   = Transform.GetRight();
            const glm::vec3 Up      = Transform.GetUp();

            float Speed = CameraState.Speed;
            if (Input.IsKeyDown(EKey::LeftShift))
            {
                Speed *= 10.0f;
            }

            glm::vec3 Acceleration(0.0f);
            if (Input.IsKeyDown(EKey::W)) Acceleration += Forward;
            if (Input.IsKeyDown(EKey::S)) Acceleration -= Forward;
            if (Input.IsKeyDown(EKey::D)) Acceleration += Right;
            if (Input.IsKeyDown(EKey::A)) Acceleration -= Right;
            if (Input.IsKeyDown(EKey::E)) Acceleration += Up;
            if (Input.IsKeyDown(EKey::Q)) Acceleration -= Up;

            if (glm::length(Acceleration) > 0.0f)
            {
                Acceleration = glm::normalize(Acceleration) * Speed;
            }

            CameraState.Velocity += Acceleration * static_cast<float>(DeltaTime);
            constexpr float Drag = 10.0f;
            CameraState.Velocity -= CameraState.Velocity * Drag * static_cast<float>(DeltaTime);

            Transform.Translate(CameraState.Velocity * static_cast<float>(DeltaTime) * CameraState.SpeedScale);

            if (bWantLook)
            {
                Input.SetMouseMode(EMouseMode::Captured);

                Transform.AddYaw(static_cast<float>(Input.GetMouseDeltaX() * 0.1));
                Transform.AddPitch(static_cast<float>(Input.GetMouseDeltaY() * 0.1));

                const double WheelZ = Input.GetMouseZ();
                CameraState.SpeedScale += Math::Pow(1.05f, CameraState.SpeedScale) * static_cast<float>(WheelZ);
                CameraState.SpeedScale = Math::Clamp(CameraState.SpeedScale, 0.2f, 100.0f);
            }
        }
        else // Orbit
        {
            // Input is gated on focus, but the transform application below is not — the
            // orbit camera position is purely derived from CameraState, so we always want
            // it written back to the entity (otherwise the first frame after SetupWorldForTool
            // renders with the default origin transform until the user clicks the viewport).
            if (bViewportFocused)
            {
                if (bWantLook)
                {
                    Input.SetMouseMode(EMouseMode::Captured);
                    CameraState.OrbitYaw   -= static_cast<float>(Input.GetMouseDeltaX() * 0.4);
                    CameraState.OrbitPitch += static_cast<float>(Input.GetMouseDeltaY() * 0.4);
                    CameraState.OrbitPitch = glm::clamp(CameraState.OrbitPitch, -89.0f, 89.0f);
                }

                if (Input.IsMouseButtonDown(EMouseKey::ButtonMiddle))
                {
                    Input.SetMouseMode(EMouseMode::Captured);
                    const float PanScale = CameraState.OrbitDistance * 0.002f;
                    const glm::vec3 Right = Transform.GetRight();
                    const glm::vec3 Up    = Transform.GetUp();
                    CameraState.OrbitTarget -= Right * static_cast<float>(Input.GetMouseDeltaX()) * PanScale;
                    CameraState.OrbitTarget += Up    * static_cast<float>(Input.GetMouseDeltaY()) * PanScale;
                }

                const double WheelZ = Input.GetMouseZ();
                if (WheelZ != 0.0)
                {
                    const float Zoom = 0.1f * CameraState.OrbitDistance;
                    CameraState.OrbitDistance -= static_cast<float>(WheelZ) * Zoom;
                    CameraState.OrbitDistance = glm::max(CameraState.OrbitDistance, 0.05f);
                }
            }

            ApplyOrbitTransform();
        }
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
    
    void FEditorTool::DrawKeybindsMenu()
    {
        const bool bDisabled = EditorActions.empty();
        ImGui::BeginDisabled(bDisabled);
        const bool bOpen = ImGui::BeginMenu(LE_ICON_KEYBOARD" Keybinds");
        ImGui::EndDisabled();
        if (!bOpen)
        {
            return;
        }

        // Group by category, preserve registration order within each.
        TVector<FString> CategoryOrder;
        THashMap<FString, TVector<const FEditorAction*>> ByCategory;
        for (const FEditorAction& A : EditorActions)
        {
            if (ByCategory.find(A.Category) == ByCategory.end())
            {
                CategoryOrder.push_back(A.Category);
            }
            ByCategory[A.Category].push_back(&A);
        }

        if (ImGui::BeginTable("KeybindsTable", 2,
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Action",   ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, 0.4f);

            for (const FString& Category : CategoryOrder)
            {
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", Category.empty() ? "General" : Category.c_str());
                ImGui::TableNextColumn();

                for (const FEditorAction* A : ByCategory[Category])
                {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(A->Name.c_str());
                    if (!A->Description.empty() && ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", A->Description.c_str());
                    }

                    ImGui::TableNextColumn();
                    const FString Chord = A->DefaultChord.ToDisplayString();
                    ImGui::TextUnformatted(Chord.empty() ? "—" : Chord.c_str());
                }
            }
            ImGui::EndTable();
        }

        ImGui::EndMenu();
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

    //
    // Transactions snapshot the entity registry on Begin and again on End; Undo restores the
    // before-state, Redo restores the after. Lives on the base so every editor tool with a
    // World (FWorldEditorTool, FAssetEditorTool subclasses with preview worlds) gets undo
    // for free.

    static constexpr int32 GMaxUndoHistory = 64;

    void FEditorTool::BeginTransaction()
    {
        if (World == nullptr)
        {
            return;
        }

        PendingBeforeState.clear();

        FMemoryWriter Writer(PendingBeforeState);
        FObjectProxyArchiver Ar(Writer, false);
        ECS::Utils::SerializeRegistry(Ar, World->GetEntityRegistry());

        RedoStack.clear();
    }

    void FEditorTool::EndTransaction(FName Name)
    {
        if (World == nullptr)
        {
            return;
        }

        TVector<uint8> AfterState;
        FMemoryWriter Writer(AfterState);
        FObjectProxyArchiver Ar(Writer, false);
        ECS::Utils::SerializeRegistry(Ar, World->GetEntityRegistry());

        if (UndoStack.size() >= GMaxUndoHistory)
        {
            UndoStack.erase(UndoStack.begin());
        }

        UndoStack.push_back({ Name, PendingBeforeState, eastl::move(AfterState) });
        PendingBeforeState.clear();

        RedoStack.clear();

        if (World->GetPackage())
        {
            World->GetPackage()->MarkDirty();
        }
    }

    void FEditorTool::Undo()
    {
        if (UndoStack.empty() || World == nullptr)
        {
            return;
        }

        FTransaction& Transaction = UndoStack.back();

        ImGuiX::Notifications::NotifyInfo("Undid {}", Transaction.Name);

        RedoStack.push_back(Transaction);

        FMemoryReader Reader(Transaction.BeforeState);
        FObjectProxyArchiver Ar(Reader, true);

        FEntityRegistry& Registry = World->GetEntityRegistry();
        ECS::Utils::SerializeRegistry(Ar, Registry);

        OnPostUndoRedo();

        UndoStack.pop_back();

        if (World->GetPackage())
        {
            World->GetPackage()->MarkDirty();
        }
    }

    void FEditorTool::Redo()
    {
        if (RedoStack.empty() || World == nullptr)
        {
            return;
        }

        FTransaction& Transaction = RedoStack.back();

        ImGuiX::Notifications::NotifyInfo("Redid {}", Transaction.Name);

        UndoStack.push_back(Transaction);

        FMemoryReader Reader(Transaction.AfterState);
        FObjectProxyArchiver Ar(Reader, true);

        FEntityRegistry& Registry = World->GetEntityRegistry();
        ECS::Utils::SerializeRegistry(Ar, Registry);

        OnPostUndoRedo();

        RedoStack.pop_back();

        if (World->GetPackage())
        {
            World->GetPackage()->MarkDirty();
        }
    }

    void FEditorTool::ClearTransactionHistory()
    {
        UndoStack.clear();
        RedoStack.clear();
        PendingBeforeState.clear();
    }

    FTransform FEditorTool::GetCameraSpawnTransform(float DistanceForward) const
    {
        FTransform Result;
        if (World == nullptr)
        {
            return Result;
        }

        // Prefer the world's active camera; fall back to the EditorEntity (asset editors
        // typically own the camera there directly and may not call SetActiveCamera).
        const SCameraComponent* Camera = World->GetActiveCamera();
        if (Camera == nullptr && EditorEntity != entt::null && World->GetEntityRegistry().valid(EditorEntity))
        {
            Camera = World->GetEntityRegistry().try_get<SCameraComponent>(EditorEntity);
        }

        if (Camera == nullptr)
        {
            return Result;
        }

        const glm::vec3 Position = Camera->GetPosition() + Camera->GetForwardVector() * DistanceForward;
        Result.SetLocation(Position);
        return Result;
    }

    entt::entity FEditorTool::HandleContentBrowserAssetDrop(FStringView VirtualPath, entt::entity DropTarget)
    {
        if (World == nullptr || VirtualPath.empty())
        {
            return entt::null;
        }

        FAssetData* AssetData = FAssetRegistry::Get().GetAssetByPath(VirtualPath);
        if (AssetData == nullptr)
        {
            return entt::null;
        }

        const FEditorAssetDropHandler* Handler = FEditorAssetDropRegistry::Get().FindHandler(AssetData->AssetClass);
        if (Handler == nullptr || !*Handler)
        {
            return entt::null;
        }

        CObject* Loaded = LoadObject<CObject>(AssetData->AssetGUID);
        if (Loaded == nullptr)
        {
            return entt::null;
        }

        const FTransform SpawnTransform = GetCameraSpawnTransform();
        return (*Handler)(World, Loaded, SpawnTransform, DropTarget);
    }
}
