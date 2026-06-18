#include "GameplayTagPropertyCustomization.h"

#include <EASTL/algorithm.h>

#include "imgui.h"
#include "Containers/String.h"
#include "Config/Config.h"
#include "Core/Object/ObjectCore.h"
#include "GameplayTags/GameplayTagRegistry.h"
#include "GameplayTags/GameplayTagsSettings.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{
    EPropertyChangeOp FGameplayTagPropertyCustomization::DrawProperty(const TSharedPtr<FPropertyHandle>& Property)
    {
        bool bChanged = false;

        // Candidate list: index 0 == "None", then every registered tag (sorted for a stable picker).
        TVector<FString> Tags;
        FGameplayTagRegistry::Get().GetAllTags(Tags);
        eastl::sort(Tags.begin(), Tags.end());

        const FString Current = Value.TagName.IsNone() ? FString() : FString(Value.TagName.c_str());
        int32 CurrentIndex = 0;
        for (size_t i = 0; i < Tags.size(); ++i)
        {
            if (Tags[i] == Current)
            {
                CurrentIndex = static_cast<int32>(i + 1);
                break;
            }
        }

        const char* Preview = Value.TagName.IsNone() ? "None" : Value.TagName.c_str();

        const float LineHeight = ImGui::GetFrameHeight();
        const float Spacing    = ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - LineHeight - Spacing);

        const int32 Picked = ImGuiX::SearchableCombo("##gameplaytag", Preview, static_cast<int32>(Tags.size()) + 1, CurrentIndex,
            [&Tags](int32 Index) -> FFixedString
            {
                return (Index == 0) ? FFixedString("None") : FFixedString(Tags[Index - 1].c_str());
            }, LE_ICON_TAG);

        ImGui::PopItemWidth();

        if (Picked != INDEX_NONE)
        {
            Value.TagName = (Picked == 0) ? FName() : FName(Tags[Picked - 1].c_str());
            bChanged = true;
        }

        // "+" : author a new tag. Interns it, persists it to the project's tag settings, and selects it.
        ImGui::SameLine(0.0f, Spacing);
        if (ImGui::Button(LE_ICON_PLUS "##addtag", ImVec2(LineHeight, LineHeight)))
        {
            NewTagBuffer[0] = '\0';
            ImGui::OpenPopup("AddGameplayTag");
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Add a new gameplay tag");
        }

        if (ImGui::BeginPopup("AddGameplayTag"))
        {
            ImGui::TextUnformatted("New tag (e.g. Ability.Fire.Fireball)");
            ImGui::SetNextItemWidth(280.0f);
            const bool bEnter = ImGui::InputText("##newtag", NewTagBuffer, sizeof(NewTagBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            const bool bAdd = ImGui::Button("Add");

            if ((bEnter || bAdd) && NewTagBuffer[0] != '\0')
            {
                const FString NewTag(NewTagBuffer);
                FGameplayTagRegistry::Get().RequestTag(FStringView(NewTag.c_str(), NewTag.size()));

                CGameplayTagsSettings* Settings = GetMutableDefault<CGameplayTagsSettings>();
                if (Settings != nullptr && eastl::find(Settings->Tags.begin(), Settings->Tags.end(), NewTag) == Settings->Tags.end())
                {
                    Settings->Tags.push_back(NewTag);
                    if (GConfig != nullptr)
                    {
                        GConfig->SaveSettings(CGameplayTagsSettings::StaticClass());
                    }
                }

                Value.TagName = FName(NewTag.c_str());
                bChanged = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        return bChanged ? EPropertyChangeOp::Updated : EPropertyChangeOp::None;
    }

    void FGameplayTagPropertyCustomization::UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property)
    {
        if (void* Ptr = Property->GetValuePtr())
        {
            *static_cast<FGameplayTag*>(Ptr) = Value;
        }
    }

    void FGameplayTagPropertyCustomization::HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property)
    {
        if (const void* Ptr = Property->GetValuePtr())
        {
            Value = *static_cast<const FGameplayTag*>(Ptr);
        }
    }
}
