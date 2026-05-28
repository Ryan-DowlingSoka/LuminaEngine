#include "pch.h"
#include "ImGuiX.h"
#include <filesystem>
#include "ImGuiDesignIcons.h"
#include "ImGuiRenderer.h"
#include "imgui_internal.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Core/Application/Application.h"
#include "Core/Engine/Engine.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Core/Windows/Window.h"
#include "Paths/Paths.h"
#include "Renderer/RenderManager.h"

namespace Lumina::ImGuiX
{
    void TextTooltip_Internal(FStringView String)
    {
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
        	if (!ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_None))
        	{
        		return;
        	}
        	
        	ImGui::TextUnformatted(String.data());
        	ImGui::EndTooltip();
        }
        ImGui::PopStyleVar();
    }

    void WrappedTooltip_Internal(FStringView String)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            if (ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_None))
            {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(String.data(), String.data() + String.size());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        ImGui::PopStyleVar();
    }

    void HelpMarker(FStringView Help)
    {
        HelpMarkerIcon("(?)", Help);
    }

    void HelpMarkerIcon(const char* Icon, FStringView Help)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(Icon);
        ImGui::PopStyleColor();
        WrappedTooltip_Internal(Help);
    }

    void TextColoredUnformatted(const ImVec4& Color, const FFixedString& String)
    {
    	ImGui::PushStyleColor(ImGuiCol_Text, Color);
    	ImGui::TextUnformatted(String.c_str());
    	ImGui::PopStyleColor();
    }

    void TextUnformatted(FStringView String)
    {
    	ImGui::TextUnformatted(String.begin(), String.end());
    }

    FStringView ImGuizmoOpToString(ImGuizmo::OPERATION Op)
    {
    	switch (Op)
    	{
    		case ImGuizmo::OPERATION::TRANSLATE:	return "Translate";
    		case ImGuizmo::OPERATION::ROTATE:		return "Rotate";
    		case ImGuizmo::OPERATION::SCALE:		return "Scale";
    	}
    	
    	return "";
    }

    bool ButtonEx(char const* pIcon, char const* pLabel, ImVec2 const& size, const ImColor& backgroundColor, const ImColor& iconColor, const ImColor& foregroundColor, bool shouldCenterContents)
    {
    	// Taken from Esoterica.
    	
    	bool wasPressed = false;

        ImU32 HoveredColor = ImGui::ColorConvertFloat4ToU32(backgroundColor.Value * 1.15f);
        ImU32 ActiveColor  = ImGui::ColorConvertFloat4ToU32(backgroundColor.Value * 1.25f);

        //-------------------------------------------------------------------------

        if ( pIcon == nullptr || strlen( pIcon ) == 0 )
        {
            ImGui::PushStyleColor( ImGuiCol_Button, (ImVec4) backgroundColor );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, HoveredColor );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, ActiveColor );
            ImGui::PushStyleColor( ImGuiCol_Text, (ImVec4) foregroundColor );
            ImGui::PushStyleVar( ImGuiStyleVar_ButtonTextAlign, shouldCenterContents ? ImVec2( 0.5f, 0.5f ) : ImVec2( 0.0f, 0.5f ) );
            wasPressed = ImGui::Button( pLabel, size );
            ImGui::PopStyleColor( 4 );
            ImGui::PopStyleVar();
        }
        else // Icon button
        {
            ImGuiContext& g = *GImGui;

            ImGuiWindow* pWindow = ImGui::GetCurrentWindow();
            if ( pWindow->SkipItems )
            {
                return false;
            }

            // Calculate ID
            //-------------------------------------------------------------------------

            char const* pID = nullptr;
            if ( pLabel == nullptr || strlen( pLabel ) == 0 )
            {
                pID = pIcon;
            }
            else
            {
                pID = pLabel;
            }

            ImGuiID const ID = pWindow->GetID( pID );

            // Calculate sizes
            //-------------------------------------------------------------------------

            ImGuiStyle const& style = ImGui::GetStyle();
            ImVec2 const iconSize = ImGui::CalcTextSize( pIcon, nullptr, true );
            ImVec2 const labelSize = ImGui::CalcTextSize( pLabel, nullptr, true );
            float const spacing = ( iconSize.x > 0 && labelSize.x > 0 ) ? style.ItemSpacing.x : 0.0f;
            float const buttonWidth = labelSize.x + iconSize.x + spacing;
            float const buttonWidthWithFramePadding = buttonWidth + ( style.FramePadding.x * 2.0f );
            float const textHeightMax = std::max( iconSize.y, labelSize.y );
            float const buttonHeight = std::max( ImGui::GetFrameHeight(), textHeightMax );

            ImVec2 const pos = pWindow->DC.CursorPos;
            ImVec2 const finalButtonSize = ImGui::CalcItemSize( size, buttonWidthWithFramePadding, buttonHeight );

            // Add item and handle input
            //-------------------------------------------------------------------------

            ImGui::ItemSize( finalButtonSize, 0 );
            ImRect const bb( pos, pos + finalButtonSize );
            if ( !ImGui::ItemAdd( bb, ID ) )
            {
                return false;
            }

            bool hovered, held;
            wasPressed = ImGui::ButtonBehavior( bb, ID, &hovered, &held, 0 );

            // Render Button
            //-------------------------------------------------------------------------

            ImGui::PushStyleColor( ImGuiCol_Button, (ImVec4) backgroundColor );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, HoveredColor );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, ActiveColor );
            ImGui::PushStyleColor( ImGuiCol_Text, (ImVec4) foregroundColor );

            ImU32 const color = ImGui::GetColorU32( ( held && hovered ) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button );
            //ImGui::RenderNavCursor( bb, ID );
            ImGui::RenderFrame( bb.Min, bb.Max, color, true, style.FrameRounding );

            bool const isDisabled = g.CurrentItemFlags & ImGuiItemFlags_Disabled;
            const ImU32 finalIconColor = isDisabled ? iconColor : iconColor;

            if ( shouldCenterContents )
            {
                if ( labelSize.x > 0 )
                {
                    ImVec2 const textOffset( ( bb.GetWidth() / 2 ) - ( buttonWidthWithFramePadding / 2 ) + iconSize.x + spacing + style.FramePadding.x, 0 );
                    ImGui::RenderTextClipped( bb.Min + textOffset, bb.Max, pLabel, NULL, &labelSize, ImVec2( 0, 0.5f ), &bb );

                    float const offsetX = textOffset.x - iconSize.x - spacing;
                    float const offsetY = ( ( bb.GetHeight() - textHeightMax ) / 2.0f );
                    ImVec2 const iconOffset( offsetX, offsetY );
                    pWindow->DrawList->AddText( pos + iconOffset, finalIconColor, pIcon );
                }
                else
                {
                    float const offsetX = ( bb.GetWidth() - iconSize.x ) / 2.0f;
                    float const offsetY = ( ( bb.GetHeight() - iconSize.y ) / 2.0f );
                    ImVec2 const iconOffset( offsetX, offsetY );
                    pWindow->DrawList->AddText( pos + iconOffset, finalIconColor, pIcon );
                }
            }
            else
            {
                ImVec2 const textOffset( iconSize.x + spacing + style.FramePadding.x, 0 );
                ImGui::RenderTextClipped( bb.Min + textOffset, bb.Max, pLabel, NULL, &labelSize, ImVec2( 0, 0.5f ), &bb );

                float const iconHeightOffset = ( ( bb.GetHeight() - iconSize.y ) / 2.0f );
                pWindow->DrawList->AddText( pos + ImVec2( style.FramePadding.x, iconHeightOffset ), finalIconColor, pIcon );
            }

            ImGui::PopStyleColor( 4 );
        }

        //-------------------------------------------------------------------------

        return wasPressed;
    }

    TPair<bool, uint32> DirectoryTreeViewRecursive(const std::filesystem::path& Path, uint32* Count, int* SelectionMask)
    {
        ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | 
                                        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_SpanFullWidth;
    
        bool any_node_clicked = false;
        uint32 node_clicked = 0;
    
        for (const auto& entry : std::filesystem::directory_iterator(Path))
        {
            ImGuiTreeNodeFlags node_flags = base_flags;
            const bool is_selected = (*SelectionMask & (1 << (*Count))) != 0;
            if (is_selected)
            {
                node_flags |= ImGuiTreeNodeFlags_Selected;
            }
    
            std::string name = entry.path().string();
            auto lastSlash = name.find_last_of("/\\");
            lastSlash = lastSlash == FString::npos ? 0 : lastSlash + 1;
            name = name.substr(lastSlash, name.size() - lastSlash);
    
            if (!std::filesystem::is_directory(entry.path()))
            {
                continue;
            }
           
            bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)(*Count), node_flags, name.c_str());
            
    
            if (ImGui::IsItemClicked())
            {
                node_clicked = *Count;
                any_node_clicked = true;
            }
    
            (*Count)--;
    
            if (node_open)
            {
                const auto& clickState = DirectoryTreeViewRecursive(entry.path(), Count, SelectionMask);
    
                if (!any_node_clicked)
                {
                    any_node_clicked = clickState.first;
                    node_clicked = clickState.second;
                }
    
                ImGui::TreePop();
            }
            else
            {
                for (const auto& e : std::filesystem::recursive_directory_iterator(entry.path()))
                {
                    (*Count)--;
                }
            }
        }
    
        return { any_node_clicked, node_clicked };
    }


    int32 SearchableCombo(const char* StrId, const char* Preview, int32 ItemCount, int32 CurrentIndex, const TFunction<FFixedString(int32)>& GetItemLabel, const char* ItemIcon)
    {
        int32 Result = INDEX_NONE;
        const ImGuiStyle& Style = ImGui::GetStyle();

        const ImGuiID ComboId = ImGui::GetID(StrId);

        // Mirror the chosen item's icon into the closed preview so it matches the open list.
        FFixedString PreviewStr = Preview;
        if (ItemIcon != nullptr && CurrentIndex != INDEX_NONE)
        {
            PreviewStr = ItemIcon;
            PreviewStr += "  ";
            PreviewStr += Preview;
        }

        if (ImGui::BeginCombo(StrId, PreviewStr.c_str(), ImGuiComboFlags_HeightLargest))
        {
            const float PopupWidth = ImGui::GetContentRegionAvail().x;

            static THashMap<ImGuiID, ImGuiTextFilter> Filters;
            ImGuiTextFilter& Filter = Filters[ComboId];

            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere();
            }
            Filter.Draw("##filter", PopupWidth);

            // Gray magnify hint while the search box is empty.
            if (!Filter.IsActive())
            {
                ImVec2 HintPos = ImGui::GetItemRectMin();
                HintPos.x += Style.FramePadding.x + 2.0f;
                HintPos.y += Style.FramePadding.y;
                ImGui::GetWindowDrawList()->AddText(HintPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), LE_ICON_MAGNIFY " Search...");
            }

            ImGui::Separator();

            // Push the row spacing first so the height math below measures the rows we
            // actually draw; using the default spacing over-counts and leaves a gap.
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Style.ItemSpacing.x, 2.0f));

            // Cap the visible rows; the child scrolls past that. The popup auto-fits this
            // child exactly, so the combo window itself never grows a scrollbar.
            const int32 VisibleRows = ItemCount < 12 ? ItemCount : 12;
            const float ListHeight = VisibleRows * ImGui::GetTextLineHeightWithSpacing() + Style.FramePadding.y * 2.0f;

            if (ImGui::BeginChild("##list", ImVec2(PopupWidth, ListHeight)))
            {
                bool bAnyVisible = false;
                for (int32 i = 0; i < ItemCount; ++i)
                {
                    const FFixedString Label = GetItemLabel(i);
                    if (!Filter.PassFilter(Label.c_str()))
                    {
                        continue;
                    }
                    bAnyVisible = true;

                    FFixedString Row;
                    if (ItemIcon != nullptr)
                    {
                        Row = ItemIcon;
                        Row += "  ";
                        Row += Label;
                    }
                    else
                    {
                        Row = Label;
                    }

                    ImGui::PushID(i);
                    const bool bSelected = (i == CurrentIndex);
                    if (ImGui::Selectable(Row.c_str(), bSelected))
                    {
                        Result = i;
                        ImGui::CloseCurrentPopup();
                    }
                    if (bSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::PopID();
                }

                if (!bAnyVisible)
                {
                    ImGui::TextDisabled("No matches");
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();

            ImGui::EndCombo();
        }

        return Result;
    }

    bool AssetReferenceCombo(const char* StrId, CClass* FilterClass, FGuid& InOutGUID, const char* ItemIcon)
    {
        if (FilterClass == nullptr)
        {
            return false;
        }

        // Every asset whose class is-a FilterClass, sorted by name.
        TVector<FAssetData*> Assets = FAssetRegistry::Get().FindByPredicate([FilterClass](const FAssetData& Data)
        {
            CClass* DataClass = FindObject<CClass>(Data.AssetClass);
            return DataClass != nullptr && DataClass->IsChildOf(FilterClass);
        });

        eastl::sort(Assets.begin(), Assets.end(), [](const FAssetData* A, const FAssetData* B)
        {
            return A->AssetName.ToString() < B->AssetName.ToString();
        });

        int32 CurrentIndex = INDEX_NONE;
        for (int32 i = 0; i < (int32)Assets.size(); ++i)
        {
            if (Assets[i]->AssetGUID == InOutGUID)
            {
                CurrentIndex = i;
                break;
            }
        }

        const char* Preview = (CurrentIndex != INDEX_NONE) ? Assets[CurrentIndex]->AssetName.c_str() : "Select an asset...";

        const int32 Picked = SearchableCombo(StrId, Preview, (int32)Assets.size(), CurrentIndex,
            [&Assets](int32 Index) { return FFixedString(Assets[Index]->AssetName.c_str()); }, ItemIcon);

        if (Picked != INDEX_NONE && Picked != CurrentIndex)
        {
            InOutGUID = Assets[Picked]->AssetGUID;
            return true;
        }
        return false;
    }

    ImTextureRef ToImTextureRef(FRHIImage* Image)
    {
		#if WITH_EDITOR
        return GRenderManager->GetImGuiRenderer()->GetOrCreateImTexture(Image);
		#else 
    	return {};
		#endif
    }

    ImTextureRef ToImTextureRef(FStringView Path)
    {
		#if WITH_EDITOR
        return GRenderManager->GetImGuiRenderer()->GetOrCreateImTexture(Path);
		#else
    	return {};
		#endif
    }

    FString FormatSize(size_t Bytes)
    {
        const char* Suffixes[] = { "B", "KB", "MB", "GB" };
        double Size = static_cast<double>(Bytes);
        int Suffix = 0;

        while (Size >= 1024.0 && Suffix < 3)
        {
            Size /= 1024.0;
            ++Suffix;
        }
    	FString Value;
    	std::format_to(std::back_inserter(Value), "{:.2f} {}", Size, Suffixes[Suffix]);
        return Value;
    }

    void RenderWindowOuterBorders(ImGuiWindow* Window)
    {
    	struct ImGuiResizeBorderDef
		{
			ImVec2 InnerDir;
			ImVec2 SegmentN1, SegmentN2;
			float  OuterAngle;
		};

		static const ImGuiResizeBorderDef resize_border_def[4] =
		{
			{ ImVec2(+1, 0), ImVec2(0, 1), ImVec2(0, 0), IM_PI * 1.00f }, // Left
			{ ImVec2(-1, 0), ImVec2(1, 0), ImVec2(1, 1), IM_PI * 0.00f }, // Right
			{ ImVec2(0, +1), ImVec2(0, 0), ImVec2(1, 0), IM_PI * 1.50f }, // Up
			{ ImVec2(0, -1), ImVec2(1, 1), ImVec2(0, 1), IM_PI * 0.50f }  // Down
		};

		auto GetResizeBorderRect = [](ImGuiWindow* window, int border_n, float perp_padding, float thickness)
		{
			ImRect rect = window->Rect();
			if (thickness == 0.0f)
			{
				rect.Max.x -= 1;
				rect.Max.y -= 1;
			}
			if (border_n == ImGuiDir_Left) { return ImRect(rect.Min.x - thickness, rect.Min.y + perp_padding, rect.Min.x + thickness, rect.Max.y - perp_padding); }
			if (border_n == ImGuiDir_Right) { return ImRect(rect.Max.x - thickness, rect.Min.y + perp_padding, rect.Max.x + thickness, rect.Max.y - perp_padding); }
			if (border_n == ImGuiDir_Up) { return ImRect(rect.Min.x + perp_padding, rect.Min.y - thickness, rect.Max.x - perp_padding, rect.Min.y + thickness); }
			if (border_n == ImGuiDir_Down) { return ImRect(rect.Min.x + perp_padding, rect.Max.y - thickness, rect.Max.x - perp_padding, rect.Max.y + thickness); }
			IM_ASSERT(0);
			return ImRect();
		};


		ImGuiContext& g = *GImGui;
		float rounding = Window->WindowRounding;
		float border_size = 1.0f; // window->WindowBorderSize;
		if (border_size > 0.0f && !(Window->Flags & ImGuiWindowFlags_NoBackground))
		{
			Window->DrawList->AddRect(Window->Pos, { Window->Pos.x + Window->Size.x,  Window->Pos.y + Window->Size.y }, ImGui::GetColorU32(ImGuiCol_Border), rounding, 0, border_size);
		}

	    int border_held = Window->ResizeBorderHeld;
		if (border_held != -1)
		{
			const ImGuiResizeBorderDef& def = resize_border_def[border_held];
			ImRect border_r = GetResizeBorderRect(Window, border_held, rounding, 0.0f);
			ImVec2 p1 = ImLerp(border_r.Min, border_r.Max, def.SegmentN1);
			const float offsetX = def.InnerDir.x * rounding;
			const float offsetY = def.InnerDir.y * rounding;
			p1.x += 0.5f + offsetX;
			p1.y += 0.5f + offsetY;

			ImVec2 p2 = ImLerp(border_r.Min, border_r.Max, def.SegmentN2);
			p2.x += 0.5f + offsetX;
			p2.y += 0.5f + offsetY;

			Window->DrawList->PathArcTo(p1, rounding, def.OuterAngle - IM_PI * 0.25f, def.OuterAngle);
			Window->DrawList->PathArcTo(p2, rounding, def.OuterAngle, def.OuterAngle + IM_PI * 0.25f);
			Window->DrawList->PathStroke(ImGui::GetColorU32(ImGuiCol_SeparatorActive), 0, ImMax(2.0f, border_size)); // Thicker than usual
		}
		if (g.Style.FrameBorderSize > 0 && !(Window->Flags & ImGuiWindowFlags_NoTitleBar) && !Window->DockIsActive)
		{
			float y = Window->Pos.y + Window->TitleBarHeight - 1;
			Window->DrawList->AddLine(ImVec2(Window->Pos.x + border_size, y), ImVec2(Window->Pos.x + Window->Size.x - border_size, y), ImGui::GetColorU32(ImGuiCol_Border), g.Style.FrameBorderSize);
		}
    }

    bool UpdateWindowManualResize(ImGuiWindow* Window, ImVec2& NewSize, ImVec2& NewPosition)
    {
    	auto CalcWindowSizeAfterConstraint = [](ImGuiWindow* window, const ImVec2& size_desired)
		{
			ImGuiContext& g = *GImGui;
			ImVec2 new_size = size_desired;
			if (g.NextWindowData.WindowFlags & ImGuiNextWindowDataFlags_HasSizeConstraint)
			{
				// Using -1,-1 on either X/Y axis to preserve the current size.
				ImRect cr = g.NextWindowData.SizeConstraintRect;
				new_size.x = (cr.Min.x >= 0 && cr.Max.x >= 0) ? ImClamp(new_size.x, cr.Min.x, cr.Max.x) : window->SizeFull.x;
				new_size.y = (cr.Min.y >= 0 && cr.Max.y >= 0) ? ImClamp(new_size.y, cr.Min.y, cr.Max.y) : window->SizeFull.y;
				if (g.NextWindowData.SizeCallback)
				{
					ImGuiSizeCallbackData data;
					data.UserData = g.NextWindowData.SizeCallbackUserData;
					data.Pos = window->Pos;
					data.CurrentSize = window->SizeFull;
					data.DesiredSize = new_size;
					g.NextWindowData.SizeCallback(&data);
					new_size = data.DesiredSize;
				}
				new_size.x = Math::Floor(new_size.x);
				new_size.y = Math::Floor(new_size.y);
			}

			// Minimum size
			if (!(window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_AlwaysAutoResize)))
			{
				ImGuiWindow* window_for_height = (window->DockNodeAsHost && window->DockNodeAsHost->VisibleWindow) ? window->DockNodeAsHost->VisibleWindow : window;
				const float decoration_up_height = window_for_height->TitleBarHeight + window_for_height->MenuBarHeight;
				new_size = ImMax(new_size, g.Style.WindowMinSize);
				new_size.y = ImMax(new_size.y, decoration_up_height + ImMax(0.0f, g.Style.WindowRounding - 1.0f)); // Reduce artifacts with very small windows
			}
			return new_size;
		};

		auto CalcWindowAutoFitSize = [CalcWindowSizeAfterConstraint](ImGuiWindow* window, const ImVec2& size_contents)
		{
			ImGuiContext& g = *GImGui;
			ImGuiStyle& style = g.Style;
			const float decoration_up_height = window->TitleBarHeight + window->MenuBarHeight;
			ImVec2 size_pad{ window->WindowPadding.x * 2.0f, window->WindowPadding.y * 2.0f };
			ImVec2 size_desired = { size_contents.x + size_pad.x + 0.0f, size_contents.y + size_pad.y + decoration_up_height };
			if (window->Flags & ImGuiWindowFlags_Tooltip)
			{
				// Tooltip always resize
				return size_desired;
			}
			else
			{
				// Maximum window size is determined by the viewport size or monitor size
				const bool is_popup = (window->Flags & ImGuiWindowFlags_Popup) != 0;
				const bool is_menu = (window->Flags & ImGuiWindowFlags_ChildMenu) != 0;
				ImVec2 size_min = style.WindowMinSize;
				if (is_popup || is_menu) // Popups and menus bypass style.WindowMinSize by default, but we give then a non-zero minimum size to facilitate understanding problematic cases (e.g. empty popups)
					size_min = ImMin(size_min, ImVec2(4.0f, 4.0f));

				// FIXME-VIEWPORT-WORKAREA: May want to use GetWorkSize() instead of Size depending on the type of windows?
				ImVec2 avail_size = window->Viewport->Size;
				if (window->ViewportOwned)
				{
					avail_size = ImVec2(FLT_MAX, FLT_MAX);
				}
				const int monitor_idx = window->ViewportAllowPlatformMonitorExtend;
				if (monitor_idx >= 0 && monitor_idx < g.PlatformIO.Monitors.Size)
				{
					avail_size = g.PlatformIO.Monitors[monitor_idx].WorkSize;
				}
				ImVec2 size_auto_fit = ImClamp(size_desired, size_min, ImMax(size_min, { avail_size.x - style.DisplaySafeAreaPadding.x * 2.0f,
					                                                             avail_size.y - style.DisplaySafeAreaPadding.y * 2.0f }));

				// When the window cannot fit all contents (either because of constraints, either because screen is too small),
				// we are growing the size on the other axis to compensate for expected scrollbar. FIXME: Might turn bigger than ViewportSize-WindowPadding.
				ImVec2 size_auto_fit_after_constraint = CalcWindowSizeAfterConstraint(window, size_auto_fit);
				bool will_have_scrollbar_x = (size_auto_fit_after_constraint.x - size_pad.x - 0.0f < size_contents.x && !(window->Flags & ImGuiWindowFlags_NoScrollbar) && (window->Flags & ImGuiWindowFlags_HorizontalScrollbar)) || (window->Flags & ImGuiWindowFlags_AlwaysHorizontalScrollbar);
				bool will_have_scrollbar_y = (size_auto_fit_after_constraint.y - size_pad.y - decoration_up_height < size_contents.y && !(window->Flags& ImGuiWindowFlags_NoScrollbar)) || (window->Flags & ImGuiWindowFlags_AlwaysVerticalScrollbar);
				if (will_have_scrollbar_x)
				{
					size_auto_fit.y += style.ScrollbarSize;
				}
				if (will_have_scrollbar_y)
				{
					size_auto_fit.x += style.ScrollbarSize;
				}
				return size_auto_fit;
			}
		};

		ImGuiContext& g = *GImGui;

		// Decide if we are going to handle borders and resize grips
		const bool handle_borders_and_resize_grips = (Window->DockNodeAsHost || !Window->DockIsActive);

		if (!handle_borders_and_resize_grips || Window->Collapsed)
		{
			return false;
		}

	    const ImVec2 size_auto_fit = CalcWindowAutoFitSize(Window, Window->ContentSizeIdeal);

		// Handle manual resize: Resize Grips, Borders, Gamepad
		int border_held = -1;
		ImU32 resize_grip_col[4] = {};
		const int resize_grip_count = g.IO.ConfigWindowsResizeFromEdges ? 2 : 1; // Allow resize from lower-left if we have the mouse cursor feedback for it.
		const float resize_grip_draw_size = Math::Floor(ImMax(g.FontSize * 1.10f, Window->WindowRounding + 1.0f + g.FontSize * 0.2f));
		Window->ResizeBorderHeld = (signed char)border_held;

		//const ImRect& visibility_rect;

		struct ImGuiResizeBorderDef
		{
			ImVec2 InnerDir;
			ImVec2 SegmentN1, SegmentN2;
			float  OuterAngle;
		};
		static const ImGuiResizeBorderDef resize_border_def[4] =
		{
			{ ImVec2(+1, 0), ImVec2(0, 1), ImVec2(0, 0), IM_PI * 1.00f }, // Left
			{ ImVec2(-1, 0), ImVec2(1, 0), ImVec2(1, 1), IM_PI * 0.00f }, // Right
			{ ImVec2(0, +1), ImVec2(0, 0), ImVec2(1, 0), IM_PI * 1.50f }, // Up
			{ ImVec2(0, -1), ImVec2(1, 1), ImVec2(0, 1), IM_PI * 0.50f }  // Down
		};

		// Data for resizing from corner
		struct ImGuiResizeGripDef
		{
			ImVec2  CornerPosN;
			ImVec2  InnerDir;
			int     AngleMin12, AngleMax12;
		};
		static const ImGuiResizeGripDef resize_grip_def[4] =
		{
			{ ImVec2(1, 1), ImVec2(-1, -1), 0, 3 },  // Lower-right
			{ ImVec2(0, 1), ImVec2(+1, -1), 3, 6 },  // Lower-left
			{ ImVec2(0, 0), ImVec2(+1, +1), 6, 9 },  // Upper-left (Unused)
			{ ImVec2(1, 0), ImVec2(-1, +1), 9, 12 }  // Upper-right (Unused)
		};

		auto CalcResizePosSizeFromAnyCorner = [CalcWindowSizeAfterConstraint](ImGuiWindow* window, const ImVec2& corner_target, const ImVec2& corner_norm, ImVec2* out_pos, ImVec2* out_size)
		{
			ImVec2 pos_min = ImLerp(corner_target, window->Pos, corner_norm);                // Expected window upper-left
			ImVec2 pos_max = ImLerp({ window->Pos.x + window->Size.x, window->Pos.y + window->Size.y }, corner_target, corner_norm); // Expected window lower-right
			ImVec2 size_expected = { pos_max.x - pos_min.x,  pos_max.y - pos_min.y };
			ImVec2 size_constrained = CalcWindowSizeAfterConstraint(window, size_expected);
			*out_pos = pos_min;
			if (corner_norm.x == 0.0f)
			{
				out_pos->x -= (size_constrained.x - size_expected.x);
			}
			if (corner_norm.y == 0.0f)
			{
				out_pos->y -= (size_constrained.y - size_expected.y);
			}
			*out_size = size_constrained;
		};

		auto GetResizeBorderRect = [](ImGuiWindow* window, int border_n, float perp_padding, float thickness)
		{
			ImRect rect = window->Rect();
			if (thickness == 0.0f)
			{
				rect.Max.x -= 1;
				rect.Max.y -= 1;
			}
			if (border_n == ImGuiDir_Left) { return ImRect(rect.Min.x - thickness, rect.Min.y + perp_padding, rect.Min.x + thickness, rect.Max.y - perp_padding); }
			if (border_n == ImGuiDir_Right) { return ImRect(rect.Max.x - thickness, rect.Min.y + perp_padding, rect.Max.x + thickness, rect.Max.y - perp_padding); }
			if (border_n == ImGuiDir_Up) { return ImRect(rect.Min.x + perp_padding, rect.Min.y - thickness, rect.Max.x - perp_padding, rect.Min.y + thickness); }
			if (border_n == ImGuiDir_Down) { return ImRect(rect.Min.x + perp_padding, rect.Max.y - thickness, rect.Max.x - perp_padding, rect.Max.y + thickness); }
			IM_ASSERT(0);
			return ImRect();
		};

		static const float WINDOWS_HOVER_PADDING = 4.0f;                        // Extend outside window for hovering/resizing (maxxed with TouchPadding) and inside windows for borders. Affect FindHoveredWindow().
		static const float WINDOWS_RESIZE_FROM_EDGES_FEEDBACK_TIMER = 0.04f;    // Reduce visual noise by only highlighting the border after a certain time.

		auto& style = g.Style;
		ImGuiWindowFlags flags = Window->Flags;

		if (/*(flags & ImGuiWindowFlags_NoResize) || */(flags & ImGuiWindowFlags_AlwaysAutoResize) || Window->AutoFitFramesX > 0 || Window->AutoFitFramesY > 0)
		{
			return false;
		}
    	if (Window->WasActive == false) // Early out to avoid running this code for e.g. an hidden implicit/fallback Debug window.
    	{
    		return false;
    	}

    	bool ret_auto_fit = false;
		const int resize_border_count = g.IO.ConfigWindowsResizeFromEdges ? 4 : 0;
		const float grip_draw_size = Math::Floor(ImMax(g.FontSize * 1.35f, Window->WindowRounding + 1.0f + g.FontSize * 0.2f));
		const float grip_hover_inner_size = Math::Floor(grip_draw_size * 0.75f);
		const float grip_hover_outer_size = g.IO.ConfigWindowsResizeFromEdges ? WINDOWS_HOVER_PADDING : 0.0f;

		ImVec2 pos_target(FLT_MAX, FLT_MAX);
		ImVec2 size_target(FLT_MAX, FLT_MAX);

		// Calculate the range of allowed position for that window (to be movable and visible past safe area padding)
		// When clamping to stay visible, we will enforce that window->Pos stays inside of visibility_rect.
		ImRect viewport_rect(Window->Viewport->GetMainRect());
		ImRect viewport_work_rect(Window->Viewport->GetWorkRect());
		ImVec2 visibility_padding = ImMax(style.DisplayWindowPadding, style.DisplaySafeAreaPadding);
		ImRect visibility_rect({ viewport_work_rect.Min.x + visibility_padding.x, viewport_work_rect.Min.y + visibility_padding.y },
			{ viewport_work_rect.Max.x - visibility_padding.x, viewport_work_rect.Max.y - visibility_padding.y });

		// Clip mouse interaction rectangles within the viewport rectangle (in practice the narrowing is going to happen most of the time).
		// - Not narrowing would mostly benefit the situation where OS windows _without_ decoration have a threshold for hovering when outside their limits.
		//   This is however not the case with current backends under Win32, but a custom borderless window implementation would benefit from it.
		// - When decoration are enabled we typically benefit from that distance, but then our resize elements would be conflicting with OS resize elements, so we also narrow.
		// - Note that we are unable to tell if the platform setup allows hovering with a distance threshold (on Win32, decorated window have such threshold).
		// We only clip interaction so we overwrite window->ClipRect, cannot call PushClipRect() yet as DrawList is not yet setup.
		const bool clip_with_viewport_rect = !(g.IO.BackendFlags & ImGuiBackendFlags_HasMouseHoveredViewport) || (g.IO.MouseHoveredViewport != Window->ViewportId) || !(Window->Viewport->Flags & ImGuiViewportFlags_NoDecoration);
		if (clip_with_viewport_rect)
		{
			Window->ClipRect = Window->Viewport->GetMainRect();
		}

    	// Resize grips and borders are on layer 1
		Window->DC.NavLayerCurrent = ImGuiNavLayer_Menu;

		// Manual resize grips
		ImGui::PushID("#RESIZE");
		for (int resize_grip_n = 0; resize_grip_n < resize_grip_count; resize_grip_n++)
		{
			const ImGuiResizeGripDef& def = resize_grip_def[resize_grip_n];

			const ImVec2 corner = ImLerp(Window->Pos, { Window->Pos.x + Window->Size.x, Window->Pos.y + Window->Size.y }, def.CornerPosN);

			// Using the FlattenChilds button flag we make the resize button accessible even if we are hovering over a child window
			bool hovered, held;
			const ImVec2 min = { corner.x - def.InnerDir.x * grip_hover_outer_size, corner.y - def.InnerDir.y * grip_hover_outer_size };
			const ImVec2 max = { corner.x + def.InnerDir.x * grip_hover_outer_size, corner.y + def.InnerDir.y * grip_hover_outer_size };
			ImRect resize_rect(min, max);

			if (resize_rect.Min.x > resize_rect.Max.x)
			{
				ImSwap(resize_rect.Min.x, resize_rect.Max.x);
			}
			if (resize_rect.Min.y > resize_rect.Max.y)
			{
				ImSwap(resize_rect.Min.y, resize_rect.Max.y);
			}
			ImGuiID resize_grip_id = Window->GetID(resize_grip_n); // == GetWindowResizeCornerID()
			ImGui::ButtonBehavior(resize_rect, resize_grip_id, &hovered, &held, ImGuiButtonFlags_FlattenChildren | ImGuiButtonFlags_NoNavFocus);
			//GetForegroundDrawList(window)->AddRect(resize_rect.Min, resize_rect.Max, IM_COL32(255, 255, 0, 255));
			if (hovered || held)
				g.MouseCursor = (resize_grip_n & 1) ? ImGuiMouseCursor_ResizeNESW : ImGuiMouseCursor_ResizeNWSE;

			if (held && g.IO.MouseDoubleClicked[0] && resize_grip_n == 0)
			{
				// Manual auto-fit when double-clicking
				size_target = CalcWindowSizeAfterConstraint(Window, size_auto_fit);
				ret_auto_fit = true;
				ImGui::ClearActiveID();
			}
			else if (held)
			{
				// Resize from any of the four corners
				// We don't use an incremental MouseDelta but rather compute an absolute target size based on mouse position
				ImVec2 clamp_min = ImVec2(def.CornerPosN.x == 1.0f ? visibility_rect.Min.x : -FLT_MAX, def.CornerPosN.y == 1.0f ? visibility_rect.Min.y : -FLT_MAX);
				ImVec2 clamp_max = ImVec2(def.CornerPosN.x == 0.0f ? visibility_rect.Max.x : +FLT_MAX, def.CornerPosN.y == 0.0f ? visibility_rect.Max.y : +FLT_MAX);

				const float x = g.IO.MousePos.x - g.ActiveIdClickOffset.x + ImLerp(def.InnerDir.x * grip_hover_outer_size, def.InnerDir.x * -grip_hover_inner_size, def.CornerPosN.x);
				const float y = g.IO.MousePos.y - g.ActiveIdClickOffset.y + ImLerp(def.InnerDir.y * grip_hover_outer_size, def.InnerDir.y * -grip_hover_inner_size, def.CornerPosN.y);

				ImVec2 corner_target(x, y); // Corner of the window corresponding to our corner grip
				corner_target = ImClamp(corner_target, clamp_min, clamp_max);
				CalcResizePosSizeFromAnyCorner(Window, corner_target, def.CornerPosN, &pos_target, &size_target);
			}

			// Only lower-left grip is visible before hovering/activating
			if (resize_grip_n == 0 || held || hovered)
			{
				resize_grip_col[resize_grip_n] = ImGui::GetColorU32(held ? ImGuiCol_ResizeGripActive : hovered ? ImGuiCol_ResizeGripHovered : ImGuiCol_ResizeGrip);
			}
		}
		for (int border_n = 0; border_n < resize_border_count; border_n++)
		{
			const ImGuiResizeBorderDef& def = resize_border_def[border_n];
			const ImGuiAxis axis = (border_n == ImGuiDir_Left || border_n == ImGuiDir_Right) ? ImGuiAxis_X : ImGuiAxis_Y;

			bool hovered, held;
			ImRect border_rect = GetResizeBorderRect(Window, border_n, grip_hover_inner_size, WINDOWS_HOVER_PADDING);
			ImGuiID border_id = Window->GetID(border_n + 4); // == GetWindowResizeBorderID()
			ImGui::ButtonBehavior(border_rect, border_id, &hovered, &held, ImGuiButtonFlags_FlattenChildren);
			//GetForegroundDrawLists(window)->AddRect(border_rect.Min, border_rect.Max, IM_COL32(255, 255, 0, 255));
			if ((hovered && g.HoveredIdTimer > WINDOWS_RESIZE_FROM_EDGES_FEEDBACK_TIMER) || held)
			{
				g.MouseCursor = (axis == ImGuiAxis_X) ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS;
				if (held)
				{
					border_held = border_n;
				}
			}
			if (held)
			{
				ImVec2 clamp_min(border_n == ImGuiDir_Right ? visibility_rect.Min.x : -FLT_MAX, border_n == ImGuiDir_Down ? visibility_rect.Min.y : -FLT_MAX);
				ImVec2 clamp_max(border_n == ImGuiDir_Left ? visibility_rect.Max.x : +FLT_MAX, border_n == ImGuiDir_Up ? visibility_rect.Max.y : +FLT_MAX);
				ImVec2 border_target = Window->Pos;
				border_target[axis] = g.IO.MousePos[axis] - g.ActiveIdClickOffset[axis] + WINDOWS_HOVER_PADDING;
				border_target = ImClamp(border_target, clamp_min, clamp_max);
				CalcResizePosSizeFromAnyCorner(Window, border_target, ImMin(def.SegmentN1, def.SegmentN2), &pos_target, &size_target);
			}
		}
		ImGui::PopID();

		bool changed = false;
		NewSize = Window->Size;
		NewPosition = Window->Pos;

		// Apply back modified position/size to window
		if (size_target.x != FLT_MAX)
		{
			Window->SizeFull = size_target;
			ImGui::MarkIniSettingsDirty(Window);
			NewSize = size_target;
			changed = true;
		}
		if (pos_target.x != FLT_MAX)
		{
			Window->Pos = ImFloor(pos_target);
			ImGui::MarkIniSettingsDirty(Window);
			NewPosition = pos_target;
			changed = true;
		}

		Window->Size = Window->SizeFull;
		return changed;
    }


    void ApplicationTitleBar::DrawWindowControls()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        FWindow* Window = Windowing::GetPrimaryWindowHandle();

        ImGui::BeginHorizontal("TitleBar");

        if (ImGuiX::FlatButton(LE_ICON_WINDOW_MINIMIZE "##Min", ImVec2(WindowControlButtonWidth, -1)))
        {
            Window->Minimize();
        }
    	
        if (ImGuiX::FlatButton(LE_ICON_WINDOW_RESTORE "##Res", ImVec2(WindowControlButtonWidth, -1)))
        {
            if (Window->IsWindowMaximized())
            {
                Window->Restore();
            }
            else
            {
                Window->Maximize();
            }
        }
    	
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        if (ImGuiX::FlatButton(LE_ICON_WINDOW_CLOSE "##X", ImVec2(WindowControlButtonWidth, -1)))
        {
            FApplication::RequestExit();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        ImGui::EndHorizontal();
    }


    void ApplicationTitleBar::Draw(TFunction<void()>&& menuDrawFunction, float menuSectionDesiredWidth, TFunction<void()>&& controlsSectionDrawFunction, float controlsSectionDesiredWidth)
    {
        Rect = FVector4(1.0f);
    
        const ImVec2 TitleBarPadding(0, 8);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, TitleBarPadding);
        ImGuiViewport* pViewport = ImGui::GetMainViewport();
    
        if (ImGui::BeginViewportSideBar("TitleBar", pViewport, ImGuiDir_Up, 40, ImGuiWindowFlags_NoDecoration))
        {
            ImGui::PopStyleVar();
    
            const float titleBarWidth = ImGui::GetWindowSize().x;
            const float titleBarHeight = ImGui::GetWindowSize().y;
            Rect = FVector4(0.0f, 0.0f, titleBarWidth, titleBarHeight);
    
            const float windowControlsWidth = GetWindowsControlsWidth();
            const float windowControlsStartPosX = eastl::max(0.0f, ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - windowControlsWidth);
            const ImVec2 windowControlsStartPos(windowControlsStartPosX, ImGui::GetCursorPosY() - TitleBarPadding.y);

            const float availableSpace = titleBarWidth - windowControlsWidth - s_minimumDraggableGap - ( s_sectionPadding * 2 );
            float remainingSpace = availableSpace;

            const float menuSectionFinalWidth = ( remainingSpace - menuSectionDesiredWidth ) > 0 ? menuSectionDesiredWidth : eastl::max( 0.0f, remainingSpace );
            remainingSpace -= menuSectionFinalWidth;
            const ImVec2 menuSectionStartPos( s_sectionPadding, ImGui::GetCursorPosY() );

            const float controlSectionFinalWidth = ( remainingSpace - controlsSectionDesiredWidth ) > 0 ? controlsSectionDesiredWidth : eastl::max( 0.0f, remainingSpace );
            remainingSpace -= controlSectionFinalWidth;
            const ImVec2 controlSectionStartPos( windowControlsStartPos.x - s_sectionPadding - controlSectionFinalWidth, ImGui::GetCursorPosY() );

            ImVec2 DragAreaStartPos = menuSectionStartPos;
            DragAreaStartPos.x += menuSectionFinalWidth + 10;
            
            ImGui::SetCursorPos(DragAreaStartPos);
            ImGui::InvisibleButton("TitleBarDragZone", ImVec2(remainingSpace, titleBarHeight));
        	
            if (FWindow* MainWindow = Windowing::GetPrimaryWindowHandle())
            {
                MainWindow->SetTitleBarHovered(ImGui::IsItemHovered());
            }


            if (menuSectionDesiredWidth > 0)
            {
                ImGui::SetCursorPos(menuSectionStartPos);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImGui::GetStyle().FramePadding + ImVec2(0, 2));
                bool const drawMenuSection = ImGui::BeginChild("Left", ImVec2(menuSectionDesiredWidth, titleBarHeight), 
                                                               ImGuiChildFlags_AlwaysUseWindowPadding, 
                                                               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar);
                
                
                ImGui::PopStyleVar(2);
                if (drawMenuSection)
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16, 8));
                    if (ImGui::BeginMenuBar())
                    {
                        menuDrawFunction();
                        ImGui::EndMenuBar();
                    }
                    ImGui::PopStyleVar();
                }
                ImGui::EndChild();
            }
            
            if (controlsSectionDesiredWidth > 0)
            {
                ImGui::SetCursorPos(controlSectionStartPos);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                bool const drawControlsSection = ImGui::BeginChild( "Right", ImVec2( controlSectionFinalWidth, titleBarHeight),
                    ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoDecoration);
                
                ImGui::PopStyleVar();

                if (drawControlsSection)
                {
                    controlsSectionDrawFunction();
                }
                ImGui::EndChild();

            }

    
            ImGui::SetCursorPos(windowControlsStartPos);
            if (ImGui::BeginChild("WindowControls", ImVec2(windowControlsWidth, titleBarHeight), false, ImGuiWindowFlags_NoDecoration))
            {
                DrawWindowControls();
                ImGui::EndChild();
            }
            
            ImGui::End();
        }
        
    }
}

#undef IMDRAW_DEBUG