#pragma once

#define USE_IMGUI_API
#include <imgui.h>

#include "ImGuizmo.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"

namespace Lumina
{
    class FAnimationEditorTool : public FAssetEditorTool
    {
    public:
        
        LUMINA_EDITOR_TOOL(FAnimationEditorTool)
        
        FAnimationEditorTool(IEditorToolContext* Context, CObject* InAsset);


        bool IsSingleWindowTool() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_FORMAT_LIST_BULLETED_TYPE; }
        void OnInitialize() override;
        void SetupWorldForTool() override;
        void Update(const FUpdateContext& UpdateContext) override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void OnAssetLoadFinished() override;
        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;
        
        void DrawSequencer();
        
        float Playrate = 1.0f;
        
        ImGuizmo::OPERATION GuizmoOp = ImGuizmo::TRANSLATE;
        entt::entity DirectionalLightEntity = entt::null;
        entt::entity MeshEntity = entt::null;
    };
}
