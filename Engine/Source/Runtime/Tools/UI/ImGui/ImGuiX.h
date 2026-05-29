#pragma once

#include <filesystem>
#include <format>
#include "imgui.h"
#include "ImGuizmo.h"
#include "imgui_internal.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Core/Math/Math.h"
#include "Platform/GenericPlatform.h"

struct ImGuiWindow;

namespace Lumina
{
    class FRHIImage;
    struct FARFilter;
    class CClass;
}

namespace Lumina::ImGuiX
{
    struct RUNTIME_API FImGuiImageInfo
    {
        FORCEINLINE bool IsValid() const { return ID != 0; }
        
        ImTextureID     ID = 0;
        ImVec2          Size = ImVec2(0, 0);
    };
    
    //--------------------------------------------------------------
    // DPI / UI scaling
    //--------------------------------------------------------------

    // Current editor UI scale (monitor DPI * bias). 1.0 = no scaling. ImGui fonts
    // and style track this automatically; multiply any hardcoded pixel dimension
    // (fixed button/toolbar sizes) by this so custom layouts stay aligned.
    RUNTIME_API float GetUIScale();

    // Set by the ImGui renderer whenever the scale is (re)resolved.
    RUNTIME_API void SetUIScale(float Scale);

    //--------------------------------------------------------------
    // Generic draw helpers...
    //--------------------------------------------------------------

    RUNTIME_API void TextTooltip_Internal(FStringView String);
    RUNTIME_API void TextColoredUnformatted(const ImVec4& Color, const FFixedString& String);

    template<typename... TArgs>
    void TextTooltip(std::format_string<TArgs...> Fmt, TArgs&&... Args)
    {
        FFixedString Buffer;
        std::format_to(std::back_inserter(Buffer), Fmt, std::forward<TArgs>(Args)...);
        TextTooltip_Internal(Buffer);
    }

    // Wrapped tooltip for help text longer than a few words; auto-wraps at ~35em.
    RUNTIME_API void WrappedTooltip_Internal(FStringView String);

    template<typename... TArgs>
    void WrappedTooltip(std::format_string<TArgs...> Fmt, TArgs&&... Args)
    {
        FFixedString Buffer;
        std::format_to(std::back_inserter(Buffer), Fmt, std::forward<TArgs>(Args)...);
        WrappedTooltip_Internal(Buffer);
    }

    // Inline `(?)` icon that shows a wrapped tooltip on hover. Place after a label or control.
    RUNTIME_API void HelpMarker(FStringView Help);

    // Same as HelpMarker but with a custom leading icon (e.g. LE_ICON_INFORMATION_OUTLINE).
    RUNTIME_API void HelpMarkerIcon(const char* Icon, FStringView Help);

    template <typename... TArgs>
    void Text(std::format_string<TArgs...> Fmt, TArgs&&... Args)
    {
        FFixedString Buffer;
        std::format_to(std::back_inserter(Buffer), Fmt, std::forward<TArgs>(Args)...);
        ImGui::TextUnformatted(Buffer.c_str());
    }
    
    RUNTIME_API void TextUnformatted(FStringView String);
    
