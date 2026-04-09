#pragma once

#define USE_IMGUI_API
#include <imgui.h>
#include "EditorTool.h"
#include "ImGuizmo.h"
#include "Core/Delegates/Delegate.h"
#include "Core/Object/Class.h"
#include "Tools/UI/ImGui/Widgets/TreeListView.h"
#include "UI/Properties/PropertyTable.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Systems/EntitySystem.h"

namespace Lumina
{
    DECLARE_MULTICAST_DELEGATE(FOnGamePreview);
    
    /**
     * Base class for display and manipulating scenes.
     */
    class FWorldEditorTool : public FEditorTool
    {
        using Super = FEditorTool;
        LUMINA_SINGLETON_EDITOR_TOOL(FWorldEditorTool)


        struct FEntityListViewItemData
        {
            entt::entity Entity;
        };
        
        struct FEntityListFilterState
        {
            ImGuiTextFilter FilterName;
            TVector<FName> ComponentFilters;
        };

        struct FComponentDestroyRequest
        {
            const CStruct* Type;
            entt::entity EntityID;
        };
        
    public:
        
        FWorldEditorTool(IEditorToolContext* Context, CWorld* InWorld);
        ~FWorldEditorTool() noexcept override = default;
        LE_NO_COPYMOVE(FWorldEditorTool);

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        bool ShouldGenerateThumbnailOnSave() const override { return true; }
        
        void Update(const FUpdateContext& UpdateContext) override;
        void EndFrame() override;
        
        void OnEntityCreated(entt::registry& Registry, entt::entity Entity);

        const char* GetTitlebarIcon() const override;
        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

        void DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize) override;
        void DrawViewportToolbar(const FUpdateContext& UpdateContext) override;

        void PushAddTagModal(entt::entity Entity);
        void PushAddComponentModal(entt::entity Entity);
        void PushRenameEntityModal(entt::entity Entity);

		void OnSave() override;

        NODISCARD bool IsAssetEditorTool() const override;
        FOnGamePreview& GetOnPreviewStartRequestedDelegate() { return OnGamePreviewStartRequested; }
        FOnGamePreview& GetOnPreviewStopRequestedDelegate() { return OnGamePreviewStopRequested; }

        void NotifyPlayInEditorStart();
        void NotifyPlayInEditorStop();

        void SetWorld(CWorld* InWorld) override;
        
        void OnEntityDestroyed(entt::registry& Registry, entt::entity Entity);
        
        void DrawSimulationControls(float ButtonSize);
        void DrawCameraControls(float ButtonSize);
        void DrawViewportOptions(float ButtonSize);
        void DrawSnapSettingsPopup();
        
        bool HasSimulatingWorld() const { return bSimulatingWorld || bGamePreviewRunning; }
        
        void StopAllSimulations();
        
        bool IsUnsavedDocument() override;


    protected:
        
        void BeginTransaction() override;
        void EndTransaction(FName Name) override;
        void Undo() override;
        void Redo() override;
        
        void AddSelectedEntity(entt::entity Entity, bool bRebuild);
        void RemoveSelectedEntity(entt::entity Entity, bool bRebuild);
        void ClearSelectedEntities();
        entt::entity GetLastSelectedEntity() const;
        void ClearLastSelectedEntity() const;
        
        void AddEntityToCopies(entt::entity Entity);
        void RemoveEntityFromCopies(entt::entity Entity);
        void ClearCopies() const;

        void SetWorldPlayInEditor(bool bShouldPlay);
        void SetWorldNewSimulate(bool bShouldSimulate);

        void DrawAddToEntityOrWorldPopup(entt::entity Entity = entt::null);
        void DrawFilterOptions();
        void RebuildSceneOutliner(FTreeListView& Tree);
        void HandleEntityEditorDragDrop(FTreeListView& Tree, entt::entity DropItem);

        void DrawWorldSettings(bool bFocused);
        void DrawOutliner(bool bFocused);
        void DrawEntityProperties(entt::entity Entity);
        void DrawEntityActionButtons(entt::entity Entity);
        void DrawComponentList(entt::entity Entity);
        void DrawTagList(entt::entity Entity);
        void DrawComponentHeader(const TUniquePtr<FPropertyTable>& Table, entt::entity Entity);
        void RemoveComponent(entt::entity Entity, const CStruct* ComponentType);
        void DrawEmptyState();
        
        void OnPrePropertyChangeEvent(const FPropertyChangedEvent& Event);
        void OnPostPropertyChangeEvent(const FPropertyChangedEvent& Event);
        
        void DrawEntityEditor(bool bFocused, entt::entity Entity);

        void DrawPropertyEditor(bool bFocused);

        void RebuildPropertyTables(entt::entity Entity);

        void CreateEntityWithComponent(const CStruct* Component);
        void CreateEntity();

        void CopyEntity(entt::entity& To, entt::entity From);

        void CycleGuizmoOp();
    
    private:

        struct FSelectionBox
        {
            bool bActive = false;
            ImVec2 Start;
            ImVec2 Current;
        } SelectionBox;
        
        TObjectPtr<CWorld>                      ProxyWorld;
        
        ImGuiTextFilter                         AddEntityComponentFilter;
        FEntityListFilterState                  EntityFilterState;
        FOnGamePreview                          OnGamePreviewStartRequested;
        FOnGamePreview                          OnGamePreviewStopRequested;
        
        ImGuizmo::OPERATION                     GuizmoOp;
        ImGuizmo::MODE                          GuizmoMode;
        
        FTreeListView                           OutlinerListView;
        FTreeListViewContext                    OutlinerContext;

        TQueue<FComponentDestroyRequest>        ComponentDestroyRequests;
        TQueue<entt::entity>                    EntityDestroyRequests;
        TVector<TUniquePtr<FPropertyTable>>     PropertyTables;
        TUniquePtr<FPropertyTable>              WorldSettingsPropertyTable;
        

        float                                   GuizmoSnapTranslate = 0.1f;
		float                                   GuizmoSnapRotate = 5.0f;
		float                                   GuizmoSnapScale = 0.1f;

        
        bool                                    bShowComponentVisualizers = true;
		bool									bGuizmoSnapEnabled = true;
        bool                                    bGamePreviewRunning = false;
        bool                                    bSimulatingWorld = false;
        
        /** IDK, this thing will return IsUsing = true always if it's never been used */
        bool                                    bImGuizmoUsedOnce = false;
    };
    
}
