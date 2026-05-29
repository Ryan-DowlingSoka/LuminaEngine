#include "PhysicsMaterialEditorTool.h"

#include "Assets/AssetTypes/PhysicsMaterial/PhysicsMaterial.h"
#include "UI/Properties/PropertyTable.h"
#include "imgui.h"

namespace Lumina
{
    static const char* PhysicsMaterialWindowName = "Physics Material";

    FPhysicsMaterialEditorTool::FPhysicsMaterialEditorTool(IEditorToolContext* Context, CObject* InAsset)
        : FAssetEditorTool(Context, InAsset->GetName().c_str(), InAsset)
    {
    }

    void FPhysicsMaterialEditorTool::OnInitialize()
    {
        FAssetEditorTool::OnInitialize();

        // PropertyTable is already bound to the asset by FAssetEditorTool's ctor; we just need
        // a window to draw it in. The base SetPostEditCallback already marks the package dirty.
        CreateToolWindow(PhysicsMaterialWindowName, [this](bool /*bFocused*/)
        {
            PropertyTable.DrawTree();
        });
    }

    void FPhysicsMaterialEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& /*InDockspaceSize*/) const
    {
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(PhysicsMaterialWindowName).c_str(), InDockspaceID);
    }
}
