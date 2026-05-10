#pragma once

#include "EditorTool.h"

namespace Lumina
{
    /**
     * Editor-side panel for the Luau debugger. Singleton tool — toggled from
     * Tools menu, autopopped when a breakpoint hits.
     *
     * Reads pause state from FLuaDebugger (engine side). Writes to it via
     * RequestContinue / RequestStep* — those are processed on the next engine
     * Tick. The panel itself never touches lua_State directly.
     */
    class FLuaDebuggerEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FLuaDebuggerEditorTool)

        FLuaDebuggerEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Lua Debugger", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_BUG; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void Update(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

    private:

        void DrawWindow(bool bIsFocused);
        void DrawToolbar();
        void DrawCallStack();
        void DrawLocalsAndUpvalues();
        void DrawBreakpointsList();

        // Selected stack frame; UI scopes the locals view to this index.
        int SelectedFrame = 0;

        // Set true the frame a fresh pause is detected so we can request
        // window focus and jump the LuaEditor to the broken line.
        bool bJustEnteredPause = false;
        bool bWasPaused = false;
    };
}
