#include "ScriptComponentCustomization.h"
#include "imgui.h"
#include "World/World.h"
#include "Platform/Process/PlatformProcess.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/ScriptExports.h"
#include "Scripting/Lua/LuaTypes.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetRegistry/TextAssetTypes.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectCore.h"
#include "Tools/UI/ImGui/ImGuiDragDrop.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "UI/EditorUI.h"
#include "UI/Tools/ContentBrowserEditorTool.h"
#include "LuminaEditor.h"

namespace Lumina
{
    namespace
    {
        // Emits one or more rows (assumes an active 2-column table) for a value and its children,
        // meta-driven. bOutRemove, when non-null, draws a remove button for array elements.
        void DrawValueRows(const Lua::FScriptExportType& Type, Lua::FScriptPropertyValue& Value,
                           const char* Label, const Lua::FScriptExportMeta& Meta, bool& bChanged, bool* bOutRemove = nullptr);

        // Format a Lua value for the debug Reference table without invoking __tostring,
        // which can crash if a userdata's backing C++ object has been destroyed.
        FString SafeRefDisplay(const Lua::FRef& Ref)
        {
            if (!Ref.IsValid())
            {
                return FString("<invalid>");
            }
            switch (Ref.GetType())
            {
            case Lua::EType::Nil:           return FString("nil");
            case Lua::EType::Boolean:
            case Lua::EType::Number:
            case Lua::EType::Vector:
            case Lua::EType::String:        return Ref.ToString();
            case Lua::EType::Table:         return FString("<table>");
            case Lua::EType::Function:      return FString("<function>");
            case Lua::EType::Userdata:      return FString("<userdata>");
            case Lua::EType::LightUserData: return FString("<lightuserdata>");
            case Lua::EType::Thread:        return FString("<thread>");
            case Lua::EType::Buffer:        return FString("<buffer>");
            default:                        return FString("<unknown>");
            }
        }

        // A printf format carrying the field's Units suffix (escaped), or empty for the ImGui default.
        FString BuildScriptUnitFormat(const Lua::FScriptExportMeta& Meta, const char* Base)
        {
            const FString* Units = Meta.Find("Units");
            if (!Units || Units->empty())
            {
                return FString();
            }
            FString Out(Base);
            Out.push_back(' ');
            for (char C : *Units)
            {
                if (C == '%') Out.push_back('%');   // escape so a "%" unit can't corrupt the format
                Out.push_back(C);
            }
            return Out;
        }

        // Maps --@export meta flags onto the ImGuiX slider style. The value is always shown over the
        // track; Gradient and Glow are opt-in flair (e.g. --@export(Slider, Gradient, Glow)).
        ImGuiX::ESliderFlags ScriptSliderFlags(const Lua::FScriptExportMeta& Meta)
        {
            ImGuiX::ESliderFlags Flags = ImGuiX::ESliderFlags::AlwaysValue;
            if (Meta.Has("Gradient") || Meta.Has("FillGradient")) Flags |= ImGuiX::ESliderFlags::FillGradient;
            if (Meta.Has("Glow")) Flags |= ImGuiX::ESliderFlags::Glow;
            return Flags;
        }

