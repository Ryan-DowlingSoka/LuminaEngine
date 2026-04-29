#include "PropertyTable.h"

#include "Core/Engine/Engine.h"
#include "Core/Object/Class.h"
#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Core/Reflection/Type/Properties/ArrayProperty.h"
#include "Core/Reflection/Type/Properties/OptionalProperty.h"
#include "Core/Reflection/Type/Properties/StructProperty.h"
#include "Customizations/CoreTypeCustomization.h"
#include "Tools/UI/DevelopmentToolUI.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{
    static constexpr int        ComplexArrayDisplayLimit = 32;
    static constexpr float      ChildIndentStep = 8.0f;
    static constexpr uint32     ArrayControlSeed = 428768833;

    static bool IsFixedHeightPropertyType(EPropertyTypeFlags Type)
    {
        switch (Type)
        {
        case EPropertyTypeFlags::Struct:
        case EPropertyTypeFlags::Vector:
        case EPropertyTypeFlags::Optional:
            return false;
        default:
            return true;
        }
    }

    static TUniquePtr<FPropertyRow> CreatePropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& InCallbacks)
    {
        switch (InPropHandle->Property->GetType())
        {
        case EPropertyTypeFlags::Vector:
            return MakeUnique<FArrayPropertyRow>(InPropHandle, InParentRow, InCallbacks);
        case EPropertyTypeFlags::Struct:
            return MakeUnique<FStructPropertyRow>(InPropHandle, InParentRow, InCallbacks);
        case EPropertyTypeFlags::Optional:
            return MakeUnique<FOptionalPropertyRow>(InPropHandle, InParentRow, InCallbacks);
        default:
            return MakeUnique<FPropertyPropertyRow>(InPropHandle, InParentRow, InCallbacks);
        }
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
        Update();

        // Children can only accumulate a ChangeOp if they were drawn last frame,
        // which only happens when we are expanded. Skipping collapsed subtrees
        // keeps property grids with huge arrays cheap.
        if (bExpanded)
        {
            for (const TUniquePtr<FPropertyRow>& Child : Children)
            {
                Child->UpdateRow();
            }
        }
    }

    float FPropertyRow::ComputeRequiredHeaderWidth(float Offset) const
    {
        float Width = Offset + GetMeasuredHeaderTextWidth();
        if (bExpanded)
        {
            for (const TUniquePtr<FPropertyRow>& Child : Children)
            {
                Width = std::max(Width, Child->ComputeRequiredHeaderWidth(Offset + ChildIndentStep));
            }
        }
        return Width;
    }

    void FPropertyRow::DispatchChange(EPropertyChangeOp Op)
    {
        if (Op == EPropertyChangeOp::None || PropertyHandle == nullptr || PropertyHandle->Property == nullptr)
        {
            return;
        }

        const FPropertyChangedEvent Event{Callbacks.Type, PropertyHandle->Property, PropertyHandle->Property->Name};

        if (Op == EPropertyChangeOp::Started && Callbacks.StartChangeCallback)
        {
            Callbacks.StartChangeCallback(Event);
        }

        if (Callbacks.PreChangeCallback)
        {
            Callbacks.PreChangeCallback(Event);
        }

        if (Customization)
        {
            Customization->UpdatePropertyValue(PropertyHandle);
        }

        if (Callbacks.PostChangeCallback)
        {
            Callbacks.PostChangeCallback(Event);
        }

        if (Op == EPropertyChangeOp::Finished && Callbacks.FinishChangeCallback)
        {
            Callbacks.FinishChangeCallback(Event);
        }
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
            const bool bHasExtras = HasExtraControls();
            const int ColumnCount = bHasExtras ? 3 : 2;

            constexpr ImGuiTableFlags Flags = ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit;
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2, 0));
            if (ImGui::BeginTable("EditorRow", ColumnCount, Flags))
            {
                ImGui::TableSetupColumn("##Editor", ImGuiTableColumnFlags_WidthStretch);
                if (bHasExtras)
                {
                    ImGui::TableSetupColumn("##Extra", ImGuiTableColumnFlags_WidthFixed, GetExtraControlsSectionWidth());
                }
                ImGui::TableSetupColumn("##Reset", ImGuiTableColumnFlags_WidthFixed, 24);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                DrawEditor(bReadOnly);

                if (bHasExtras)
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

        if (bExpanded && !Children.empty())
        {
            ImGui::BeginDisabled(IsReadOnly());
            DrawChildren(Offset + ChildIndentStep, bReadOnly);
            ImGui::EndDisabled();
        }
    }

    void FPropertyRow::DrawChildren(float ChildOffset, bool bReadOnly)
    {
        for (const TUniquePtr<FPropertyRow>& Row : Children)
        {
            Row->DrawRow(ChildOffset, bReadOnly);
        }
    }

    bool FPropertyRow::IsReadOnly() const
    {
        return PropertyHandle == nullptr ? false : PropertyHandle->Property->IsReadOnly();
    }

    static void DrawPropertyTooltip(const FProperty* Property)
    {
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            return;
        }

        const FString* Tooltip = Property->TryGetMetadata("ToolTip");
        if (Tooltip == nullptr)
        {
            Tooltip = &Property->GetPropertyDisplayName();
        }
        ImGuiX::TextTooltip_Internal(*Tooltip);
    }

    FPropertyPropertyRow::FPropertyPropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& InCallbacks)
        : FPropertyRow(InPropHandle, InParentRow, InCallbacks)
    {
        switch (PropertyHandle->Property->GetType())
        {
        case EPropertyTypeFlags::Int8:
            Customization = FNumericPropertyCustomization<int8, ImGuiDataType_S8>::MakeInstance();
            break;
        case EPropertyTypeFlags::Int16:
            Customization = FNumericPropertyCustomization<int16, ImGuiDataType_S16>::MakeInstance();
            break;
        case EPropertyTypeFlags::Int32:
            Customization = FNumericPropertyCustomization<int32, ImGuiDataType_S32>::MakeInstance();
            break;
        case EPropertyTypeFlags::Int64:
            Customization = FNumericPropertyCustomization<int64, ImGuiDataType_S64>::MakeInstance();
            break;
        case EPropertyTypeFlags::UInt8:
            Customization = FNumericPropertyCustomization<uint8, ImGuiDataType_U8>::MakeInstance();
            break;
        case EPropertyTypeFlags::UInt16:
            Customization = FNumericPropertyCustomization<uint16, ImGuiDataType_U16>::MakeInstance();
            break;
        case EPropertyTypeFlags::UInt32:
            Customization = FNumericPropertyCustomization<uint32, ImGuiDataType_U32>::MakeInstance();
            break;
        case EPropertyTypeFlags::UInt64:
            Customization = FNumericPropertyCustomization<uint64, ImGuiDataType_U64>::MakeInstance();
            break;
        case EPropertyTypeFlags::Float:
            Customization = FNumericPropertyCustomization<float, ImGuiDataType_Float>::MakeInstance();
            break;
        case EPropertyTypeFlags::Double:
            Customization = FNumericPropertyCustomization<double, ImGuiDataType_Double>::MakeInstance();
            break;
        case EPropertyTypeFlags::Bool:
            Customization = FBoolPropertyCustomization::MakeInstance();
            break;
        case EPropertyTypeFlags::Object:
            Customization = FCObjectPropertyCustomization::MakeInstance();
            break;
        case EPropertyTypeFlags::Name:
            Customization = FNamePropertyCustomization::MakeInstance();
            break;
        case EPropertyTypeFlags::String:
            Customization = FStringPropertyCustomization::MakeInstance();
            break;
        case EPropertyTypeFlags::Enum:
            Customization = FEnumPropertyCustomization::MakeInstance();
            break;
        default:
            break;
        }
    }

    void FPropertyPropertyRow::Update()
    {
        DispatchChange(ChangeOp);
        ChangeOp = EPropertyChangeOp::None;
    }

    void FPropertyPropertyRow::DrawHeader(float Offset)
    {
        ImGui::Dummy(ImVec2(Offset, 0));
        ImGui::SameLine();

        if (IsArrayElementProperty())
        {
            ImGui::Text("%lld", static_cast<long long>(PropertyHandle->Index));
        }
        else
        {
            ImGui::TextUnformatted(PropertyHandle->Property->GetPropertyDisplayName().c_str());
        }

        DrawPropertyTooltip(PropertyHandle->Property);
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
            ImGui::TextUnformatted(LE_ICON_EXCLAMATION "Missing Property Customization");
        }

        ImGui::EndDisabled();
    }

    float FPropertyPropertyRow::GetMeasuredHeaderTextWidth() const
    {
        if (IsArrayElementProperty())
        {
            char Buf[32];
            snprintf(Buf, sizeof(Buf), "%lld", static_cast<long long>(PropertyHandle->Index));
            return ImGui::CalcTextSize(Buf).x;
        }
        return ImGui::CalcTextSize(PropertyHandle->Property->GetPropertyDisplayName().c_str()).x;
    }

    bool FPropertyPropertyRow::HasExtraControls() const
    {
        return bArrayElement;
    }

    float FPropertyPropertyRow::GetExtraControlsSectionWidth()
    {
        return bArrayElement ? 22.0f : 0.0f;
    }

    void FPropertyPropertyRow::DrawExtraControlsSection()
    {
        FArrayPropertyRow* ArrayRow = static_cast<FArrayPropertyRow*>(ParentRow);
        FArrayProperty* ArrayProperty = ArrayRow->ArrayProperty;
        void* ContainerPtr = ArrayRow->GetPropertyHandle()->ContainerPtr;
        const size_t ArrayNum = ArrayProperty->GetNum(ContainerPtr);
        const int64 Index = PropertyHandle->Index;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4));
        ImGuiX::FlatButton(LE_ICON_DOTS_HORIZONTAL, ImVec2(18, 24), ArrayControlSeed);
        ImGui::PopStyleVar();

        if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft))
        {
            if (ImGui::MenuItem(LE_ICON_PLUS " Insert New Element"))
            {
                ArrayProperty->PushBack(ContainerPtr, nullptr);
                ArrayRow->RebuildChildren();
            }

            if (Index > 0 && ImGui::MenuItem(LE_ICON_ARROW_UP " Move Element Up"))
            {
                ArrayProperty->Swap(ContainerPtr, Index, Index - 1);
                ArrayRow->RebuildChildren();
            }

            if (ArrayNum > 0 && std::cmp_less(Index, ArrayNum - 1) && ImGui::MenuItem(LE_ICON_ARROW_DOWN " Move Element Down"))
            {
                ArrayProperty->Swap(ContainerPtr, Index, Index + 1);
                ArrayRow->RebuildChildren();
            }

            if (ImGui::MenuItem(LE_ICON_TRASH_CAN " Remove Element"))
            {
                ArrayProperty->RemoveAt(ContainerPtr, Index);
                ArrayRow->RebuildChildren();
            }

            ImGui::EndPopup();
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGuiX::TextTooltip_Internal("Array Element Options");
        }
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

        const size_t ElementCount = ArrayProperty->GetNum(GetPropertyHandle()->ContainerPtr);

        ImGui::SetNextItemOpen(bExpanded);
        ImGui::PushStyleColor(ImGuiCol_Header, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
        const ImGuiTreeNodeFlags Flags = ElementCount ? 0 : ImGuiTreeNodeFlags_Leaf;
        bExpanded = ImGui::CollapsingHeader(ArrayProperty->GetPropertyDisplayName().c_str(), Flags);
        ImGui::PopStyleColor(3);

        DrawPropertyTooltip(ArrayProperty);
    }

    void FArrayPropertyRow::DrawEditor(bool bReadOnly)
    {
        const size_t ElementCount = ArrayProperty->GetNum(GetPropertyHandle()->ContainerPtr);
        ImGui::TextColored(ImVec4(0.24f, 0.24f, 0.24f, 1.0f), "%llu Elements", static_cast<unsigned long long>(ElementCount));
    }

    float FArrayPropertyRow::GetMeasuredHeaderTextWidth() const
    {
        return ImGui::GetTreeNodeToLabelSpacing() + ImGui::CalcTextSize(ArrayProperty->GetPropertyDisplayName().c_str()).x;
    }

    bool FArrayPropertyRow::IsInnerFixedHeight() const
    {
        if (ArrayProperty == nullptr)
        {
            return false;
        }

        FProperty* Inner = ArrayProperty->GetInternalProperty();
        if (Inner == nullptr)
        {
            return false;
        }

        return IsFixedHeightPropertyType(Inner->GetType());
    }

    void FArrayPropertyRow::DrawChildren(float ChildOffset, bool bReadOnly)
    {
        const int ChildCount = static_cast<int>(Children.size());
        if (ChildCount == 0)
        {
            return;
        }

        if (IsInnerFixedHeight())
        {
            // All elements render as a single fixed-height table row, so the clipper
            // can skip offscreen rows entirely. Critical for arrays with thousands of entries.
            ImGuiListClipper Clipper;
            Clipper.Begin(ChildCount);
            while (Clipper.Step())
            {
                for (int i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i)
                {
                    Children[i]->DrawRow(ChildOffset, bReadOnly);
                }
            }
            return;
        }

        // Complex element types (struct/nested array) have variable height due to
        // independent expansion state, so the clipper can't be used.
        const int DisplayCount = bShowAllElements ? ChildCount : std::min(ChildCount, ComplexArrayDisplayLimit);
        for (int i = 0; i < DisplayCount; ++i)
        {
            Children[i]->DrawRow(ChildOffset, bReadOnly);
        }

        if (DisplayCount < ChildCount)
        {
            DrawTruncationRow(ChildOffset, ChildCount - DisplayCount);
        }
    }

    void FArrayPropertyRow::DrawTruncationRow(float Offset, int HiddenCount)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Dummy(ImVec2(Offset, 0));
        ImGui::SameLine();
        ImGui::TextDisabled("%d more element%s hidden", HiddenCount, HiddenCount == 1 ? "" : "s");

        ImGui::TableNextColumn();
        if (ImGui::SmallButton("Show All"))
        {
            bShowAllElements = true;
        }
    }

    void FArrayPropertyRow::RebuildChildren()
    {
        DestroyChildren();

        void* ContainerPtr = GetPropertyHandle()->ContainerPtr;
        const size_t ElementCount = ArrayProperty->GetNum(ContainerPtr);

        // Reset the truncation override whenever the array shape changes, so a
        // resize or clear does not leave a stale "show all" flag in place.
        bShowAllElements = false;

        Children.reserve(ElementCount);
        FProperty* InnerProperty = ArrayProperty->GetInternalProperty();
        for (size_t i = 0; i < ElementCount; ++i)
        {
            TSharedPtr<FPropertyHandle> ElementPropHandle = MakeShared<FPropertyHandle>(ArrayProperty->GetAt(ContainerPtr, i), InnerProperty, static_cast<int64>(i));
            TUniquePtr<FPropertyRow> NewRow = CreatePropertyRow(ElementPropHandle, this, Callbacks);
            NewRow->SetIsArrayElement(true);
            Children.push_back(Move(NewRow));
        }
    }

    float FArrayPropertyRow::GetExtraControlsSectionWidth()
    {
        return 18 * 2 + 4;
    }

    void FArrayPropertyRow::DrawExtraControlsSection()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4));
        if (ImGuiX::FlatButton(LE_ICON_PLUS, ImVec2(18, 24), ArrayControlSeed))
        {
            ArrayProperty->PushBack(PropertyHandle->ContainerPtr, nullptr);
            RebuildChildren();
        }
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGuiX::TextTooltip_Internal("Add array element");
        }

        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4));
        if (ImGuiX::FlatButton(LE_ICON_TRASH_CAN, ImVec2(18, 24), ArrayControlSeed))
        {
            ArrayProperty->Clear(PropertyHandle->ContainerPtr);
            RebuildChildren();
        }
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGuiX::TextTooltip_Internal("Remove all array elements");
        }
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
        DispatchChange(ChangeOp);
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
        ImGui::PopStyleColor(3);

        DrawPropertyTooltip(StructProperty);
    }

    void FStructPropertyRow::DrawEditor(bool bReadOnly)
    {
        if (!bExpanded)
        {
            return;
        }

        ImGui::BeginDisabled(IsReadOnly());

        if (Customization)
        {
            ChangeOp = Customization->UpdateAndDraw(PropertyHandle, bReadOnly);
        }
        else if (PropertyTable)
        {
            PropertyTable->DrawTree(bReadOnly);
        }

        ImGui::EndDisabled();
    }

    float FStructPropertyRow::GetMeasuredHeaderTextWidth() const
    {
        return ImGui::GetTreeNodeToLabelSpacing() + ImGui::CalcTextSize(StructProperty->GetPropertyDisplayName().c_str()).x;
    }

    void FStructPropertyRow::RebuildChildren()
    {
        PropertyTable = MakeUnique<FPropertyTable>(PropertyHandle->Property->GetValuePtr<void>(PropertyHandle->ContainerPtr), StructProperty->GetStruct());
        PropertyTable->RebuildTree();
    }

    FOptionalPropertyRow::FOptionalPropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& InCallbacks)
        : FPropertyRow(InPropHandle, InParentRow, InCallbacks)
        , OptionalProperty(static_cast<FOptionalProperty*>(InPropHandle->Property))
    {
        bWasEngaged = OptionalProperty->HasValue(GetPropertyHandle()->ContainerPtr);
        RebuildChildren();
    }

    void FOptionalPropertyRow::Update()
    {
        // Detect external mutations of the engaged state (e.g. gameplay code or
        // an undo) and rebuild the child tree to match before drawing.
        const bool bEngagedNow = OptionalProperty->HasValue(GetPropertyHandle()->ContainerPtr);
        if (bEngagedNow != bWasEngaged)
        {
            bWasEngaged = bEngagedNow;
            RebuildChildren();
        }

        DispatchChange(ChangeOp);
        ChangeOp = EPropertyChangeOp::None;
    }

    void FOptionalPropertyRow::DrawHeader(float Offset)
    {
        ImGui::Dummy(ImVec2(Offset, 0));
        ImGui::SameLine();
        ImGui::TextUnformatted(OptionalProperty->GetPropertyDisplayName().c_str());
        DrawPropertyTooltip(OptionalProperty);
    }

    void FOptionalPropertyRow::DrawEditor(bool bReadOnly)
    {
        void* ContainerPtr = GetPropertyHandle()->ContainerPtr;
        bool bEngaged = OptionalProperty->HasValue(ContainerPtr);

        ImGui::BeginDisabled(bReadOnly || IsReadOnly());
        if (ImGui::Checkbox("##OptionalSet", &bEngaged))
        {
            // Toggling switches the engaged state; engaging default-constructs
            // the payload, disengaging discards it. Children are rebuilt next
            // frame in Update() (or right now to keep the UI in sync).
            if (bEngaged)
            {
                OptionalProperty->SetValue(ContainerPtr, nullptr);
            }
            else
            {
                OptionalProperty->Reset(ContainerPtr);
            }

            bWasEngaged = bEngaged;
            RebuildChildren();
            ChangeOp = EPropertyChangeOp::Updated;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (bEngaged)
        {
            ImGui::TextDisabled("set");
        }
        else
        {
            ImGui::TextDisabled("(none)");
        }
    }

    float FOptionalPropertyRow::GetMeasuredHeaderTextWidth() const
    {
        return ImGui::CalcTextSize(OptionalProperty->GetPropertyDisplayName().c_str()).x;
    }

    void FOptionalPropertyRow::RebuildChildren()
    {
        DestroyChildren();

        void* ContainerPtr = GetPropertyHandle()->ContainerPtr;
        if (!OptionalProperty->HasValue(ContainerPtr))
        {
            return;
        }

        FProperty* Inner = OptionalProperty->GetInternalProperty();
        if (Inner == nullptr)
        {
            return;
        }

        // The optional owns the storage for T; pass &T directly as the child's
        // container pointer so the inner row's customization edits in-place.
        TSharedPtr<FPropertyHandle> InnerHandle = MakeShared<FPropertyHandle>(OptionalProperty->GetValue(ContainerPtr), Inner);
        Children.push_back(CreatePropertyRow(InnerHandle, this, Callbacks));
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

    FCategoryPropertyRow* FCategoryPropertyRow::FindOrCreateChildCategory(const FName& InCategory)
    {
        // First look for an already-created sub-category row so multiple
        // properties sharing the same `Outer|Inner` prefix coalesce into a
        // single child row. We walk Children directly because their order
        // is the draw order and we want a stable layout.
        for (const TUniquePtr<FPropertyRow>& Child : Children)
        {
            if (Child->IsCategory())
            {
                FCategoryPropertyRow* AsCategory = static_cast<FCategoryPropertyRow*>(Child.get());
                if (AsCategory->GetCategoryName() == InCategory)
                {
                    return AsCategory;
                }
            }
        }

        TUniquePtr<FCategoryPropertyRow> NewRow = MakeUnique<FCategoryPropertyRow>(OwnerObject, InCategory, Callbacks);
        FCategoryPropertyRow* RawPtr = NewRow.get();
        Children.emplace_back(Move(NewRow));
        return RawPtr;
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

    float FCategoryPropertyRow::GetMeasuredHeaderTextWidth() const
    {
        return ImGui::GetTreeNodeToLabelSpacing() + ImGui::CalcTextSize(Category.c_str()).x;
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
                FString CategoryPath = "General";
                if (Current->Metadata.HasMetadata("Category"))
                {
                    CategoryPath = Current->Metadata.GetMetadata("Category");
                }

                // Split the metadata path on '|' so `Foo|Bar|Baz` walks
                // through Foo -> Bar -> Baz as nested category rows. The
                // leaf row owns the property; intermediate rows are reused
                // across properties that share the prefix so we don't
                // duplicate headers.
                FCategoryPropertyRow* TargetRow = nullptr;
                size_t SegmentStart = 0;
                while (SegmentStart <= CategoryPath.length())
                {
                    size_t Sep = CategoryPath.find('|', SegmentStart);
                    if (Sep == FString::npos)
                    {
                        Sep = CategoryPath.length();
                    }

                    if (Sep > SegmentStart)
                    {
                        FName SegmentName(CategoryPath.data() + SegmentStart, Sep - SegmentStart);
                        TargetRow = (TargetRow == nullptr)
                            ? FindOrCreateCategoryRow(SegmentName)
                            : TargetRow->FindOrCreateChildCategory(SegmentName);
                    }

                    if (Sep == CategoryPath.length())
                    {
                        break;
                    }
                    SegmentStart = Sep + 1;
                }

                // Empty / pathological "|" or "||" -- fall back to General so
                // the property still has a home.
                if (TargetRow == nullptr)
                {
                    TargetRow = FindOrCreateCategoryRow(FName("General"));
                }

                TSharedPtr<FPropertyHandle> Property = MakeShared<FPropertyHandle>(Object, Current);
                TargetRow->AddProperty(Property);
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

            const EPropertyChangeOp ChangeOp = Customization->UpdateAndDraw(PropertyHandle, bReadOnly);
            if (ChangeOp == EPropertyChangeOp::Updated)
            {
                Customization->UpdatePropertyValue(PropertyHandle);
            }
            return;
        }

        constexpr ImGuiTableFlags Flags =
            ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_NoBordersInBodyUntilResize |
            ImGuiTableFlags_SizingStretchSame;

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 2));
        ImGui::PushID(this);

        // Size the header column to fit the widest visible label so short
        // property names don't waste space and force the editor cell to be
        // cramped. Recomputed every frame so changes in expansion state
        // (collapsing a category, opening a struct) tighten or widen as needed.
        float HeaderColumnWidth = 0.0f;
        for (auto& [Name, Row] : CategoryMap)
        {
            HeaderColumnWidth = std::max(HeaderColumnWidth, Row->ComputeRequiredHeaderWidth(0.0f));
        }
        HeaderColumnWidth += ImGui::GetStyle().ItemSpacing.x * 4.0f + 12.0f;

        if (ImGui::BeginTable("GridTable", 2, Flags))
        {
            ImGui::TableSetupColumn("##Header", ImGuiTableColumnFlags_WidthFixed, HeaderColumnWidth);
            ImGui::TableSetupColumn("##Editor", ImGuiTableColumnFlags_WidthStretch);

            for (auto& [Name, Row] : CategoryMap)
            {
                Row->UpdateRow();
                Row->DrawRow(0.0f, bReadOnly);
            }

            ImGui::EndTable();
        }

        ImGui::PopID();
        ImGui::PopStyleVar();
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
        auto It = CategoryMap.find(CategoryName);
        if (It == CategoryMap.end())
        {
            auto NewRow = MakeUnique<FCategoryPropertyRow>(Object, CategoryName, ChangeEventCallbacks);
            It = CategoryMap.emplace(CategoryName, Move(NewRow)).first;
        }
        return It->second.get();
    }
}
