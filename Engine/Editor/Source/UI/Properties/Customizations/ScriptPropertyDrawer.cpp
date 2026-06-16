#include "pch.h"
#include "ScriptPropertyDrawer.h"

#include "imgui.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetRegistry/TextAssetTypes.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectCore.h"
#include "Tools/UI/ImGui/ImGuiX.h"

// The recursive script-property inspector (scalars, arrays, nested structs), used by the C#
// script-component customization, which drives the FScriptExportType / FScriptPropertyValue model.
namespace Lumina::ScriptPropertyDrawer
{
    namespace
    {
        FString BuildScriptUnitFormat(const Scripting::FScriptExportMeta& Meta, const char* Base)
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
        ImGuiX::ESliderFlags ScriptSliderFlags(const Scripting::FScriptExportMeta& Meta)
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
        bool DrawAssetPicker(Scripting::FScriptPropertyValue& Value, const char* HL, const FString& AssetType)
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
        bool DrawScalarWidget(const Scripting::FScriptExportType& Type, Scripting::FScriptPropertyValue& Value,
                              const char* HL, const Scripting::FScriptExportMeta& Meta)
        {
            using namespace Scripting;

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

        void DrawScalarRow(const Scripting::FScriptExportType& Type, Scripting::FScriptPropertyValue& Value,
                           const char* Label, const Scripting::FScriptExportMeta& Meta, bool& bChanged, bool* bOutRemove)
        {
            using namespace Scripting;

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

        void DrawArrayRows(const Scripting::FScriptExportType& Type, Scripting::FScriptPropertyValue& Value,
                           const char* Label, const Scripting::FScriptExportMeta& Meta, bool& bChanged, bool* bOutRemove)
        {
            using namespace Scripting;
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

        void DrawStructRows(const Scripting::FScriptExportType& Type, Scripting::FScriptPropertyValue& Value,
                            const char* Label, const Scripting::FScriptExportMeta& Meta, bool& bChanged, bool* bOutRemove)
        {
            using namespace Scripting;

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
    }

    void DrawValueRows(const Scripting::FScriptExportType& Type, Scripting::FScriptPropertyValue& Value,
                       const char* Label, const Scripting::FScriptExportMeta& Meta, bool& bChanged, bool* bOutRemove)
    {
        using namespace Scripting;
        // ID must be stable across frames: ReconcileOverrides rebuilds the override vector every frame, so
        // &Value is not stable. Labels are unique among siblings (field names; array element "[i]"; struct
        // field names), so key on the label.
        ImGui::PushID(Label);
        switch (Type.Kind)
        {
        case EScriptExportKind::Array:        DrawArrayRows (Type, Value, Label, Meta, bChanged, bOutRemove); break;
        case EScriptExportKind::NestedStruct: DrawStructRows(Type, Value, Label, Meta, bChanged, bOutRemove); break;
        default:                              DrawScalarRow (Type, Value, Label, Meta, bChanged, bOutRemove); break;
        }
        ImGui::PopID();
    }
}