        // A searchable asset picker for a string value carrying --@export(AssetType="..."). The
        // string holds the chosen asset's virtual path. AssetType resolves as a reflected class
        // first (e.g. "StaticMesh"/"CStaticMesh" -> binary .lasset assets of/under that class);
        // otherwise it's parsed as loose text-asset kinds (luau/rml/rcss). Index 0 clears.
        bool DrawAssetPicker(Lua::FScriptPropertyValue& Value, const char* HL, const FString& AssetType)
        {
            TVector<FFixedString> Paths;
            Paths.push_back(FFixedString("(None)"));

            FAssetRegistry& Registry = FAssetRegistry::Get();

            CClass* FilterClass = FindObject<CClass>(FName(AssetType.c_str()));
            if (FilterClass == nullptr)
            {
                FString Prefixed("C");
                Prefixed += AssetType;
                FilterClass = FindObject<CClass>(FName(Prefixed.c_str()));
            }

            if (FilterClass != nullptr)
            {
                TVector<FAssetData*> Assets = Registry.FindByPredicate([FilterClass](const FAssetData& Data)
                {
                    CClass* DataClass = FindObject<CClass>(Data.AssetClass);
                    return DataClass != nullptr && DataClass->IsChildOf(FilterClass);
                });
                for (const FAssetData* Data : Assets)
                {
                    Paths.push_back(Data->Path);
                }
            }
            else
            {
                for (ETextAssetKind Kind : TextAsset::ParseAssetTypeMeta(FStringView(AssetType.c_str(), AssetType.size())))
                {
                    for (const FTextAssetData* Data : Registry.GetTextAssetsOfKind(Kind))
                    {
                        Paths.push_back(Data->Path);
                    }
                }
            }

            int32 Current = 0; // 0 == (None)
            for (int32 i = 1; i < (int32)Paths.size(); ++i)
            {
                if (strcmp(Paths[i].c_str(), Value.AsString.c_str()) == 0) { Current = i; break; }
            }

            const char* Preview = Value.AsString.empty() ? "Select an asset..." : Value.AsString.c_str();
            const int32 Picked = ImGuiX::SearchableCombo(HL, Preview, (int32)Paths.size(), Current,
                [&Paths](int32 Index) { return Paths[Index]; });

            if (Picked != INDEX_NONE && Picked != Current)
            {
                if (Picked == 0)
                {
                    Value.AsString.clear();
                }
                else
                {
                    Value.AsString.assign(Paths[Picked].c_str(), Paths[Picked].size());
                }
                return true;
            }
            return false;
        }

