#pragma once
#include "EditorTool.h"

namespace Lumina
{
    class FGPUProfilerEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FGPUProfilerEditorTool)

        FGPUProfilerEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "GPU Profiler", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_CHART_TIMELINE; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

    private:

        void DrawProfilerWindow(bool bIsFocused);
        void DrawScopeTree();
        void DrawFrameTimeGraph();
        void DrawPipelineStats();
        void DrawBarriers();
        void DrawDiagnostics();

        bool    bExpandAll          = true;
        float   GraphHeight         = 110.0f;
        float   FilterMinTimeMs     = 0.0f;

        // Barrier inspector filters.
        bool    bShowImageBarriers      = true;
        bool    bShowBufferBarriers     = true;
        bool    bShowRestoreBarriers    = true;
        bool    bShowRedundantOnly      = false;
        bool    bGroupBarriersByResource= false;

        // Result of the most recent barrier export (file path or error), shown inline.
        FString BarrierExportStatus;
    };
}
