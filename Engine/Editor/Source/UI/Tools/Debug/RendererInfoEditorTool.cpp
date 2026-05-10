#include "RendererInfoEditorTool.h"

#include "Renderer/RenderManager.h"
#include "Tools/UI/ImGui/ImGuiRenderer.h"

namespace Lumina
{
    void FRendererInfoEditorTool::OnInitialize()
    {
        CreateToolWindow("Renderer Info", [this](bool bIsFocused)
        {
            DrawWindow(GEngine->GetUpdateContext(), bIsFocused);
        });
    }

    void FRendererInfoEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FRendererInfoEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("What this is",
            "Live snapshot of the active render device: API, adapter, queue counts, descriptor heap usage, "
            "frame-time and GPU memory counters. Read-only.");
        DrawHelpTextRow("Backend",
            "Lumina renders through the abstract IRHI layer; this panel reflects whichever backend was "
            "selected at startup (Vulkan today). Backend selection lives in Project Settings > Renderer.");
        DrawHelpTextRow("Bindless",
            "Texture/buffer descriptors live in a global bindless heap. Slot counters help spot leaks "
            "(slot count climbing without a matching free).");
        DrawHelpTextRow("Related tools",
            "GPU Profiler — per-pass timings. Shadow Atlas — live shadow map slot layout. "
            "Memory Profiler — VRAM allocation breakdown.");
    }

    void FRendererInfoEditorTool::DrawWindow(const FUpdateContext& UpdateContext, bool bIsFocused)
    {
        // Render info is owned by the per-API ImGui renderer; this tool is
        // just a docked host that drives the existing draw call.
        GRenderManager->GetImGuiRenderer()->DrawRenderDebugContents(UpdateContext);
    }
}