        // The editable widget for a leaf value, drawn into the active value column. Meta drives
        // clamp (ClampMin/Max), drag speed (Delta), unit suffix (Units), Color picker, NoDrag input,
        // and AssetType (string -> asset picker).
        bool DrawScalarWidget(const Lua::FScriptExportType& Type, Lua::FScriptPropertyValue& Value,
                              const char* HL, const Lua::FScriptExportMeta& Meta)
        {
            using namespace Lua;

            double dMin = 0.0, dMax = 0.0, dDelta = 0.0;
            const bool  bHasMin = Meta.GetNumber("ClampMin", dMin);
            const bool  bHasMax = Meta.GetNumber("ClampMax", dMax);
            const bool  bNoDrag = Meta.Has("NoDrag");
            const float Delta   = Meta.GetNumber("Delta", dDelta) ? (float)dDelta : 0.0f;

            bool bChanged = false;
            switch (Type.Kind)
            {
            case EScriptExportKind::Bool:
                bChanged = ImGui::Checkbox(HL, &Value.AsBool);
                break;
            case EScriptExportKind::Int:
            {
                int Tmp = (int)Value.AsInt;
                int Min = (int)dMin, Max = (int)dMax;
                // Slider needs a finite range; fall back to drag/input if either clamp is missing.
                if (Meta.Has("Slider") && bHasMin && bHasMax)
                {
                    if (ImGuiX::SliderInt(HL, &Tmp, Min, Max, ScriptSliderFlags(Meta)))
                    {
                        Value.AsInt = ImClamp(Tmp, Min, Max);
                        bChanged = true;
                    }
                    break;
                }
                bool bEdited;
                if (bNoDrag)
                {
                    int Step = (Delta > 0.0f) ? (int)Delta : 1;
                    bEdited = ImGui::InputScalar(HL, ImGuiDataType_S32, &Tmp, &Step, nullptr, nullptr);
                }
                else
                {
                    bEdited = ImGui::DragScalar(HL, ImGuiDataType_S32, &Tmp, Delta > 0.0f ? Delta : 1.0f,
                                                bHasMin ? &Min : nullptr, bHasMax ? &Max : nullptr);
                }
                if (bEdited)
                {
                    if (bHasMin && Tmp < Min) Tmp = Min;
                    if (bHasMax && Tmp > Max) Tmp = Max;
                    Value.AsInt = Tmp;
                    bChanged = true;
                }
                break;
            }
            case EScriptExportKind::Double:
            {
                const FString Fmt = BuildScriptUnitFormat(Meta, "%.3f");
                const char* Format = Fmt.empty() ? "%.3f" : Fmt.c_str();
                if (Meta.Has("Slider") && bHasMin && bHasMax)
                {
                    float Tmp = (float)Value.AsDouble;
                    if (ImGuiX::SliderFloat(HL, &Tmp, (float)dMin, (float)dMax, ScriptSliderFlags(Meta), Format))
                    {
                        Value.AsDouble = ImClamp((double)Tmp, dMin, dMax);
                        bChanged = true;
                    }
                    break;
                }
                bool bEdited;
                if (bNoDrag)
                {
                    bEdited = ImGui::InputScalar(HL, ImGuiDataType_Double, &Value.AsDouble, nullptr, nullptr, Format);
                }
                else
                {
                    bEdited = ImGui::DragScalar(HL, ImGuiDataType_Double, &Value.AsDouble, Delta > 0.0f ? Delta : 0.1f,
                                                bHasMin ? &dMin : nullptr, bHasMax ? &dMax : nullptr, Format);
                }
                if (bEdited)
                {
                    if (bHasMin && Value.AsDouble < dMin) Value.AsDouble = dMin;
                    if (bHasMax && Value.AsDouble > dMax) Value.AsDouble = dMax;
                    bChanged = true;
                }
                break;
            }
            case EScriptExportKind::String:
            {
                const FString* AssetType = Meta.Find("AssetType");
                if (AssetType && !AssetType->empty())
                {
                    bChanged = DrawAssetPicker(Value, HL, *AssetType);
                    break;
                }

                char Buffer[1024] = {0};
                const size_t Copy = Value.AsString.size() < sizeof(Buffer) - 1 ? Value.AsString.size() : sizeof(Buffer) - 1;
                memcpy(Buffer, Value.AsString.c_str(), Copy);
                if (ImGui::InputText(HL, Buffer, sizeof(Buffer)))
                {
                    Value.AsString = Buffer;
                    bChanged = true;
                }
                break;
            }
            case EScriptExportKind::Vec2:
            case EScriptExportKind::Vec3:
            case EScriptExportKind::Vec4:
            {
                const int Components = (Type.Kind == EScriptExportKind::Vec2) ? 2 : (Type.Kind == EScriptExportKind::Vec3) ? 3 : 4;
                if (Meta.Has("Color") && Components == 3)
                {
                    bChanged = ImGui::ColorEdit3(HL, &Value.AsVec.x);
                }
                else if (Meta.Has("Color") && Components == 4)
                {
                    bChanged = ImGui::ColorEdit4(HL, &Value.AsVec.x);
                }
                else
                {
                    float fMin = (float)dMin, fMax = (float)dMax;
                    const FString Fmt = BuildScriptUnitFormat(Meta, "%.3f");
                    bChanged = ImGui::DragScalarN(HL, ImGuiDataType_Float, &Value.AsVec.x, Components,
                                                  Delta > 0.0f ? Delta : 0.1f,
                                                  bHasMin ? &fMin : nullptr, bHasMax ? &fMax : nullptr,
                                                  Fmt.empty() ? "%.3f" : Fmt.c_str());
                }
                break;
            }
            default:
                break;
            }
            return bChanged;
        }

        void DrawScalarRow(const Lua::FScriptExportType& Type, Lua::FScriptPropertyValue& Value,
                           const char* Label, const Lua::FScriptExportMeta& Meta, bool& bChanged, bool* bOutRemove)
        {
            using namespace Lua;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Label);

            const FString* Tip = Meta.Find("Tooltip");
            if (Tip && !Tip->empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("%s", Tip->c_str());
            }

            ImGui::TableNextColumn();

            if (bOutRemove)
            {
                if (ImGui::SmallButton("x")) *bOutRemove = true;
                ImGui::SameLine();
            }

            if (Type.Kind == EScriptExportKind::UnknownUserdata)
            {
                ImGui::TextDisabled("(%s: not editable)", Value.UserdataTypeName.ToString().c_str());
                return;
            }

            ImGui::SetNextItemWidth(-FLT_MIN);

            const bool bReadOnly = Meta.Has("ReadOnly");
            if (bReadOnly) ImGui::BeginDisabled(true);

            FFixedString HiddenLabel;
            HiddenLabel.append("##").append(Label);
            if (DrawScalarWidget(Type, Value, HiddenLabel.c_str(), Meta))
            {
                bChanged = true;
            }

