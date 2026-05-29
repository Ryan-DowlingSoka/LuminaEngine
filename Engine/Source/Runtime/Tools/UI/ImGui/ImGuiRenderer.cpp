#include "pch.h"
#include "ImGuiRenderer.h"

#include "ImGuiDesignIcons.h"
#include "ImGuiFonts.h"
#include "Core/Engine/Engine.h"
#include "imgui/misc/freetype/imgui_freetype.h"

#include "Renderer/CommandList.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RHIGlobals.h"
#include "TaskSystem/TaskSystem.h"
#include "Tools/UI/Notification/ImGuiNotifications.h"
#include <imgui.h>

#include "implot.h"
#include "Paths/Paths.h"
#include "Tools/UI/ImGui/ImGuiAllocator.h"

namespace Lumina
{
    void IImGuiRenderer::Initialize()
    {
        IMGUI_CHECKVERSION();

        // Route this (Runtime) ImGui copy through our allocator. Must also be done in every
        // other module that links ImGui -- see ImGuiAllocator.h. Before CreateContext so even
        // ImGui's first internal allocation is ours.
        ImGuiX::InstallImGuiAllocator();
		
        Context = ImGui::CreateContext();
    	ImPlotContext = ImPlot::CreateContext();
		
		FString FontFile_Regular = Paths::GetEngineFontDirectory() + "/Lexend/Lexend-Regular.ttf";
		FString FontFile_Bold = Paths::GetEngineFontDirectory() + "/Lexend/Lexend-Bold.ttf";
		FString FontFile_Mono = Paths::GetEngineFontDirectory() + "/JetbrainsMono/JetBrainsMono-Regular.ttf";
		FString FontFile_MonoBold = Paths::GetEngineFontDirectory() + "/JetbrainsMono/JetBrainsMono-Bold.ttf";
		FString IconFontFile = Paths::GetEngineFontDirectory() + "/materialdesignicons-webfont.ttf";
		constexpr ImWchar IconRanges[] = { LE_ICONRANGE_MIN, LE_ICONRANGE_MAX, 0 };

    	ImFontConfig FontConfig;
    	FontConfig.FontDataOwnedByAtlas = false;

    	ImFontConfig IconFontConfig;
    	IconFontConfig.FontDataOwnedByAtlas = false;
    	IconFontConfig.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LoadColor | ImGuiFreeTypeLoaderFlags_Bitmap;
    	IconFontConfig.MergeMode = true;
    	IconFontConfig.PixelSnapH = true;
    	IconFontConfig.RasterizerMultiply = 1.5f;

		ImGuiIO& io = ImGui::GetIO();
    	auto CreateFontFromFile = [&] (FStringView Path, float FontSize, float IconFontSize, ImGuiX::Font::EFont FontID, const ImVec2& GlyphOffset)
    	{
    		ImFont* pFont = io.Fonts->AddFontFromFileTTF(Path.data(), FontSize, &FontConfig);
    		if (pFont == nullptr)
    		{
    			PANIC("Failed to load font '{}'. Engine resources could not be resolved -- "
    			      "LUMINA_DIR may be unset or pointing at the wrong directory. Run the engine's Setup.bat.",
    			      Path.data());
    		}
		    ImGuiX::Font::GFonts[static_cast<uint8>(FontID)] = pFont;

    		IconFontConfig.GlyphOffset = GlyphOffset;
    		IconFontConfig.GlyphMinAdvanceX = IconFontSize;
    		io.Fonts->AddFontFromFileTTF(IconFontFile.c_str(), IconFontSize, &IconFontConfig, IconRanges);
    	};
		

    	constexpr float DPIScale = 1.0f;
    	const float size12 = std::floor(12 * DPIScale);
    	const float size14 = std::floor(14 * DPIScale);
    	const float size16 = std::floor(16 * DPIScale);
    	const float size18 = std::floor(18 * DPIScale);
    	const float size24 = std::floor(24 * DPIScale);
    	
    	CreateFontFromFile(FontFile_Regular, size12, size14, ImGuiX::Font::EFont::Tiny, ImVec2(0, 2));
    	CreateFontFromFile(FontFile_Bold, size12, size14, ImGuiX::Font::EFont::TinyBold, ImVec2(0, 2));

    	CreateFontFromFile(FontFile_Regular, size14, size16, ImGuiX::Font::EFont::Small, ImVec2(0, 2));
    	CreateFontFromFile(FontFile_Bold, size14, size16, ImGuiX::Font::EFont::SmallBold, ImVec2(0, 2));

    	CreateFontFromFile(FontFile_Regular, size16, size18, ImGuiX::Font::EFont::Medium, ImVec2(0, 2));
    	CreateFontFromFile(FontFile_Bold, size16, size18, ImGuiX::Font::EFont::MediumBold, ImVec2(0, 2));

    	CreateFontFromFile(FontFile_Regular, size24, size24, ImGuiX::Font::EFont::Large, ImVec2(0, 2));
    	CreateFontFromFile(FontFile_Bold, size24, size24, ImGuiX::Font::EFont::LargeBold, ImVec2(0, 2));

    	// Monospace pair for code editors (Lua / RML / shaders).
    	CreateFontFromFile(FontFile_Mono, size16, size18, ImGuiX::Font::EFont::Mono, ImVec2(0, 2));
    	CreateFontFromFile(FontFile_MonoBold, size16, size18, ImGuiX::Font::EFont::MonoBold, ImVec2(0, 2));

    	io.Fonts->TexMinWidth = 4096;

    	using namespace ImGuiX::Font;
    	io.FontDefault = GFonts[static_cast<uint8>(EFont::Medium)];
    	
        DEBUG_ASSERT(GFonts[(uint8)EFont::Small]->IsLoaded());
        DEBUG_ASSERT(GFonts[(uint8)EFont::SmallBold]->IsLoaded());
        DEBUG_ASSERT(GFonts[(uint8)EFont::Medium]->IsLoaded());
        DEBUG_ASSERT(GFonts[(uint8)EFont::MediumBold]->IsLoaded());
        DEBUG_ASSERT(GFonts[(uint8)EFont::Large]->IsLoaded());
        DEBUG_ASSERT(GFonts[(uint8)EFont::LargeBold]->IsLoaded());
        DEBUG_ASSERT(GFonts[(uint8)EFont::Mono]->IsLoaded());
        DEBUG_ASSERT(GFonts[(uint8)EFont::MonoBold]->IsLoaded());
		
    	
    	io.ConfigWindowsMoveFromTitleBarOnly = true;
    	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
    	io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;
    	io.ConfigViewportsNoDefaultParent = true;

        ImGui::StyleColorsDark();
        ImGuiStyle& Style = ImGui::GetStyle();
    	
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			Style.WindowRounding = 0.0f;
			Style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
    	
        Style.Alpha = 1.0f;
        Style.Colors[ImGuiCol_Text] =                   ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        Style.Colors[ImGuiCol_TextDisabled] =           ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        Style.Colors[ImGuiCol_WindowBg] =               ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
        Style.Colors[ImGuiCol_ChildBg] =                ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
        Style.Colors[ImGuiCol_PopupBg] =                ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
        Style.Colors[ImGuiCol_Border] =                 ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
        Style.Colors[ImGuiCol_BorderShadow] =           ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        Style.Colors[ImGuiCol_FrameBg] =                ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        Style.Colors[ImGuiCol_FrameBgHovered] =         ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        Style.Colors[ImGuiCol_FrameBgActive] =          ImVec4(0.37f, 0.37f, 0.37f, 0.39f);
        Style.Colors[ImGuiCol_TitleBg] =                ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
        Style.Colors[ImGuiCol_TitleBgActive] =          ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
        Style.Colors[ImGuiCol_TitleBgCollapsed] =       ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
        Style.Colors[ImGuiCol_MenuBarBg] =              ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        Style.Colors[ImGuiCol_ScrollbarBg] =            ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
        Style.Colors[ImGuiCol_ScrollbarGrab] =          ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
        Style.Colors[ImGuiCol_ScrollbarGrabHovered] =   ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
        Style.Colors[ImGuiCol_ScrollbarGrabActive] =    ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
        Style.Colors[ImGuiCol_CheckMark] =              ImVec4(0.11f, 0.64f, 0.92f, 1.00f);
        Style.Colors[ImGuiCol_SliderGrab] =             ImVec4(0.11f, 0.64f, 0.92f, 1.00f);
        Style.Colors[ImGuiCol_SliderGrabActive] =       ImVec4(0.08f, 0.50f, 0.72f, 1.00f);
        Style.Colors[ImGuiCol_Button] =                 ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        Style.Colors[ImGuiCol_ButtonHovered] =          ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
        Style.Colors[ImGuiCol_ButtonActive] =           ImVec4(0.67f, 0.67f, 0.67f, 0.39f);
        Style.Colors[ImGuiCol_Header] =                 ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
        Style.Colors[ImGuiCol_HeaderHovered] =          ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        Style.Colors[ImGuiCol_HeaderActive] =           ImVec4(0.67f, 0.67f, 0.67f, 0.39f);
        Style.Colors[ImGuiCol_Separator] =              Style.Colors[ImGuiCol_Border];
        Style.Colors[ImGuiCol_SeparatorHovered] =       ImVec4(0.41f, 0.42f, 0.44f, 1.00f);
        Style.Colors[ImGuiCol_SeparatorActive] =        ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
        Style.Colors[ImGuiCol_ResizeGrip] =             ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        Style.Colors[ImGuiCol_ResizeGripHovered] =      ImVec4(0.29f, 0.30f, 0.31f, 0.67f);
        Style.Colors[ImGuiCol_ResizeGripActive] =       ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
        Style.Colors[ImGuiCol_Tab] =                    ImVec4(0.08f, 0.08f, 0.09f, 0.83f);
        Style.Colors[ImGuiCol_TabHovered] =             ImVec4(0.33f, 0.34f, 0.36f, 0.83f);
        Style.Colors[ImGuiCol_TabSelected] =            ImVec4(0.23f, 0.23f, 0.24f, 1.00f);
        Style.Colors[ImGuiCol_TabDimmed] =				ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
        Style.Colors[ImGuiCol_TabDimmedSelected] =		ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
        Style.Colors[ImGuiCol_DockingPreview] =         ImVec4(0.26f, 0.49f, 0.28f, 0.70f);
        Style.Colors[ImGuiCol_DockingEmptyBg] =         ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        Style.Colors[ImGuiCol_PlotLines] =              ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        Style.Colors[ImGuiCol_PlotLinesHovered] =       ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        Style.Colors[ImGuiCol_PlotHistogram] =          ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        Style.Colors[ImGuiCol_PlotHistogramHovered] =   ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        Style.Colors[ImGuiCol_TextSelectedBg] =         ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        Style.Colors[ImGuiCol_DragDropTarget] =         ImVec4(0.11f, 0.64f, 0.92f, 1.00f);
        Style.Colors[ImGuiCol_NavCursor] =				ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        Style.Colors[ImGuiCol_NavWindowingHighlight] =  ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        Style.Colors[ImGuiCol_NavWindowingDimBg] =      ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        Style.Colors[ImGuiCol_ModalWindowDimBg] =       ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    	Style.Colors[ImGuiCol_CheckMark] =				ImVec4(0.25f, 1.0f, 0.25f, 1.0f);

    	
    	Style.FramePadding			= ImVec2(6, 4);
    	Style.WindowPadding			= ImVec2(4, 4);
    	Style.ItemSpacing			= ImVec2(4, 4);
    	Style.CellPadding			= ImVec2(4, 6);
        Style.GrabRounding			= 2.3f;
		Style.FrameRounding			= 2.3f;
		Style.DockingSeparatorSize	= 0.0f;
    	Style.ChildBorderSize		= 0.0f;
    	Style.TabBorderSize			= 1.0f;
    	Style.GrabRounding			= 0.0f;
    	Style.GrabMinSize			= 8.0f;
    	Style.WindowRounding		= 0.0f;
    	Style.WindowBorderSize		= 1.0f;
    	Style.FrameRounding			= 3.0f;
    	Style.IndentSpacing			= 8.0f;
    	Style.TabRounding			= 6.0f;
    	Style.ScrollbarSize			= 16.0f;
    	Style.ScrollbarRounding 	= 0.0f;
    }

