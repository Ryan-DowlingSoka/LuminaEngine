#pragma once

#include "Core/Object/ObjectHandleTyped.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"

namespace Lumina
{
    class CEdGraphNode;
}

namespace Lumina
{
    class CMaterialNodeGraph;

    class FMaterialEditorTool : public FAssetEditorTool
    {
    public:

        struct FCompilationResultInfo
        {
            FString CompilationLog;
            bool bIsError;
        };

        LUMINA_EDITOR_TOOL(FMaterialEditorTool)

        FMaterialEditorTool(IEditorToolContext* Context, CObject* InAsset);
        
        bool IsSingleWindowTool() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_FORMAT_LIST_BULLETED_TYPE; }
        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void SetupWorldForTool() override;

        bool DrawViewport(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture) override;
        bool ShouldGenerateThumbnailOnSave() const override { return true; }
        void OnAssetLoadFinished() override;
        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void DrawMaterialGraph();
        void DrawMaterialProperties();
        void DrawGLSLPreview();

        void Compile();
        void ApplyMaterialToPreview();
        void OnSave() override;
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

    protected:
        
        void DrawHelpMenu() override;
        
    private:
        
        entt::entity                    MeshEntity;
        entt::entity                    DirectionalLightEntity;
        
        FString                         Tree;
        size_t                          ReplacementStart = 0;
        size_t                          ReplacementEnd = 0;
        CEdGraphNode*                   SelectedNode = nullptr;
        FCompilationResultInfo          CompilationResult;
        
        
        TUniquePtr<FPropertyTable>      EnvironmentEditor;
        TUniquePtr<FPropertyTable>      DirectionalEditor;
        
        TObjectPtr<CMaterialNodeGraph>  NodeGraph;
        bool                            bGLSLPreviewDirty = false;
    };
}
