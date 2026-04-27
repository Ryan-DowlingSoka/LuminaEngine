#pragma once
#include "../EditorTool.h"

namespace Lumina
{
    class FObjectBrowserEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FObjectBrowserEditorTool)

        FObjectBrowserEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Object Browser", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_LIST_BOX; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;

    private:

        void DrawWindow(bool bIsFocused);

        FString SearchFilter;
        FString ClassFilter;
        bool    bSortByName     = true;
        bool    bShowOnlyActive = false;
        int32   SelectedObjectIndex = -1;
        FString SelectedPackage;
        char    SearchBuffer[256] = {};
        char    ClassFilterBuffer[256] = {};
    };
}
