#include "ScriptComponentCustomization.h"
#include "imgui.h"
#include "World/World.h"
#include "Platform/Process/PlatformProcess.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/ScriptExports.h"
#include "Scripting/Lua/LuaTypes.h"
#include "Tools/UI/ImGui/ImGuiDragDrop.h"
#include "UI/Tools/ContentBrowserEditorTool.h"

namespace Lumina
{
    namespace
    {
        bool DrawScriptPropertyValue(const Lua::FScriptExportType& Type, Lua::FScriptPropertyValue& Value, const char* Label);

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

        // Draws "Name | widget" as a row; returns whether the widget changed.
        bool DrawScalarValue(const Lua::FScriptExportType& Type, Lua::FScriptPropertyValue& Value, const char* Label)
        {
            using namespace Lua;
            bool bChanged = false;

            ImGui::PushID(Label);

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Label);
            ImGui::SameLine();

            const float LabelColumnEnd = 180.0f;
            if (ImGui::GetCursorPosX() < LabelColumnEnd)
            {
                ImGui::SetCursorPosX(LabelColumnEnd);
            }
            ImGui::SetNextItemWidth(-FLT_MIN);

            FFixedString HiddenLabel;
            HiddenLabel.append("##").append(Label);
            const char* HL = HiddenLabel.c_str();

            switch (Type.Kind)
            {
            case EScriptExportKind::Bool:
                bChanged = ImGui::Checkbox(HL, &Value.AsBool);
                break;
            case EScriptExportKind::Int:
            {
                int Tmp = (int)Value.AsInt;
                if (ImGui::DragInt(HL, &Tmp, 1.0f))
                {
                    Value.AsInt = Tmp;
                    bChanged = true;
                }
                break;
            }
            case EScriptExportKind::Double:
            {
                float Tmp = (float)Value.AsDouble;
                if (ImGui::DragFloat(HL, &Tmp, 0.1f))
                {
                    Value.AsDouble = (double)Tmp;
                    bChanged = true;
                }
                break;
            }
            case EScriptExportKind::String:
            {
                char Buffer[1024] = {0};
                size_t Copy = Value.AsString.size() < sizeof(Buffer) - 1 ? Value.AsString.size() : sizeof(Buffer) - 1;
                memcpy(Buffer, Value.AsString.c_str(), Copy);
                if (ImGui::InputText(HL, Buffer, sizeof(Buffer)))
                {
                    Value.AsString = Buffer;
                    bChanged = true;
                }
                break;
            }
            case EScriptExportKind::Vec2:
                bChanged = ImGui::DragFloat2(HL, &Value.AsVec.x, 0.1f);
                break;
            case EScriptExportKind::Vec3:
                bChanged = ImGui::DragFloat3(HL, &Value.AsVec.x, 0.1f);
                break;
            case EScriptExportKind::Vec4:
                bChanged = ImGui::DragFloat4(HL, &Value.AsVec.x, 0.1f);
                break;
            case EScriptExportKind::UnknownUserdata:
                ImGui::TextDisabled("(%s: not yet editable)", Value.UserdataTypeName.ToString().c_str());
                break;
            default:
                break;
            }

            ImGui::PopID();
            return bChanged;
        }

        bool DrawArrayValue(const Lua::FScriptExportType& Type, Lua::FScriptPropertyValue& Value, const char* Label)
        {
            using namespace Lua;
            if (!Type.ElementType) return false;

            bool bChanged = false;
            ImGui::PushID(Label);

            if (ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen, "%s [%u]", Label, (uint32)Value.Items.size()))
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("+"))
                {
                    Value.Items.emplace_back(FScriptPropertyValue::FromType(*Type.ElementType));
                    bChanged = true;
                }

                for (size_t i = 0; i < Value.Items.size(); )
                {
                    ImGui::PushID((int)i);
                    char RowLabel[32];
                    snprintf(RowLabel, sizeof(RowLabel), "[%zu]", i);

                    bool bRemoved = false;
                    if (ImGui::SmallButton("x"))
                    {
                        Value.Items.erase(Value.Items.begin() + i);
                        bChanged = true;
                        bRemoved = true;
                    }
                    else
                    {
                        ImGui::SameLine();
                        bChanged |= DrawScriptPropertyValue(*Type.ElementType, Value.Items[i], RowLabel);
                    }

                    ImGui::PopID();
                    if (!bRemoved) ++i;
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
            return bChanged;
        }

        bool DrawStructValue(const Lua::FScriptExportType& Type, Lua::FScriptPropertyValue& Value, const char* Label)
        {
            using namespace Lua;
            bool bChanged = false;
            ImGui::PushID(Label);

            if (ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen))
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
                    bChanged |= DrawScriptPropertyValue(*Field.Type, *FieldValue, FieldLabel.c_str());
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
            return bChanged;
        }

        bool DrawScriptPropertyValue(const Lua::FScriptExportType& Type, Lua::FScriptPropertyValue& Value, const char* Label)
        {
            using namespace Lua;
            switch (Type.Kind)
            {
            case EScriptExportKind::Array:
                return DrawArrayValue(Type, Value, Label);
            case EScriptExportKind::NestedStruct:
                return DrawStructValue(Type, Value, Label);
            default:
                return DrawScalarValue(Type, Value, Label);
            }
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
                if (Name == R) return true;
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

            for (const Lua::FScriptExportField& Field : Component.Script->ExportsSchema.Fields)
            {
                if (!Field.Type) continue;

                Lua::FScriptPropertyValue* Value = nullptr;
                for (Lua::FScriptPropertyEntry& Entry : Component.PropertyOverrides.Items)
                {
                    if (Entry.Name == Field.Name) { Value = &Entry.Value; break; }
                }
                if (!Value) continue;

                FString Label = Field.Name.ToString();
                if (DrawScriptPropertyValue(*Field.Type, *Value, Label.c_str()))
                {
                    bOutChanged = true;
                }
            }
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
                        ScriptComponent->ScriptPath.Path = NewPath;
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
                    // Every script across all mounts (project, plugins, engine) — see Lua::GatherScriptPaths.
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
                                ScriptComponent->ScriptPath.Path = NewPath;
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
                VFS::PlatformOpen(ScriptComponent->ScriptPath.Path);
            }
            
            ImGuiX::TextTooltip("Open the script in your native editor");
            
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
                    if (ScriptComponent->World)
                    {
                        ScriptComponent->World->OnScriptComponentCreated(ScriptComponent->Entity, *ScriptComponent, true);
                    }
                };
                bWasChanged = true;
            }
            
            ImGuiX::TextTooltip("Reload the script, will attempt to keep any matching values");

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
        // Runs inside DispatchChange, after BeginTransaction — replay the deferred edit here so
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
