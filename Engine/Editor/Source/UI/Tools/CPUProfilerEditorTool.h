#pragma once
#include "EditorTool.h"

namespace Lumina
{
    struct FCPUProfileTarget;

    class FCPUProfilerEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FCPUProfilerEditorTool)

        FCPUProfilerEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "CPU Profiler", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_CHART_BAR; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

    private:

        void DrawProfilerWindow(bool bIsFocused);
        void DrawTargetPicker();
        void DrawFrameTimeGraph(const FCPUProfileTarget& Target);
        void DrawScopeTree(const FCPUProfileTarget& Target);

        void* SelectedTargetKey   = nullptr;
        bool  bExpandAll          = true;
        float GraphHeight         = 110.0f;
        float FilterMinTimeMs     = 0.0f;
    };
}
