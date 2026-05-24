#pragma once

#define USE_IMGUI_API
#include <imgui.h>
#include "EditorTool.h"
#include "ImGuizmo.h"
#include "Core/Delegates/Delegate.h"
#include "Core/Object/Class.h"
#include "NavMeshEditMode.h"
#include "WorldEditorMode.h"
#include "Memory/Memory.h"
#include "Tools/UI/ImGui/Widgets/TreeListView.h"
#include "UI/Properties/PropertyTable.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Systems/EntitySystem.h"

namespace Lumina
{
    class CStaticMesh;
    DECLARE_MULTICAST_DELEGATE(FOnGamePreview);
    
    /**
     * Base class for display and manipulating scenes.
     */
    class FWorldEditorTool : public FEditorTool, public IEditorModeContext
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

        bool OnEvent(FEvent& Event) override;
        
        void OnEntityCreated(entt::registry& Registry, entt::entity Entity);

        const char* GetTitlebarIcon() const override;
        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

        void DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize) override;
        void DrawViewportToolbar(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

        void PushAddTagModal(entt::entity Entity);
        void PushAddComponentModal(entt::entity Entity);
        void PushRenameEntityModal(entt::entity Entity);
        void PushSaveAsAssetModal();
        void PushCreatePrefabFromSelectionModal();
        void PushCreatePrefabModalForEntity(entt::entity Entity);

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

        // QoL viewport actions, all bound from the Update() key handler. Each begins/ends
        // a transaction internally where appropriate so they compose with undo/redo.
        void GroupSelectedEntities();
        void DropSelectionToFloor();
        void FrameAllEntities();
        void CopyTransformFromLastSelected();
        void PasteTransformToSelection();
        void SaveCameraBookmark(int32 Slot);
        void RecallCameraBookmark(int32 Slot);
        void DrawCursorWorldPositionOverlay(ImVec2 ViewportOrigin, ImVec2 ViewportSize, const SCameraComponent& Camera);
        void DrawEntityDebugOverlay(ImVec2 ViewportOrigin, ImVec2 ViewportSize, const SCameraComponent& Camera);
        void DrawOffscreenSelectionIndicators(ImVec2 ViewportOrigin, ImVec2 ViewportSize, const SCameraComponent& Camera);
        
        bool HasSimulatingWorld() const { return bSimulatingWorld || bGamePreviewRunning; }
        
        void StopAllSimulations();
        
        bool IsUnsavedDocument() override;

        // IEditorModeContext: lets an active mode wrap an interaction in the undo system.
        void BeginModeTransaction() override { BeginTransaction(); }
        void EndModeTransaction(const char* Name) override { EndTransaction(FName(Name)); }


    protected:

        void OnPostUndoRedo() override;
        
        // Replace the entire selection with a single entity. Use this for the most
        // common "click an entity" path — single-select with implicit clear.
        void SetSingleSelectedEntity(entt::entity Entity);

        // Add Entity to the current selection. Becomes the new last-selected.
        // bRebuild is retained for source compatibility but is no longer needed:
        // outliner row state and the details panel sync automatically.
        void AddSelectedEntity(entt::entity Entity, bool bRebuild = false);

        // Remove Entity from the current selection. If it was the last-selected
        // entity, a new one is picked from the remaining set (or null if empty).
        void RemoveSelectedEntity(entt::entity Entity, bool bRebuild = false);

        // Toggle Entity in/out of the selection set (Ctrl-click semantics).
        void ToggleSelectedEntity(entt::entity Entity);

        void ClearSelectedEntities();

        // Rebuild the cached set + LastSelectedEntity from the registry tags. Used after
        // bulk operations (undo/redo, world swap) where the registry is the authority.
        void ResyncSelectionFromRegistry();

        NODISCARD bool IsEntitySelected(entt::entity Entity) const { return SelectedEntities.find(Entity) != SelectedEntities.end(); }
        NODISCARD const THashSet<entt::entity>& GetSelectedEntities() const { return SelectedEntities; }

        // O(1) — returns the cached "last clicked / focused" entity. May be entt::null
        // if nothing is selected or if the last-selected entity was destroyed.
        NODISCARD entt::entity GetLastSelectedEntity() const { return LastSelectedEntity; }
        
        void AddEntityToCopies(entt::entity Entity);
        void RemoveEntityFromCopies(entt::entity Entity);
        void ClearCopies() const;

        void SetWorldPlayInEditor(bool bShouldPlay);
        void SetWorldNewSimulate(bool bShouldSimulate);

        /** Rebind OnEntityCreated/OnEntityDestroyed observers to the current World's registry. */
        void RebindRegistryObservers();

        /** Engine-driven world travel: drop everything tied to OldWorld and re-bind to NewWorld. ProxyWorld is preserved. */
        void OnWorldTravelled(CWorld* OldWorld, CWorld* NewWorld);

        /** Accept a content-browser drag payload in the current scope and, if it's a prefab, instantiate it under DropTarget. */
        void AcceptContentBrowserPrefabPayload(entt::entity DropTarget);

        void DrawAddToEntityOrWorldPopup(entt::entity Entity = entt::null);
        void DrawFilterOptions();

        /**
         * Renders a categorized, filterable list of addable components as styled buttons.
         * Populates OutMetaType / OutStruct and returns true when the user clicks an entry.
         * Caller is responsible for the actual emplace / create-entity behavior and popup closure.
         */
        bool DrawAddableComponentList(const ImGuiTextFilter& Filter, entt::meta_type& OutMetaType, CStruct*& OutStruct);

        // Initial population (called on tree dirty); just enumerates roots and lets lazy children
        // build subtrees on first expand.
        void RebuildSceneOutliner(FTreeListView& Tree);

        // Build the immediate component nodes + child entity nodes for one entity row when
        // it's expanded for the first time (or after a reset).
        void BuildEntityChildren(FTreeListView& Tree, FTreeNodeID Item);

        // Add a single entity to the outliner under its current world parent (if that parent
        // already has a tree node), or as a root. Returns the new tree node, or InvalidTreeNode
        // if the entity should not appear (hidden / no name component).
        FTreeNodeID AddEntityToOutliner(entt::entity Entity);

        // Drop a single entity from the outliner if present (also removes its child rows).
        void RemoveEntityFromOutliner(entt::entity Entity);

        // Re-attach an existing tree node to its world entity's current parent. Used when the
        // hierarchy changes (drag-drop, unparent).
        void ReparentEntityInOutliner(entt::entity Entity);

        // EnTT signal handlers wired to the world's SNameComponent storage.
        void OnOutlinerEntityConstructed(entt::registry& Registry, entt::entity Entity);
        void OnOutlinerEntityDestroyed(entt::registry& Registry, entt::entity Entity);

        // Drains PendingOutlinerAdds before the outliner draws.
        void FlushOutlinerPending();

        void HandleEntityEditorDragDrop(FTreeListView& Tree, entt::entity DropItem);
        void HandlePrefabContentDrop(FStringView VirtualPath, entt::entity DropTarget);

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
        void CreatePrimitiveEntity(CStaticMesh* PrimitiveMesh, const char* DisplayName);

        void CopyEntity(entt::entity& To, entt::entity From);

        void CycleGuizmoOp();
        void ToggleGuizmoMode();

        /** Toggle the editor's "game view": hide grid, billboards, AABBs, gizmos so the
         *  viewport shows only what a runtime camera would. Bound to G by default. */
        void ToggleGameViewMode();

    private:

        /** Register the tool's keyboard shortcuts with the FEditorAction registry.
         *  Called from OnInitialize. */
        void RegisterEditorActions();

        /** Build the editor-mode registry. Selection mode is always first. New
         *  modes (foliage, painting, splines, etc.) are added by appending to
         *  EditorModes here. */
        void RegisterEditorModes();

        /** Current viewport mode. Always non-null after OnInitialize. */
        NODISCARD IWorldEditorMode* GetActiveMode() const;

        /** Switch the active mode by registry index. Drives OnEnter/OnExit and
         *  clears any half-finished gizmo / vertex-snap state from the prior mode. */
        void SetActiveMode(int32 NewIndex);

    private:

        struct FSelectionBox
        {
            bool bActive = false;
            ImVec2 Start;
            ImVec2 Current;
        } SelectionBox;
        
        TObjectPtr<CWorld>                      ProxyWorld;

        // Editor entity living in ProxyWorld. Tracked independently of EditorEntity
        // (which always refers to the active World) so PIE/Simulate can restore the
        // editor world cleanly even if Travel swaps the active world mid-session.
        entt::entity                            ProxyEditorEntity = entt::null;
        
        ImGuiTextFilter                         AddEntityComponentFilter;
        FEntityListFilterState                  EntityFilterState;
        FOnGamePreview                          OnGamePreviewStartRequested;
        FOnGamePreview                          OnGamePreviewStopRequested;
        
        ImGuizmo::OPERATION                     GuizmoOp;
        ImGuizmo::MODE                          GuizmoMode;
        
        FTreeListView                           OutlinerListView;
        FTreeListViewContext                    OutlinerContext;

        // World entity → outliner tree node. Populated incrementally as entities are created
        // and torn down via the on_construct/on_destroy hooks for SNameComponent.
        THashMap<entt::entity, FTreeNodeID>     EntityToTreeNode;

        // Entities created since last outliner flush, queued because their FRelationshipComponent
        // may not be set yet at on_construct time. Drained at the top of DrawOutliner.
        TVector<entt::entity>                   PendingOutlinerAdds;

        TQueue<FComponentDestroyRequest>        ComponentDestroyRequests;
        TQueue<entt::entity>                    EntityDestroyRequests;
        TVector<TUniquePtr<FPropertyTable>>     PropertyTables;
        TUniquePtr<FPropertyTable>              WorldSettingsPropertyTable;

        // SelectedEntities is the authoritative set; FSelectedInEditorComponent on the
        // registry is mirrored from it (other systems — render highlight, prefab editor,
        // visualizers — read the tag). LastSelectedEntity is the focus target for the
        // gizmo pivot and details panel. FLastSelectedTag mirrors it for systems that
        // observe via the registry. All mutations go through ApplySelectionMutation.
        THashSet<entt::entity>                  SelectedEntities;
        entt::entity                            LastSelectedEntity = entt::null;

        // The entity whose components are currently shown in the Details panel.
        // PropertyTables hold raw pointers into that entity's component storage and
        // become stale whenever LastSelectedEntity changes; tracking this separately
        // lets us rebuild lazily on draw rather than on every selection mutation.
        entt::entity                            DetailsEntity = entt::null;
        bool                                    bDetailsDirty = false;

        // Registered viewport modes; index 0 is always the default Selection mode so
        // initial state matches the pre-mode-system behavior. SetActiveMode handles
        // OnEnter/OnExit hooks and resets gizmo bookkeeping so partial-drag state
        // can't leak across a mode switch.
        TVector<TUniquePtr<IWorldEditorMode>>   EditorModes;
        int32                                   ActiveModeIndex = 0;

        FNavMeshEditMode                        NavMeshEditMode;

        // Viewport-local transform clipboard (Ctrl+Shift+C / Ctrl+Shift+V). Captured from
        // the last-selected entity's world transform; pasted onto every selected entity.
        FTransform                              CopiedTransform;
        bool                                    bHasCopiedTransform = false;

        // Camera bookmark slots. Index 0..8 maps to keys 1..9; Ctrl+N saves the current
        // EditorEntity transform into slot N, plain N recalls it. Session-only for now.
        static constexpr int32                  NumCameraBookmarks = 9;
        FTransform                              CameraBookmarks[NumCameraBookmarks];
        bool                                    bCameraBookmarkSet[NumCameraBookmarks] = {};


        float                                   GuizmoSnapTranslate = 0.1f;
		float                                   GuizmoSnapRotate = 5.0f;
		float                                   GuizmoSnapScale = 0.1f;

        
        bool                                    bShowComponentVisualizers = true;
        bool                                    bDrawEntityDebugInfo = false;
		bool									bGuizmoSnapEnabled = true;
        bool                                    bGamePreviewRunning = false;
        bool                                    bSimulatingWorld = false;

        // Who owns the mouse/keyboard while playing. Game = ImGui ignores input
        // (editor chrome + picking inert), routed to game + UI; Editor = ImGui live
        // so you can drive panels. Toggled with Shift+F1 during Play. Simulate stays
        // Editor (live editing) and never enters Game focus.
        enum class EInputFocus : uint8 { Editor, Game };
        EInputFocus                             InputFocus = EInputFocus::Editor;

        // Sets ImGui NoMouse|NoKeyboard per InputFocus; reasserted each frame while
        // in Game focus so a script's SetMouseMode("Normal") can't re-enable ImGui.
        void ApplyInputFocus();
        void SetInputFocus(EInputFocus NewFocus);

        // Game view mode (toggled with G): hides grid, component visualizers, billboards
        // and bounds so the viewport shows just what a runtime camera would see. The
        // "Saved*" fields snapshot the user's prior toggles on enter so toggling back
        // off restores their preferences instead of forcing everything on.
        bool                                    bGameViewMode = false;
        bool                                    bSavedWorldGridEnabled = true;
        bool                                    bSavedShowComponentVisualizers = true;
        bool                                    bSavedDrawBillboards = true;
        bool                                    bSavedDrawAABB = true;

        FDelegateHandle                         WorldTravelledHandle;
        
        /** IDK, this thing will return IsUsing = true always if it's never been used */
        bool                                    bImGuizmoUsedOnce = false;

        // Vertex snap (Godot-style): while CTRL is held during a translate drag,
        // snap a chosen vertex on the dragged mesh to the nearest vertex on a
        // non-selected mesh in screen space. AnchorLocal is captured once per
        // drag in the pivot mesh's local frame.
        bool                                    bVertexSnapAnchorValid = false;
        glm::vec3                               VertexSnapAnchorLocal = glm::vec3(0.0f);
        bool                                    bVertexSnapApplied = false;
        glm::vec3                               VertexSnapTargetWorld = glm::vec3(0.0f);
        glm::vec3                               VertexSnapAnchorWorld = glm::vec3(0.0f);
        float                                   VertexSnapPixelRadius = 16.0f;
    };
    
}