    template <typename... TArgs>
    void TextColored(const ImVec4& Color, std::format_string<TArgs...> fmt, TArgs&&... Args)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Color);
        ImGui::TextUnformatted(std::format(fmt, std::forward<TArgs>(Args)...).c_str());
        ImGui::PopStyleColor();
    }
    
    template <typename... TArgs>
    void TextWrapped(std::format_string<TArgs...> fmt, TArgs&&... Args)
    {
        ImGuiContext& g = *GImGui;
        const bool need_backup = (g.CurrentWindow->DC.TextWrapPos < 0.0f);
        if (need_backup)
        {
            ImGui::PushTextWrapPos(0.0f);
        }
        
        Text(std::forward<decltype(fmt)>(fmt), std::forward<TArgs>(Args)...);
        
        if (need_backup)
        {
            ImGui::PopTextWrapPos();
        }
    }
    
    RUNTIME_API FStringView ImGuizmoOpToString(ImGuizmo::OPERATION Op);

    RUNTIME_API bool ButtonEx(char const* pIcon, char const* pLabel, ImVec2 const& size = ImVec2( 0, 0 ), const ImColor& backgroundColor = ImGui::ColorConvertFloat4ToU32( ImGui::GetStyle().Colors[ImGuiCol_Button] ), const ImColor& iconColor = ImGui::ColorConvertFloat4ToU32( ImGui::GetStyle().Colors[ImGuiCol_Text] ), const ImColor& foregroundColor = ImGui::ColorConvertFloat4ToU32( ImGui::GetStyle().Colors[ImGuiCol_Text] ), bool shouldCenterContents = false );

    RUNTIME_API inline bool FlatButton( char const* pLabel, ImVec2 const& size = ImVec2( 0, 0 ), const ImColor& foregroundColor = ImGui::ColorConvertFloat4ToU32( ImGui::GetStyle().Colors[ImGuiCol_Text] ) )
    {
        return ButtonEx( nullptr, pLabel, size, ImColor(0), ImColor(0), ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]));
    }

    RUNTIME_API inline bool IconButton(char const* pIcon, char const* pLabel, const ImColor& iconColor = ImGui::ColorConvertFloat4ToU32( ImGui::GetStyle().Colors[ImGuiCol_Text] ), ImVec2 const& size = ImVec2(0, 0), bool shouldCenterContents = false )
    {
        return ButtonEx(pIcon, pLabel, size, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Button]), iconColor, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]), shouldCenterContents);
    }

    RUNTIME_API TPair<bool, uint32> DirectoryTreeViewRecursive(const std::filesystem::path& Path, uint32* Count, int* SelectionMask);

    // A searchable single-select dropdown.
    RUNTIME_API int32 SearchableCombo(const char* StrId, const char* Preview, int32 ItemCount, int32 CurrentIndex, const TFunction<FFixedString(int32)>& GetItemLabel, const char* ItemIcon = nullptr);

    // Searchable combo for picking an asset of (or deriving from) FilterClass from the asset
    // registry. Shows the current selection, writes the chosen asset's GUID into InOutGUID and
    // returns true when it changes. The one widget every "select an underlying asset" UI uses.
    RUNTIME_API bool AssetReferenceCombo(const char* StrId, CClass* FilterClass, FGuid& InOutGUID, const char* ItemIcon = nullptr);
    
    RUNTIME_API ImTextureRef ToImTextureRef(FRHIImage* Image);
    RUNTIME_API ImTextureRef ToImTextureRef(FStringView Path);

    RUNTIME_API FString FormatSize(size_t Bytes);

    RUNTIME_API void RenderWindowOuterBorders(ImGuiWindow* Window);
    RUNTIME_API bool UpdateWindowManualResize(ImGuiWindow* Window, ImVec2& NewSize, ImVec2& NewPosition);
    
    namespace Notifications
    {
        enum class EType
        {
            None,
            Success,
            Warning,
            Error,
            Info,
        };


        RUNTIME_API void NotifyInternal(EType Type, FStringView Msg);
        
        template <typename... TArgs>
        void NotifyInfo(std::format_string<TArgs...> fmt, TArgs&&... Args)
        {
            FFixedString MessageStr;
            std::format_to(std::back_inserter(MessageStr), fmt, Forward<TArgs>(Args)...);
            NotifyInternal(EType::Info, MessageStr);
        }

        template <typename... TArgs>
        void NotifySuccess(std::format_string<TArgs...> fmt, TArgs&&... Args)
        {
            FFixedString MessageStr;
            std::format_to(std::back_inserter(MessageStr), fmt, Forward<TArgs>(Args)...);
            NotifyInternal(EType::Success, MessageStr);
        }

        template <typename... TArgs>
        void NotifyWarning(std::format_string<TArgs...> fmt, TArgs&&... Args)
        {
            FFixedString MessageStr;
            std::format_to(std::back_inserter(MessageStr), fmt, Forward<TArgs>(Args)...);
            NotifyInternal(EType::Warning, MessageStr);
        }

        template <typename... TArgs>
        void NotifyError(std::format_string<TArgs...> fmt, TArgs&&... Args)
        {
            FFixedString MessageStr;
            std::format_to(std::back_inserter(MessageStr), fmt, Forward<TArgs>(Args)...);
            NotifyInternal(EType::Error, MessageStr);
        }
    }
    
    struct RUNTIME_API ApplicationTitleBar
    {
        constexpr static float WindowControlButtonWidth = 45;
        constexpr static float s_minimumDraggableGap = 24; // Minimum open gap left open to allow dragging
        constexpr static float s_sectionPadding = 8; // Padding between the window frame/window controls and the menu/control sections

        static inline float GetWindowsControlsWidth() { return WindowControlButtonWidth * 3; }
        static void DrawWindowControls();

    public:

        // This function takes two delegates and sizes each representing the title bar menu and an extra optional controls section
        void Draw(TFunction<void()>&& menuSectionDrawFunction = TFunction<void()>(), float menuSectionWidth = 0, TFunction<void()>&& controlsSectionDrawFunction = TFunction<void()>(), float controlsSectionWidth = 0);

        // Get the screen space rectangle for this title bar
        FVector4 const& GetScreenRectangle() const { return Rect; }

    private:

        FVector4 Rect = FVector4(0.0f);
    };
    
}
