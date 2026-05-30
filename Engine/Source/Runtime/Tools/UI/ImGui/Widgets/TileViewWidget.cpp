#include "pch.h"
#include "TileViewWidget.h"

#include "Tools/UI/ImGui/ImGuiFonts.h"

namespace Lumina
{
    namespace
    {
        constexpr float GTileSpacing  = 5.0f;   // gap between cells, horizontal and vertical
        constexpr float GLabelGap     = 4.0f;   // gap between the icon button and its label
        constexpr float GLabelHeight  = 36.0f;  // fixed label band; keeps every row the same height
        constexpr float GColumnBudget = 8.0f;   // slack for the button's frame padding when packing columns
    }

    void FTileViewWidget::Draw(const FTileViewContext& Context)
    {
        // Rebuild lazily, then fall through and draw the fresh tree the same frame (no blank frame).
        if (bDirty)
        {
            RebuildTree(Context);
        }

        const int ItemCount = (int)ListItems.size();
        if (ItemCount == 0)
        {
            return;
        }

        const float PaneWidth   = ImGui::GetContentRegionAvail().x;
        const float CellWidth   = TileSize + GColumnBudget + GTileSpacing;
        const int   ItemsPerRow = std::max(1, (int)((PaneWidth + GTileSpacing) / CellWidth));
        const int   RowCount    = (ItemCount + ItemsPerRow - 1) / ItemsPerRow;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(GTileSpacing, GTileSpacing));

        // Virtualize by row: the clipper measures one row's height and only submits visible rows,
        // so a folder with thousands of files costs the same as one screenful.
        ImGuiListClipper Clipper;
        Clipper.Begin(RowCount);
        while (Clipper.Step())
        {
            for (int Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
            {
                const int RowBegin = Row * ItemsPerRow;
                const int RowEnd   = std::min(RowBegin + ItemsPerRow, ItemCount);

                for (int Index = RowBegin; Index < RowEnd; ++Index)
                {
                    if (Index > RowBegin)
                    {
                        ImGui::SameLine();
                    }
                    DrawTile(ListItems[Index], Context);
                }
            }
        }
        Clipper.End();

        ImGui::PopStyleVar();
    }

