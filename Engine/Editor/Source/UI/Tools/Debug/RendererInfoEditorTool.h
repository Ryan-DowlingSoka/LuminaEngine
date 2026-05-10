#pragma once
#include "../EditorTool.h"

namespace Lumina
{
    class FRendererInfoEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FRendererInfoEditorTool)

        FRendererInfoEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Renderer Info", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_CHART_LINE; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

    private:

        void DrawWindow(const FUpdateContext& UpdateContext, bool bIsFocused);
    };
}
