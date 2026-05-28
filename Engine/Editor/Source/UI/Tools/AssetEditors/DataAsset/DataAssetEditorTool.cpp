#include "DataAssetEditorTool.h"

#include "Assets/AssetTypes/DataAsset/DataAsset.h"
#include "Assets/AssetTypes/DataAsset/DataAssetSchema.h"
#include "Core/Object/Package/Package.h"
#include "Core/Reflection/PropertyBag/PropertyBag.h"
#include "UI/Properties/PropertyTable.h"
#include "imgui.h"

namespace Lumina
{
    static const char* DataAssetWindowName = "Data Asset";

    FDataAssetEditorTool::FDataAssetEditorTool(IEditorToolContext* Context, CObject* InAsset)
        : FAssetEditorTool(Context, InAsset->GetName().c_str(), InAsset)
    {
    }

    void FDataAssetEditorTool::OnInitialize()
    {
        FAssetEditorTool::OnInitialize();

        FPropertyBag& Bag = GetAsset<CDataAsset>()->GetPropertyBag();
        PropertyTable = MakeUnique<FPropertyTable>(Bag.GetValueData(), Bag.GetLayout());
        BoundLayout = Bag.GetLayout();
        PropertyTable->SetPostEditCallback([this](const FPropertyChangedEvent&)
        {
            if (CDataAsset* Asset = GetAsset<CDataAsset>())
            {
                Asset->GetPackage()->MarkDirty();
            }
        });

        CreateToolWindow(DataAssetWindowName, [this](bool bFocused)
        {
            DrawEditorWindow(bFocused);
        });
    }

    void FDataAssetEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& /*InDockspaceSize*/) const
    {
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(DataAssetWindowName).c_str(), InDockspaceID);
    }

    void FDataAssetEditorTool::DrawEditorWindow(bool /*bFocused*/)
    {
        CDataAsset* Asset = GetAsset<CDataAsset>();
        if (Asset == nullptr)
        {
            return;
        }

        // Schema is shown read-only -- it's chosen at creation.
        CDataAssetSchema* Schema = Asset->GetSchema();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Schema:");
        ImGui::SameLine();
        if (Schema != nullptr)
        {
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", Schema->GetName().c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "<none>");
        }
        ImGui::Separator();
        ImGui::Spacing();

        FPropertyBag& Bag = Asset->GetPropertyBag();

        // The schema may have been edited (instance re-synced) or deleted, reallocating the
        // bag's layout/buffer; re-point the table before drawing so it never reads freed memory.
        if (Bag.GetLayout() != BoundLayout)
        {
            BoundLayout = Bag.GetLayout();
            PropertyTable->SetObject(Bag.GetValueData(), Bag.GetLayout());
        }

        // Without a live schema the layout is ownerless (deleted or unresolved on load); don't
        // present its fields as editable.
        if (Schema == nullptr)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "Schema is missing -- this data asset is orphaned.");
            return;
        }

        if (Bag.GetNumProperties() == 0)
        {
            ImGui::TextDisabled("Schema has no fields.");
            return;
        }

        PropertyTable->DrawTree();
    }
}
