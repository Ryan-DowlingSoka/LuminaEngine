#pragma once

#include "EditorTool.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"

namespace Lumina
{
    class FPlugin;

    // View + per-project enable/disable for discovered plugins.
    //
    // Toggles are pending in-memory until the user clicks "Apply & Restart":
    // the tool then rewrites the loaded .lproject's "Plugins" array (other
    // top-level keys are preserved verbatim) and opens a restart-required
    // modal. Plugin state only takes effect on next engine start because
    // FModuleManager has no hot-unload — see project_plugin_system.md.
    //
    // If no project is loaded the tool is a read-only viewer: per-project
    // overrides have nowhere to live yet. Engine plugins still show so
    // the user can see what's available.
    class FPluginBrowserEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FPluginBrowserEditorTool)

        FPluginBrowserEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Plugin Browser", nullptr)
        {}

        bool        IsSingleWindowTool()   const override { return true; }
        const char* GetTitlebarIcon()      const override;

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

    private:

        void DrawBrowserWindow(bool bIsFocused);
        void DrawToolbar();
        void DrawTable(const TVector<FPlugin*>& Plugins);
        void DrawDetailPanel(FPlugin* Plugin);
        void DrawFooter();

        // Returns the in-memory desired-enabled state for a plugin: if the
        // user has staged a toggle, that wins; otherwise the plugin's
        // current state from FPluginManager.
        bool IsEffectivelyEnabled(const FPlugin* Plugin) const;

        // Stage / clear a pending toggle.
        void SetPendingEnabled(FName PluginName, bool bEnabled);

        // Persist staged toggles to <Project>/<Name>.lproject. Returns false
        // + populates OutError on JSON parse / write failure.
        bool ApplyAndPersist(FString& OutError);

        // Open the "restart required" modal via the editor's modal manager.
        void PromptRestart();

        // Open the "create plugin" modal (name + description → scaffold into
        // <Project>/Plugins/ and regenerate the project). Project must be loaded.
        void OpenCreatePluginDialog();

    private:

        // Plugin name (FName) → desired enabled. Sparse: only contains
        // entries that differ from the current state.
        THashMap<FName, bool> PendingChanges;

        FName    SelectedPlugin;
        FString  SearchFilter;
        bool     bShowEngine  = true;
        bool     bShowProject = true;

        // After ApplyAndPersist succeeds, sticky banner that says
        // "<N> change(s) saved; restart to apply".
        bool     bChangesSavedBanner = false;

        // Create-plugin modal state. Buffers persist across frames while the
        // modal is open; CreatePluginResult holds the post-create confirmation
        // (non-empty switches the modal to its "done" view).
        char     NewPluginNameBuf[64]  = {};
        char     NewPluginDescBuf[160] = {};
        FString  NewPluginError;
        FString  CreatePluginResult;
    };
}
