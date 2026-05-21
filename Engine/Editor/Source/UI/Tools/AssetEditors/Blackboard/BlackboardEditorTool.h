#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"
#include "imgui.h"

namespace Lumina
{
    class CEnum;

    // Editor for CBlackboard assets: a polished list editor for the blackboard's
    // typed keys (name / type / default), including reflected-enum keys whose
    // type is picked from the engine's CEnum registry. No 3D preview -- it is
    // pure schema authoring, so the tool runs without a world.
    class FBlackboardEditorTool : public FAssetEditorTool
    {
    public:

        LUMINA_EDITOR_TOOL(FBlackboardEditorTool)

        FBlackboardEditorTool(IEditorToolContext* Context, CObject* InAsset);

        bool IsSingleWindowTool() const override { return false; }
        bool ShouldGenerateThumbnailOnSave() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_FORMAT_LIST_BULLETED; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override {}
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

    private:

        void DrawKeysWindow();
        void DrawDefaultEditor(struct FBlackboardKey& Key);
        void DrawEnumTypeCombo(struct FBlackboardKey& Key);

        // Reflected-enum discovery, gathered once from the object array.
        void EnsureEnumCacheBuilt();
        CEnum* FindEnum(const FName& Name) const;

        TVector<CEnum*>          ReflectedEnums;
        THashMap<FName, CEnum*>  EnumsByName;
        bool                     bEnumCacheBuilt = false;
    };
}
