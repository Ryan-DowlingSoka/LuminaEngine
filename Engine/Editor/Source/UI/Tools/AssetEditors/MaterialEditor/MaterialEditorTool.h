#pragma once

#include "Core/Object/ObjectHandleTyped.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"
#include "UI/Tools/NodeGraph/EdGraphNode.h"
#include "UI/Tools/NodeGraph/Material/MaterialCompiler.h"

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

        struct FCompilationError
        {
            FString             Title;
            FString             Description;
            CEdGraphNode*       Node = nullptr;
        };

        struct FCompilationResultInfo
        {
            FString                     CompilationLog;
            TVector<FCompilationError>  Errors;
            bool                        bIsError = false;
        };
        
        enum class EDebugMesh : uint8
        {
            Sphere,
            Cube,
            Plane,
            Cylinder,
            Cone,
        };

        LUMINA_EDITOR_TOOL(FMaterialEditorTool)

        FMaterialEditorTool(IEditorToolContext* Context, CObject* InAsset);
        
        bool IsSingleWindowTool() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_FORMAT_LIST_BULLETED_TYPE; }
        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void SetupWorldForTool() override;

        bool DrawViewport(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture) override;
        void DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize) override;
        bool ShouldGenerateThumbnailOnSave() const override { return true; }
        void OnAssetLoadFinished() override;
        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void DrawMaterialGraph();
        void DrawMaterialProperties();
        void DrawShaderStats();

        void Compile();
        void ApplyMaterialToPreview();
        void FocusGraphNode(CEdGraphNode* Node);
        void OnSave() override;
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

    protected:
        
        void DrawHelpMenu() override;
        
    private:
        
        void SetDebugMesh(EDebugMesh Mesh, FStringView Path = "");
        
    private:
        
        entt::entity                    MeshEntity;
        entt::entity                    DirectionalLightEntity;
        
        FString                         Tree;
        FString                         VertexTree;
        FMaterialCompiler::FShaderStats ShaderStats;
        bool                            bHasCompiledOnce = false;
        size_t                          ReplacementStart = 0;
        size_t                          ReplacementEnd = 0;
        CEdGraphNode*                   SelectedNode = nullptr;
        FCompilationResultInfo          CompilationResult;
        
        
        TUniquePtr<FPropertyTable>      EnvironmentEditor;
        TUniquePtr<FPropertyTable>      DirectionalEditor;
        
        TObjectPtr<CMaterialNodeGraph>  NodeGraph;
        bool                            bGLSLPreviewDirty = false;
        
        EDebugMesh                      DebugMesh;
    };
}