    void FTileViewWidget::DrawTile(FTileViewItem* Item, const FTileViewContext& Context)
    {
        ImGui::PushID(Item);
        ImGui::BeginGroup();

        DrawItem(Item, Context, ImVec2(TileSize, TileSize));

        // Draw the label as a raw draw-list primitive (not an ImGui item) so the cell's logical
        // height stays fixed regardless of name length, keeping the row clipper aligned.
        const FStringView Name = Item->GetCachedDisplayName();
        const char* NameBegin  = Name.data();
        const char* NameEnd    = Name.data() + Name.size();

        ImGuiX::Font::PushFont(ImGuiX::Font::EFont::SmallBold);
        ImFont*      LabelFont = ImGui::GetFont();
        const float  FontSize  = ImGui::GetFontSize();
        const ImVec2 TextSize  = ImGui::CalcTextSize(NameBegin, NameEnd, false, TileSize);
        ImGuiX::Font::PopFont();

        ImGui::Dummy(ImVec2(0.0f, GLabelGap));

        const ImVec2 LabelPos = ImGui::GetCursorScreenPos();
        const float  TextX    = LabelPos.x + (TileSize - std::min(TextSize.x, TileSize)) * 0.5f;
        const ImVec4 ClipRect(LabelPos.x, LabelPos.y, LabelPos.x + TileSize, LabelPos.y + GLabelHeight);

        ImGui::GetWindowDrawList()->AddText(LabelFont, FontSize, ImVec2(TextX, LabelPos.y),
            ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 1.0f)), NameBegin, NameEnd, TileSize, &ClipRect);

        // Reserve the fixed label band so the group (and thus every row) has a uniform height.
        ImGui::Dummy(ImVec2(TileSize, GLabelHeight));

        ImGui::EndGroup();
        ImGui::PopID();
    }
    
    void FTileViewWidget::ClearTree()
    {
        Allocator.Reset();
        ListItems.clear();
    }

    bool FTileViewWidget::HandleKeyPressed(const FTileViewContext& Context, FTileViewItem& Item, ImGuiKey Key)
    {
        if (Context.KeyPressedFunction)
        {
            return Context.KeyPressedFunction(Item, Key);
        }

        return false;
    }

    void FTileViewWidget::RebuildTree(const FTileViewContext& Context, bool bKeepSelections)
    {
        ASSERT(bDirty);

        TVector<FTileViewItem*> CachedSelections = Selections;
        
        ClearSelections();
        ClearTree();

        if (bKeepSelections)
        {
            for (FTileViewItem* Select : CachedSelections)
            {
                ToggleSelection(Select, Context);
            }
        }
        
        Context.RebuildTreeFunction(this);

        bDirty = false;
    }

    void FTileViewWidget::DrawItem(FTileViewItem* ItemToDraw, const FTileViewContext& Context, ImVec2 DrawSize)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        
        
        if (Context.DrawItemOverrideFunction)
        {
            FTileViewItem::EClickState ClickState = Context.DrawItemOverrideFunction(ItemToDraw);
            if (ClickState == FTileViewItem::EClickState::SingleWithCtrl)
            {
                ToggleSelection(ItemToDraw, Context);
            }
            else if (ClickState == FTileViewItem::EClickState::Single)
            {
                ClearSelections();
                ToggleSelection(ItemToDraw, Context);
            }
            else if (ClickState == FTileViewItem::EClickState::Double)
            {
                if (Context.ItemDoubleClickedFunction)
                {
                    Context.ItemDoubleClickedFunction(ItemToDraw);
                }
            }
        }
        else
        {
            if (ImGui::Button("##", DrawSize))
            {
                ClearSelections();
                ToggleSelection(ItemToDraw, Context);
            }
        }
        
        ImGui::PopStyleVar(2);
    
        if (ImGui::BeginItemTooltip())
        {
            ItemToDraw->DrawTooltip();
            ImGui::EndTooltip();
        }
        
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ItemToDraw->HasContextMenu())
        {
            ImGui::OpenPopup("ItemContextMenu");
        }

        if (ImGui::IsItemHovered())
        {
            for (int Key = ImGuiKey_NamedKey_BEGIN; Key < ImGuiKey_NamedKey_END; Key++)
            {
                if (ImGui::IsKeyPressed((ImGuiKey)Key))
                {
                    if (HandleKeyPressed(Context, *ItemToDraw, (ImGuiKey)Key))
                    {
                        break;
                    }
                }
            }
        }
        
        if (ImGui::BeginDragDropSource())
        {
            ItemToDraw->SetDragDropPayloadData();
            if (Context.DrawItemOverrideFunction)
            {
                Context.DrawItemOverrideFunction(ItemToDraw);
            }
            ImGui::EndDragDropSource();
        }
    
        if (ImGui::BeginDragDropTarget())
        {
            if (Context.DragDropFunction)
            {
                Context.DragDropFunction(ItemToDraw, Selections);
            }
            
            ImGui::EndDragDropTarget();
        }
        
        if (ItemToDraw->HasContextMenu())
        {
            if (ImGui::BeginPopupContextItem("ItemContextMenu"))
            {
                TVector<FTileViewItem*> SelectionsToDraw;
                SelectionsToDraw.push_back(ItemToDraw);
                Context.DrawItemContextMenuFunction(SelectionsToDraw);
                
                ImGui::EndPopup();
            }
        }
    }

    void FTileViewWidget::ToggleSelection(FTileViewItem* Item, const FTileViewContext& Context)
    {
        bool bWasSelected = Item->bSelected;
        
        if (!bWasSelected)
        {
            DEBUG_ASSERT(eastl::find(Selections.begin(), Selections.end(), Item) == Selections.end());
            Selections.push_back(Item);
            Context.ItemSelectedFunction(Item);
            Item->bSelected = true;
        }
        else
        {
            auto It = eastl::remove(Selections.begin(), Selections.end(), Item);
            Selections.erase(It);
            Item->bSelected = false;
        }

        Item->OnSelectionStateChanged();
    }

    void FTileViewWidget::ClearSelections()
    {
        for (FTileViewItem* Item : Selections)
        {
            ASSERT(Item->bSelected);
            
            Item->bSelected = false;
            Item->OnSelectionStateChanged();    
        }

        Selections.clear();
    }
}
