#pragma once
#include "UI/Tools/EditorTool.h"
#include "Input/InputAction.h"

namespace Lumina
{
    class FInputActionEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FInputActionEditorTool)

        FInputActionEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Input Actions", nullptr)
        {}

        bool        IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon()    const override { return LE_ICON_KEYBOARD; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void Update(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

    private:

        enum class ECaptureSlot : uint8 { None, Key, Positive, Negative, MouseButton };

        void DrawWindow(bool bIsFocused);
        void DrawToolbar();
        void DrawActionList();
        void DrawActionDetails();
        void DrawBindingRow(int32 BindingIndex, FInputBinding& Binding);
        void DrawKeyPickerButton(const char* Label, EKey& Key, int32 BindingIndex, ECaptureSlot Slot);
        void DrawMouseButtonCombo(const char* Label, EMouseKey& Button);

        FInputAction* FindAction(FName Name);
        void          AddAction(const char* Name);
        void          RemoveAction(FName Name);
        void          AddBinding(FInputAction& Action);

        void Reload();
        void Save();

        // Working copy. Edits aren't pushed to FInputActionMap until Save.
        TVector<FInputAction>  EditedActions;

        FName                  SelectedAction;
        char                   SearchBuffer[64]   = {};
        char                   NewActionName[64]  = {};

        ECaptureSlot           CaptureSlot          = ECaptureSlot::None;
        int32                  CaptureBindingIndex  = -1;
        bool                   CaptureSkipFrame     = false;

        void BeginCapture(int32 BindingIndex, ECaptureSlot Slot);
        void EndCapture();
    };
}
