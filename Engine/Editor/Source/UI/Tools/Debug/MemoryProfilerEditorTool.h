#pragma once
#include "../EditorTool.h"

namespace Lumina
{
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

    private:

        struct FMemorySnapshot
        {
            double timestamp;
            size_t processMemory;
            size_t currentMapped;
            size_t cachedMemory;
            size_t hugeAllocs;
        };

        void DrawWindow(bool bIsFocused);
        void DrawOverviewTab();
        void DrawDetailedTab();
        void DrawDistributionTab();

        TVector<FMemorySnapshot>    History;
        float                       UpdateTimer = 0.0f;
        bool                        bPaused     = false;

        static constexpr int32 MaxHistoryPoints = 60;
    };
}
