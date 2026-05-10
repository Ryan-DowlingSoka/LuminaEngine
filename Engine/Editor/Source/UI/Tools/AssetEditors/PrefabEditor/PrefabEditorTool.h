#pragma once

#define USE_IMGUI_API
#include <imgui.h>
#include "ImGuizmo.h"
#include "Tools/UI/ImGui/Widgets/TreeListView.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"
#include "Containers/Array.h"


namespace Lumina
{
    class CPrefab;
    class CStaticMesh;
    class CStruct;

    class FPrefabEditorTool : public FAssetEditorTool
    {
    public:

        LUMINA_EDITOR_TOOL(FPrefabEditorTool)

        struct FEntityListViewItemData
        {
            entt::entity Entity = entt::null;
        };

        FPrefabEditorTool(IEditorToolContext* Context, CObject* InAsset);

        bool IsSingleWindowTool() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_PACKAGE_VARIANT_CLOSED; }
        void OnInitialize() override;
        void SetupWorldForTool() override;
        void Update(const FUpdateContext& UpdateContext) override;
        void EndFrame() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void OnAssetLoadFinished() override;
        void OnSave() override;
        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;
        void DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize) override;
        void DrawViewportToolbar(const FUpdateContext& UpdateContext) override;
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;
        bool ShouldGenerateThumbnailOnSave() const override { return true; }

        bool bShowAABB = false;

        ImGuizmo::OPERATION GuizmoOp = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE      GuizmoMode = ImGuizmo::WORLD;
        entt::entity DirectionalLightEntity = entt::null;

    protected:

        void OnPostUndoRedo() override;

    private:

        void DrawOutliner(bool bFocused);
        void DrawEntityProperties(bool bFocused);
        void DrawAddEntityButton();
        void DrawAddComponentButton(entt::entity Entity);
        void DrawAddPrimitiveMenu(entt::entity Entity);
        void DrawComponentList(entt::entity Entity);
        void DrawComponentHeader(const TUniquePtr<FPropertyTable>& Table, entt::entity Entity, CStruct* StructType);
        void DrawEmptyState();
        void RebuildPropertyTables(entt::entity Entity);

        void RebuildOutlinerTree(FTreeListView& Tree);
        void HandleOutlinerDragDrop(FTreeListView& Tree, entt::entity DropItem);
        void HandlePrefabContentDrop(FStringView VirtualPath, entt::entity DropTarget);

        entt::entity CreateEntityAtRoot(const char* DisplayName = "NewEntity");
        entt::entity CreatePrimitiveEntityAtRoot(CStaticMesh* PrimitiveMesh, const char* DisplayName);
        void RequestDestroyEntity(entt::entity Entity);
        void ProcessDestroyRequests();
        void RemoveComponent(entt::entity Entity, CStruct* StructType);
        void ResetSelectionTransform();

        // Hierarchy / multi-select shortcuts.
        entt::entity DuplicatePrefabEntity(entt::entity Source);
        void ProcessClipboardShortcuts();

        // Selection — multi-select via FSelectedInEditorComponent / FLastSelectedTag, mirrored
        // by SelectedEntities. Mirrors WorldEditor's selection model so the same shortcuts work.
        void SetSingleSelectedEntity(entt::entity Entity);
        void AddSelectedEntity(entt::entity Entity, bool bRebuild);
        void RemoveSelectedEntity(entt::entity Entity);
        void ToggleSelectedEntity(entt::entity Entity);
        void ClearSelectedEntities();
        void ResyncSelectionFromRegistry();
        entt::entity GetLastSelectedEntity() const;
        bool IsEntitySelected(entt::entity Entity) const { return SelectedEntities.find(Entity) != SelectedEntities.end(); }

        // View / camera helpers.
        void FrameAllEntities();

        // Prefab world setup.
        void LoadPrefabIntoPreviewWorld();
        void CommitPreviewWorldToPrefab();

        entt::entity FindPrefabRoot() const;

        CPrefab* GetPrefab() const;

        // Editor actions registration (W/E/R/X gizmo, Ctrl+Z/Y undo/redo, F focus, Home frame all,
        // Ctrl+S save). Must be called from OnInitialize.
        void RegisterEditorActions();

    private:

        TVector<TUniquePtr<FPropertyTable>> ComponentPropertyTables;
        TVector<CStruct*>                   ComponentStructs;
        entt::entity                        CachedPropertyEntity = entt::null;
        bool                                bPropertyTablesDirty = false;

        FTreeListView                       OutlinerListView;
        FTreeListViewContext                OutlinerContext;
        TQueue<entt::entity>                EntityDestroyRequests;

        // Multi-selection mirror; canonical state lives on the registry as FSelectedInEditorComponent.
        THashSet<entt::entity>              SelectedEntities;

        // Outliner search filter; mirrors world editor's pattern.
        ImGuiTextFilter                     OutlinerNameFilter;

        // Add-component popup filter; persists across opens.
        ImGuiTextFilter                     AddComponentFilter;

        bool                                bImGuizmoUsedOnce = false;

        // Gizmo snap settings, persisted to the editor config so they stick across sessions.
        bool                                bGuizmoSnapEnabled = true;
        float                               GuizmoSnapTranslate = 0.1f;
        float                               GuizmoSnapRotate = 5.0f;
        float                               GuizmoSnapScale = 0.1f;

        // Component visualizers — toggleable like the world editor; defaults on for prefab authoring.
        bool                                bShowComponentVisualizers = true;

        // Track when a prefab-owning entity is destroyed mid-frame so we can mark the package
        // dirty exactly once even when several entities go down in the same batch.
        bool                                bPendingDirtyOnDestroy = false;

        static constexpr const char* OutlinerWindowName = "PrefabOutliner";
        static constexpr const char* PropertiesWindowName = "PrefabProperties";
    };
}
