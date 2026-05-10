#pragma once
#include "../EditorTool.h"

namespace Lumina
{
    /**
     * Godot-style project settings editor. Reads the FConfig registry to
     * group declared settings by category and render the right widget per
     * type (Path -> file picker, Color -> ColorEdit4, Enum -> Combo, etc.).
     *
     * Unregistered values that exist on disk are still present and writable
     * via the script/console paths, but they don't appear here — declaring
     * a setting with FConfig::RegisterSetting is what makes it visible.
     */
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
