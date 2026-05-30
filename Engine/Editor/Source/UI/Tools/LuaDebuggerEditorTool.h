#pragma once

#include "EditorTool.h"

namespace Lumina
{
    // Editor panel for the Luau debugger; reads pause state from FLuaDebugger,
    // writes via RequestContinue/RequestStep* (processed next engine Tick).
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
