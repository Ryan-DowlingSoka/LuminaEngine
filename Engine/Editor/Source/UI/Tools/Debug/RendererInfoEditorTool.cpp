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

    void FRendererInfoEditorTool::DrawWindow(const FUpdateContext& UpdateContext, bool bIsFocused)
    {
        // Render info is owned by the per-API ImGui renderer; this tool is
        // just a docked host that drives the existing draw call.
        GRenderManager->GetImGuiRenderer()->DrawRenderDebugContents(UpdateContext);
    }
}
