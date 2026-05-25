#include "EntityPropertyCustomization.h"

#include "imgui.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Lumina.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "UI/Properties/EntityPropertyContext.h"
#include "World/World.h"
#include "World/Entity/Components/EditorComponent.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Registry/EntityRegistry.h"

namespace Lumina
{
    namespace
    {
        // Integral id of an unset entity reference.
        const uint32 GNoneEntityId = static_cast<uint32>(entt::to_integral(static_cast<entt::entity>(entt::null)));

        FFixedString MakeEntityLabel(FEntityRegistry& Registry, entt::entity Entity)
        {
            FFixedString Label;
            if (const SNameComponent* Name = Registry.try_get<SNameComponent>(Entity))
            {
                Label.append(Name->Name.c_str());
            }
            else
            {
                Label.append("Entity");
            }
            Label.append_sprintf(" (%u)", static_cast<uint32>(entt::to_integral(Entity)));
            return Label;
        }
    }

    EPropertyChangeOp FEntityPropertyCustomization::DrawProperty(TSharedPtr<FPropertyHandle> Property)
    {
        CWorld* World = GetEntityPropertyContextWorld();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

        // No world to resolve against (e.g. an asset editor); show the raw id, read-only.
        if (World == nullptr)
        {
            int Value = static_cast<int>(CachedValue);
            ImGui::BeginDisabled(true);
            ImGui::InputInt("##entity", &Value, 0, 0, ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            ImGui::PopItemWidth();
            return EPropertyChangeOp::None;
        }

        FEntityRegistry& Registry = World->GetEntityRegistry();

        // Index 0 is always "None"; the rest are the same named entities the outliner shows.
        TVector<entt::entity> Candidates;
        Candidates.push_back(entt::null);

        const entt::entity CurrentEntity = static_cast<entt::entity>(CachedValue);
        const bool bHasCurrent = (CachedValue != GNoneEntityId) && Registry.valid(CurrentEntity);

        int32 CurrentIndex = 0; // default to None
        for (entt::entity Entity : Registry.view<SNameComponent>(entt::exclude<FHideInSceneOutliner>))
        {
            if (bHasCurrent && Entity == CurrentEntity)
            {
                CurrentIndex = static_cast<int32>(Candidates.size());
            }
            Candidates.push_back(Entity);
        }

        const FFixedString Preview = bHasCurrent ? MakeEntityLabel(Registry, CurrentEntity) : FFixedString("None");

        const int32 Picked = ImGuiX::SearchableCombo("##entity", Preview.c_str(), static_cast<int32>(Candidates.size()), CurrentIndex,
            [&Registry, &Candidates](int32 Index) -> FFixedString
            {
                if (Index == 0)
                {
                    return FFixedString("None");
                }
                return MakeEntityLabel(Registry, Candidates[Index]);
            }, LE_ICON_CUBE);

        ImGui::PopItemWidth();

        if (Picked != INDEX_NONE)
        {
            CachedValue = (Picked == 0) ? GNoneEntityId : static_cast<uint32>(entt::to_integral(Candidates[Picked]));
            return EPropertyChangeOp::Updated;
        }

        return EPropertyChangeOp::None;
    }

    void FEntityPropertyCustomization::UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property)
    {
        Property->Property->SetValue(Property->ContainerPtr, CachedValue, Property->Index);
    }

    void FEntityPropertyCustomization::HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property)
    {
        Property->Property->GetValue(Property->ContainerPtr, &CachedValue, Property->Index);
    }
}
