#include "GamePreviewTool.h"
#include "World/WorldManager.h"

namespace Lumina
{
    FGamePreviewTool::FGamePreviewTool(IEditorToolContext* Context, CWorld* InWorld)
        :FEditorTool(Context, "Game Preview", InWorld)
    {
        
    }

    void FGamePreviewTool::OnInitialize()
    {
    }

    void FGamePreviewTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FGamePreviewTool::Update(const FUpdateContext& UpdateContext)
    {
        
    }

    void FGamePreviewTool::SetupWorldForTool()
    {
        //FEditorTool::SetupWorldForTool();//... Don't create editor entity.
    }

    void FGamePreviewTool::DrawToolMenu(const FUpdateContext& UpdateContext)
    {

    }

    void FGamePreviewTool::DrawHelpMenu()
    {
        DrawHelpTextRow("Play (PIE)",
            "This tool runs the duplicated PIE world. The original editor world is suspended; "
            "Stop returns to it and discards PIE-side changes.");
        DrawHelpTextRow("Input",
            "Click the viewport to focus. Bound input actions and Lua input handlers fire as they would in a packaged build.");
        DrawHelpTextRow("Lua",
            "BeginPlay/Tick/EndPlay run for every entity with an attached ScriptComponent. "
            "Open Tools > Debug > Scripts Info for a live API reference.");
        DrawHelpTextRow("Pause",
            "Use the editor's simulation controls to pause/resume the running PIE world.");
    }

    void FGamePreviewTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(), InDockspaceID);
    }

    void FGamePreviewTool::DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize)
    {
        
    }
}
