#pragma once
#include "UI/Tools/AssetEditors/AssetEditorTool.h"
#include <entt/entt.hpp>

namespace Lumina
{
    class FMaterialInstanceEditorTool : public FAssetEditorTool
    {
    public:

        LUMINA_EDITOR_TOOL(FMaterialInstanceEditorTool)

        FMaterialInstanceEditorTool(IEditorToolContext* Context, CObject* InAsset);

        bool IsSingleWindowTool() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_FORMAT_LIST_BULLETED_TYPE; }
        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void SetupWorldForTool() override;

        void OnAssetLoadFinished() override;
        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

    private:

        void DrawParameterEditor(bool bFocused);
        void DrawTextureParameterColumn(class CMaterialInstance* Instance, const struct FMaterialParameter& Param, bool bHasOverride);

        entt::entity MeshEntity;
        entt::entity DirectionalLightEntity;

        ImGuiTextFilter TexturePickerFilter;
    };
}