    void IImGuiRenderer::Deinitialize()
    {
    	ClearSnapshots();
    	ImGui::DestroyContext();
    }

    void IImGuiRenderer::StartFrame(const FUpdateContext& UpdateContext)
    {
    	OnStartFrame(UpdateContext);
    }

    void IImGuiRenderer::ClearSnapshots()
    {
        for (FImDrawDataSnapshot& Snapshot : Snapshots)
        {
            Snapshot.Clear();
        }
    }

    FImDrawDataSnapshot* IImGuiRenderer::BuildFrame_GameThread(uint8 FrameIndex)
    {
        LUMINA_PROFILE_SCOPE();

        const uint8 Slot = FrameIndex % FRAMES_IN_FLIGHT;

        // Must wait: game thread wrapping the ring while render thread reads it → null draw lists.
        WaitForSnapshotSlot(Slot, SnapshotProduced[Slot].load(std::memory_order_acquire));

        ImGuiIO& Io = ImGui::GetIO();
        Io.DisplaySize.x = static_cast<float>(FEngine::GetEngineViewport()->GetSize().x);
        Io.DisplaySize.y = static_cast<float>(FEngine::GetEngineViewport()->GetSize().y);

        ImGuiX::Notifications::Render();
        ImGui::Render();

        ProcessTextureUpdates_GameThread();

        // Do NOT call RenderPlatformWindowsDefault: cross-thread present ordering trips SyncVal WRITE_AFTER_PRESENT.
        if (Io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            CaptureViewports_GameThread(Slot);
        }

        FImDrawDataSnapshot& Snapshot = Snapshots[Slot];
        Snapshot.SnapUsingSwap(ImGui::GetDrawData(), ImGui::GetTime());
        if (!Snapshot.IsValid())
        {
            // Still bump produced -- the render thread won't try to read this
            // slot, but a Signal pair must follow every Wait for the counters
            // to stay paired across slots.
            SnapshotProduced[Slot].fetch_add(1, std::memory_order_release);
            SignalSnapshotSlotConsumed(FrameIndex);
            return nullptr;
        }

        // Null Textures: render thread must not re-iterate the live PlatformIO.Textures we just processed (device lost).
        if (ImDrawData* SnapshotDrawData = Snapshot.GetDrawData())
        {
            SnapshotDrawData->Textures = nullptr;
        }

        Snapshot.ReferencedImages.clear();
        FillReferencedImagesSnapshot(Snapshot.ReferencedImages);

        SnapshotProduced[Slot].fetch_add(1, std::memory_order_release);
        return &Snapshot;
    }