            if (bReadOnly) ImGui::EndDisabled();
        }

        void DrawArrayRows(const Lua::FScriptExportType& Type, Lua::FScriptPropertyValue& Value,
                           const char* Label, const Lua::FScriptExportMeta& Meta, bool& bChanged, bool* bOutRemove)
        {
            using namespace Lua;
            if (!Type.ElementType) return;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const bool bOpen = ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen, "%s", Label);

            ImGui::TableNextColumn();
            ImGui::Text("[%u]", (uint32)Value.Items.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("+"))
            {
                Value.Items.emplace_back(FScriptPropertyValue::FromType(*Type.ElementType));
                bChanged = true;
            }
            if (bOutRemove)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) *bOutRemove = true;
            }

            if (bOpen)
            {
                for (size_t i = 0; i < Value.Items.size(); )
                {
                    char RowLabel[32];
                    snprintf(RowLabel, sizeof(RowLabel), "[%zu]", i);
                    bool bRemoveElem = false;
                    // Element widgets inherit the array's own --@export meta (clamp/units/color).
                    DrawValueRows(*Type.ElementType, Value.Items[i], RowLabel, Meta, bChanged, &bRemoveElem);
                    if (bRemoveElem)
                    {
                        Value.Items.erase(Value.Items.begin() + i);
                        bChanged = true;
                    }
                    else
                    {
                        ++i;
                    }
                }
                ImGui::TreePop();
            }
        }

        void DrawStructRows(const Lua::FScriptExportType& Type, Lua::FScriptPropertyValue& Value,
                            const char* Label, const Lua::FScriptExportMeta& Meta, bool& bChanged, bool* bOutRemove)
        {
            using namespace Lua;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const bool bOpen = ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen, "%s", Label);

            ImGui::TableNextColumn();
            if (bOutRemove)
            {
                if (ImGui::SmallButton("x")) *bOutRemove = true;
            }

            if (bOpen)
            {
                for (const FScriptExportField& Field : Type.Fields)
                {
                    if (!Field.Type) continue;

                    FScriptPropertyValue* FieldValue = nullptr;
                    for (FScriptPropertyEntry& Entry : Value.StructFields)
                    {
                        if (Entry.Name == Field.Name) { FieldValue = &Entry.Value; break; }
                    }
                    if (!FieldValue)
                    {
                        FScriptPropertyEntry Entry;
                        Entry.Name = Field.Name;
                        Entry.Value = FScriptPropertyValue::FromType(*Field.Type);
                        Value.StructFields.emplace_back(eastl::move(Entry));
                        FieldValue = &Value.StructFields.back().Value;
                    }
                    FString FieldLabel = Field.Name.ToString();
                    DrawValueRows(*Field.Type, *FieldValue, FieldLabel.c_str(), Field.Meta, bChanged);
                }
                ImGui::TreePop();
            }
        }

        void DrawValueRows(const Lua::FScriptExportType& Type, Lua::FScriptPropertyValue& Value,
                           const char* Label, const Lua::FScriptExportMeta& Meta, bool& bChanged, bool* bOutRemove)
        {
            using namespace Lua;
            // ID must be stable across frames: ReconcileOverrides rebuilds the override
            // vector every frame, so &Value is not stable. Labels are unique among siblings
            // (field names; array element "[i]"; struct field names), so key on the label.
            ImGui::PushID(Label);
            switch (Type.Kind)
            {
            case EScriptExportKind::Array:        DrawArrayRows (Type, Value, Label, Meta, bChanged, bOutRemove); break;
            case EScriptExportKind::NestedStruct: DrawStructRows(Type, Value, Label, Meta, bChanged, bOutRemove); break;
            default:                              DrawScalarRow (Type, Value, Label, Meta, bChanged, bOutRemove); break;
            }
            ImGui::PopID();
        }

        // Engine-recognized lifecycle / physics callbacks; these are driven by the world, not
        // meant to be fired by hand, so they're excluded from the editor action list.
        bool IsReservedCallbackName(const FString& Name)
        {
            static const char* const Reserved[] =
            {
                "OnAttach", "OnReady", "OnUpdate", "OnDetach",
                "OnFixedUpdate", "OnEditorUpdate",
                "OnContactBegin", "OnContactEnd",
                "OnOverlapBegin", "OnOverlapEnd",
            };
            for (const char* R : Reserved)
            {
                if (Name == R)
                {
                    return true;
                }
            }
            return false;
        }

        // True for a Lua (non-C) function taking no caller args: zero params or a lone `self`.
        // C functions report nparams=0/vararg=1 and are rejected (only script routines qualify).
        bool IsZeroInputLuaFunction(const Lua::FRef& Func)
        {
            if (!Func.IsValid() || !Func.IsInvokable())
            {
                return false;
            }

            lua_State* State = Func.GetState();
            if (State == nullptr)
            {
                return false;
            }

            Func.Push();
            bool bResult = false;
            if (!lua_iscfunction(State, -1))
            {
                lua_Debug Info = {};
                if (lua_getinfo(State, -1, "a", &Info) != 0)
                {
                    bResult = (Info.isvararg == 0) && (Info.nparams <= 1);
                }
            }
            lua_pop(State, 1);
            return bResult;
        }

        // A button per zero-input script function; clicking defers the call into OutInvoke so it
        // runs after the draw (may spawn/destroy entities). Self passed per the colon convention.
        void DrawCallableFunctionsSection(SScriptComponent& Component, TFunction<void()>& OutInvoke)
        {
            using namespace Lua;
            if (!Component.Script || !Component.Script->Reference.IsValid() || !Component.Script->Reference.IsTable())
            {
                return;
            }

            TVector<TPair<FString, FRef>> Callables;
            for (auto&& [Key, Value] : Component.Script->Reference)
            {
                if (Key.GetType() != EType::String)
                {
                    continue;
                }
                FString Name = Key.ToString();
                if (IsReservedCallbackName(Name) || !IsZeroInputLuaFunction(Value))
                {
                    continue;
                }
                Callables.emplace_back(eastl::move(Name), Value);
            }

            if (Callables.empty())
            {
                return;
            }

            if (!ImGui::CollapsingHeader("Functions", ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }

            for (const TPair<FString, FRef>& Entry : Callables)
            {
                const FString& Name = Entry.first;
                const FRef& Func = Entry.second;
                if (ImGui::Button(Name.c_str()))
                {
                    OutInvoke = [Script = Component.Script, Func]
                    {
                        Script->InvokeAsCoroutine(Func, Script->Reference);
                    };
                }
                ImGuiX::TextTooltip("Execute {}() on this script", Name);
            }
        }

        // Field's --@export(Category="...") group, or empty when uncategorized.
        FString ExportCategoryOf(const Lua::FScriptExportField& Field)
        {
            const FString* Category = Field.Meta.Find("Category");
            return (Category && !Category->empty()) ? *Category : FString();
        }

        // A PropertyTable-style category band: paints both row cells dark to read as a section
        // break, and draws a borderless collapsing header. Returns whether the section is expanded.
        // Mirrors FCategoryPropertyRow::DrawHeader (PropertyTable.cpp).
        bool DrawExportCategoryRow(const char* Name)
        {
            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(38, 38, 42, 255));
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(38, 38, 42, 255));

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Header, 0);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(255, 255, 255, 16));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 12));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 222, 255));
            const bool bOpen = ImGui::CollapsingHeader(Name, ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor(4);

            ImGui::TableNextColumn(); // category rows leave the value cell empty
            return bOpen;
        }

        // Draws every field belonging to one category as rows (assumes an active table).
        void DrawExportCategoryFields(SScriptComponent& Component, const FString& Category, bool& bOutChanged)
        {
            for (const Lua::FScriptExportField& Field : Component.Script->ExportsSchema.Fields)
            {
                if (!Field.Type || ExportCategoryOf(Field) != Category) continue;

                Lua::FScriptPropertyValue* Value = nullptr;
                for (Lua::FScriptPropertyEntry& Entry : Component.PropertyOverrides.Items)
                {
                    if (Entry.Name == Field.Name) { Value = &Entry.Value; break; }
                }
                if (!Value) continue;

                FString Label = Field.Name.ToString();
                DrawValueRows(*Field.Type, *Value, Label.c_str(), Field.Meta, bOutChanged);
            }
        }

        void DrawExportsSection(SScriptComponent& Component, bool& bOutChanged)
        {
            if (!Component.Script || !Component.Script->ExportsSchema.IsValid())
            {
                return;
            }

            // Keep overrides in sync with the schema in case the script just changed.
            Lua::ReconcileOverrides(
                Component.Script->ExportsSchema,
                Component.Script->ExportDefaults,
                Component.PropertyOverrides.Items);

            if (!ImGui::CollapsingHeader("Exports", ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }

            // Categories in first-seen order; empty string is the uncategorized bucket.
            TVector<FString> Categories;
            bool bAnyCategory = false;
            for (const Lua::FScriptExportField& Field : Component.Script->ExportsSchema.Fields)
            {
                FString Category = ExportCategoryOf(Field);
                if (!Category.empty()) bAnyCategory = true;

                bool bFound = false;
                for (const FString& Existing : Categories)
                {
                    if (Existing == Category) { bFound = true; break; }
                }
                if (!bFound) Categories.push_back(Category);
            }

            // One table for everything; categories are header rows within it (PropertyTable style).
            constexpr ImGuiTableFlags Flags =
                ImGuiTableFlags_BordersOuter |
                ImGuiTableFlags_NoBordersInBodyUntilResize |
                ImGuiTableFlags_SizingStretchSame |
                ImGuiTableFlags_RowBg;

            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 3));
            if (ImGui::BeginTable("##ScriptExports", 2, Flags))
            {
                ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch);

                if (!bAnyCategory)
                {
                    DrawExportCategoryFields(Component, FString(), bOutChanged);
                }
                else
                {
                    for (const FString& Category : Categories)
                    {
                        const char* Header = Category.empty() ? "General" : Category.c_str();
                        if (DrawExportCategoryRow(Header))
                        {
                            DrawExportCategoryFields(Component, Category, bOutChanged);
                        }
                    }
                }

                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
        }
    }
}

