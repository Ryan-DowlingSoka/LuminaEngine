#pragma once
#include "imgui.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/Function.h"
#include "Core/Threading/Atomic.h"

namespace Lumina
{
    class FTreeListView;

    struct RUNTIME_API FTreeNodeID
    {
        int32 Index = -1;

        FORCEINLINE bool IsValid() const { return Index >= 0; }
        FORCEINLINE bool operator==(const FTreeNodeID& Other) const { return Index == Other.Index; }
        FORCEINLINE bool operator!=(const FTreeNodeID& Other) const { return Index != Other.Index; }
    };

    inline constexpr FTreeNodeID InvalidTreeNode{};

    struct RUNTIME_API FTreeNodeDisplay
    {
        FString         DisplayName;
        FString         TooltipText;
        ImVec4          DisplayColor = ImVec4(0.725f, 0.725f, 0.725f, 1.0f);

        uint8           bShowDisabledIcon:1 = false;
		uint8		    bAllowRenaming:1 = false;
    };

    struct RUNTIME_API FTreeNodeState
    {
        uint8 bExpanded:1       = false;
        uint8 bSelected:1       = false;
        uint8 bDisabled:1       = false;
        uint8 bEditingText:1    = false;
    };

    struct RUNTIME_API FTreeListViewContext
    {
        /** Check if the item to draw passes a filter */
        TFunction<bool(FTreeListView&, FTreeNodeID)>                    FilterFunction;

        /** Callback to draw any context menus this item may want */
        TFunction<void(FTreeListView&, FTreeNodeID)>                    ItemContextMenuFunction;

        /** Called when a full rebuild of the widget tree is requested */
        TFunction<void(FTreeListView&)>                                 RebuildTreeFunction;

        /** Called when an item has been selected in the tree */
        TFunction<void(FTreeListView&, FTreeNodeID, bool)>              ItemSelectedFunction;

        /** Called when an item is double-clicked in the tree */
        TFunction<void(FTreeListView&, FTreeNodeID)>                    ItemDoubleClickedFunction;

        /** Called when we have a drag-drop operation on a target */
        TFunction<void(FTreeListView&, FTreeNodeID)>                    DragDropFunction;

        /** Set drag drop payload */
        TFunction<void(FTreeListView&, FTreeNodeID)>                    SetDragDropFunction;

        /** Called when a key is pressed while hovering the tile item, return true to absorb. */
        TFunction<bool(FTreeListView&, FTreeNodeID, ImGuiKey)>          KeyPressedFunction;

        /** Called when the visibility icon is toggled */
        TFunction<void(FTreeListView&, FTreeNodeID)>                    VisibilityToggleFunction;
        
        /** Called when a tree item is hovered */
        TFunction<void(FTreeListView&, FTreeNodeID)>                    HoveredFunction;

        /** Called when an item is being renamed */
        TFunction<void(FTreeListView&, FTreeNodeID, FStringView)>       RenameFunction;

        /**
         * Called once when a node flagged as having lazy children is expanded for the first time.
         * The callback should add the node's children via CreateNode(). After it returns, the
         * children remain in the tree across collapses; they are only rebuilt if the node is
         * marked lazy again or the whole tree is dirtied.
         *
         * The callback should be idempotent: if some children were already added explicitly
         * (e.g. via CreateNode from outside the callback), it should skip them rather than
         * creating duplicates. Explicit CreateNode calls do NOT mark a parent as built.
         */
        TFunction<void(FTreeListView&, FTreeNodeID)>                    BuildChildrenFunction;
    };

    /**
     * Hierarchical list widget backed by a flat node pool.
     *
     * Designed for trees that can be very large (hundreds of thousands of nodes). Drawing iterates
     * a cached flat list of currently-visible rows so the ImGui clipper actually skips off-screen
     * work; the list is rebuilt lazily when nodes are added/removed or expansion state changes.
     *
     * Two construction modes are supported and can be mixed:
     *   1. Eager full rebuild via Context.RebuildTreeFunction (triggered by MarkTreeDirty()).
     *   2. Lazy per-subtree population: mark a node with MarkHasLazyChildren() and provide
     *      Context.BuildChildrenFunction; children are created the first time the node is opened.
     *
     * Note on reference stability: Get<>() and EmplaceUserData<>() return references into a
     * contiguous TVector. Calling CreateNode/RemoveNode/ClearTree may reallocate that storage
     * and invalidate references. Use the references locally and re-fetch after any mutation.
     */
    class RUNTIME_API FTreeListView
    {
    public:

