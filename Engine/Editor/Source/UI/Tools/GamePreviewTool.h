#pragma once
#include "EditorTool.h"

namespace Lumina
{
    class FGamePreviewTool : public FEditorTool
    {
    public:

        LUMINA_EDITOR_TOOL(FGamePreviewTool)

        // ClientIndex > 0 names the tool "Client: N" so multiple previews are uniquely named (the tool name
        // is the window/dock identity); 0 keeps the lone "Game Preview" title.
        FGamePreviewTool(IEditorToolContext* Context, CWorld* InWorld, int32 ClientIndex = 0);


        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        const char* GetTitlebarIcon() const override { return LE_ICON_EARTH; }
        void Update(const FUpdateContext& UpdateContext) override;

        void SetupWorldForTool() override;

        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

        void DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize) override;

    };
}
