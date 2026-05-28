#include "EntityComponentTypeEditorTool.h"

#include "Assets/AssetTypes/EntityComponent/EntityComponentType.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectArray.h"
#include "Core/Object/Package/Package.h"
#include "Core/Reflection/PropertyBag/PropertyBag.h"
#include "UI/Properties/PropertyTable.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "World/Entity/EntityUtils.h"
#include "imgui.h"

namespace Lumina
{
    static const char* ComponentTypeWindowName = "Component Type";

    // Strips only a PascalCase prefix (MakeDisplayName strips the first char unconditionally).
    static FString PrettyReflectedName(const FName& Name)
    {
        FString Result = Name.ToString();
        if (Result.size() >= 2 && Result[0] >= 'A' && Result[0] <= 'Z' && Result[1] >= 'A' && Result[1] <= 'Z')
        {
            Result.erase(0, 1);
        }
        return Result;
    }

    static bool IsBuiltinVectorStruct(const FName& Name)
    {
        const FString S = Name.ToString();
        return S == "vec2" || S == "vec3" || S == "vec4";
    }

    FEntityComponentTypeEditorTool::FEntityComponentTypeEditorTool(IEditorToolContext* Context, CObject* InAsset)
        : FAssetEditorTool(Context, InAsset->GetName().c_str(), InAsset)
    {
    }

    void FEntityComponentTypeEditorTool::OnInitialize()
    {
        FAssetEditorTool::OnInitialize();

        FPropertyBag& Bag = GetAsset<CEntityComponentType>()->GetSchemaBag();
        PropertyTable = MakeUnique<FPropertyTable>(Bag.GetValueData(), Bag.GetLayout());
        PropertyTable->SetPostEditCallback([this](const FPropertyChangedEvent&)
        {
            if (CEntityComponentType* Asset = GetAsset<CEntityComponentType>())
            {
                Asset->GetPackage()->MarkDirty();
            }
        });
        PropertyTable->ChangeEventCallbacks.RowTrailingControlFn = [this](FProperty* Property)
        {
            DrawFieldDeleteButton(Property);
        };
        PropertyTable->ChangeEventCallbacks.RowTrailingControlWidth = 34.0f;

        CreateToolWindow(ComponentTypeWindowName, [this](bool bFocused)
        {
            DrawEditorWindow(bFocused);
        });
    }

