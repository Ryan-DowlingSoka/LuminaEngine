#pragma once

#include "UI/Tools/AssetEditors/AssetEditorTool.h"
#include "imgui.h"

namespace Lumina
{
    /**
     * Editor for CPhysicsMaterial. All fields (Friction / Restitution / Density / combine modes)
     * are reflected, so this is just a thin wrapper that hosts FAssetEditorTool's PropertyTable
     * in a docked window. No 3D preview -- physics-material authoring is pure scalar editing.
     */
    class FPhysicsMaterialEditorTool : public FAssetEditorTool
    {
    public:

        LUMINA_EDITOR_TOOL(FPhysicsMaterialEditorTool)

        FPhysicsMaterialEditorTool(IEditorToolContext* Context, CObject* InAsset);

        bool IsSingleWindowTool() const override { return false; }
        bool ShouldGenerateThumbnailOnSave() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_BOWLING; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override {}
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;
    };
}
