#pragma once
#include "UI/Tools/EditorTool.h"
#include "Scripting/Lua/Scripting.h"

namespace Lumina
{
    class FScriptsInfoEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FScriptsInfoEditorTool)

        FScriptsInfoEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Scripts Info", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_LANGUAGE_LUA; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;

        void DrawHelpMenu() override;

    private:

        void DrawWindow(bool bIsFocused);
        void DrawRuntimeTab();
        void DrawApiReferenceTab();
        void RebuildApiCache();

        TVector<Lua::FLuaSymbol> CachedSymbols;
        double                   LastHarvestTimeSeconds = -1.0;
    };
}
