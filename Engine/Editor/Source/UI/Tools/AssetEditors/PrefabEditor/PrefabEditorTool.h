#pragma once

#define USE_IMGUI_API
#include <imgui.h>
#include "ImGuizmo.h"
#include "Tools/UI/ImGui/Widgets/TreeListView.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"


namespace Lumina
{
    class CPrefab;

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
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void OnAssetLoadFinished() override;
        void OnSave() override;
        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize) override;
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
        void RebuildPropertyTables(entt::entity Entity);

        void RebuildOutlinerTree(FTreeListView& Tree);
        void HandleOutlinerDragDrop(FTreeListView& Tree, entt::entity DropItem);
        void HandlePrefabContentDrop(FStringView VirtualPath, entt::entity DropTarget);

        entt::entity CreateEntityAtRoot();
        void RequestDestroyEntity(entt::entity Entity);
        void ProcessDestroyRequests();

        // Clipboard helpers — local to the prefab editor (shares the FCopiedTag type the
        // world editor uses, but each tool's clipboard is scoped to its own preview world).
        entt::entity DuplicatePrefabEntity(entt::entity Source);
        void ProcessClipboardShortcuts();

        void AddSelectedEntity(entt::entity Entity, bool bRebuild);
        void ClearSelectedEntities();
        entt::entity GetLastSelectedEntity() const;

        // Copies the prefab's entities into the preview world so they become editable.
        void LoadPrefabIntoPreviewWorld();

        // Captures the preview world's prefab-owned entities back into the prefab asset.
        void CommitPreviewWorldToPrefab();

        entt::entity FindPrefabRoot() const;

        CPrefab* GetPrefab() const;

        TVector<TUniquePtr<FPropertyTable>> ComponentPropertyTables;
        TVector<CStruct*>                   ComponentStructs;
        entt::entity                        CachedPropertyEntity = entt::null;
        bool                                bPropertyTablesDirty = false;

        FTreeListView                       OutlinerListView;
        FTreeListViewContext                OutlinerContext;
        TQueue<entt::entity>                EntityDestroyRequests;

        bool                                bImGuizmoUsedOnce = false;

        static constexpr const char* OutlinerWindowName = "PrefabOutliner";
        static constexpr const char* PropertiesWindowName = "PrefabProperties";
    };
}
