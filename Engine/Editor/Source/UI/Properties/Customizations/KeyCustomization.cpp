#include "CoreTypeCustomization.h"

#include "imgui.h"
#include "Input/InputActionMap.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiKeyCapture.h"
#include "Tools/UI/ImGui/ImGuiX.h"

#include <cmath>

namespace Lumina
{
    namespace
    {
        // Compact label for the key chip (the actual bound key/button, sans modifiers).
        FString KeyChipText(const SKey& Key)
        {
            if (Key.IsMouse())
            {
                switch (Key.MouseButton)
                {
                case EMouseKey::ButtonLeft:   return FString("LMB");
                case EMouseKey::ButtonRight:  return FString("RMB");
                case EMouseKey::ButtonMiddle: return FString("MMB");
                default:                      return FInputActionMap::MouseButtonToString(Key.MouseButton);
                }
            }
            return FInputActionMap::KeyToString(Key.Key);
        }
    }

    EPropertyChangeOp FKeyPropertyCustomization::DrawProperty(const TSharedPtr<FPropertyHandle>& Property)
    {
        bool bCommitted = false; // a new binding was set (or cleared) this frame

        const float        Scale  = ImGuiX::GetUIScale();
        const ImGuiStyle&   St     = ImGui::GetStyle();
        ImDrawList*         DL     = ImGui::GetWindowDrawList();

        const float  Width    = ImGui::GetContentRegionAvail().x;
        const float  Height   = ImGui::GetFrameHeight();
        const ImVec2 P0       = ImGui::GetCursorScreenPos();
        const ImVec2 P1       = ImVec2(P0.x + (Width > 1.0f ? Width : 1.0f), P0.y + Height);
        const float  Rounding = St.FrameRounding + 2.0f * Scale;

        // One hit area for the whole keycap; the clear button is a sub-region tested by mouse-x.
        ImGui::InvisibleButton("##skey", ImVec2(Width > 1.0f ? Width : 1.0f, Height));
        const bool bHovered = ImGui::IsItemHovered();
        const bool bClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool bValid   = DisplayValue.IsValid();

        const float ClearW     = (bValid && !bCapturing) ? Height : 0.0f;
        const float ClearX     = P1.x - ClearW;
        const bool  bOverClear = ClearW > 0.0f && bHovered && ImGui::GetIO().MousePos.x >= ClearX;

        // ---- Interaction ----------------------------------------------------------------------
        if (!bCapturing && bClicked)
        {
            if (bOverClear) { DisplayValue.Clear(); bCommitted = true; }
            else            { bCapturing = true; bArmed = false; }
        }

        if (bCapturing)
        {
            if (!bArmed)
            {
                // Skip the frame the activating click landed on; otherwise that click binds immediately.
                bArmed = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Escape, /*repeat*/ false))
            {
                bCapturing = false; // cancel, leave the binding unchanged
            }
            else
            {
                // Commit only on a real (non-modifier) key or a mouse button, reading the modifiers
                // held at that instant. This is the standard key-binder flow: hold Ctrl/Shift/Alt and
                // then press the key. Bare modifier presses keep listening so a chord can be built.
                const ImGuiIO& Io = ImGui::GetIO();
                const EKey K = ImGuiX::PollPressedKey();
                if (K != EKey::Num && !ImGuiX::IsModifierEKey(K))
                {
                    DisplayValue.SetKey(K);
                    DisplayValue.bCtrl  = Io.KeyCtrl;
                    DisplayValue.bShift = Io.KeyShift;
                    DisplayValue.bAlt   = Io.KeyAlt;
                    bCapturing = false;
                    bCommitted = true;
                }
                else if (const EMouseKey M = ImGuiX::PollPressedMouseButton(); M != EMouseKey::Num)
                {
                    DisplayValue.SetMouseButton(M);
                    DisplayValue.bCtrl  = Io.KeyCtrl;
                    DisplayValue.bShift = Io.KeyShift;
                    DisplayValue.bAlt   = Io.KeyAlt;
                    bCapturing = false;
                    bCommitted = true;
                }
            }
        }

        // ---- Palette (themed off the active ImGui style) --------------------------------------
        const ImVec4 AccentV    = St.Colors[ImGuiCol_CheckMark];
        const ImU32  ColText    = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32  ColDim     = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32  ColBorder  = ImGui::GetColorU32(ImGuiCol_Border);
        const ImU32  ChipFill   = ImGui::GetColorU32(ImGuiCol_Button);
        const ImU32  AccentFill = ImGui::GetColorU32(ImVec4(AccentV.x, AccentV.y, AccentV.z, 0.22f));
        const ImU32  AccentBord = ImGui::GetColorU32(ImVec4(AccentV.x, AccentV.y, AccentV.z, 0.90f));
        const ImU32  AccentTxt  = ImGui::GetColorU32(AccentV);

