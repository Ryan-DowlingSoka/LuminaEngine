#pragma once
#include "../EditorTool.h"

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

    private:

        void DrawWindow(bool bIsFocused);
    };
}
