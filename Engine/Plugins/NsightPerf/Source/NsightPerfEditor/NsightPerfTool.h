#pragma once

#include "UI/Tools/EditorTool.h"

namespace Lumina
{
    // pImpl: owns the NvPerf periodic sampler + HUD model/renderer. Defined in the .cpp so the heavy
    // (and std::-heavy) Nsight Perf headers never leak into anything that includes this tool.
    struct FNsightPerfState;

    // "Nsight Perf HUD": a live GPU metrics dashboard driven by the NVIDIA Nsight Perf periodic
    // sampler (SM occupancy, throughputs, memory bandwidth, ...). Sampling starts when the window
    // opens and stops when it closes. Requires an NVIDIA GPU and the device created with NvPerf's
    // required extensions (registered by FNsightPerfEditorModule at the Core loading phase). When
    // those aren't available the window opens and explains why. Editor-only singleton.
    class FNsightPerfTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FNsightPerfTool)

        FNsightPerfTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Nsight Perf", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_GAUGE; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;

    private:

        void DrawWindow(bool bIsFocused);

        FNsightPerfState* State = nullptr;
    };
}
