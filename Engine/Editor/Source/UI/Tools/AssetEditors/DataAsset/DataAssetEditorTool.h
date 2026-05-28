#pragma once

#include "Memory/SmartPtr.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"
#include "imgui.h"

namespace Lumina
{
    class FPropertyTable;
    class CStruct;

    // Editor for CDataAsset: nothing but a property grid over the instance's values. The
    // structure is owned by the asset's CDataAssetSchema (edited in its own tool), so there
    // are no add/remove/type controls here.
    class FDataAssetEditorTool : public FAssetEditorTool
    {
    public:

        LUMINA_EDITOR_TOOL(FDataAssetEditorTool)

        FDataAssetEditorTool(IEditorToolContext* Context, CObject* InAsset);

        bool IsSingleWindowTool() const override { return false; }
        bool ShouldGenerateThumbnailOnSave() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_DATABASE; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override {}
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

    private:

        void DrawEditorWindow(bool bFocused);

        TUniquePtr<FPropertyTable> PropertyTable;

        // The bag's layout CStruct is reallocated whenever the schema changes underneath us
        // (PropagateToInstances). Re-point the table when this differs from what it holds.
        CStruct* BoundLayout = nullptr;
    };
}
