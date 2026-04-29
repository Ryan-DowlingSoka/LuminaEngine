#include "RmlUiEditorTool.h"

#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Renderer/CommandList.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RHIGlobals.h"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiRenderer.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "UI/RmlUiBridge.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Lumina
{
    namespace
    {
        const char* RmlEditorWindowName  = "RmlEditor";
        const char* RmlPreviewWindowName = "RmlPreview";

        FString DisplayNameFromPath(FStringView Path)
        {
            const FStringView Name = VFS::FileName(Path);
            return FString(Name.data(), Name.size());
        }

        // Standard 16:9 game-UI resolutions plus 4K. Index 0 = "Fit to pane".
        struct FResolutionPreset
        {
            const char* Label;
            uint32      Width;
            uint32      Height;
        };
        const FResolutionPreset ResolutionPresets[] =
        {
            { "Fit to pane",   0u,    0u    },
            { "1280x720",      1280u, 720u  },
            { "1920x1080",     1920u, 1080u },
            { "2560x1440",     2560u, 1440u },
            { "3840x2160",     3840u, 2160u },
            { "Custom",        0u,    0u    },
        };
        constexpr int CustomPresetIndex = (int)(sizeof(ResolutionPresets) / sizeof(ResolutionPresets[0])) - 1;

        ImU32 ToU32(const ImVec4& C) { return ImGui::ColorConvertFloat4ToU32(C); }

        // RML/RCSS identifier rule: standard XID-style start, hyphens allowed
        // anywhere after (so `font-size`, `border-top-left-radius` highlight
        // as one token, not three).
        TextEditor::Iterator GetRmlIdentifier(TextEditor::Iterator start, TextEditor::Iterator end)
        {
            if (start < end && TextEditor::CodePoint::isXidStart(*start))
            {
                start++;
                while (start < end && (TextEditor::CodePoint::isXidContinue(*start) || *start == '-'))
                {
                    start++;
                }
            }
            return start;
        }

        // Lazy-init the RML/RCSS language once. Static so multiple editor
        // instances share the same definition. Highlights:
        //   - <!-- ... --> as comments (multi-line)
        //   - "..." and '...' attribute values as strings
        //   - common RML tag names as keywords
        //   - common RCSS property names as known-identifiers
        // Limitations: embedded /* ... */ RCSS comments aren't colored
        // (TextEditor's Language only supports one multi-line comment pair).
        const TextEditor::Language* GetRmlLanguage()
        {
            static bool Initialized = false;
            static TextEditor::Language Lang;
            if (Initialized)
            {
                return &Lang;
            }

            Lang.name = "RML/RCSS";
            Lang.caseSensitive = false;
            // Use /* */ as the built-in multi-line comment so CSS comments
            // get proper cross-line state tracking. HTML <!-- --> is handled
            // by the custom tokenizer below for the (much more common)
            // single-line case. A multi-line HTML comment in an RML file
            // won't carry comment styling onto its continuation lines, but
            // the spill-into-strings bug we'd otherwise see (apostrophes in
            // commentary opening a quoted string) is fully avoided.
            Lang.commentStart = "/*";
            Lang.commentEnd = "*/";
            Lang.hasSingleQuotedStrings = true;
            Lang.hasDoubleQuotedStrings = true;
            Lang.stringEscape = '\\';
            Lang.getIdentifier = GetRmlIdentifier;

            // Single-line <!-- ... --> recognition + CSS hex color literals.
            // The hex case gives the inline color-swatch overlay a reliable
            // anchor and stops `#abcdef` from being lexed as an identifier.
            // The iterator only exposes ++ and comparisons, so step manually.
            Lang.customTokenizer = [](TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color& color) -> TextEditor::Iterator
            {
                // <!-- ... --> single-line HTML comment. We consume to the
                // closing --> if it's on this line, else to end-of-line.
                if (start != end && *start == '<')
                {
                    auto cursor = start;
                    ++cursor;
                    if (cursor != end && *cursor == '!')
                    {
                        ++cursor;
                        if (cursor != end && *cursor == '-')
                        {
                            ++cursor;
                            if (cursor != end && *cursor == '-')
                            {
                                ++cursor;
                                while (cursor != end)
                                {
                                    if (*cursor == '-')
                                    {
                                        auto a = cursor; ++a;
                                        if (a != end && *a == '-')
                                        {
                                            auto b = a; ++b;
                                            if (b != end && *b == '>')
                                            {
                                                cursor = b;
                                                ++cursor;
                                                break;
                                            }
                                        }
                                    }
                                    ++cursor;
                                }
                                color = TextEditor::Color::comment;
                                return cursor;
                            }
                        }
                    }
                }

                // # followed by 3, 4, 6, or 8 hex digits — CSS color literal.
                if (start != end && *start == '#')
                {
                    auto cursor = start;
                    ++cursor;
                    int digits = 0;
                    while (cursor != end && digits < 8)
                    {
                        const auto c = *cursor;
                        const bool isHex =
                            (c >= '0' && c <= '9') ||
                            (c >= 'a' && c <= 'f') ||
                            (c >= 'A' && c <= 'F');
                        if (!isHex)
                        {
                            break;
                        }
                        ++cursor;
                        ++digits;
                    }
                    if (digits == 3 || digits == 4 || digits == 6 || digits == 8)
                    {
                        color = TextEditor::Color::number;
                        return cursor;
                    }
                }

                return start;
            };

            // RML elements (HTML subset + RmlUI-specific widgets).
            static const char* const Tags[] = {
                "rml", "head", "body", "title", "link", "style", "script", "meta",
                "div", "span", "p", "br", "hr",
                "h1", "h2", "h3", "h4", "h5", "h6",
                "b", "i", "u", "em", "strong", "small", "sub", "sup",
                "a", "img", "icon",
                "ul", "ol", "li",
                "table", "tr", "td", "th", "thead", "tbody", "tfoot",
                "form", "input", "button", "select", "option", "textarea", "label",
                "tabset", "tab", "panels", "panel", "handle", "progressbar", "progress",
                "dataselect", "datagrid", "datagridrow", "datagridcell", "datagridheader",
                "template", "include",
            };
            for (const char* T : Tags) Lang.keywords.insert(T);

            // Common HTML/RML attribute names — colored as declarations so
            // `class=` and `id=` stand out from arbitrary identifiers.
            static const char* const Attributes[] = {
                "id", "class", "style", "src", "href", "type", "name", "value",
                "checked", "disabled", "readonly", "selected", "for",
                "onclick", "onchange", "onsubmit", "onfocus", "onblur",
                "onmouseover", "onmouseout", "onmousedown", "onmouseup",
                "onkeydown", "onkeyup", "onload", "data-model", "data-bind",
            };
            for (const char* A : Attributes) Lang.declarations.insert(A);

            // RCSS properties (CSS subset + RmlUI extensions).
            static const char* const Properties[] = {
                // Layout
                "display", "position", "top", "right", "bottom", "left",
                "margin", "margin-top", "margin-right", "margin-bottom", "margin-left",
                "padding", "padding-top", "padding-right", "padding-bottom", "padding-left",
                "width", "height", "min-width", "max-width", "min-height", "max-height",
                "box-sizing", "overflow", "overflow-x", "overflow-y", "z-index", "clip",
                // Flex
                "flex", "flex-direction", "flex-wrap", "flex-flow",
                "flex-grow", "flex-shrink", "flex-basis",
                "justify-content", "align-items", "align-self", "align-content", "gap",
                "row-gap", "column-gap",
                // Typography
                "font", "font-family", "font-size", "font-style", "font-weight",
                "line-height", "letter-spacing", "word-spacing",
                "text-align", "text-decoration", "text-transform", "white-space",
                "color", "opacity",
                // Background
                "background", "background-color", "background-image",
                // Borders
                "border", "border-color", "border-width", "border-style", "border-radius",
                "border-top", "border-right", "border-bottom", "border-left",
                "border-top-color", "border-right-color", "border-bottom-color", "border-left-color",
                "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
                "border-top-left-radius", "border-top-right-radius",
                "border-bottom-left-radius", "border-bottom-right-radius",
                // RmlUI-specific / animations
                "transition", "animation", "decorator", "font-effect",
                "perspective", "perspective-origin",
                "transform", "transform-origin",
                "image-color", "fill-image",
                "drag", "focus", "tab-index", "scrollbar-margin",
                "pointer-events", "cursor",
                "mix-blend-mode", "filter", "backdrop-filter",
                // Pseudo-property values used as identifiers in RCSS
                "none", "auto", "inherit", "initial",
                "block", "inline", "inline-block", "flex",
                "absolute", "relative", "fixed", "static",
                "row", "column", "row-reverse", "column-reverse",
                "wrap", "nowrap", "wrap-reverse",
                "flex-start", "flex-end", "center", "space-between", "space-around", "space-evenly",
                "stretch", "baseline",
                "hidden", "visible", "scroll",
                "bold", "italic", "normal", "underline",
            };
            for (const char* P : Properties) Lang.identifiers.insert(P);

            Initialized = true;
            return &Lang;
        }
    }

    FRmlUiEditorTool::FRmlUiEditorTool(IEditorToolContext* Context, FStringView InVirtualPath)
        : FAssetEditorTool(Context, DisplayNameFromPath(InVirtualPath))
        , VirtualPath(InVirtualPath.data(), InVirtualPath.size())
    {
        const FStringView ParentView = VFS::Parent(InVirtualPath, true);
        ParentDir = FString(ParentView.data(), ParentView.size());
    }

    void FRmlUiEditorTool::OnInitialize()
    {
        FAssetEditorTool::OnInitialize();

        ApplyEditorSettings();
        CodeEditor.SetLanguage(GetRmlLanguage());
        CodeEditor.SetPostRenderCallback([this] { DrawInlineColorSwatches(); });
        LoadFromDisk();

        char NameBuf[96];
        std::snprintf(NameBuf, sizeof(NameBuf), "rml_editor_%p", static_cast<void*>(this));

        const glm::uvec2 InitialSize{1280u, 720u};
        PreviewContext = RmlUi::CreateEditorContext(NameBuf, InitialSize);
        if (PreviewContext == nullptr)
        {
            LOG_ERROR("[RmlUiEditor] Failed to create preview context for '{}'.", VirtualPath.c_str());
        }
        else
        {
            RmlUi::SetEditorContextDpiScale(PreviewContext, PreviewDpiScale);
            // Start with a transparent clear so checker/solid bg can show through.
            RmlUi::SetEditorContextClearColor(PreviewContext, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        }

        ReloadDocument();
        StartWatching();

        // Delayed change callback: fires once after 250ms of edit-quiet.
        // Compare against the last-synced text to ignore programmatic SetText.
        CodeEditor.SetChangeCallback([this]
        {
            const std::string Current = CodeEditor.GetText();
            if (Current == LastSyncedText)
            {
                return;
            }
            bBufferDirty = true;
            if (bAutoReload)
            {
                ReloadDocument();
            }
        }, /*delay ms*/ 250);

        CreateToolWindow(RmlEditorWindowName, [this](bool bFocused)
        {
            DrawEditorToolbar();
            ImGui::Separator();

            const ImVec2 Avail = ImGui::GetContentRegionAvail();
            const float StatusBarHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
            const ImVec2 EditorSize(Avail.x, std::max(32.0f, Avail.y - StatusBarHeight));

            // Ctrl+wheel over the editor adjusts font scale. Steal the wheel
            // so TextEditor doesn't also use it for vertical scroll.
            const ImVec2 EditorMin = ImGui::GetCursorScreenPos();
            const ImVec2 EditorMax(EditorMin.x + EditorSize.x, EditorMin.y + EditorSize.y);
            ImGuiIO& Io = ImGui::GetIO();
            if (Io.KeyCtrl && Io.MouseWheel != 0.0f && ImGui::IsMouseHoveringRect(EditorMin, EditorMax))
            {
                EditorFontScale = std::clamp(EditorFontScale * (1.0f + Io.MouseWheel * 0.1f), 0.5f, 4.0f);
                Io.MouseWheel = 0.0f;
            }

            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Mono);
            ImGui::PushFontSize(ImGui::GetStyle().FontSizeBase * EditorFontScale);
            CodeEditor.Render("##rml_text", EditorSize);
            ImGui::PopFontSize();
            ImGuiX::Font::PopFont();

            DrawEditorStatusBar();
        });

        CreateToolWindow(RmlPreviewWindowName, [this](bool bFocused)
        {
            DrawPreviewToolbar();
            ImGui::Separator();
            DrawPreviewCanvas();
        });
    }

    void FRmlUiEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
        FileWatcher.Stop();
        TearDownPreview();
    }

    void FRmlUiEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        FAssetEditorTool::Update(UpdateContext);

        if (bExternalChangePending.exchange(false, Atomic::MemoryOrderAcquire))
        {
            if (!bBufferDirty)
            {
                LoadFromDisk();
                ReloadDocument();
            }
            else
            {
                LOG_WARN("[RmlUiEditor] '{}' changed on disk but buffer is dirty; ignoring.", VirtualPath.c_str());
            }
        }
    }

    void FRmlUiEditorTool::OnSave()
    {
        const std::string Body = CodeEditor.GetText();
        const FStringView View(Body.data(), Body.size());

        if (!VFS::WriteFile(FStringView(VirtualPath.c_str(), VirtualPath.size()), View))
        {
            ImGuiX::Notifications::NotifyError("Failed to save '{0}'.", VirtualPath.c_str());
            return;
        }

        LastSyncedText = Body;
        bBufferDirty = false;
        ImGuiX::Notifications::NotifySuccess("Saved '{0}'.", VirtualPath.c_str());

        ReloadDocument();
    }

    void FRmlUiEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGuiID LeftDockID = 0;
        ImGuiID RightDockID = 0;
        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.5f, &RightDockID, &LeftDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(RmlEditorWindowName).c_str(), LeftDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(RmlPreviewWindowName).c_str(), RightDockID);
    }

    void FRmlUiEditorTool::ApplyEditorSettings()
    {
        CodeEditor.SetTabSize(std::max(1, std::min(8, EditorTabSize)));
        CodeEditor.SetLineSpacing(EditorLineSpacing);
        CodeEditor.SetShowWhitespacesEnabled(bEditorShowWhitespace);
        CodeEditor.SetShowLineNumbersEnabled(bEditorShowLineNumbers);
        CodeEditor.SetReadOnlyEnabled(bEditorReadOnly);
        CodeEditor.SetPalette(EditorPalette == EPalette::Dark
            ? TextEditor::GetDarkPalette()
            : TextEditor::GetLightPalette());
    }

    // ---------------------------------------------------------------- editor ---

    void FRmlUiEditorTool::DrawEditorToolbar()
    {
        ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "%s", VirtualPath.c_str());
        ImGuiX::Font::PopFont();

        if (bBufferDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "  *unsaved");
        }

        ImGui::Spacing();

        if (ImGui::Button("Save"))
        {
            OnSave();
        }
        ImGuiX::TextTooltip("Write the buffer to disk (Ctrl+S).");

        ImGui::SameLine();
        if (ImGui::Button("Reload from Disk"))
        {
            LoadFromDisk();
            ReloadDocument();
        }
        ImGuiX::TextTooltip("Discard buffer changes and reload from disk.");

        ImGui::SameLine();
        if (ImGui::Button("Re-render Preview"))
        {
            ReloadDocument();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Auto-reload", &bAutoReload);
        ImGuiX::TextTooltip("Re-parse the buffer ~250ms after each edit.");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_COG " Editor Settings"))
        {
            ImGui::OpenPopup("##rml_editor_settings");
        }

        if (ImGui::BeginPopup("##rml_editor_settings"))
        {
            bool bDirty = false;

            ImGui::TextDisabled("Display");
            ImGui::Separator();

            if (ImGui::SliderFloat("Font scale", &EditorFontScale, 0.75f, 3.0f, "%.2fx")) {}

            if (ImGui::SliderFloat("Line spacing", &EditorLineSpacing, 1.0f, 2.0f, "%.2f")) bDirty = true;

            if (ImGui::SliderInt("Tab size", &EditorTabSize, 1, 8)) bDirty = true;

            if (ImGui::Checkbox("Show line numbers", &bEditorShowLineNumbers)) bDirty = true;
            if (ImGui::Checkbox("Show whitespace",   &bEditorShowWhitespace))  bDirty = true;
            if (ImGui::Checkbox("Read-only",         &bEditorReadOnly))        bDirty = true;

            ImGui::Spacing();
            ImGui::TextDisabled("Theme");
            ImGui::Separator();

            int PaletteIdx = (int)EditorPalette;
            const char* PaletteLabels[] = { "Dark", "Light" };
            if (ImGui::Combo("Palette", &PaletteIdx, PaletteLabels, IM_ARRAYSIZE(PaletteLabels)))
            {
                EditorPalette = (EPalette)PaletteIdx;
                bDirty = true;
            }

            ImGui::Spacing();
            if (ImGui::Button("Reset to defaults", ImVec2(-1, 0)))
            {
                EditorFontScale = 1.25f;
                EditorTabSize = 4;
                EditorLineSpacing = 1.0f;
                bEditorShowWhitespace = false;
                bEditorShowLineNumbers = true;
                bEditorReadOnly = false;
                EditorPalette = EPalette::Dark;
                bDirty = true;
            }

            if (bDirty)
            {
                ApplyEditorSettings();
            }

            ImGui::EndPopup();
        }
    }

    void FRmlUiEditorTool::DrawEditorStatusBar()
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 25, 200));
        if (ImGui::BeginChild("##rml_editor_status", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            const TextEditor::CursorPosition Pos = CodeEditor.GetCurrentCursorPosition();
            const int LineCount = CodeEditor.GetLineCount();
            const std::string Body = CodeEditor.GetText();
            const size_t Bytes = Body.size();

            ImGui::Text("Ln %d, Col %d", Pos.line + 1, Pos.column + 1);
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%d lines", LineCount);
            ImGui::SameLine(0, 20);
            if (Bytes >= 1024)
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%.1f KB", float(Bytes) / 1024.0f);
            }
            else
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%zu B", Bytes);
            }

            if (bEditorReadOnly)
            {
                ImGui::SameLine(0, 20);
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "READ-ONLY");
            }

            if (bBufferDirty)
            {
                ImGui::SameLine(0, 20);
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "modified");
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // --------------------------------------------------------------- preview ---

    void FRmlUiEditorTool::DrawPreviewToolbar()
    {
        // Resolution preset.
        ImGui::TextUnformatted("Canvas:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);

        const char* Items[IM_ARRAYSIZE(ResolutionPresets)];
        for (int i = 0; i < IM_ARRAYSIZE(ResolutionPresets); ++i) Items[i] = ResolutionPresets[i].Label;

        if (ImGui::Combo("##rml_resolution", &ResolutionPreset, Items, IM_ARRAYSIZE(Items)))
        {
            if (ResolutionPreset != CustomPresetIndex)
            {
                CanvasWidth = ResolutionPresets[ResolutionPreset].Width;
                CanvasHeight = ResolutionPresets[ResolutionPreset].Height;
            }
        }
        ImGuiX::TextTooltip("Render canvas resolution. The preview pane scales the canvas to fit; use View Zoom for 1:1 inspection.");

        if (ResolutionPreset == CustomPresetIndex)
        {
            ImGui::SameLine();
            int W = (int)CanvasWidth, H = (int)CanvasHeight;
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::DragInt("##rml_w", &W, 4.0f, 16, 7680, "W %d")) CanvasWidth  = (uint32)std::max(16, W);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::DragInt("##rml_h", &H, 4.0f, 16, 4320, "H %d")) CanvasHeight = (uint32)std::max(16, H);
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("|  DPI:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::SliderFloat("##rml_dpi", &PreviewDpiScale, 0.5f, 4.0f, "%.2fx"))
        {
            if (PreviewContext != nullptr) RmlUi::SetEditorContextDpiScale(PreviewContext, PreviewDpiScale);
        }
        ImGuiX::TextTooltip("Density-independent pixel ratio. RML/RCSS dp-sized layout grows with this.");

        ImGui::SameLine();
        ImGui::TextUnformatted("|  View:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("##rml_view", &ViewZoom, 0.1f, 4.0f, "%.2fx");
        ImGuiX::TextTooltip("Pan with middle-drag, zoom with Ctrl+wheel, double-click to reset.");

        ImGui::SameLine();
        if (ImGui::SmallButton("Reset View"))
        {
            ViewZoom = 1.0f;
            ViewPan = ImVec2(0, 0);
        }

        // Second row.
        ImGui::Spacing();

        if (ImGui::Button(LE_ICON_PALETTE " Background"))
        {
            ImGui::OpenPopup("##rml_bg_popup");
        }
        if (ImGui::BeginPopup("##rml_bg_popup"))
        {
            int Mode = (int)BgMode;
            const char* Modes[] = { "Checker", "Solid", "Transparent" };
            if (ImGui::Combo("Mode", &Mode, Modes, IM_ARRAYSIZE(Modes))) BgMode = (EBgMode)Mode;
            if (BgMode == EBgMode::Solid)
            {
                ImGui::ColorEdit4("Color", &BgColor.x, ImGuiColorEditFlags_NoInputs);
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Grid", &bShowGrid);
        if (bShowGrid)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            ImGui::DragFloat("##rml_grid_size", &GridSize, 1.0f, 4.0f, 512.0f, "%.0fpx");
            ImGui::SameLine();
            ImGui::ColorEdit4("##rml_grid_color", &GridColor.x, ImGuiColorEditFlags_NoInputs);
            ImGuiX::TextTooltip("Canvas-space grid for layout alignment.");
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        ImGui::Checkbox("Safe zones", &bShowSafeZones);
        if (bShowSafeZones)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::SliderFloat("##rml_safe_action", &SafeZoneAction, 0.50f, 1.0f, "Act %.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::SliderFloat("##rml_safe_title", &SafeZoneTitle, 0.50f, 1.0f, "Tit %.2f");
            ImGui::SameLine();
            ImGui::ColorEdit4("##rml_safe_color", &SafeZoneColor.x, ImGuiColorEditFlags_NoInputs);
            ImGuiX::TextTooltip("Inner rectangle: title-safe (text/HUD). Outer: action-safe (interactive elements).");
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        ImGui::Checkbox("Rulers", &bShowRulers);
    }

    void FRmlUiEditorTool::DrawPreviewCanvas()
    {
        const ImVec2 Pane = ImGui::GetContentRegionAvail();
        if (Pane.x < 16.0f || Pane.y < 16.0f)
        {
            return;
        }

        // Resolve canvas size: 0,0 means "fit to pane".
        uint32 EffW = CanvasWidth;
        uint32 EffH = CanvasHeight;
        if (EffW == 0 || EffH == 0)
        {
            EffW = (uint32)std::max(16.0f, Pane.x);
            EffH = (uint32)std::max(16.0f, Pane.y);
        }
        EnsurePreviewTarget(EffW, EffH);

        if (PreviewTarget == nullptr || PreviewContext == nullptr)
        {
            ImGui::TextDisabled("Preview unavailable.");
            return;
        }

        // Push the bridge clear color according to the chosen background.
        // For checker/transparent we clear to alpha=0 so ImGui composites
        // bg below the image. For solid we clear with the chosen color.
        glm::vec4 ClearColor;
        switch (BgMode)
        {
        case EBgMode::Solid:       ClearColor = glm::vec4(BgColor.x, BgColor.y, BgColor.z, 1.0f); break;
        case EBgMode::Checker:     // fallthrough — we draw the checker in ImGui below
        case EBgMode::Transparent: ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); break;
        }
        RmlUi::SetEditorContextClearColor(PreviewContext, ClearColor);
        RmlUi::SetEditorContextTarget(PreviewContext, PreviewTarget.GetReference(), glm::uvec2(PreviewWidth, PreviewHeight));

        // Scrollable / pan child for the canvas.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(18, 18, 22, 255));
        ImGui::BeginChild("##rml_canvas_view", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);

        const ImVec2 PaneMin = ImGui::GetWindowPos();
        const ImVec2 PaneSize = ImGui::GetWindowSize();
        ImDrawList* DL = ImGui::GetWindowDrawList();

        // --- Pan/zoom input ---------------------------------------------------
        if (ImGui::IsWindowHovered())
        {
            ImGuiIO& Io = ImGui::GetIO();
            if (Io.KeyCtrl && Io.MouseWheel != 0.0f)
            {
                const float Old = ViewZoom;
                ViewZoom = std::clamp(ViewZoom * (1.0f + Io.MouseWheel * 0.1f), 0.1f, 8.0f);
                // Pan-correct so zoom is centered on the mouse cursor.
                const ImVec2 Mouse = Io.MousePos;
                const ImVec2 Center(PaneMin.x + PaneSize.x * 0.5f, PaneMin.y + PaneSize.y * 0.5f);
                ViewPan.x = (ViewPan.x - (Mouse.x - Center.x)) * (ViewZoom / Old) + (Mouse.x - Center.x);
                ViewPan.y = (ViewPan.y - (Mouse.y - Center.y)) * (ViewZoom / Old) + (Mouse.y - Center.y);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            {
                const ImVec2 D = Io.MouseDelta;
                ViewPan.x += D.x;
                ViewPan.y += D.y;
            }
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                ViewZoom = 1.0f;
                ViewPan = ImVec2(0, 0);
            }
        }

        // --- Canvas placement -------------------------------------------------
        // Fit the canvas inside the pane at View=1.0, then scale by ViewZoom.
        const float CanvasAspect = float(EffW) / float(EffH);
        const float PaneAspect   = PaneSize.x / std::max(1.0f, PaneSize.y);
        ImVec2 FitSize;
        if (CanvasAspect > PaneAspect)
        {
            FitSize.x = PaneSize.x;
            FitSize.y = PaneSize.x / CanvasAspect;
        }
        else
        {
            FitSize.y = PaneSize.y;
            FitSize.x = PaneSize.y * CanvasAspect;
        }
        const ImVec2 CanvasSize(FitSize.x * ViewZoom, FitSize.y * ViewZoom);
        const ImVec2 PaneCenter(PaneMin.x + PaneSize.x * 0.5f, PaneMin.y + PaneSize.y * 0.5f);
        const ImVec2 CanvasMin(
            PaneCenter.x - CanvasSize.x * 0.5f + ViewPan.x,
            PaneCenter.y - CanvasSize.y * 0.5f + ViewPan.y);
        const ImVec2 CanvasMax(CanvasMin.x + CanvasSize.x, CanvasMin.y + CanvasSize.y);

        // --- Background -------------------------------------------------------
        if (BgMode == EBgMode::Checker)
        {
            const float Cell = 12.0f;
            const ImU32 A = IM_COL32(48, 48, 52, 255);
            const ImU32 B = IM_COL32(36, 36, 40, 255);
            // Clip to canvas rect.
            DL->PushClipRect(CanvasMin, CanvasMax, true);
            for (float y = CanvasMin.y; y < CanvasMax.y; y += Cell)
            {
                for (float x = CanvasMin.x; x < CanvasMax.x; x += Cell)
                {
                    const bool Even = (int((x - CanvasMin.x) / Cell) + int((y - CanvasMin.y) / Cell)) & 1;
                    DL->AddRectFilled(ImVec2(x, y), ImVec2(x + Cell, y + Cell), Even ? A : B);
                }
            }
            DL->PopClipRect();
        }
        else if (BgMode == EBgMode::Solid)
        {
            DL->AddRectFilled(CanvasMin, CanvasMax, ToU32(BgColor));
        }
        // Transparent — draw nothing, the pane background shows through.

        // --- The render --------------------------------------------------------
        const ImTextureID Tex = GRenderManager->GetImGuiRenderer()->GetOrCreateImTexture(PreviewTarget.GetReference());
        DL->AddImage(Tex, CanvasMin, CanvasMax);
        DL->AddRect(CanvasMin, CanvasMax, IM_COL32(80, 80, 95, 255), 0.0f, 0, 1.0f);

        // --- Overlays ----------------------------------------------------------
        // Convert canvas-space px to pane-space px:
        const float ScalePx = CanvasSize.x / float(EffW);

        if (bShowGrid && GridSize > 0.0f)
        {
            const ImU32 GridU = ToU32(GridColor);
            const float Step = GridSize * ScalePx;
            DL->PushClipRect(CanvasMin, CanvasMax, true);
            for (float x = CanvasMin.x + Step; x < CanvasMax.x; x += Step)
            {
                DL->AddLine(ImVec2(x, CanvasMin.y), ImVec2(x, CanvasMax.y), GridU);
            }
            for (float y = CanvasMin.y + Step; y < CanvasMax.y; y += Step)
            {
                DL->AddLine(ImVec2(CanvasMin.x, y), ImVec2(CanvasMax.x, y), GridU);
            }
            DL->PopClipRect();
        }

        if (bShowSafeZones)
        {
            const ImU32 SafeU = ToU32(SafeZoneColor);
            auto DrawSafe = [&](float Frac)
            {
                const ImVec2 Sz(CanvasSize.x * Frac, CanvasSize.y * Frac);
                const ImVec2 A(CanvasMin.x + (CanvasSize.x - Sz.x) * 0.5f,
                               CanvasMin.y + (CanvasSize.y - Sz.y) * 0.5f);
                const ImVec2 B(A.x + Sz.x, A.y + Sz.y);
                DL->AddRect(A, B, SafeU, 0.0f, 0, 1.5f);
            };
            DrawSafe(SafeZoneAction);
            DrawSafe(SafeZoneTitle);
        }

        if (bShowRulers)
        {
            // Tick marks every 100 canvas px along top + left edges.
            const ImU32 RU = IM_COL32(180, 180, 200, 200);
            DL->PushClipRect(PaneMin, ImVec2(PaneMin.x + PaneSize.x, PaneMin.y + PaneSize.y), true);
            const float TickStep = 100.0f * ScalePx;
            for (float x = CanvasMin.x; x <= CanvasMax.x; x += TickStep)
            {
                DL->AddLine(ImVec2(x, CanvasMin.y - 6.0f), ImVec2(x, CanvasMin.y), RU);
            }
            for (float y = CanvasMin.y; y <= CanvasMax.y; y += TickStep)
            {
                DL->AddLine(ImVec2(CanvasMin.x - 6.0f, y), ImVec2(CanvasMin.x, y), RU);
            }
            DL->PopClipRect();
        }

        // HUD line (bottom-left).
        const float HudY = PaneMin.y + PaneSize.y - ImGui::GetTextLineHeightWithSpacing();
        DL->AddText(ImVec2(PaneMin.x + 8.0f, HudY),
                    IM_COL32(170, 170, 190, 220),
                    [&]
                    {
                        static char Buf[128];
                        std::snprintf(Buf, sizeof(Buf), "%ux%u  view %.2fx  dpi %.2fx", EffW, EffH, ViewZoom, PreviewDpiScale);
                        return Buf;
                    }());

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // --------------------------------------------------------------- helpers ---

    void FRmlUiEditorTool::LoadFromDisk()
    {
        FString Body;
        if (!VFS::ReadFile(Body, FStringView(VirtualPath.c_str(), VirtualPath.size())))
        {
            LOG_WARN("[RmlUiEditor] Could not read '{}'.", VirtualPath.c_str());
            CodeEditor.SetText("");
            LastSyncedText.clear();
            bBufferDirty = false;
            return;
        }

        // Our own OnSave fires the directory watcher and routes back here.
        // If the disk content matches what we last synced, the file hasn't
        // actually changed under us — skip SetText so the user's cursor,
        // selection, scroll, and undo/redo stack are preserved.
        if (Body.size() == LastSyncedText.size()
            && std::memcmp(Body.data(), LastSyncedText.data(), Body.size()) == 0)
        {
            bBufferDirty = false;
            return;
        }

        const std::string_view View(Body.c_str(), Body.size());
        CodeEditor.SetText(View);
        LastSyncedText.assign(Body.c_str(), Body.size());
        bBufferDirty = false;
    }

    void FRmlUiEditorTool::DrawInlineColorSwatches()
    {
        const int FirstLine = CodeEditor.GetFirstVisibleLine();
        const int LastLine  = CodeEditor.GetLastVisibleLine();
        const int LineCount = CodeEditor.GetLineCount();
        const int TabSize   = CodeEditor.GetTabSize();
        const float LineHeight = CodeEditor.GetLineHeight();
        const float GlyphWidth = CodeEditor.GetGlyphWidth();

        const float SwatchSize = std::max(8.0f, LineHeight - 4.0f);
        const float SwatchPad  = 2.0f;

        ImDrawList* DrawList = ImGui::GetWindowDrawList();

        auto HexDigit = [](char C) -> int
        {
            if (C >= '0' && C <= '9') return C - '0';
            if (C >= 'a' && C <= 'f') return 10 + (C - 'a');
            if (C >= 'A' && C <= 'F') return 10 + (C - 'A');
            return -1;
        };

        // Saved selection so we can restore it after ReplaceSectionText. Without
        // this the picker drag would jump the cursor to the hex token's end.
        const TextEditor::CursorPosition SavedCursor = CodeEditor.GetCurrentCursorPosition();

        for (int Line = FirstLine; Line <= LastLine && Line < LineCount; ++Line)
        {
            const std::string Text = CodeEditor.GetLineText(Line);
            const int Len = static_cast<int>(Text.size());

            // Walk byte-by-byte tracking visual column so tabs map correctly.
            int Column = 0;
            for (int i = 0; i < Len; )
            {
                const char C = Text[i];

                if (C == '#' && i + 1 < Len)
                {
                    // Count contiguous hex digits.
                    int Digits = 0;
                    while (i + 1 + Digits < Len && Digits < 8 && HexDigit(Text[i + 1 + Digits]) >= 0)
                    {
                        Digits++;
                    }

                    const bool ValidLength = (Digits == 3 || Digits == 4 || Digits == 6 || Digits == 8);
                    // Also require that the next char (if any) isn't a hex digit
                    // and isn't an identifier-continuation char like '_'. This
                    // avoids matching the "#abcd" prefix of `#abcdef` etc.
                    bool BoundaryOk = ValidLength;
                    if (BoundaryOk && i + 1 + Digits < Len)
                    {
                        const char Next = Text[i + 1 + Digits];
                        if (HexDigit(Next) >= 0 || Next == '_')
                        {
                            BoundaryOk = false;
                        }
                    }

                    if (BoundaryOk)
                    {
                        // Decode RGBA. Short forms (3/4) expand each digit to two.
                        auto Expand = [&](int Index) -> int
                        {
                            const int H = HexDigit(Text[i + 1 + Index]);
                            return (H << 4) | H;
                        };
                        int R, G, B, A = 255;
                        if (Digits == 3)
                        {
                            R = Expand(0); G = Expand(1); B = Expand(2);
                        }
                        else if (Digits == 4)
                        {
                            R = Expand(0); G = Expand(1); B = Expand(2); A = Expand(3);
                        }
                        else if (Digits == 6)
                        {
                            R = (HexDigit(Text[i + 1]) << 4) | HexDigit(Text[i + 2]);
                            G = (HexDigit(Text[i + 3]) << 4) | HexDigit(Text[i + 4]);
                            B = (HexDigit(Text[i + 5]) << 4) | HexDigit(Text[i + 6]);
                        }
                        else
                        {
                            R = (HexDigit(Text[i + 1]) << 4) | HexDigit(Text[i + 2]);
                            G = (HexDigit(Text[i + 3]) << 4) | HexDigit(Text[i + 4]);
                            B = (HexDigit(Text[i + 5]) << 4) | HexDigit(Text[i + 6]);
                            A = (HexDigit(Text[i + 7]) << 4) | HexDigit(Text[i + 8]);
                        }

                        const int TokenLen = 1 + Digits;
                        const int EndColumn = Column + TokenLen;
                        const ImVec2 EndPos = CodeEditor.GetScreenPosForCoordinate(Line, EndColumn);

                        const ImVec2 SwatchMin(EndPos.x + SwatchPad, EndPos.y + (LineHeight - SwatchSize) * 0.5f);
                        const ImVec2 SwatchMax(SwatchMin.x + SwatchSize, SwatchMin.y + SwatchSize);

                        // Stable per-token id so ImGui state survives reflow.
                        ImGui::PushID(Line * 4096 + i);

                        ImGui::SetCursorScreenPos(SwatchMin);
                        ImGui::InvisibleButton("##sw", ImVec2(SwatchSize, SwatchSize));
                        const bool Clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                        const bool Hovered = ImGui::IsItemHovered();

                        DrawList->AddRectFilled(SwatchMin, SwatchMax, IM_COL32(R, G, B, A), 2.0f);
                        DrawList->AddRect(SwatchMin, SwatchMax,
                            Hovered ? IM_COL32(255, 255, 255, 200) : IM_COL32(0, 0, 0, 200), 2.0f);

                        if (Clicked)
                        {
                            ImGui::OpenPopup("##rml_color_picker");
                        }

                        if (ImGui::BeginPopup("##rml_color_picker"))
                        {
                            float Col[4] = { R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f };
                            const ImGuiColorEditFlags Flags = (Digits == 4 || Digits == 8)
                                ? ImGuiColorEditFlags_AlphaBar
                                : ImGuiColorEditFlags_NoAlpha;

                            if (ImGui::ColorPicker4("##picker", Col, Flags))
                            {
                                const int NewR = static_cast<int>(std::round(Col[0] * 255.0f));
                                const int NewG = static_cast<int>(std::round(Col[1] * 255.0f));
                                const int NewB = static_cast<int>(std::round(Col[2] * 255.0f));
                                const int NewA = static_cast<int>(std::round(Col[3] * 255.0f));

                                char Buf[10];
                                if (Digits == 3 || Digits == 4)
                                {
                                    // Preserve short form when possible (high+low nibbles match).
                                    auto Compress = [](int V) -> int { return ((V >> 4) == (V & 0xF)) ? (V >> 4) : -1; };
                                    const int CR = Compress(NewR), CG = Compress(NewG), CB = Compress(NewB), CA = Compress(NewA);
                                    if (CR >= 0 && CG >= 0 && CB >= 0 && (Digits == 3 || CA >= 0))
                                    {
                                        if (Digits == 3)
                                        {
                                            std::snprintf(Buf, sizeof(Buf), "#%01X%01X%01X", CR, CG, CB);
                                        }
                                        else
                                        {
                                            std::snprintf(Buf, sizeof(Buf), "#%01X%01X%01X%01X", CR, CG, CB, CA);
                                        }
                                    }
                                    else
                                    {
                                        // Couldn't preserve short form — promote to long.
                                        if (Digits == 3)
                                        {
                                            std::snprintf(Buf, sizeof(Buf), "#%02X%02X%02X", NewR, NewG, NewB);
                                        }
                                        else
                                        {
                                            std::snprintf(Buf, sizeof(Buf), "#%02X%02X%02X%02X", NewR, NewG, NewB, NewA);
                                        }
                                    }
                                }
                                else if (Digits == 6)
                                {
                                    std::snprintf(Buf, sizeof(Buf), "#%02X%02X%02X", NewR, NewG, NewB);
                                }
                                else
                                {
                                    std::snprintf(Buf, sizeof(Buf), "#%02X%02X%02X%02X", NewR, NewG, NewB, NewA);
                                }

                                CodeEditor.ReplaceSectionText(Line, Column, Line, EndColumn, Buf);
                                CodeEditor.SetCursor(SavedCursor.line, SavedCursor.column);
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::PopID();

                        // Advance past the entire token in one go.
                        Column += TokenLen;
                        i += TokenLen;
                        continue;
                    }
                }

                // Default advance — tabs jump to next tab stop.
                if (C == '\t')
                {
                    Column += TabSize - (Column % TabSize);
                }
                else
                {
                    Column++;
                }
                i++;
            }
            (void)GlyphWidth; // silence unused
        }
    }

    void FRmlUiEditorTool::ReloadDocument()
    {
        if (PreviewContext == nullptr)
        {
            return;
        }

        const std::string Body = CodeEditor.GetText();
        LastSyncedText = Body;

        if (Body.empty())
        {
            RmlUi::ClearEditorContextDocument(PreviewContext);
            return;
        }

        const FStringView View(Body.data(), Body.size());
        const FStringView SourceUrl(VirtualPath.c_str(), VirtualPath.size());

        if (!RmlUi::ReplaceEditorContextDocument(PreviewContext, View, SourceUrl))
        {
            LOG_WARN("[RmlUiEditor] Failed to parse buffer for '{}'.", VirtualPath.c_str());
        }
    }

    void FRmlUiEditorTool::EnsurePreviewTarget(uint32 Width, uint32 Height)
    {
        if (Width == 0 || Height == 0)
        {
            return;
        }
        if (PreviewTarget != nullptr && PreviewWidth == Width && PreviewHeight == Height)
        {
            return;
        }

        FRHIImageDesc Desc;
        Desc.Format = EFormat::RGBA8_UNORM;
        Desc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);
        Desc.Extent = glm::uvec2(Width, Height);
        Desc.InitialState = EResourceStates::RenderTarget;
        Desc.bKeepInitialState = true;
        Desc.DebugName = "RmlUiEditorPreview";

        PreviewTarget = GRenderContext->CreateImage(Desc);
        PreviewWidth = Width;
        PreviewHeight = Height;

        if (PreviewContext != nullptr)
        {
            ReloadDocument();
        }
    }

    void FRmlUiEditorTool::TearDownPreview()
    {
        if (PreviewContext != nullptr)
        {
            RmlUi::ClearEditorContextDocument(PreviewContext);
            RmlUi::SetEditorContextTarget(PreviewContext, nullptr, glm::uvec2(0, 0));
            RmlUi::DestroyEditorContext(PreviewContext);
            PreviewContext = nullptr;
        }
        PreviewTarget = nullptr;
        PreviewWidth = 0;
        PreviewHeight = 0;
    }

    void FRmlUiEditorTool::StartWatching()
    {
        if (ParentDir.empty())
        {
            return;
        }

        FFixedString DiskParentDir;
        const FStringView TargetVirtual(VirtualPath.c_str(), VirtualPath.size());
        VFS::DirectoryIterator(FStringView(ParentDir.c_str(), ParentDir.size()),
            [&](const VFS::FFileInfo& Info)
            {
                if (!DiskParentDir.empty())
                {
                    return;
                }
                if (FStringView(Info.VirtualPath.c_str(), Info.VirtualPath.size()) != TargetVirtual)
                {
                    return;
                }
                FStringView Source(Info.PathSource.c_str(), Info.PathSource.size());
                const size_t Slash = Source.find_last_of('/');
                const size_t BackSlash = Source.find_last_of('\\');
                size_t Cut = FString::npos;
                if (Slash != FStringView::npos)
                {
                    Cut = Slash;
                }
                if (BackSlash != FStringView::npos && (Cut == FStringView::npos || BackSlash > Cut))
                {
                    Cut = BackSlash;
                }
                if (Cut == FStringView::npos)
                {
                    return;
                }
                DiskParentDir.assign(Source.data(), Cut);
            });

        if (DiskParentDir.empty())
        {
            return;
        }

        const FStringView FileNameView = VFS::FileName(TargetVirtual);
        const FString FileName(FileNameView.data(), FileNameView.size());

        FileWatcher.Watch(DiskParentDir, [this, FileName](const FFileEvent& Event)
        {
            if (Event.Action != EFileAction::Modified && Event.Action != EFileAction::Added)
            {
                return;
            }

            FStringView EventPath(Event.Path.c_str(), Event.Path.size());
            if (EventPath.size() < FileName.size())
            {
                return;
            }
            const FStringView Tail = EventPath.substr(EventPath.size() - FileName.size());
            if (Tail != FStringView(FileName.c_str(), FileName.size()))
            {
                return;
            }

            bExternalChangePending.store(true, Atomic::MemoryOrderRelease);
        }, false);
    }
}
