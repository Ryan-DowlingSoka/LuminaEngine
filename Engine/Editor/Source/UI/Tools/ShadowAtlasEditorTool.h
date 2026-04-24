#pragma once
#include "EditorTool.h"

namespace Lumina
{
    // Runtime debug view for FShadowAtlas. Shows the quad-tree allocator
    // state for the world's forward render scene: utilization, per-size
    // bucket histogram, and a 2D visualization of every allocated tile.
    class FShadowAtlasEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FShadowAtlasEditorTool)

        FShadowAtlasEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Shadow Atlas", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_GRID; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;

    private:

        void DrawAtlasWindow(bool bIsFocused);
        void DrawStats(const class FShadowAtlas& Atlas);
        void DrawAtlasCanvas(const class FShadowAtlas& Atlas);
        void DrawTileTable(const class FShadowAtlas& Atlas);

        float CanvasSize    = 512.0f;
        bool  bShowGrid     = true;
        bool  bShowLabels   = true;
    };
}