        const float Pulse = 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 7.0f);

        // ---- Background well + border ---------------------------------------------------------
        const ImGuiCol BgIdx = bCapturing ? ImGuiCol_FrameBgActive
                                          : (bHovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
        DL->AddRectFilled(P0, P1, ImGui::GetColorU32(BgIdx), Rounding);
        // Subtle top sheen for a bit of depth.
        DL->AddRectFilled(P0, ImVec2(P1.x, P0.y + Height * 0.5f), IM_COL32(255, 255, 255, 10), Rounding, ImDrawFlags_RoundCornersTop);
        if (bCapturing)
        {
            DL->AddRect(P0, P1, ImGui::GetColorU32(ImVec4(AccentV.x, AccentV.y, AccentV.z, Pulse)), Rounding, 0, 1.6f * Scale);
        }
        else
        {
            DL->AddRect(P0, P1, bHovered ? ImGui::GetColorU32(AccentV) : ColBorder, Rounding, 0, 1.0f * Scale);
        }

        // ---- Content --------------------------------------------------------------------------
        const float FontH = ImGui::GetFontSize();
        const float TextY = P0.y + (Height - FontH) * 0.5f;
        const float CenterY = P0.y + Height * 0.5f;
        const float PadX = St.FramePadding.x + 2.0f * Scale;
        ImVec2 Pen = ImVec2(P0.x + PadX, 0.0f);

        // Chip: rounded pill with centered text; accented for the key, neutral for modifiers.
        auto DrawChip = [&](const char* Text, bool bAccent)
        {
            const float  ChipPad = 6.0f * Scale;
            const ImVec2 Ts      = ImGui::CalcTextSize(Text);
            const float  ChipH   = FontH + 5.0f * Scale;
            const ImVec2 C0(Pen.x, CenterY - ChipH * 0.5f);
            const ImVec2 C1(C0.x + Ts.x + ChipPad * 2.0f, C0.y + ChipH);
            const float  ChipRnd = 3.0f * Scale;
            DL->AddRectFilled(C0, C1, bAccent ? AccentFill : ChipFill, ChipRnd);
            DL->AddRect(C0, C1, bAccent ? AccentBord : ColBorder, ChipRnd, 0, 1.0f);
            DL->AddText(ImVec2(C0.x + ChipPad, CenterY - Ts.y * 0.5f), bAccent ? AccentTxt : ColText, Text);
            Pen.x = C1.x + 4.0f * Scale;
        };

        ImGui::PushClipRect(P0, ImVec2(ClearX - 2.0f * Scale, P1.y), true);
        if (bCapturing)
        {
            const float R = 4.0f * Scale;
            DL->AddCircleFilled(ImVec2(Pen.x + R, CenterY), R, ImGui::GetColorU32(ImVec4(0.95f, 0.27f, 0.27f, Pulse)));
            Pen.x += R * 2.0f + 7.0f * Scale;

            // Live preview of the modifiers held this instant, so it's clear they'll be captured.
            const ImGuiIO& Io = ImGui::GetIO();
            if (Io.KeyCtrl)  DrawChip("Ctrl",  true);
            if (Io.KeyShift) DrawChip("Shift", true);
            if (Io.KeyAlt)   DrawChip("Alt",   true);

            DL->AddText(ImVec2(Pen.x, TextY), ColDim, "press a key   " LE_ICON_KEYBOARD_ESC " Esc");
        }
        else
        {
            const char* DevIcon = !bValid ? LE_ICON_KEYBOARD_OFF
                                          : (DisplayValue.IsMouse() ? LE_ICON_MOUSE : LE_ICON_KEYBOARD);
            DL->AddText(ImVec2(Pen.x, TextY), bValid ? ColText : ColDim, DevIcon);
            Pen.x += ImGui::CalcTextSize(DevIcon).x + 7.0f * Scale;

            if (!bValid)
            {
                DL->AddText(ImVec2(Pen.x, TextY), ColDim, "Unbound");
            }
            else
            {
                if (DisplayValue.bCtrl)  DrawChip("Ctrl",  false);
                if (DisplayValue.bShift) DrawChip("Shift", false);
                if (DisplayValue.bAlt)   DrawChip("Alt",   false);
                const FString KeyName = KeyChipText(DisplayValue);
                DrawChip(KeyName.c_str(), true);
            }
        }
        ImGui::PopClipRect();

        // Clear glyph in the right zone, revealed on hover.
        if (ClearW > 0.0f && bHovered)
        {
            const ImVec2 Xs = ImGui::CalcTextSize(LE_ICON_CLOSE);
            DL->AddText(ImVec2(ClearX + (ClearW - Xs.x) * 0.5f, CenterY - Xs.y * 0.5f),
                        bOverClear ? ColText : ColDim, LE_ICON_CLOSE);
        }

        // Context tooltip (manual: the clear sub-region needs its own text).
        if (bHovered && !bCapturing)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(bOverClear ? "Clear binding"
                                              : "Click, then press a key or mouse button to rebind");
            ImGui::EndTooltip();
        }

        // Discrete-edit transaction: open on the change frame, close the next.
        if (bCommitted)
        {
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

    void FKeyPropertyCustomization::UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property)
    {
        CachedValue = DisplayValue;
        Property->SetValue(CachedValue);
    }

    void FKeyPropertyCustomization::HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property)
    {
        SKey ActualValue;
        Property->GetValue(&ActualValue);

        // An external change (reset-to-default, undo/redo, multi-select) must win, even mid-listen:
        // otherwise the next UpdatePropertyValue writes our stale DisplayValue back and undoes it.
        // While listening with no external change, ActualValue == CachedValue, so capture isn't disturbed.
        if (CachedValue != ActualValue)
        {
            CachedValue = DisplayValue = ActualValue;
            bCapturing  = false;
            bArmed      = false;
        }
    }
}
