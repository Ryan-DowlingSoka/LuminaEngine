#pragma once
#include "imgui.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Memory/Memory.h"
#include "Memory/Allocators/Allocator.h"

namespace Lumina
{

    class RUNTIME_API FTileViewItem
    {
        friend class FTileViewWidget;
    public:
        
        enum class EClickState : uint8
        {
            None,
            Single,
            SingleWithCtrl,
            Double,
        };
        

        FTileViewItem(FTileViewItem* InParent)
            : bExpanded(false)
            , bVisible(false)
            , bSelected(false)
            , bDisplayNameCached(false)
        {}

        virtual ~FTileViewItem() = default;
        LE_NO_COPYMOVE(FTileViewItem);

        virtual FStringView GetName() const { return {}; }
        
        virtual void DrawTooltip() const { }

        virtual bool HasContextMenu() { return false; }

        virtual ImVec4 GetDisplayColor() const { return {};  }

        virtual void OnSelectionStateChanged() { }

        virtual void SetDragDropPayloadData() const { }
        
        virtual FFixedString GetDisplayName() const
        {
            return { GetName().begin(), GetName().end() };
        }

        // Display name resolved once on first draw, then reused. GetName()/GetDisplayName() may
        // parse paths and allocate, so they must never run per-frame in the draw loop.
        FStringView GetCachedDisplayName()
        {
            if (!bDisplayNameCached)
            {
                CachedDisplayName = GetDisplayName();
                bDisplayNameCached = true;
            }
            return FStringView(CachedDisplayName.data(), CachedDisplayName.size());
        }

        bool IsSelected() const { return bSelected; }

    protected:

        FFixedString                CachedDisplayName;

        uint8                       bExpanded:1;
        uint8                       bVisible:1;
        uint8                       bSelected:1;
        uint8                       bDisplayNameCached:1;

    };

    struct RUNTIME_API FTileViewContext
    {
        /** Callback to draw any context menus this item may want */
        TFunction<void(const TVector<FTileViewItem*>&)>         DrawItemContextMenuFunction;

        /** Callback to override item drawing */
        TFunction<FTileViewItem::EClickState(FTileViewItem*)>  DrawItemOverrideFunction;

        /** Called when a rebuild of the widget tree is requested */
        TFunction<void(FTileViewWidget*)>                       RebuildTreeFunction;

        /** Called when an item has been selected in the tree */
        TFunction<void(FTileViewItem*)>                         ItemSelectedFunction;
        
        /** Called when an item has been double-clicked. */
        TFunction<void(FTileViewItem*)>                         ItemDoubleClickedFunction;

        /** Called when we have a drag-drop operation on a target */
        TFunction<void(FTileViewItem*, const TVector<FTileViewItem*>&)>         DragDropFunction;

        /** Called when a key is pressed while hovering the tile item, return true to absorb. */
        TFunction<bool(FTileViewItem&, ImGuiKey)>               KeyPressedFunction;

    };

    class RUNTIME_API FTileViewWidget
    {
    public:
        
        FTileViewWidget() = default;
        ~FTileViewWidget() = default;
        LE_NO_COPYMOVE(FTileViewWidget);

        void Draw(const FTileViewContext& Context);
        void ClearTree();
        void MarkTreeDirty() { bDirty = true; }

        FORCEINLINE bool IsDirty() const { return bDirty; }
        
        template<typename T, typename... Args>
        requires (eastl::is_base_of_v<FTileViewItem, T> && eastl::is_constructible_v<T, Args...>)
        void AddItemToTree(Args&&... args);

        void ClearSelections();
        
        const TVector<FTileViewItem*>& GetSelections() const { return Selections; }

		float GetTileSize() const { return TileSize; }
		void SetTileSize(float Size) { TileSize = Size; }

    private:

        bool HandleKeyPressed(const FTileViewContext& Context, FTileViewItem& Item, ImGuiKey Key);
        
        void RebuildTree(const FTileViewContext& Context, bool bKeepSelections = false);

        // Draws one full cell (icon button + label) as a single group for SameLine layout.
        void DrawTile(FTileViewItem* Item, const FTileViewContext& Context);

        void DrawItem(FTileViewItem* ItemToDraw, const FTileViewContext& Context, ImVec2 DrawSize);

        void ToggleSelection(FTileViewItem* Item, const FTileViewContext& Context);
    

    private:

        FBlockLinearAllocator                   Allocator;

        TVector<FTileViewItem*>                 Selections;

        /** Root nodes */
        TVector<FTileViewItem*>                 ListItems;
        
		float                                   TileSize = 84.0f;

        uint8                                   bDirty:1 = false;
    };

    
    
    
    template <typename T, typename ... Args> 
    requires (eastl::is_base_of_v<FTileViewItem, T> && eastl::is_constructible_v<T, Args...>)
    void FTileViewWidget::AddItemToTree(Args&&... args)
    {
        T* New = Allocator.TAlloc<T>(eastl::forward<Args>(args)...);
        ListItems.push_back(New);
    }
}
