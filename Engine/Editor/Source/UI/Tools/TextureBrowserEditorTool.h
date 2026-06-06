#pragma once
#include "EditorTool.h"

namespace Lumina
{
    class FTextureBrowserEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FTextureBrowserEditorTool)

        FTextureBrowserEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Texture Viewer", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_CIRCLE; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        
    protected:
        
        void DrawHelpMenu() override;

    private:

        void DrawWindow(bool bIsFocused);
    };
}