namespace Lumina
{
    static constexpr ImVec2 GButtonSize(42, 0);
    
    TSharedPtr<FScriptComponentPropertyCustomization> FScriptComponentPropertyCustomization::MakeInstance()
    {
        return MakeShared<FScriptComponentPropertyCustomization>();
    }

    EPropertyChangeOp FScriptComponentPropertyCustomization::DrawProperty(const TSharedPtr<FPropertyHandle>& Property)
    {
        bool bWasChanged = false;

        // Set when an editor-triggered script function is clicked; replayed after the draw so the
        // script (which may spawn/destroy entities) doesn't run while we're mid-component-draw.
        TFunction<void()> PendingInvoke;

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        SScriptComponent* ScriptComponent = static_cast<SScriptComponent*>(Property->ContainerPtr);
        
        ImGui::PushID(this);
        if (ImGui::BeginChild("OP", ImVec2(-1, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            const auto& Style = ImGui::GetStyle();
            
            ImGui::BeginGroup();

            float const ComboArrowWidth = ImGui::GetFrameHeight();
            float const TotalPathWidgetWidth = ImGui::GetContentRegionAvail().x;
            float const TextWidgetWidth = TotalPathWidgetWidth - ComboArrowWidth;

            ImGui::SetNextItemWidth(TextWidgetWidth);
            
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        
            FFixedString PathString(ScriptComponent->ScriptPath.Path.begin(), ScriptComponent->ScriptPath.Path.length());
            ImGui::InputText("##ScriptPathText", PathString.data(), PathString.max_size(), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);

            // Drop a .luau / .lua file from the content browser to bind it.
            if (ImGui::BeginDragDropTarget())
            {
                FFixedString DroppedPath;
                if (DragDrop::AcceptScript(DroppedPath))
                {
                    const FString NewPath(DroppedPath.c_str(), DroppedPath.size());
                    PendingMutation = [ScriptComponent, NewPath]
                    {
                        ScriptComponent->ScriptPath.SetPath(NewPath);
                        if (ScriptComponent->World)
                        {
                            ScriptComponent->World->OnScriptComponentCreated(ScriptComponent->Entity, *ScriptComponent, true);
                        }
                    };
                    bWasChanged = true;
                }
                ImGui::EndDragDropTarget();
            }

            ImGuiX::TextTooltip("{}", ScriptComponent->ScriptPath.Path);

            ImGui::PopStyleColor();

            const ImVec2 ComboDropDownSize = ImMax(ImVec2(200, 200), ImVec2(TextWidgetWidth, 300.0f));

            ImGui::SameLine(0, 0);
        
            bool bComboOpen = ImGui::BeginCombo("##ScriptPath", "", ImGuiComboFlags_HeightLarge | ImGuiComboFlags_PopupAlignLeft | ImGuiComboFlags_NoPreview);
            
            if (bComboOpen)
            {
                SearchFilter.Draw("##Search", ComboDropDownSize.x - 30.0f);
                if (!SearchFilter.IsActive())
                {
                    ImDrawList* DrawList = ImGui::GetWindowDrawList();
                    ImVec2 TextPos = ImGui::GetItemRectMin();
                    TextPos.x += Style.FramePadding.x + 2.0f;
                    TextPos.y += Style.FramePadding.y;
                    DrawList->AddText(TextPos, IM_COL32(100, 100, 110, 255), LE_ICON_FILE_SEARCH " Search Scripts...");
                }
                
                ImGui::SameLine();
                ImGui::Button(LE_ICON_FILTER, ImVec2(30.0f, 0.0f));
                ImGui::SetNextWindowSizeConstraints(ImVec2(200, 200), ComboDropDownSize);

                if (ImGui::BeginChild("##OptList", ComboDropDownSize, false, ImGuiChildFlags_NavFlattened))
                {
                    // Every script across all mounts (project, plugins, engine), see Lua::GatherScriptPaths.
                    for (const FFixedString& VirtualPath : Lua::GatherScriptPaths())
                    {
                        if (!SearchFilter.PassFilter(VirtualPath.c_str()))
                        {
                            continue;
                        }

                        FFixedString SelectableLabel;
                        SelectableLabel.append(LE_ICON_LANGUAGE_LUA).append(" ").append(VirtualPath.c_str());
                        if (ImGui::Selectable(SelectableLabel.c_str()))
                        {
                            const FString NewPath(VirtualPath.c_str(), VirtualPath.size());
                            PendingMutation = [ScriptComponent, NewPath]
                            {
                                ScriptComponent->ScriptPath.SetPath(NewPath);
                                if (ScriptComponent->World)
                                {
                                    ScriptComponent->World->OnScriptComponentCreated(ScriptComponent->Entity, *ScriptComponent, true);
                                }
                            };
                            ImGui::CloseCurrentPopup();
                            bWasChanged = true;
                        }

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        {
                            FString FileContents;
                            if (VFS::ReadFile(FileContents, VirtualPath))
                            {
                                FString Preview;
                                int LineCount = 0;
                                for (char C : FileContents)
                                {
                                    if (C == '\n' && ++LineCount >= 10)
                                    {
                                        Preview.append("\n...");
                                        break;
                                    }
                                    Preview.push_back(C);
                                }

                                ImGui::BeginTooltip();
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.6f, 1.0f));
                                ImGui::TextUnformatted(Preview.c_str());
                                ImGui::PopStyleColor();
                                ImGui::EndTooltip();
                            }
                        }
                    }
                }
                
                ImGui::EndChild();
                ImGui::EndCombo();
            }
            
            if (ImGui::Button(LE_ICON_OPEN_IN_NEW "##Open", GButtonSize))
            {
                // Route through the editor's file-open path so it honors the Lua Editor setting
                // (in-engine FLuaEditorTool by default, native editor when bUsePlatformEditor is set),
                // same as double-clicking the script in the content browser.
                const FStringView Path(ScriptComponent->ScriptPath.Path.c_str(),
                                       ScriptComponent->ScriptPath.Path.length());
                if (!Path.empty())
                {
                    static_cast<FEditorUI*>(GEditorEngine->GetDevelopmentToolsUI())->OpenFileEditor(Path);
                }
            }

            ImGuiX::TextTooltip("Open the script for editing (uses the Lua Editor setting for in-engine vs. native)");
            
            ImGui::SameLine();
            
            if (ImGui::Button(LE_ICON_CONTENT_COPY "##Copy", GButtonSize))
            {
                ImGui::SetClipboardText(ScriptComponent->ScriptPath.Path.c_str());
            }
            
            ImGuiX::TextTooltip("Copy the script-path to your native clipboard");

            ImGui::SameLine();
            
            if (ImGui::Button(LE_ICON_REFRESH "##Refresh", GButtonSize))
            {
                PendingMutation = [ScriptComponent]
                {
                    // Drop the cached bytecode/schema so the reload re-reads (and re-scans --@export)
                    // from disk; ScriptReloaded then refreshes every component bound to this path,
                    // reconciling each one's overrides against the rebuilt schema.
                    const FStringView ResolvedPath = ScriptComponent->ScriptPath.ResolvePath();
                    if (!ResolvedPath.empty())
                    {
                        Lua::FScriptingContext::Get().ScriptReloaded(ResolvedPath);
                    }
                };
                bWasChanged = true;
            }

            ImGuiX::TextTooltip("Reload the script from disk, will attempt to keep any matching values");

            ImGui::SameLine();
        
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));

