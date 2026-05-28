#pragma once
#include "UI/Tools/EditorTool.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "GUID/GUID.h"

namespace Lumina
{
    class CObject;

    // Dockable replacement for the old Asset Registry modal. Lists every known asset grouped by
    // class, shows loaded/unloaded state, live strong ref-count, estimated CPU footprint, on-disk
    // size, and (for the selected asset) which loaded objects reference it.
    class FAssetRegistryEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FAssetRegistryEditorTool)

        FAssetRegistryEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Asset Registry", nullptr)
        {}

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_DATABASE; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

    private:

        // One row of resolved per-asset state, rebuilt each frame from the registry.
        struct FAssetRow
        {
            FGuid           GUID;
            FName           Name;
            FName           Class;
            FFixedString    Path;
            CObject*        Loaded      = nullptr;
            int32           RefCount    = 0;
            uint64          CpuBytes    = 0;
            uint64          DiskBytes   = 0;
        };

        // A loaded object that holds a reference to the selected asset.
        struct FReferencer
        {
            FName   Name;
            FName   Class;
            FName   Package;
        };

        void DrawWindow(bool bIsFocused);
        void DrawStatsBar(const TVector<FAssetRow>& Rows);
        void DrawFilterBar();
        void DrawAssetTable(const TVector<FAssetRow>& Rows);
        void DrawDetailsPanel(const TVector<FAssetRow>& Rows);
        void DrawAssetTableRows(const TVector<const FAssetRow*>& Rows);

        // Sums approximate CPU-side bulk data held by a loaded asset (textures/meshes/materials).
        static uint64 EstimateCpuBytes(CObject* Asset);

        // Walks every live object's reflected references; records those pointing at Target.
        void RebuildReferencers(CObject* Target);

        bool PassesFilter(const FAssetRow& Row) const;

        FGuid           SelectedGUID;
        FString         SearchFilter;
        char            SearchBuffer[256] = {};
        FString         CategoryFilter;                 // empty = all categories
        bool            bShowLoadedOnly   = false;
        bool            bGroupByCategory  = true;

        // Disk sizes hit the filesystem, so cache them; cleared by Refresh.
        THashMap<FGuid, uint64> DiskSizeCache;

        // Referencer list is computed on selection change (and Refresh), not per-frame.
        FGuid               CachedReferencerTarget;
        TVector<FReferencer> Referencers;
    };
}
