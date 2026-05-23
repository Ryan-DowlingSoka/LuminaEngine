#pragma once
#include "UI/Tools/EditorTool.h"
#include "Memory/MemoryTracking.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderResource.h"

namespace Lumina
{
    // Unified CPU + GPU memory tool.
    //
    // CPU side is the always-on category tracker: set a baseline, watch which category's Delta
    // climbs, then enable call-stack capture for the exact leaking line.
    //
    // GPU side is fully backend-agnostic -- it reads abstracted heap stats and a per-purpose
    // breakdown through IRenderContext, so nothing here references the rendering API.
    class FMemoryProfilerEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FMemoryProfilerEditorTool)

        FMemoryProfilerEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Memory", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_MEMORY; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

    private:

        void DrawWindow(bool bIsFocused);
        void RefreshSnapshot();

        // Serializes the full snapshot (CPU + GPU + resources + call sites) to the clipboard
        // as a structured text report, formatted for pasting into an AI assistant.
        void CopyAllStatsToClipboard();

        void DrawHeaderCards();
        void DrawOverviewTab();
        void DrawGPUTab();
        void DrawCPUTab();
        void DrawScriptMemory();

        // GPU sub-panels.
        void DrawCategorySegmentBar(float Height);
        void DrawGPUCategoryTable();
        void DrawGPUHeaps();
        void DrawResourceCounts();

        // CPU sub-panels.
        void DrawCPUComposition();
        void DrawControls();
        void DrawCategoryTable(float Height);
        void DrawCallSites();

        // GPU snapshot (backend-agnostic). Refreshed on a timer; always available.
        FGPUMemoryStats         GPUStats;
        FGPUMemoryCategoryUsage GPUCategories[(int)EGPUMemoryCategory::Count] = {};
        FGPUDeviceInfo          DeviceInfo;
        bool                    bDeviceInfoValid = false;
        uint32                  ResourceCounts[RRT_Num] = {};
        uint32                  TotalResources = 0;

        // Total bytes the Luau VM has allocated (whole shared global state across script threads).
        size_t                  LuaBytes = 0;

        // Rolling timelines in MB, advanced once per refresh tick.
        TVector<float>          HistRSS;
        TVector<float>          HistCPUTracked;
        TVector<float>          HistMapped;     // rpmalloc's OS footprint (mapped bytes)
        TVector<float>          HistExternal;   // RSS - mapped (driver / Luau / CRT)
        TVector<float>          HistVRAM;
        TVector<float>          HistLua;

        float                   RefreshTimer = 0.0f;

#if LUMINA_MEMORY_TRACKING
        // CPU category snapshot, refreshed on the timer so the table reads steady.
        TVector<Memory::FMemoryCategoryStats> Categories;
        TVector<Memory::FMemoryCategoryStats> Baseline;
        bool  bHasBaseline = false;

        // Top Call Sites ranking: false = live bytes (leaks), true = total allocs (churn).
        bool  bSortCallSitesByAllocs = false;
#endif
    };
}
