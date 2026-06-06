#include "TextureBrowserEditorTool.h"

#include "Renderer/RenderManager.h"


namespace Lumina
{
    void FTextureBrowserEditorTool::OnInitialize()
    {
        CreateToolWindow("Texture Registry", [&] (bool bIsFocused)
        {
            DrawWindow(bIsFocused);
        });
    }

    void FTextureBrowserEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FTextureBrowserEditorTool::DrawHelpMenu()
    {
        FEditorTool::DrawHelpMenu();
    }

    void FTextureBrowserEditorTool::DrawWindow(bool bIsFocused)
    {
        RHI::FTextureManager& Manager = GRenderManager->GetTextureManager();
        
        constexpr ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner;
        if (ImGui::BeginTable("TextureTable", 2, Flags))
        {
            ImGui::TableSetupColumn("##thumb", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Path",    ImGuiTableColumnFlags_WidthStretch);

            auto Size = Manager.GetDescriptorManager().GetDescriptors().size();
            
            ImGuiListClipper clipper;
            clipper.Begin((int)Size);

            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const auto& Item = Manager.GetDescriptorManager().GetDescriptors()[i];

                    if (Item.ResourceHandle == nullptr)
                    {
                        continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    
                    const FRHIImageDesc* Desc = nullptr;
                    if (Item.Type == ERHIBindingResourceType::Texture_SRV || Item.Type == ERHIBindingResourceType::Texture_UAV)
                    {
                        auto* ImageHandle = static_cast<FRHIImage*>(Item.ResourceHandle);
                        Desc = &ImageHandle->GetDescription();
                        auto ImGuiImage = ImGuiX::ToImTextureRef(ImageHandle);

                        ImGui::Image(ImGuiImage, ImVec2(300, 300));
                    }

                    ImGui::TableNextColumn();
                    
                    if (Desc)
                    {
                        ImGui::Text("Name: %s", Desc->DebugName.c_str());
                        ImGui::Text("Mips: %i", Desc->NumMips);
                    }
                }
            }
            ImGui::EndTable();
        }
    }
}
