#pragma once
#include "UI/Tools/EditorTool.h"

namespace Lumina
{
    // Project settings editor: groups FConfig-declared settings by category with a per-type
    // widget. Only settings registered via FConfig::RegisterSetting appear here.
    class FProjectSettingsEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FProjectSettingsEditorTool)

        FProjectSettingsEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Project Settings", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_SETTINGS_HELPER; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

    private:

        void DrawWindow(bool bIsFocused);
        void DrawCategoryTree();
        void DrawSettingsForCategory();
        void DrawSettingRow(const struct FConfigSetting& Setting);

        FString SelectedCategory;
        char    SearchBuffer[128] = {};
    };
}
