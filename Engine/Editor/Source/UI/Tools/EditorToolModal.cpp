#include "EditorToolModal.h"

#include "Core/Templates/LuminaTemplate.h"
#include "UI/SlowTaskModal.h"

namespace Lumina
{
    void FEditorModalManager::CreateDialogue(const FString& Title, ImVec2 Size, TMoveOnlyFunction<bool()> DrawFunction, bool bBlocking, bool bCloseable)
    {
        if (ActiveModal != nullptr)
        {
            return;
        }

        ActiveModal = MakeUnique<FEditorToolModal>(Title, Size, bCloseable);
        ActiveModal->DrawFunction = Move(DrawFunction);
        ActiveModal->bBlocking = bBlocking;
    }


    void FEditorModalManager::DrawDialogue()
    {
        // A blocking modal must host the slow-task popup as a nested child. ImGui keeps
        // only one modal chain, so two sibling modals close each other every frame;
        // nesting keeps both alive and stacks the progress popup cleanly on top.
        if (ActiveModal && ActiveModal->bBlocking)
        {
            ImGui::OpenPopup(ActiveModal->Title.c_str());

            ImGuiViewport* Viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(Viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ActiveModal->Size, ImGuiCond_Appearing);

            bool* bOpen = ActiveModal->bCloseable ? &ActiveModal->bOpen : nullptr;

            if (ImGui::BeginPopupModal(ActiveModal->Title.c_str(), bOpen, ImGuiWindowFlags_NoCollapse))
            {
                const bool bClose = ActiveModal->DrawModal() || !ActiveModal->bOpen;

                SlowTaskModal::Render();

                if (bClose)
                {
                    ImGui::CloseCurrentPopup();
                    ActiveModal.reset();
                }
                ImGui::EndPopup();
            }
            return;
        }

        if (ActiveModal)
        {
            // Non-blocking modal: a plain window, so there is no modal chain to conflict with.
            ImGuiViewport* Viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(Viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ActiveModal->Size, ImGuiCond_Appearing);

            bool* bOpen = ActiveModal->bCloseable ? &ActiveModal->bOpen : nullptr;

            const bool bVisible = ImGui::Begin(ActiveModal->Title.c_str(), bOpen, ImGuiWindowFlags_NoCollapse);
            if (bVisible && (ActiveModal->DrawModal() || !ActiveModal->bOpen))
            {
                ActiveModal.reset();
            }
            ImGui::End();
        }

        // No blocking modal in the way: the slow-task popup owns the root modal scope.
        SlowTaskModal::Render();
    }
}