    void FEntityComponentTypeEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& /*InDockspaceSize*/) const
    {
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(ComponentTypeWindowName).c_str(), InDockspaceID);
    }

    void FEntityComponentTypeEditorTool::OnSchemaChanged()
    {
        CEntityComponentType* Asset = GetAsset<CEntityComponentType>();
        FPropertyBag& Bag = Asset->GetSchemaBag();
        PropertyTable->SetObject(Bag.GetValueData(), Bag.GetLayout());
        Asset->BumpSchemaRevision();
        Asset->GetPackage()->MarkDirty();

        ECS::Utils::RefreshAllWorldsRuntimeComponentSchemas();
    }

    void FEntityComponentTypeEditorTool::DrawFieldDeleteButton(FProperty* Property)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button(LE_ICON_TRASH_CAN, ImVec2(30.0f, 0.0f)))
        {
            PendingRemoveField = Property->GetPropertyName();
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGuiX::TextTooltip_Internal("Remove field");
        }
    }

    void FEntityComponentTypeEditorTool::EnsureTypeChoices()
    {
        if (bTypeChoicesBuilt)
        {
            return;
        }
        bTypeChoicesBuilt = true;

        const EBagPropertyType Builtins[] =
        {
            EBagPropertyType::Bool, EBagPropertyType::Int32, EBagPropertyType::Int64,
            EBagPropertyType::Float, EBagPropertyType::Double,
            EBagPropertyType::Vector2, EBagPropertyType::Vector3, EBagPropertyType::Vector4,
            EBagPropertyType::Name, EBagPropertyType::String, EBagPropertyType::Object,
        };
        for (EBagPropertyType Type : Builtins)
        {
            TypeChoices.push_back({ Type, FName(), BagPropertyTypeToString(Type) });
        }

        TVector<CEnum*>   Enums;
        TVector<CStruct*> Structs;
        GObjectArray.ForEachObject([&](CObjectBase* Object, int32)
        {
            if (Object == nullptr)
            {
                return;
            }
            if (Object->IsA<CEnum>())
            {
                CEnum* Enum = static_cast<CEnum*>(Object);
                if (!Enum->Names.empty())
                {
                    Enums.push_back(Enum);
                }
            }
            else if (Object->IsA<CStruct>() && !Object->IsA<CClass>() && !Object->HasAnyFlag(OF_Transient)
                && !IsBuiltinVectorStruct(Object->GetName()))
            {
                Structs.push_back(static_cast<CStruct*>(Object));
            }
        });

        auto ByPrettyName = [](auto* A, auto* B)
        {
            return strcmp(PrettyReflectedName(A->GetName()).c_str(), PrettyReflectedName(B->GetName()).c_str()) < 0;
        };
        eastl::sort(Enums.begin(), Enums.end(), ByPrettyName);
        eastl::sort(Structs.begin(), Structs.end(), ByPrettyName);

        for (CEnum* Enum : Enums)
        {
            TypeChoices.push_back({ EBagPropertyType::Enum, Enum->GetName(),
                FString(LE_ICON_CODE_BRACES " ") + PrettyReflectedName(Enum->GetName()) });
        }
        for (CStruct* Struct : Structs)
        {
            TypeChoices.push_back({ EBagPropertyType::Struct, Struct->GetName(),
                FString(LE_ICON_CUBE_OUTLINE " ") + PrettyReflectedName(Struct->GetName()) });
        }
    }

    void FEntityComponentTypeEditorTool::DrawAddFieldRow()
    {
        FPropertyBag& Bag = GetAsset<CEntityComponentType>()->GetSchemaBag();
        EnsureTypeChoices();

        ImGui::TextWrapped("Fields declared here are the runtime component's layout. Every entity that "
            "carries this component stores these fields contiguously. Edit a field's value below to set "
            "the default new instances inherit.");
        ImGui::Separator();

        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##newname", "Field name", NewFieldNameBuffer, sizeof(NewFieldNameBuffer));
        ImGui::SameLine();

        const FString& Preview = TypeChoices[SelectedTypeChoice].Display;
        ImGui::SetNextItemWidth(220.0f);
        const int32 Picked = ImGuiX::SearchableCombo("##newtype", Preview.c_str(), (int32)TypeChoices.size(), SelectedTypeChoice,
            [this](int32 Index) { return FFixedString(TypeChoices[Index].Display.c_str()); }, LE_ICON_SHAPE);
        if (Picked != INDEX_NONE)
        {
            SelectedTypeChoice = Picked;
        }
        ImGui::SameLine();

        const FName NewName(NewFieldNameBuffer);
        const bool bCanAdd = !NewName.IsNone() && !Bag.HasProperty(NewName);
        ImGui::BeginDisabled(!bCanAdd);
        if (ImGui::Button(LE_ICON_PLUS " Add Field"))
        {
            const FTypeChoice& Choice = TypeChoices[SelectedTypeChoice];
            Bag.AddProperty(NewName, Choice.Type, Choice.TypeName);
            OnSchemaChanged();
            strncpy(NewFieldNameBuffer, "NewField", sizeof(NewFieldNameBuffer));
        }
        ImGui::EndDisabled();
        if (!bCanAdd && !NewName.IsNone())
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "name in use");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%d field%s", Bag.GetNumProperties(), Bag.GetNumProperties() == 1 ? "" : "s");
    }

    void FEntityComponentTypeEditorTool::DrawEditorWindow(bool /*bFocused*/)
    {
        if (GetAsset<CEntityComponentType>() == nullptr)
        {
            return;
        }

        DrawAddFieldRow();
        ImGui::Spacing();

        FPropertyBag& Bag = GetAsset<CEntityComponentType>()->GetSchemaBag();
        if (Bag.GetNumProperties() == 0)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.35f, 1.0f), "No fields yet -- add one above.");
            return;
        }

        PropertyTable->DrawTree();

        if (!PendingRemoveField.IsNone())
        {
            Bag.RemoveProperty(PendingRemoveField);
            PendingRemoveField = FName();
            OnSchemaChanged();
        }
    }
}