            if (ImGui::Button(LE_ICON_CLOSE_CIRCLE "##Clear", GButtonSize))
            {
                PendingMutation = [ScriptComponent]
                {
                    ScriptComponent->Script = {};
                    ScriptComponent->AttachFunc = {};
                    ScriptComponent->ReadyFunc = {};
                    ScriptComponent->UpdateFunc = {};
                    ScriptComponent->DetachFunc = {};
                    ScriptComponent->FixedUpdateFunc = {};
                    ScriptComponent->EditorUpdateFunc = {};
                    ScriptComponent->ScriptPath = {};
                    ScriptComponent->ScriptMetaTable = {};
                    ScriptComponent->TickRate = 0.0f;
                };
                bWasChanged = true;
            }
            
            ImGuiX::TextTooltip("Clears the script from this component");
            
            ImGui::Separator();
            
            if (ScriptComponent->Script && ScriptComponent->Script->Reference.IsValid()
                && ScriptComponent->Script->Reference.IsTable())
            {
                // Debug-only dump of the live script table; collapsed by default so it doesn't
                // render (or walk the table) unless the user expands it.
                if (ImGui::CollapsingHeader("Reference Table"))
                {
                    if (ImGui::BeginTable("ScriptReference", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Key");
                        ImGui::TableSetupColumn("Value");
                        ImGui::TableHeadersRow();

                        for (auto&& [Key, Value] : ScriptComponent->Script->Reference)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(SafeRefDisplay(Key).c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextUnformatted(SafeRefDisplay(Value).c_str());
                        }

                        ImGui::EndTable();
                    }
                }
            }
            
            ImGui::PopStyleColor(3);

            // Export-value edits mutate live immediately; excluded from change-op to avoid an
            // empty transaction (mutation precedes BeginTransaction's snapshot).
            bool bExportsChanged = false;
            DrawExportsSection(*ScriptComponent, bExportsChanged);

            DrawCallableFunctionsSection(*ScriptComponent, PendingInvoke);

            ImGui::EndGroup();
        }
        ImGui::EndChild();

        ImGui::PopID();

        ImGui::PopItemWidth();

        // Component draw is complete; safe to run a function that may mutate the world.
        if (PendingInvoke)
        {
            PendingInvoke();
        }

        if (bWasChanged)
        {
            // Fold a follow-up edit into the already-open transaction.
            if (bFinishPending)
            {
                return EPropertyChangeOp::Updated;
            }
            // Open the transaction now; the deferred PendingMutation runs in UpdatePropertyValue,
            // after StartChangeCallback (BeginTransaction) has snapshotted the pre-change state.
            bFinishPending = true;
            return EPropertyChangeOp::Started;
        }

        if (bFinishPending)
        {
            bFinishPending = false;
            return EPropertyChangeOp::Finished;
        }

        return EPropertyChangeOp::None;
    }

    void FScriptComponentPropertyCustomization::UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property)
    {
        // Runs inside DispatchChange, after BeginTransaction, replay the deferred edit here so
        // the undo snapshot captured the pre-change state.
        if (PendingMutation)
        {
            PendingMutation();
            PendingMutation = {};
        }
    }

    void FScriptComponentPropertyCustomization::HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property)
    {

    }
}
