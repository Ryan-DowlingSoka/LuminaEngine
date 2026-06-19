#pragma once

#include "EditorTool.h"
#include "Containers/Array.h"
#include "Core/Profiler/GameplayProfiler.h"
#include "World/World.h"

namespace Lumina
{
    // A unified inspector for how the world's gameplay logic executes.
    class FGameplayInsightsEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FGameplayInsightsEditorTool)

        FGameplayInsightsEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Gameplay Insights", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_CHART_TIMELINE_VARIANT; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;

    private:

        void DrawWindow(bool bIsFocused);
        void DrawTimeline();
        void DrawSchedule();
        void DrawStats();
        void DrawDetail();

        // The world whose schedule we inspect: the active gameplay (Game/Simulation) world if one is running,
        // else the editor world. Spans in the timeline are process-global (whichever world is ticking).
        CWorld* ResolveWorld() const;
        void    RefreshSchedule();

        // Display copies, refreshed each frame unless frozen, so Freeze holds a stable frame to inspect.
        FGameplayProfileFrame         DisplayFrame;   // aggregate scope timings (scripts + Sample + C# systems)
        FSystemSpanFrame              DisplaySpans;   // per-system execution spans (the thread timeline)
        TVector<FSystemScheduleEntry> Schedule;       // batch/access snapshot from the active world

        char    Filter[64]  = {};
        bool    bFrozen     = false;
        uint32  DrawTicks   = 0;
        float   ZoomT       = 1.0f;     // fraction of the captured frame window shown in the timeline
        float   PanT        = 0.0f;     // 0..1 scroll within that window
        float   RowHeight   = 18.0f;
        FName   Selected;               // system selected in the Schedule tab (drives Detail)
    };
}