    void IImGuiRenderer::RecordFrame_RenderThread(ICommandList& CmdList, FImDrawDataSnapshot& Snapshot, uint8 FrameIndex)
    {
        LUMINA_PROFILE_SCOPE();

        OnEndFrame(CmdList, Snapshot);
        RenderViewports_RenderThread(FrameIndex % FRAMES_IN_FLIGHT);
    }

    void IImGuiRenderer::SignalSnapshotSlotConsumed(uint8 FrameIndex)
    {
        const uint8 Slot = FrameIndex % FRAMES_IN_FLIGHT;
        SnapshotConsumed[Slot].fetch_add(1, std::memory_order_release);
        { std::lock_guard<std::mutex> Lock(SnapshotSlotMutex); }
        SnapshotSlotCV.notify_all();
    }

    void IImGuiRenderer::WaitForSnapshotSlot(uint8 Slot, uint64 Target)
    {
        if (SnapshotConsumed[Slot].load(std::memory_order_acquire) >= Target)
        {
            return;
        }
        LUMINA_PROFILE_SECTION_COLORED("WaitForImGuiSnapshotSlot", tracy::Color::Crimson);
        std::unique_lock<std::mutex> Lock(SnapshotSlotMutex);
        SnapshotSlotCV.wait(Lock, [&]()
        {
            return SnapshotConsumed[Slot].load(std::memory_order_acquire) >= Target;
        });
    }
}