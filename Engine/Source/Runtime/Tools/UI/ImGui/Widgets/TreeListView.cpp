#include "pch.h"
#include "TreeListView.h"
#include "imgui.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{
    namespace
    {
        constexpr float kRowHeight = 15.0f;
        constexpr float kIndentPerDepth = 21.0f;
    }

    FTreeListView::~FTreeListView()
    {
        for (FNode& Node : Nodes)
        {
            if (Node.bAlive && Node.UserData != nullptr && Node.UserDataDeleter != nullptr)
            {
                Node.UserDataDeleter(Node.UserData);
                Node.UserData = nullptr;
            }
        }
    }

    void FTreeListView::Draw(const FTreeListViewContext& Context)
    {
        LUMINA_PROFILE_SCOPE();

        if (bDirty)
        {
            RebuildTreeNow(Context);
        }

        if (bVisibleListDirty)
        {
            RebuildVisibleList();
        }

        ImGuiTableFlags TableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_BordersV;

        bool bAnyRowExpansionChanged = false;

        ImGui::PushID(this);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2, 2));
        if (ImGui::BeginTable("TreeViewTable", 1, TableFlags))
        {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);

            ImGuiListClipper Clipper;
            Clipper.Begin(static_cast<int>(VisibleList.size()));
            while (Clipper.Step())
            {
                for (int i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i)
                {
                    int32 NodeIdx = VisibleList[i];

                    if (Context.FilterFunction)
                    {
                        if (!Context.FilterFunction(*this, FTreeNodeID{NodeIdx}))
                        {
                            continue;
                        }
                    }

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    DrawSingleRow(NodeIdx, Context, bAnyRowExpansionChanged);
                }
            }

            ImGui::EndTable();
        }

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        {
            ClearSelections(Context);
        }

        ImGui::PopStyleVar();
        ImGui::PopID();

        if (bAnyRowExpansionChanged)
        {
            bVisibleListDirty = true;
        }
    }

    void FTreeListView::ClearTree()
    {
        for (FNode& Node : Nodes)
        {
            if (Node.bAlive && Node.UserData != nullptr && Node.UserDataDeleter != nullptr)
            {
                Node.UserDataDeleter(Node.UserData);
            }
        }
        Nodes.clear();
        FreeList.clear();
        Roots.clear();
        VisibleList.clear();
        AliveCount = 0;
        bVisibleListDirty = true;
    }

    void FTreeListView::RebuildTreeNow(const FTreeListViewContext& Context)
    {
        DEBUG_ASSERT(Context.RebuildTreeFunction);
        DEBUG_ASSERT(bDirty);

        // Snapshot expansion state by hash so users keep their open subtrees across rebuilds.
        THashSet<uint64> CachedExpandedItems;
        CachedExpandedItems.reserve(AliveCount);
        for (const FNode& Node : Nodes)
        {
            if (Node.bAlive && Node.State.bExpanded)
            {
                CachedExpandedItems.emplace(Node.Hash);
            }
        }

        // Do NOT call ClearSelections here. It fires ItemSelectedFunction with InvalidTreeNode,
        // which clients treat as "user cleared selection" and propagate to their canonical state
        // (e.g. registry tags). A structural rebuild is unrelated to user intent — the rebuild
        // path is expected to re-apply bSelected from the client's own source of truth.
        ClearTree();

        Context.RebuildTreeFunction(*this);

        for (FNode& Node : Nodes)
        {
            if (Node.bAlive && CachedExpandedItems.find(Node.Hash) != CachedExpandedItems.end())
            {
                Node.State.bExpanded = true;
            }
        }

        bDirty = false;
        bVisibleListDirty = true;
    }

    void FTreeListView::RebuildVisibleList()
    {
        LUMINA_PROFILE_SCOPE();

        VisibleList.clear();
        VisibleList.reserve(AliveCount);

        for (int32 RootIdx : Roots)
        {
            AppendVisibleSubtree(RootIdx);
        }

        bVisibleListDirty = false;
    }

    void FTreeListView::AppendVisibleSubtree(int32 NodeIdx)
    {
        VisibleList.push_back(NodeIdx);
        const FNode& Node = Nodes[NodeIdx];
        if (!Node.State.bExpanded)
        {
            return;
        }

        // Snapshot the children indices because callbacks during draw aren't supposed to mutate
        // the tree, but defensively iterate by index to avoid iterator-invalidation bugs.
        const int32 NumChildren = static_cast<int32>(Node.Children.size());
        for (int32 i = 0; i < NumChildren; ++i)
        {
            AppendVisibleSubtree(Nodes[NodeIdx].Children[i]);
        }
    }

    void FTreeListView::DrawSingleRow(int32 NodeIdx, const FTreeListViewContext& Context, bool& bAnyRowExpansionChanged)
    {
        FNode& Node = Nodes[NodeIdx];
        FTreeNodeDisplay& Display = Node.Display;
        FTreeNodeState& State = Node.State;
        const bool bDeclaresChildren = !Node.Children.empty() || Node.bHasLazyChildren;

        ImGui::PushID(NodeIdx);

        if (Node.Depth > 0)
        {
            ImGui::Indent(Node.Depth * kIndentPerDepth);
        }

        ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_DrawLinesFull
            | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        if (!bDeclaresChildren)
        {
            Flags |= ImGuiTreeNodeFlags_Leaf;
        }
        else
        {
            ImGui::SetNextItemOpen(State.bExpanded);
            Flags |= ImGuiTreeNodeFlags_OpenOnArrow;
        }

        if (State.bSelected)
        {
            Flags |= ImGuiTreeNodeFlags_Selected;
        }

        ImVec4 TextColor = Display.DisplayColor;
        if (State.bDisabled)
        {
            TextColor.w *= 0.4f;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, TextColor);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.52f, 0.22f, 0.40f));

        const bool bNowExpanded = ImGui::TreeNodeEx("##TreeNode", Flags, "%s", Display.DisplayName.c_str());

        const bool bTreeNodeDoubleClicked = ImGui::IsItemHovered()
            && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
            && !ImGui::IsItemToggledOpen();

        if (bDeclaresChildren && bNowExpanded != State.bExpanded)
        {
            State.bExpanded = bNowExpanded;
            bAnyRowExpansionChanged = true;

            if (bNowExpanded && Node.bHasLazyChildren && !Node.bChildrenBuilt)
            {
                EnsureChildrenBuilt(NodeIdx, Context);
            }
        }

        if (State.bEditingText && Display.bAllowRenaming)
        {
            ImVec2 ItemMin = ImGui::GetItemRectMin();
            ImVec2 ItemMax = ImGui::GetItemRectMax();

            float ArrowWidth = ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(ImVec2(ItemMin.x + ArrowWidth, ItemMin.y));

            ImGui::SetNextItemWidth(ItemMax.x - ItemMin.x - ArrowWidth);
            ImGui::SetKeyboardFocusHere();

            static char EditBuffer[256];

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
            if (ImGui::InputText("##EditLabel", EditBuffer, sizeof(EditBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                State.bEditingText = false;
                if (Context.RenameFunction)
                {
                    Context.RenameFunction(*this, FTreeNodeID{NodeIdx}, EditBuffer);
                }

				EditBuffer[0] = '\0';
            }
            ImGui::PopStyleColor();

            if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                (!ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
            {
                State.bEditingText = false;
            }
        }

        ImGui::PopStyleColor(); // text color

        bool bTreeNodeClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen();

        if (ImGui::IsItemHovered())
        {
            if (Context.HoveredFunction)
            {
                Context.HoveredFunction(*this, FTreeNodeID{NodeIdx});
            }
            
            if (ImGui::IsKeyDown(ImGuiKey_F2) && Display.bAllowRenaming)
            {
				State.bEditingText = true;
            }

            for (int Key = ImGuiKey_NamedKey_BEGIN; Key < ImGuiKey_NamedKey_END; Key++)
            {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(Key)))
                {
                    if (HandleKeyPressed(Context, FTreeNodeID{NodeIdx}, (ImGuiKey)Key))
                    {
                        break;
                    }
                }
            }
        }

        if (ImGui::BeginDragDropSource())
        {
            if (Context.SetDragDropFunction)
            {
                Context.SetDragDropFunction(*this, FTreeNodeID{NodeIdx});
            }

            ImGui::TextUnformatted(Display.DisplayName.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (Context.DragDropFunction)
            {
                Context.DragDropFunction(*this, FTreeNodeID{NodeIdx});
            }

            ImGui::EndDragDropTarget();
        }

        ImGuiX::TextTooltip("{}", Display.TooltipText);

        if (Context.ItemContextMenuFunction)
        {
            if (ImGui::BeginPopupContextItem("ItemContextMenu"))
            {
                Context.ItemContextMenuFunction(*this, FTreeNodeID{NodeIdx});
                ImGui::EndPopup();
            }
        }

        bool bMouseOverVisibilityButton = false;
        if (Display.bShowDisabledIcon)
        {
            ImGui::SameLine();
            float AvailableWidth = ImGui::GetContentRegionAvail().x;
            float ButtonWidth = 40.0f;

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + AvailableWidth - ButtonWidth + ImGui::GetStyle().FramePadding.x);

            const char* Icon = State.bDisabled ? LE_ICON_EYE_OFF : LE_ICON_EYE;
            bool bSmallButtonClicked = ImGui::SmallButton(Icon);
			bMouseOverVisibilityButton = ImGui::IsItemHovered();

            if(bSmallButtonClicked)
            {
                State.bDisabled = ~State.bDisabled;

                // Cascade disabled state down the subtree iteratively.
                TVector<int32> Stack(Nodes[NodeIdx].Children.begin(), Nodes[NodeIdx].Children.end());
                while (!Stack.empty())
                {
                    int32 Current = Stack.back();
                    Stack.pop_back();

                    Nodes[Current].State.bDisabled = State.bDisabled;
                    for (int32 Grandchild : Nodes[Current].Children)
                    {
                        Stack.push_back(Grandchild);
                    }
                }

                if (Context.VisibilityToggleFunction)
                {
                    Context.VisibilityToggleFunction(*this, FTreeNodeID{NodeIdx});
                }
            }
        }

        if (bTreeNodeClicked && !bMouseOverVisibilityButton)
        {
            const bool bShift = ImGui::GetIO().KeyShift;
            const bool bCtrl  = ImGui::GetIO().KeyCtrl;
            (void)bShift;
            SetSelection(FTreeNodeID{NodeIdx}, Context, !bCtrl);
		}

        if (bTreeNodeDoubleClicked && !bMouseOverVisibilityButton && Context.ItemDoubleClickedFunction)
        {
            Context.ItemDoubleClickedFunction(*this, FTreeNodeID{NodeIdx});
        }

        ImGui::PopStyleColor(); // header color

        if (Node.Depth > 0)
        {
            ImGui::Unindent(Node.Depth * kIndentPerDepth);
        }

        ImGui::PopID();
    }

    void FTreeListView::EnsureChildrenBuilt(int32 NodeIdx, const FTreeListViewContext& Context)
    {
        if (!Context.BuildChildrenFunction)
        {
            Nodes[NodeIdx].bChildrenBuilt = true;
            return;
        }

        Context.BuildChildrenFunction(*this, FTreeNodeID{NodeIdx});
        Nodes[NodeIdx].bChildrenBuilt = true;
    }

    int32 FTreeListView::AllocNode()
    {
        int32 Idx;
        if (!FreeList.empty())
        {
            Idx = FreeList.back();
            FreeList.pop_back();
            Nodes[Idx] = FNode{};
        }
        else
        {
            Idx = static_cast<int32>(Nodes.size());
            Nodes.emplace_back();
        }
        Nodes[Idx].bAlive = true;
        ++AliveCount;
        return Idx;
    }

    FTreeNodeID FTreeListView::CreateNode(FTreeNodeID Parent, FStringView Name, uint64 Hash)
    {
        const int32 Idx = AllocNode();

        // Compute hash before any further mutation so we don't hold a reference across grow.
        const uint64 NodeHash = (Hash == 0) ? Hash::GetHash64(Name.data(), Name.length()) : Hash;

        bool bShouldBeDisabled = false;
        int32 ParentDepth = -1;
        if (Parent.IsValid() && Parent.Index < static_cast<int32>(Nodes.size()) && Nodes[Parent.Index].bAlive)
        {
            ParentDepth = Nodes[Parent.Index].Depth;
            bShouldBeDisabled = Nodes[Parent.Index].State.bDisabled;
            Nodes[Parent.Index].Children.push_back(Idx);
            // Note: we deliberately do NOT set bChildrenBuilt here. Callers can mix explicit
            // CreateNode calls with lazy BuildChildrenFunction; the lazy callback is responsible
            // for being idempotent (skipping already-present children) if both are used.
        }
        else
        {
            Roots.push_back(Idx);
        }

        FNode& Node = Nodes[Idx];
        Node.Hash = NodeHash;
        Node.ParentIdx = (ParentDepth >= 0) ? Parent.Index : -1;
        Node.Depth = ParentDepth + 1;
        Node.Display.DisplayName.assign(Name.data(), Name.length());
        Node.Display.TooltipText.assign(Name.data(), Name.length());
        Node.State.bDisabled = bShouldBeDisabled;

        bVisibleListDirty = true;
        return FTreeNodeID{Idx};
    }

    void FTreeListView::RemoveNode(FTreeNodeID Handle)
    {
        if (!IsValid(Handle))
        {
            return;
        }

        UnlinkFromParent(Handle.Index);
        RemoveSubtree(Handle.Index);

        bVisibleListDirty = true;
    }

    void FTreeListView::UnlinkFromParent(int32 Idx)
    {
        const int32 ParentIdx = Nodes[Idx].ParentIdx;
        if (ParentIdx >= 0)
        {
            auto& Children = Nodes[ParentIdx].Children;
            Children.erase(eastl::remove(Children.begin(), Children.end(), Idx), Children.end());
        }
        else
        {
            Roots.erase(eastl::remove(Roots.begin(), Roots.end(), Idx), Roots.end());
        }
    }

    void FTreeListView::RemoveSubtree(int32 RootIdx)
    {
        TVector<int32> Stack;
        Stack.push_back(RootIdx);
        while (!Stack.empty())
        {
            const int32 Cur = Stack.back();
            Stack.pop_back();

            // Push children first so we can free them too.
            for (int32 Child : Nodes[Cur].Children)
            {
                Stack.push_back(Child);
            }

            DestroyNodeData(Cur);
        }
    }

    void FTreeListView::DestroyNodeData(int32 Idx)
    {
        FNode& Node = Nodes[Idx];
        if (Node.UserData != nullptr && Node.UserDataDeleter != nullptr)
        {
            Node.UserDataDeleter(Node.UserData);
            Node.UserData = nullptr;
            Node.UserDataDeleter = nullptr;
        }
        Node.Children.clear();
        Node.bAlive = false;
        Node.bChildrenBuilt = false;
        Node.bHasLazyChildren = false;
        FreeList.push_back(Idx);
        --AliveCount;
    }

    void FTreeListView::MarkHasLazyChildren(FTreeNodeID Handle, bool bHasLazy)
    {
        if (!IsValid(Handle))
        {
            return;
        }
        FNode& Node = Nodes[Handle.Index];
        Node.bHasLazyChildren = bHasLazy;
        if (bHasLazy)
        {
            Node.bChildrenBuilt = false;
        }
    }

    bool FTreeListView::IsValid(FTreeNodeID Handle) const
    {
        return Handle.IsValid()
            && Handle.Index < static_cast<int32>(Nodes.size())
            && Nodes[Handle.Index].bAlive;
    }

    void FTreeListView::SetSelection(FTreeNodeID Item, const FTreeListViewContext& Context, bool bShouldClear)
    {
        // bShouldClear == true is the plain-click path: replace the selection with this one row.
        // bShouldClear == false is the Ctrl-click (multi-select) path: toggle just this row's
        // selected state and leave any other selected rows alone. Single-select consumers don't
        // care: they ignore bShouldClear and treat every callback as "this is now the selection".
        if (bShouldClear)
        {
            for (FNode& Node : Nodes)
            {
                if (Node.bAlive)
                {
                    Node.State.bSelected = false;
                }
            }

            if (IsValid(Item))
            {
                Nodes[Item.Index].State.bSelected = true;
            }
        }
        else if (IsValid(Item))
        {
            Nodes[Item.Index].State.bSelected = !Nodes[Item.Index].State.bSelected;
        }

        if (Context.ItemSelectedFunction)
        {
            Context.ItemSelectedFunction(*this, Item, bShouldClear);
        }
    }

    bool FTreeListView::HandleKeyPressed(const FTreeListViewContext& Context, FTreeNodeID Item, ImGuiKey Key)
    {
        if (Context.KeyPressedFunction)
        {
            return Context.KeyPressedFunction(*this, Item, Key);
        }

        return false;
    }

    void FTreeListView::ClearSelections(const FTreeListViewContext& Context)
    {
        for (FNode& Node : Nodes)
        {
            if (Node.bAlive)
            {
                Node.State.bSelected = false;
            }
        }

        // Notify the client so any external selection state stays in lockstep with the
        // visual state. Single-select consumers can ignore the InvalidTreeNode callback;
        // multi-select consumers (e.g. the world outliner) clear their canonical set here.
        if (Context.ItemSelectedFunction)
        {
            Context.ItemSelectedFunction(*this, InvalidTreeNode, true);
        }
    }

    void FTreeListView::RecomputeDepthsRecursive(int32 NodeIdx, int32 Depth)
    {
        Nodes[NodeIdx].Depth = Depth;
        for (int32 Child : Nodes[NodeIdx].Children)
        {
            RecomputeDepthsRecursive(Child, Depth + 1);
        }
    }
}
