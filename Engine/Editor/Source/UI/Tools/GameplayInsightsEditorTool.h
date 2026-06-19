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
        void DrawSchedule();
        void DrawStats();
        void DrawDetail();

        // The world whose schedule we inspect: the active gameplay (Game/Simulation) world if one is running,
        // else the editor world.
        CWorld* ResolveWorld() const;
        void    RefreshSchedule();

        // Display copies, refreshed each frame unless frozen, so Freeze holds a stable frame to inspect.
        FGameplayProfileFrame         DisplayFrame;   // aggregate scope timings (scripts + Sample + C# systems)
        TVector<FSystemScheduleEntry> Schedule;       // batch/access snapshot from the active world

        char    Filter[64]  = {};
        bool    bFrozen     = false;
        uint32  DrawTicks   = 0;
        FName   Selected;               // system selected in the Schedule tab (drives Detail)
    };
}
