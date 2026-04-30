#pragma once

#include "Core/Object/ObjectHandleTyped.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"

namespace Lumina
{
    class CEdGraphNode;
    class CParticleNodeGraph;

    class FParticleSystemEditorTool : public FAssetEditorTool
    {
    public:

        struct FCompilationResultInfo
        {
            FString CompilationLog;
            bool bIsError = false;
        };

        LUMINA_EDITOR_TOOL(FParticleSystemEditorTool)

        FParticleSystemEditorTool(IEditorToolContext* Context, CObject* InAsset);

        bool IsSingleWindowTool() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_FORMAT_LIST_BULLETED_TYPE; }

        void OnInitialize() override;

        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void SetupWorldForTool() override;
        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

        void Compile();
        void OnSave() override;
        bool ShouldGenerateThumbnailOnSave() const override { return true; }

    private:

        void DrawParticleGraph();
        void DrawParticleProperties();
        void DrawShaderPreview();

    private:

        entt::entity            ParticleEntity;
        entt::entity            DirectionalLightEntity;

        CEdGraphNode*           SelectedNode = nullptr;
        FCompilationResultInfo  CompilationResult;
        FString                 CompiledSource;

        TObjectPtr<CParticleNodeGraph> NodeGraph;
    };
}
