#include "InputActionEditorTool.h"

#include "Core/Application/Application.h"
#include "Input/InputActionMap.h"
#include "Input/InputContext.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{
    namespace
    {
        const char* BindingTypeLabel(EInputBindingType T)
        {
            switch (T)
            {
            case EInputBindingType::Key:         return "Key";
            case EInputBindingType::MouseButton: return "Mouse Button";
            case EInputBindingType::Axis1D:      return "Axis (1D)";
            }
            return "?";
        }

        FString FormatBindingSummary(const FInputBinding& Binding)
        {
            switch (Binding.Type)
            {
            case EInputBindingType::Key:
                return FInputActionMap::KeyToString(Binding.Key);
            case EInputBindingType::MouseButton:
                return FString("Mouse ") + FInputActionMap::MouseButtonToString(Binding.MouseButton);
            case EInputBindingType::Axis1D:
                {
                    FString Pos = FInputActionMap::KeyToString(Binding.AxisPositive);
                    FString Neg = FInputActionMap::KeyToString(Binding.AxisNegative);
                    if (Pos.empty()) Pos = "?";
                    if (Neg.empty()) Neg = "?";
                    return Pos + FString(" / ") + Neg + FString(" axis");
                }
            }
            return FString();
        }

        // Must poll ImGui, not FEventProcessor: detached tool windows are secondary ImGui viewports
        // whose GLFW key callbacks never chain back to Lumina's window callback.
        EKey ImGuiKeyToEKey(ImGuiKey Key)
        {
            if (Key >= ImGuiKey_A && Key <= ImGuiKey_Z) return static_cast<EKey>(int(EKey::A) + (Key - ImGuiKey_A));
            if (Key >= ImGuiKey_0 && Key <= ImGuiKey_9) return static_cast<EKey>(int(EKey::D0) + (Key - ImGuiKey_0));
            if (Key >= ImGuiKey_F1 && Key <= ImGuiKey_F24) return static_cast<EKey>(int(EKey::F1) + (Key - ImGuiKey_F1));
            if (Key >= ImGuiKey_Keypad0 && Key <= ImGuiKey_Keypad9) return static_cast<EKey>(int(EKey::KP0) + (Key - ImGuiKey_Keypad0));

            switch (Key)
            {
            case ImGuiKey_Tab:           return EKey::Tab;
            case ImGuiKey_LeftArrow:     return EKey::Left;
            case ImGuiKey_RightArrow:    return EKey::Right;
            case ImGuiKey_UpArrow:       return EKey::Up;
            case ImGuiKey_DownArrow:     return EKey::Down;
            case ImGuiKey_PageUp:        return EKey::PageUp;
            case ImGuiKey_PageDown:      return EKey::PageDown;
            case ImGuiKey_Home:          return EKey::Home;
            case ImGuiKey_End:           return EKey::End;
            case ImGuiKey_Insert:        return EKey::Insert;
            case ImGuiKey_Delete:        return EKey::Delete;
            case ImGuiKey_Backspace:     return EKey::Backspace;
            case ImGuiKey_Space:         return EKey::Space;
            case ImGuiKey_Enter:         return EKey::Enter;
            case ImGuiKey_Escape:        return EKey::Escape;
            case ImGuiKey_LeftCtrl:      return EKey::LeftControl;
            case ImGuiKey_LeftShift:     return EKey::LeftShift;
            case ImGuiKey_LeftAlt:       return EKey::LeftAlt;
            case ImGuiKey_LeftSuper:     return EKey::LeftSuper;
            case ImGuiKey_RightCtrl:     return EKey::RightControl;
            case ImGuiKey_RightShift:    return EKey::RightShift;
            case ImGuiKey_RightAlt:      return EKey::RightAlt;
            case ImGuiKey_RightSuper:    return EKey::RightSuper;
            case ImGuiKey_Menu:          return EKey::Menu;
            case ImGuiKey_Apostrophe:    return EKey::Apostrophe;
            case ImGuiKey_Comma:         return EKey::Comma;
            case ImGuiKey_Minus:         return EKey::Minus;
            case ImGuiKey_Period:        return EKey::Period;
            case ImGuiKey_Slash:         return EKey::Slash;
            case ImGuiKey_Semicolon:     return EKey::Semicolon;
            case ImGuiKey_Equal:         return EKey::Equal;
            case ImGuiKey_LeftBracket:   return EKey::LeftBracket;
            case ImGuiKey_Backslash:     return EKey::Backslash;
            case ImGuiKey_RightBracket:  return EKey::RightBracket;
            case ImGuiKey_GraveAccent:   return EKey::GraveAccent;
            case ImGuiKey_CapsLock:      return EKey::CapsLock;
            case ImGuiKey_ScrollLock:    return EKey::ScrollLock;
            case ImGuiKey_NumLock:       return EKey::NumLock;
            case ImGuiKey_PrintScreen:   return EKey::PrintScreen;
            case ImGuiKey_Pause:         return EKey::Pause;
            case ImGuiKey_KeypadDecimal: return EKey::KPDecimal;
            case ImGuiKey_KeypadDivide:  return EKey::KPDivide;
            case ImGuiKey_KeypadMultiply:return EKey::KPMultiply;
            case ImGuiKey_KeypadSubtract:return EKey::KPSubtract;
            case ImGuiKey_KeypadAdd:     return EKey::KPAdd;
            case ImGuiKey_KeypadEnter:   return EKey::KPEnter;
            case ImGuiKey_KeypadEqual:   return EKey::KPEqual;
            default:                     return EKey::Num;
            }
        }

        EKey PollPressedKey()
        {
            for (int K = ImGuiKey_NamedKey_BEGIN; K < ImGuiKey_NamedKey_END; ++K)
            {
                const ImGuiKey Key = static_cast<ImGuiKey>(K);
                // Skip mouse / gamepad / mod-alias entries, they live in the named-key range too.
                if (Key >= ImGuiKey_MouseLeft && Key <= ImGuiKey_MouseWheelY) continue;
                if (Key >= ImGuiKey_GamepadStart && Key <= ImGuiKey_GamepadRStickDown) continue;
                if (Key >= ImGuiKey_ReservedForModCtrl && Key <= ImGuiKey_ReservedForModSuper) continue;

                if (ImGui::IsKeyPressed(Key, /*repeat*/ false))
                {
                    const EKey Mapped = ImGuiKeyToEKey(Key);
                    if (Mapped != EKey::Num)
                    {
                        return Mapped;
                    }
                }
            }
            return EKey::Num;
        }

        EMouseKey PollPressedMouseButton()
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left,   false)) return EMouseKey::ButtonLeft;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right,  false)) return EMouseKey::ButtonRight;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle, false)) return EMouseKey::ButtonMiddle;
            return EMouseKey::Num;
        }
    }

    void FInputActionEditorTool::OnInitialize()
    {
        Reload();
        CreateToolWindow("Input Actions", [this](bool bIsFocused)
        {
            DrawWindow(bIsFocused);
        });
    }

    void FInputActionEditorTool::OnDeinitialize(const FUpdateContext&)
    {
        EndCapture();
    }

    void FInputActionEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("Actions vs Bindings",
            "An Action is a named gameplay event (e.g. 'Jump'). Bindings are the physical inputs that "
            "fire it (Spacebar, Gamepad-A, Mouse4). One action can have many bindings.");
        DrawHelpTextRow("Capture",
            "Click a Key/Mouse field's 'Listen' button and press the input you want to bind. Esc cancels. "
            "Multiple modifiers can be held; the captured chord is what fires the action.");
        DrawHelpTextRow("Axes",
            "Positive/Negative slot pairs synthesize a -1..+1 axis (e.g. A/D for steering). The runtime "
            "InputContext exposes them as floats.");
        DrawHelpTextRow("Saving",
            "Edits live in a working copy until you hit Save, at which point they're committed to "
            "FInputActionMap and persisted to project config.");
        DrawHelpTextRow("In Lua",
            "Input.BindAction(\"Jump\", function() ... end), see Tools > Debug > Scripts Info > API "
            "Reference under 'Input' for the full surface.");
    }

    void FInputActionEditorTool::Update(const FUpdateContext&)
    {
        if (CaptureSlot == ECaptureSlot::None)
        {
            return;
        }
        
        if (CaptureSkipFrame)
        {
            CaptureSkipFrame = false;
            return;
        }

        FInputAction* Action = FindAction(SelectedAction);
        if (Action == nullptr || CaptureBindingIndex >= int32(Action->Bindings.size()))
        {
            EndCapture();
            return;
        }
        FInputBinding& Binding = Action->Bindings[CaptureBindingIndex];

        if (CaptureSlot == ECaptureSlot::MouseButton)
        {
            const EMouseKey M = PollPressedMouseButton();
            if (M != EMouseKey::Num)
            {
                Binding.MouseButton = M;
                EndCapture();
            }
            return;
        }

        const EKey K = PollPressedKey();
        if (K == EKey::Num)
        {
            return;
        }

        switch (CaptureSlot)
        {
        case ECaptureSlot::Key:      Binding.Key          = K; break;
        case ECaptureSlot::Positive: Binding.AxisPositive = K; break;
        case ECaptureSlot::Negative: Binding.AxisNegative = K; break;
        default: break;
        }
        EndCapture();
    }

    void FInputActionEditorTool::BeginCapture(int32 BindingIndex, ECaptureSlot Slot)
    {
        CaptureSlot         = Slot;
        CaptureBindingIndex = BindingIndex;
        CaptureSkipFrame    = true;
    }

    void FInputActionEditorTool::EndCapture()
    {
        CaptureSlot         = ECaptureSlot::None;
        CaptureBindingIndex = -1;
        CaptureSkipFrame    = false;
    }

    void FInputActionEditorTool::DrawWindow(bool /*bIsFocused*/)
    {
        DrawToolbar();
        ImGui::Separator();

        const float Avail = ImGui::GetContentRegionAvail().x;
        const float LeftWidth = Avail * 0.32f;

        ImGui::BeginChild("##ActionList", ImVec2(LeftWidth, 0), true);
        DrawActionList();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##ActionDetails", ImVec2(0, 0), true);
        DrawActionDetails();
        ImGui::EndChild();
    }

    void FInputActionEditorTool::DrawToolbar()
    {
        if (ImGui::Button(LE_ICON_CONTENT_SAVE " Save"))
        {
            Save();
        }
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_REFRESH " Reload"))
        {
            Reload();
        }
        ImGui::SameLine();

        ImGui::SetNextItemWidth(180);
        ImGui::InputTextWithHint("##NewName", "New action name...", NewActionName, IM_ARRAYSIZE(NewActionName));
        ImGui::SameLine();
        const bool bCanAdd = NewActionName[0] != '\0' && FindAction(FName(NewActionName)) == nullptr;
        ImGui::BeginDisabled(!bCanAdd);
        if (ImGui::Button(LE_ICON_PLUS " Add Action"))
        {
            AddAction(NewActionName);
            NewActionName[0] = '\0';
        }
        ImGui::EndDisabled();
    }

    void FInputActionEditorTool::DrawActionList()
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Search", LE_ICON_MAGNIFY " Filter", SearchBuffer, IM_ARRAYSIZE(SearchBuffer));
        ImGui::Separator();

        for (FInputAction& Action : EditedActions)
        {
            const FString NameStr = Action.Name.ToString();
            if (SearchBuffer[0] != '\0' && NameStr.find(SearchBuffer) == FString::npos)
            {
                continue;
            }

            const bool bSelected = (SelectedAction == Action.Name);
            ImGui::PushID(NameStr.c_str());
            if (ImGui::Selectable(NameStr.c_str(), bSelected))
            {
                SelectedAction = Action.Name;
                EndCapture();
            }
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem(LE_ICON_TRASH_CAN " Delete"))
                {
                    RemoveAction(Action.Name);
                    ImGui::EndPopup();
                    ImGui::PopID();
                    return;
                }
                ImGui::EndPopup();
            }

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.6f, 0.65f, 1.0f));
            ImGui::Indent(12.0f);
            ImGui::TextUnformatted((std::to_string(Action.Bindings.size()) + " binding(s)").c_str());
            if (Action.bRunsInUI)
            {
                ImGui::SameLine();
                ImGui::TextUnformatted("(runs in UI)");
            }
            ImGui::Unindent(12.0f);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }

    void FInputActionEditorTool::DrawActionDetails()
    {
        FInputAction* Action = FindAction(SelectedAction);
        if (Action == nullptr)
        {
            ImGui::TextDisabled("Select an action on the left, or add a new one.");
            return;
        }

        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", Action->Name.ToString().c_str());
        ImGui::Separator();

        ImGui::Checkbox("Runs in UI", &Action->bRunsInUI);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("If enabled, this action still fires when the active\n"
                              "viewport is in EInputMode::UI. Use for Pause / Save\n"
                              "hotkeys that must work even with a menu open.");
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Bindings");
        ImGui::SameLine();
        ImGui::TextDisabled("(click to capture, click again to cancel, right-click to clear)");
        ImGui::Separator();

        for (int32 i = 0; i < int32(Action->Bindings.size()); ++i)
        {
            ImGui::PushID(i);
            DrawBindingRow(i, Action->Bindings[i]);
            ImGui::PopID();
        }

        ImGui::Spacing();
        if (ImGui::Button(LE_ICON_PLUS " Add Binding"))
        {
            AddBinding(*Action);
        }
    }

    void FInputActionEditorTool::DrawBindingRow(int32 BindingIndex, FInputBinding& Binding)
    {
        ImGui::BeginGroup();

        // Switching type clears the other slots so stale fields don't leak through.
        const char* TypeLabels[] = { "Key", "Mouse Button", "Axis (1D)" };
        int32 TypeIdx = int32(Binding.Type);
        ImGui::SetNextItemWidth(140);
        if (ImGui::Combo("##Type", &TypeIdx, TypeLabels, IM_ARRAYSIZE(TypeLabels)))
        {
            Binding.Type = static_cast<EInputBindingType>(TypeIdx);
            Binding.Key          = EKey::Num;
            Binding.AxisPositive = EKey::Num;
            Binding.AxisNegative = EKey::Num;
            Binding.MouseButton  = EMouseKey::Num;
            if (CaptureBindingIndex == BindingIndex)
            {
                EndCapture();
            }
        }

        ImGui::SameLine();

        switch (Binding.Type)
        {
        case EInputBindingType::Key:
            DrawKeyPickerButton("##Key", Binding.Key, BindingIndex, ECaptureSlot::Key);
            break;
        case EInputBindingType::MouseButton:
            DrawMouseButtonCombo("##Btn", Binding.MouseButton);
            break;
        case EInputBindingType::Axis1D:
            ImGui::TextUnformatted("+");
            ImGui::SameLine();
            DrawKeyPickerButton("##Pos", Binding.AxisPositive, BindingIndex, ECaptureSlot::Positive);
            ImGui::SameLine();
            ImGui::TextUnformatted("-");
            ImGui::SameLine();
            DrawKeyPickerButton("##Neg", Binding.AxisNegative, BindingIndex, ECaptureSlot::Negative);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            ImGui::DragFloat("##Scale", &Binding.AxisScale, 0.05f, -10.0f, 10.0f, "x%.2f");
            break;
        }

        if (Binding.Type != EInputBindingType::Axis1D)
        {
            ImGui::SameLine();
            ImGui::Checkbox("Ctrl",  &Binding.bRequireCtrl);
            ImGui::SameLine();
            ImGui::Checkbox("Shift", &Binding.bRequireShift);
            ImGui::SameLine();
            ImGui::Checkbox("Alt",   &Binding.bRequireAlt);
        }

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_TRASH_CAN "##Del"))
        {
            FInputAction* Action = FindAction(SelectedAction);
            if (Action != nullptr)
            {
                if (CaptureBindingIndex == BindingIndex)
                {
                    EndCapture();
                }
                Action->Bindings.erase(Action->Bindings.begin() + BindingIndex);
            }
        }

        ImGui::EndGroup();
    }

    void FInputActionEditorTool::DrawKeyPickerButton(const char* Label, EKey& Key, int32 BindingIndex, ECaptureSlot Slot)
    {
        const bool bCapturingHere = (CaptureSlot == Slot && CaptureBindingIndex == BindingIndex);

        FString Display;
        if (bCapturingHere)
        {
            Display = "Press a key...";
        }
        else
        {
            Display = FInputActionMap::KeyToString(Key);
            if (Display.empty())
            {
                Display = "(unbound)";
            }
        }

        if (bCapturingHere)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.20f, 1.0f));
        }
        if (ImGui::Button((Display + Label).c_str(), ImVec2(220, 0)))
        {
            if (bCapturingHere)
            {
                EndCapture();
            }
            else
            {
                BeginCapture(BindingIndex, Slot);
            }
        }
        if (bCapturingHere)
        {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            if (bCapturingHere)
            {
                EndCapture();
            }
            Key = EKey::Num;
        }
    }

    void FInputActionEditorTool::DrawMouseButtonCombo(const char* Label, EMouseKey& Button)
    {
        const TVector<EMouseKey>& All = FInputActionMap::AllSupportedMouseButtons();
        int32 CurIdx = 0;
        for (size_t i = 0; i < All.size(); ++i)
        {
            if (All[i] == Button) { CurIdx = int32(i); break; }
        }
        ImGui::SetNextItemWidth(140);
        if (ImGui::BeginCombo(Label, FInputActionMap::MouseButtonToString(All[CurIdx]).c_str()))
        {
            for (size_t i = 0; i < All.size(); ++i)
            {
                const bool bSelected = (int32(i) == CurIdx);
                if (ImGui::Selectable(FInputActionMap::MouseButtonToString(All[i]).c_str(), bSelected))
                {
                    Button = All[i];
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    FInputAction* FInputActionEditorTool::FindAction(FName Name)
    {
        for (FInputAction& A : EditedActions)
        {
            if (A.Name == Name) return &A;
        }
        return nullptr;
    }

    void FInputActionEditorTool::AddAction(const char* Name)
    {
        FInputAction A;
        A.Name = FName(Name);
        EditedActions.push_back(std::move(A));
        SelectedAction = FName(Name);
    }

    void FInputActionEditorTool::RemoveAction(FName Name)
    {
        for (auto It = EditedActions.begin(); It != EditedActions.end(); ++It)
        {
            if (It->Name == Name)
            {
                EditedActions.erase(It);
                if (SelectedAction == Name)
                {
                    SelectedAction = NAME_None;
                }
                return;
            }
        }
    }

    void FInputActionEditorTool::AddBinding(FInputAction& Action)
    {
        FInputBinding B;
        B.Type = EInputBindingType::Key;
        Action.Bindings.push_back(B);
    }

    void FInputActionEditorTool::Reload()
    {
        EditedActions = FInputActionMap::Get().GetAllActions();
        if (EditedActions.empty())
        {
            SelectedAction = NAME_None;
        }
        else if (FindAction(SelectedAction) == nullptr)
        {
            SelectedAction = EditedActions.front().Name;
        }
        EndCapture();
    }

    void FInputActionEditorTool::Save()
    {
        // Push to the live map first so runtime picks it up without a reload.
        FInputActionMap::Get().SetActions(EditedActions);
        FInputActionMap::Get().SaveToProjectConfig();
    }
}
