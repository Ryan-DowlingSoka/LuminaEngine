#pragma once

#include "Assets/AssetTypes/EntityComponent/EntityComponentType.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Core/Reflection/PropertyBag/PropertyBag.h"
#include "Memory/SmartPtr.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"
#include "imgui.h"

namespace Lumina
{
    class FPropertyTable;
    class FProperty;

    class FEntityComponentTypeEditorTool : public FAssetEditorTool
    {
    public:

        LUMINA_EDITOR_TOOL(FEntityComponentTypeEditorTool)

        FEntityComponentTypeEditorTool(IEditorToolContext* Context, CObject* InAsset);

        bool IsSingleWindowTool() const override { return false; }
        bool ShouldGenerateThumbnailOnSave() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_SHAPE_PLUS; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override {}
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

    private:

        void DrawEditorWindow(bool bFocused);
        void DrawAddFieldRow();
        void DrawFieldDeleteButton(FProperty* Property);

        void OnSchemaChanged();

        struct FTypeChoice
        {
            EBagPropertyType Type = EBagPropertyType::Float;
            FName            TypeName;
            FString          Display;
        };
        void EnsureTypeChoices();

        TUniquePtr<FPropertyTable> PropertyTable;

        TVector<FTypeChoice> TypeChoices;
        bool                 bTypeChoicesBuilt = false;
        int32                SelectedTypeChoice = 0;

        char  NewFieldNameBuffer[128] = "NewField";
        FName PendingRemoveField;
    };
}
