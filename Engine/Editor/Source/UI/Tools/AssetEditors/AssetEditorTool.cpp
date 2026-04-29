#include "AssetEditorTool.h"
#include "Core/Object/Package/Package.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{
    void FAssetEditorTool::OnInitialize()
    {
    }

    void FAssetEditorTool::Deinitialize(const FUpdateContext& UpdateContext)
    {
        FEditorTool::Deinitialize(UpdateContext);
    }

    FName FAssetEditorTool::GetToolName() const
    {
        if (Asset != nullptr)
        {
            return Asset->GetName();
        }
        return ToolName;
    }

    void FAssetEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        DrawWorldGrid();

        if (!bAssetLoadBroadcasted && Asset != nullptr)
        {
            OnAssetLoadFinished();
            bAssetLoadBroadcasted = true;
        }

        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S))
        {
            OnSave();
        }
    }

    void FAssetEditorTool::OnSave()
    {
        // Default save path applies to CObject-backed assets only. Tools that
        // edit raw files (e.g. .rml) override this to write through the VFS.
        if (Asset == nullptr)
        {
            return;
        }

        if (ShouldGenerateThumbnailOnSave() && Asset->GetPackage())
        {
            GenerateThumbnail(Asset->GetPackage());
        }

        if (CPackage::SavePackage(Asset->GetPackage(), Asset->GetPackage()->GetPackagePath()))
        {
            FAssetRegistry::Get().AssetSaved(Asset);
            ImGuiX::Notifications::NotifySuccess("Successfully saved package: \"{0}\"", Asset->GetName().c_str());
        }
        else
        {
            ImGuiX::Notifications::NotifyError("Failed to save package: \"{0}\"", Asset->GetName().c_str());
        }
    }

    bool FAssetEditorTool::IsAssetEditorTool() const
    {
        return true;
    }
}
