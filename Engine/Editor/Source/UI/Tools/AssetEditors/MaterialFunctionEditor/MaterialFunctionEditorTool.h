#pragma once

#include "Core/Object/ObjectHandleTyped.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"

namespace Lumina
{
    class CEdGraphNode;
    class CMaterialFunctionGraph;

    // Editor for CMaterialFunction assets: a node graph (the same material node library plus
    // FunctionInput / FunctionOutput nodes), a properties panel, and a derived-signature panel. There
    // is no 3D preview -- a function produces no shader of its own, it is inlined into materials. On
    // save it mirrors the graph's FunctionInput / FunctionOutput nodes into the asset's signature and
    // runs a validation compile to surface node errors.
    class FMaterialFunctionEditorTool : public FAssetEditorTool
    {
    public:

        LUMINA_EDITOR_TOOL(FMaterialFunctionEditorTool)

        FMaterialFunctionEditorTool(IEditorToolContext* Context, CObject* InAsset);

        bool IsSingleWindowTool() const override { return false; }
        bool ShouldGenerateThumbnailOnSave() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_FUNCTION; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void OnSave() override;

    private:

        void DrawGraphWindow();
        void DrawPropertiesWindow();
        void DrawSignatureWindow();

        // Scans the graph's FunctionInput / FunctionOutput nodes into the asset's signature (sorted by
        // SortPriority then name) and runs a throwaway validation compile to surface node errors.
        void CompileAndSyncSignature();

        TObjectPtr<CMaterialFunctionGraph> NodeGraph;
        CEdGraphNode*                      SelectedNode = nullptr;
        FString                            CompilationLog;
        bool                               bHasErrors = false;
    };
}
