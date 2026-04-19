#include "PropertyTable.h"

#include "Core/Engine/Engine.h"
#include "Core/Object/Class.h"
#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Core/Reflection/Type/Properties/ArrayProperty.h"
#include "Core/Reflection/Type/Properties/StructProperty.h"
#include "Customizations/CoreTypeCustomization.h"
#include "Tools/UI/DevelopmentToolUI.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{
    
    static TUniquePtr<FPropertyRow> CreatePropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& InCallbacks)
    {
        TUniquePtr<FPropertyRow> NewRow;
        if (InPropHandle->Property->GetType() == EPropertyTypeFlags::Vector)
        {
            NewRow = MakeUnique<FArrayPropertyRow>(InPropHandle, InParentRow, InCallbacks);
        }
        else if (InPropHandle->Property->GetType() == EPropertyTypeFlags::Struct)
        {
            NewRow = MakeUnique<FStructPropertyRow>(InPropHandle, InParentRow, InCallbacks);
        }
        else
        {
            NewRow = MakeUnique<FPropertyPropertyRow>(InPropHandle, InParentRow, InCallbacks);
        }

        return NewRow;
    }
    
    FPropertyRow::FPropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& InCallbacks)
        : Callbacks(InCallbacks)
        , PropertyHandle(InPropHandle)
        , ParentRow(InParentRow)
    {
    }
    
    void FPropertyRow::DestroyChildren()
    {
        Children.clear();
    }

    void FPropertyRow::UpdateRow()
    {
        for (const TUniquePtr<FPropertyRow>& Child : Children)
        {
            Child->UpdateRow();
        }

        Update();
    }

    void FPropertyRow::DrawRow(float Offset, bool bReadOnly)
    {
        ImGui::PushID(this);
        
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        DrawHeader(Offset);
        
        ImGui::TableNextColumn();
        {
            constexpr ImGuiTableFlags Flags = ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit;
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2, 0));
            if (ImGui::BeginTable("GridTable", HasExtraControls() ? 3 : 2, Flags))
            {
                ImGui::TableSetupColumn("##Editor", ImGuiTableColumnFlags_WidthStretch);

                if (HasExtraControls())
                {
                    ImGui::TableSetupColumn("##Extra", ImGuiTableColumnFlags_WidthFixed, GetExtraControlsSectionWidth());
                }
                
                ImGui::TableSetupColumn("##Reset", ImGuiTableColumnFlags_WidthFixed, 24);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                
                DrawEditor(bReadOnly);

                if (HasExtraControls())
                {
                    ImGui::TableNextColumn();
                    DrawExtraControlsSection();
                }
                
                ImGui::TableNextColumn();

                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
        }
        ImGui::PopID();

        if (bExpanded)
        {
            ImGui::BeginDisabled(IsReadOnly());
            const float ChildHeaderOffset = Offset + 8;
            
            for (TUniquePtr<FPropertyRow>& Row : Children)
            {
                Row->DrawRow(ChildHeaderOffset, bReadOnly);
            }
            ImGui::EndDisabled();
        }
    }

    bool FPropertyRow::IsReadOnly() const
    {
        return PropertyHandle == nullptr ? false : PropertyHandle->Property->IsReadOnly();
    }
    
    FPropertyPropertyRow::FPropertyPropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& InCallbacks)
        : FPropertyRow(InPropHandle, InParentRow, InCallbacks)
    {
        
        switch (PropertyHandle->Property->GetType())
        {
        case EPropertyTypeFlags::None:
            break;
        case EPropertyTypeFlags::Int8:
            {
                Customization = FNumericPropertyCustomization<int8, ImGuiDataType_S8>::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::Int16:
            {
                Customization = FNumericPropertyCustomization<int16, ImGuiDataType_S16>::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::Int32:
            {
                Customization = FNumericPropertyCustomization<int32, ImGuiDataType_S32>::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::Int64:
            {
                Customization = FNumericPropertyCustomization<int64, ImGuiDataType_S64>::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::UInt8:
            {
                Customization = FNumericPropertyCustomization<uint8, ImGuiDataType_U8>::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::UInt16:
            {
                Customization = FNumericPropertyCustomization<uint16, ImGuiDataType_U16>::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::UInt32:
            {
                Customization = FNumericPropertyCustomization<uint32, ImGuiDataType_U32>::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::UInt64:
            {
                Customization = FNumericPropertyCustomization<uint64, ImGuiDataType_U64>::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::Float:
            {
                Customization = FNumericPropertyCustomization<float, ImGuiDataType_Float>::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::Double:
            {
                Customization = FNumericPropertyCustomization<double, ImGuiDataType_Double>::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::Bool:
            {
                Customization = FBoolPropertyCustomization::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::Object:
            {
                Customization = FCObjectPropertyCustomization::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::Class:
            break;
        case EPropertyTypeFlags::Name:
            {
                Customization = FNamePropertyCustomization::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::String:
            {
                Customization = FStringPropertyCustomization::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::Enum:
            {
                Customization = FEnumPropertyCustomization::MakeInstance();
            }
            break;
        case EPropertyTypeFlags::Vector:
            break;
        case EPropertyTypeFlags::Struct:
            break;
        case EPropertyTypeFlags::Count:
            break;
        }
    }

    void FPropertyPropertyRow::Update()
    {
        switch (ChangeOp)
        {
        case EPropertyChangeOp::None:
            break;
        case EPropertyChangeOp::Updated:
            {
                if (Callbacks.PreChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PreChangeCallback(Move(Event));
                }
                
                Customization->UpdatePropertyValue(PropertyHandle);

                if (Callbacks.PostChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PostChangeCallback(Move(Event));
                }
            }
            break;
        case EPropertyChangeOp::Started:
            {
                if (Callbacks.StartChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.StartChangeCallback(Move(Event));
                }
             
                if (Callbacks.PreChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PreChangeCallback(Move(Event));
                }
                
                Customization->UpdatePropertyValue(PropertyHandle);

                if (Callbacks.PostChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PostChangeCallback(Move(Event));
                }
            }
            break;
        case EPropertyChangeOp::Finished:
            {
                if (Callbacks.PreChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PreChangeCallback(Move(Event));
                }
                
                Customization->UpdatePropertyValue(PropertyHandle);

                if (Callbacks.PostChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PostChangeCallback(Move(Event));
                }
                
                if (Callbacks.FinishChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.FinishChangeCallback(Move(Event));
                }
            }
            break;
        }

        ChangeOp = EPropertyChangeOp::None;
    }

    void FPropertyPropertyRow::DrawHeader(float Offset)
    {
        ImGui::Dummy(ImVec2(Offset, 0));
        ImGui::SameLine();
        
        FString DisplayName = IsArrayElementProperty() ? eastl::to_string(PropertyHandle->Index) : PropertyHandle->Property->GetPropertyDisplayName();
        ImGui::TextUnformatted(DisplayName.c_str());
        
        const FString* Tooltip = PropertyHandle->Property->TryGetMetadata("ToolTip");
        if (Tooltip == nullptr)
        {
            Tooltip = &PropertyHandle->Property->GetPropertyDisplayName();
        }
        
        ImGuiX::TextTooltip("{}", *Tooltip);
    }

    void FPropertyPropertyRow::DrawEditor(bool bReadOnly)
    {
        ImGui::BeginDisabled(IsReadOnly());

        if (Customization)
        {
            ChangeOp = Customization->UpdateAndDraw(PropertyHandle, bReadOnly);
        }
        else
        {
            ImGui::Text(LE_ICON_EXCLAMATION "Missing Property Customization");
        }
        
        ImGui::EndDisabled();
    }

    bool FPropertyPropertyRow::HasExtraControls() const
    {
        return bArrayElement;
    }

    void FPropertyPropertyRow::DrawExtraControlsSection()
    {
        FArrayPropertyRow* ArrayRow = static_cast<FArrayPropertyRow*>(ParentRow);
        FArrayProperty* ArrayProperty = static_cast<FArrayPropertyRow*>(ParentRow)->ArrayProperty;
        void* ContainerPtr = ArrayRow->GetPropertyHandle()->ContainerPtr;
        size_t ArrayNum = ArrayProperty->GetNum(ContainerPtr);
        
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4));
        ImGuiX::FlatButton(LE_ICON_DOTS_HORIZONTAL, ImVec2(18, 24), 428768833);
        ImGui::PopStyleVar();

        if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft))
        {
            if (ImGui::MenuItem(LE_ICON_PLUS" Insert New Element"))
            {
                ArrayProperty->PushBack(ContainerPtr, nullptr);
                ArrayRow->RebuildChildren();
            }

            if (PropertyHandle->Index > 0)
            {
                if (ImGui::MenuItem(LE_ICON_ARROW_UP" Move Element Up"))
                {
                    ArrayProperty->Swap(ContainerPtr, PropertyHandle->Index, PropertyHandle->Index - 1);
                    ArrayRow->RebuildChildren();
                }
                
                ImGuiX::TextTooltip("Move the element up the array (lower index)");
            }
            
            
            if (std::cmp_less(PropertyHandle->Index, (ArrayNum - 1)))
            {
                if (ImGui::MenuItem(LE_ICON_ARROW_DOWN" Move Element Down"))
                {
                    ArrayProperty->Swap(ContainerPtr, PropertyHandle->Index, PropertyHandle->Index + 1);
                    ArrayRow->RebuildChildren();
                }
                
                ImGuiX::TextTooltip("Move the element down the array (higher index)");

            }

            if (ImGui::MenuItem(LE_ICON_TRASH_CAN" Remove Element"))
            {
                ArrayProperty->RemoveAt(ContainerPtr, PropertyHandle->Index);
                ArrayRow->RebuildChildren();
            }
            
            ImGuiX::TextTooltip("Remove the element.");

            ImGui::EndPopup();
        }

        ImGuiX::TextTooltip("Array Element Options");
    }

    FArrayPropertyRow::FArrayPropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& InCallbacks)
        : FPropertyRow(InPropHandle, InParentRow, InCallbacks)
        , ArrayProperty(static_cast<FArrayProperty*>(InPropHandle->Property))
    {
        RebuildChildren();
    }

    void FArrayPropertyRow::Update()
    {
        ChangeOp = EPropertyChangeOp::None;
    }

    void FArrayPropertyRow::DrawHeader(float Offset)
    {
        ImGui::Dummy(ImVec2(Offset, 0));
        ImGui::SameLine();

        ImGui::SetNextItemOpen(bExpanded);
        ImGui::PushStyleColor(ImGuiCol_Header, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
        ImGuiTreeNodeFlags Flags = ArrayProperty->GetNum(GetPropertyHandle()->ContainerPtr) ? 0 : ImGuiTreeNodeFlags_Leaf;
        bExpanded = ImGui::CollapsingHeader(ArrayProperty->GetPropertyDisplayName().c_str(), Flags);
        
        const FString* Tooltip = ArrayProperty->TryGetMetadata("ToolTip");
        if (Tooltip == nullptr)
        {
            Tooltip = &ArrayProperty->GetPropertyDisplayName();
        }
        
        ImGuiX::TextTooltip("{}", *Tooltip);
        ImGui::PopStyleColor(3);
    }

    void FArrayPropertyRow::DrawEditor(bool bReadOnly)
    {
        size_t ElementCount = ArrayProperty->GetNum(GetPropertyHandle()->ContainerPtr);
        
        ImGui::TextColored(ImVec4(0.24f, 0.24f, 0.24f, 1.0f), "%llu Elements", ElementCount);
    }

    void FArrayPropertyRow::RebuildChildren()
    {
        DestroyChildren();

        void* ContainerPtr = GetPropertyHandle()->ContainerPtr;
        size_t ElementCount = ArrayProperty->GetNum(ContainerPtr);
        
        for (size_t i = 0; i < ElementCount; ++i)
        {
            TSharedPtr<FPropertyHandle> ElementPropHandle = MakeShared<FPropertyHandle>(ArrayProperty->GetAt(ContainerPtr, i), ArrayProperty->GetInternalProperty(), i);
            TUniquePtr<FPropertyRow> NewRow = CreatePropertyRow(ElementPropHandle, this, Callbacks);
            NewRow->SetIsArrayElement(true);
            
            Children.emplace_back(Move(NewRow));
        }
    }

    float FArrayPropertyRow::GetExtraControlsSectionWidth()
    {
        return 18 * 2 + 4;
    }

    void FArrayPropertyRow::DrawExtraControlsSection()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4));
        if (ImGuiX::FlatButton(LE_ICON_PLUS, ImVec2(18, 24), 428768833))
        {
            ArrayProperty->PushBack(PropertyHandle->ContainerPtr, nullptr);
            RebuildChildren();
        }
        ImGui::PopStyleVar();
        ImGuiX::TextTooltip("Add array element");

        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4));
        if (ImGuiX::FlatButton(LE_ICON_TRASH_CAN, ImVec2(18, 24), 428768833))
        {
            ArrayProperty->Clear(PropertyHandle->ContainerPtr);
            RebuildChildren();
            
        }
        ImGui::PopStyleVar();
        ImGuiX::TextTooltip("Remove all array elements");

    }

    FStructPropertyRow::FStructPropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& InCallbacks)
        : FPropertyRow(InPropHandle, InParentRow, InCallbacks)
        , StructProperty(static_cast<FStructProperty*>(InPropHandle->Property))
    {
        FPropertyCustomizationRegistry* Registry = GEngine->GetDevelopmentToolsUI()->GetPropertyCustomizationRegistry();
        Customization = Registry->GetPropertyCustomizationForType(StructProperty->GetStruct()->GetName());
        
        if (!Customization)
        {
            RebuildChildren();
        }
    }

    void FStructPropertyRow::Update()
    {
        switch (ChangeOp)
        {
        case EPropertyChangeOp::None:
            break;
        case EPropertyChangeOp::Updated:
            {
                if (Callbacks.PreChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PreChangeCallback(Move(Event));
                }
                
                Customization->UpdatePropertyValue(PropertyHandle);

                if (Callbacks.PostChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PostChangeCallback(Move(Event));
                }
            }
            break;
        case EPropertyChangeOp::Started:
            {
                if (Callbacks.StartChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.StartChangeCallback(Move(Event));
                }
             
                if (Callbacks.PreChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PreChangeCallback(Move(Event));
                }
                
                Customization->UpdatePropertyValue(PropertyHandle);

                if (Callbacks.PostChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PostChangeCallback(Move(Event));
                }
            }
            break;
        case EPropertyChangeOp::Finished:
            {
                if (Callbacks.PreChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PreChangeCallback(Move(Event));
                }
                
                Customization->UpdatePropertyValue(PropertyHandle);

                if (Callbacks.PostChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.PostChangeCallback(Move(Event));
                }
                
                if (Callbacks.FinishChangeCallback)
                {
                    FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};
                    Callbacks.FinishChangeCallback(Move(Event));
                }
            }
            break;
        }

        ChangeOp = EPropertyChangeOp::None;
    }

    void FStructPropertyRow::DrawHeader(float Offset)
    {
        ImGui::Dummy(ImVec2(Offset, 0));
        ImGui::SameLine();

        ImGui::SetNextItemOpen(bExpanded);
        ImGui::PushStyleColor(ImGuiCol_Header, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
        bExpanded = ImGui::CollapsingHeader(StructProperty->GetPropertyDisplayName().c_str(), ImGuiTreeNodeFlags_Leaf);
        
        const FString* Tooltip = StructProperty->TryGetMetadata("ToolTip");
        if (Tooltip == nullptr)
        {
            Tooltip = &StructProperty->GetPropertyDisplayName();
        }
        
        ImGuiX::TextTooltip("{}", *Tooltip);
        
        ImGui::PopStyleColor(3);
    }

    void FStructPropertyRow::DrawEditor(bool bReadOnly)
    {
        ImGui::BeginDisabled(IsReadOnly());
        
        if (bExpanded)
        {
            if (Customization)
            {
                ChangeOp = Customization->UpdateAndDraw(PropertyHandle, bReadOnly);    
            }
            else if (PropertyTable)
            {
                PropertyTable->DrawTree(bReadOnly);
            }
        }
        
        ImGui::EndDisabled();
    }

    void FStructPropertyRow::RebuildChildren()
    {
        PropertyTable = MakeUnique<FPropertyTable>(PropertyHandle->Property->GetValuePtr<void>(PropertyHandle->ContainerPtr), StructProperty->GetStruct());
        PropertyTable->RebuildTree();
    }

    FCategoryPropertyRow::FCategoryPropertyRow(void* InObj, const FName& InCategory, const FPropertyChangedEventCallbacks& InCallbacks)
        : FPropertyRow(InCallbacks)
        , Category(InCategory)
    {
        OwnerObject = InObj;
    }

    void FCategoryPropertyRow::AddProperty(const TSharedPtr<FPropertyHandle>& InPropHandle)
    {
        TUniquePtr<FPropertyRow> NewRow = CreatePropertyRow(InPropHandle, this, Callbacks);
        Children.emplace_back(Move(NewRow));
    }

    void FCategoryPropertyRow::DrawHeader(float Offset)
    {
        ImGui::Dummy(ImVec2(Offset, 0));
        ImGui::SameLine();

        ImGui::SetNextItemOpen(bExpanded);
        ImGui::PushStyleColor(ImGuiCol_Header, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
        bExpanded = ImGui::CollapsingHeader(Category.c_str());
        ImGui::PopStyleColor(3);

        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, 0xFF1C1C1C);
    }

    FPropertyTable::FPropertyTable(void* InObject, CStruct* InType)
        : ChangeEventCallbacks()
        , Struct(InType)
        , Object(InObject)
    {
        ChangeEventCallbacks.Type = InType;
    }

    void FPropertyTable::RebuildTree()
    {
        CategoryMap.clear();

        if (Struct == nullptr || Object == nullptr)
        {
            return;
        }
        
        FProperty* Current = Struct->LinkedProperty;
        while (Current != nullptr)
        {
            if (Current->IsVisible())
            {
                FName Category = "General";
                if (Current->Metadata.HasMetadata("Category"))
                {
                    Category = Current->Metadata.GetMetadata("Category");
                }
                
                FCategoryPropertyRow* CategoryRow = FindOrCreateCategoryRow(Category);

                TSharedPtr<FPropertyHandle> Property = MakeShared<FPropertyHandle>(Object, Current);
                CategoryRow->AddProperty(Property);
            }
            
            Current = static_cast<FProperty*>(Current->Next);
        }
    }

    void FPropertyTable::MarkDirty()
    {
        bDirty = true;
    }

    void FPropertyTable::DrawTree(bool bReadOnly)
    {
        if (bDirty)
        {
            FPropertyCustomizationRegistry* Registry = GEngine->GetDevelopmentToolsUI()->GetPropertyCustomizationRegistry();
            Customization = Registry->GetPropertyCustomizationForType(Struct->GetName());
            
            if (Customization == nullptr)
            {
                RebuildTree();
            }
            bDirty = false;
        }
        
        if (Customization)
        {
            if (PropertyHandle == nullptr)
            {
                PropertyHandle = MakeShared<FPropertyHandle>(Object, nullptr);
            }
            
            EPropertyChangeOp ChangeOp = Customization->UpdateAndDraw(PropertyHandle, bReadOnly);
            if (ChangeOp == EPropertyChangeOp::Updated)
            {
                Customization->UpdatePropertyValue(PropertyHandle);
            }
            
            bDirty = false;
            return;
        }
        
        
        constexpr ImGuiTableFlags Flags = 
            ImGuiTableFlags_BordersOuter | 
            ImGuiTableFlags_NoBordersInBodyUntilResize | 
            ImGuiTableFlags_SizingStretchSame;
        
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 2));
        ImGui::PushID(this);
        
        if (ImGui::BeginTable("GridTable", 2, Flags))
        {
            ImGui::TableSetupColumn("##Header", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn("##Editor", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            
            for (auto& [Name, Row] : CategoryMap)
            {
                Row->UpdateRow();
                Row->DrawRow(0.0f, bReadOnly);
            }

            ImGui::EndTable();
        }

        ImGui::PopStyleVar();
        ImGui::PopID();
    }

    void FPropertyTable::SetObject(void* InObject, CStruct* StructType)
    {
        Object = InObject;
        Struct = StructType;

        RebuildTree();
    }

    void FPropertyTable::SetPreEditCallback(const FPropertyChangedEventFn& Callback)
    {
        ChangeEventCallbacks.PreChangeCallback = Callback;
    }

    void FPropertyTable::SetPostEditCallback(const FPropertyChangedEventFn& Callback)
    {
        ChangeEventCallbacks.PostChangeCallback = Callback;
    }

    void FPropertyTable::SetStartEditCallback(const FPropertyChangedEventFn& Callback)
    {
        ChangeEventCallbacks.StartChangeCallback = Callback;
    }

    void FPropertyTable::SetFinishEditCallback(const FPropertyChangedEventFn& Callback)
    {
        ChangeEventCallbacks.FinishChangeCallback = Callback;
    }

    FCategoryPropertyRow* FPropertyTable::FindOrCreateCategoryRow(const FName& CategoryName)
    {
        if (CategoryMap.find(CategoryName) == CategoryMap.end())
        {
            auto NewRow = MakeUnique<FCategoryPropertyRow>(Object, CategoryName, ChangeEventCallbacks);
            CategoryMap.emplace(CategoryName, Move(NewRow));
        }
        
        return CategoryMap[CategoryName].get();
    }
}
