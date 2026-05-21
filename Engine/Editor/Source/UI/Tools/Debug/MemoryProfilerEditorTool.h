#pragma once
#include "UI/Tools/EditorTool.h"
#include "Memory/MemoryTracking.h"

namespace Lumina
{
    // Single-window leak hunter. Category tracking is always on in Debug/Development;
    // this window just visualizes it. Set a baseline, watch which category's Delta climbs,
    // then tick call-stack capture to get the exact leaking line.
    class FMemoryProfilerEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FMemoryProfilerEditorTool)

        FMemoryProfilerEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Memory Profiler", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_MEMORY; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

    private:

        void DrawWindow(bool bIsFocused);
        void DrawControls();
        void DrawSummary();
        void DrawCategoryTable(float Height);
        void DrawCallSites();

#if LUMINA_MEMORY_TRACKING
        // Snapshot refreshed on a timer so the table is readable rather than flickering.
        TVector<Memory::FMemoryCategoryStats> Categories;
        TVector<Memory::FMemoryCategoryStats> Baseline;
        bool  bHasBaseline = false;
        float RefreshTimer = 0.0f;

        // Top Call Sites ranking: false = live bytes (leaks), true = total allocs (churn).
        bool  bSortCallSitesByAllocs = false;
#endif
    };
}
