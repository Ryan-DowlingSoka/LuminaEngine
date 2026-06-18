#pragma once

#include "EditorTool.h"
#include "Core/Profiler/GameplayProfiler.h"

namespace Lumina
{
    // "Gameplay Profiler": per-frame, name-aggregated CPU timings for gameplay work — every C# script and
    // system OnUpdate (auto-labeled by type) plus any Profiler.Sample scope. Recording is on while this
    // tool window is open (zero cost otherwise). Editor-only.
    class FGameplayProfilerEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FGameplayProfilerEditorTool)

        FGameplayProfilerEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Gameplay Profiler", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_GAUGE; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;

    private:

        void DrawWindow(bool bIsFocused);

        // Display copy, refreshed each frame unless frozen, so Freeze holds a stable frame to inspect.
        FGameplayProfileFrame DisplayFrame;
        char    Filter[64] = {};
        bool    bFrozen    = false;
        uint32  DrawTicks  = 0;
    };
}