        FTreeListView() = default;
        ~FTreeListView();
        LE_NO_COPYMOVE(FTreeListView);

        void Draw(const FTreeListViewContext& Context);

        // -- Tree mutation --
        FTreeNodeID CreateNode(FTreeNodeID Parent, FStringView Name, uint64 Hash = 0);
        void RemoveNode(FTreeNodeID Handle);
        void ClearTree();

        // -- Dirty state --
        void MarkTreeDirty() { bDirty = true; }
        NODISCARD bool IsDirty() const { return bDirty; }

        /** Force the visible-row cache to rebuild on next Draw. Call after batched mutations. */
        void InvalidateVisibleList() { bVisibleListDirty = true; }

        /**
         * Flag a node as having children that should be created on demand. The first time the
         * node is expanded, BuildChildrenFunction is invoked. Calling CreateNode on the parent
         * before that happens still works; the lazy callback will only fire on the first expand.
         */
        void MarkHasLazyChildren(FTreeNodeID Handle, bool bHasLazy = true);

        NODISCARD bool IsValid(FTreeNodeID Handle) const;
        NODISCARD int32 NumNodes() const { return AliveCount; }

        // -- Per-node component access --
        // Get<FTreeNodeState>, Get<FTreeNodeDisplay>, or Get<UserDataT> (the type previously
        // installed via EmplaceUserData on this node).
        template<typename T>
        T& Get(FTreeNodeID Handle)
        {
            FNode& Node = Nodes[Handle.Index];
            if constexpr (eastl::is_same_v<T, FTreeNodeState>)
            {
                return Node.State;
            }
            else if constexpr (eastl::is_same_v<T, FTreeNodeDisplay>)
            {
                return Node.Display;
            }
            else
            {
                return *static_cast<T*>(Node.UserData);
            }
        }

        template<typename T, typename ... TArgs>
        T& EmplaceUserData(FTreeNodeID Handle, TArgs&&... Args)
        {
            FNode& Node = Nodes[Handle.Index];
            if (Node.UserData != nullptr && Node.UserDataDeleter != nullptr)
            {
                Node.UserDataDeleter(Node.UserData);
            }
            T* NewData = new T(eastl::forward<TArgs>(Args)...);
            Node.UserData = NewData;
            Node.UserDataDeleter = [](void* Ptr) { delete static_cast<T*>(Ptr); };
            return *NewData;
        }

    private:

        struct FNode
        {
            int32                       ParentIdx = -1;
            TVector<int32>              Children;
            int32                       Depth = 0;
            uint64                      Hash = 0;

            FTreeNodeState              State;
            FTreeNodeDisplay            Display;

            void*                       UserData = nullptr;
            void                        (*UserDataDeleter)(void*) = nullptr;

            uint8                       bAlive:1 = false;
            uint8                       bChildrenBuilt:1 = false;
            uint8                       bHasLazyChildren:1 = false;
            uint8                       bWasExpanded:1 = false;
        };

        void DestroyNodeData(int32 Idx);
        void RebuildTreeNow(const FTreeListViewContext& Context);
        void RebuildVisibleList();
        void AppendVisibleSubtree(int32 NodeIdx);
        void DrawSingleRow(int32 NodeIdx, const FTreeListViewContext& Context, bool& bAnyRowExpansionChanged);
        void EnsureChildrenBuilt(int32 NodeIdx, const FTreeListViewContext& Context);
        void SetSelection(FTreeNodeID Item, const FTreeListViewContext& Context, bool bShouldClear);
        bool HandleKeyPressed(const FTreeListViewContext& Context, FTreeNodeID Item, ImGuiKey Key);
        void ClearSelections(const FTreeListViewContext& Context);

        int32 AllocNode();
        void RemoveSubtree(int32 RootIdx);
        void UnlinkFromParent(int32 Idx);
        void RecomputeDepthsRecursive(int32 NodeIdx, int32 Depth);

    private:

        TVector<FNode>      Nodes;
        TVector<int32>      FreeList;
        TVector<int32>      Roots;

        TVector<int32>      VisibleList;     // depth-first list of currently visible row indices
        int32               AliveCount = 0;

        bool                bVisibleListDirty = true;
        bool                bDirty = false;
        TAtomic<bool>       bRebuilding = false;
    };
}
