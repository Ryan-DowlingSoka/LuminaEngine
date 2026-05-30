#pragma once

#define USE_IMGUI_API
#include <imgui.h>
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"

namespace Lumina
{
    class CWorld;
    struct SCameraComponent;

    // Host services a mode can call into: wrap an interaction in the editor's undo
    // transaction without depending on the concrete tool class.
    class IEditorModeContext
    {
    public:
        virtual ~IEditorModeContext() = default;

        /** Capture the pre-interaction state for undo. Pair with EndModeTransaction. */
        virtual void BeginModeTransaction() = 0;

        /** Capture the post-interaction state and push it onto the undo stack. */
        virtual void EndModeTransaction(const char* Name) = 0;
    };

    // Mutually-exclusive viewport input mode. Host routes Tick/overlay/toolbar to the
    // active mode and suppresses its own input when ConsumesViewportInput() is true.
    class IWorldEditorMode
    {
    public:
        virtual ~IWorldEditorMode() = default;

        /** Short label shown on the mode-selector dropdown. */
        virtual const char* GetDisplayName() const = 0;

        /** Glyph shown beside the label in the mode-selector dropdown. */
        virtual const char* GetIcon() const { return LE_ICON_CURSOR_DEFAULT; }

        /** Optional hover tooltip shown on the mode-selector dropdown. */
        virtual const char* GetTooltip() const { return nullptr; }

        /** Called once when the user switches into this mode. */
        virtual void OnEnter(CWorld* World) { (void)World; }

        /** Called once when the user switches out of this mode. */
        virtual void OnExit(CWorld* World) { (void)World; }

        /** Per-frame input + state update. Called only while this mode is active. */
        virtual void Tick(CWorld* World,
                          const SCameraComponent& Camera,
                          bool bViewportHovered,
                          ImVec2 ViewportScreenOrigin,
                          ImVec2 ViewportSize)
        {
            (void)World; (void)Camera; (void)bViewportHovered;
            (void)ViewportScreenOrigin; (void)ViewportSize;
        }

        /** Draws into the viewport (brush rings, mode-specific gizmos, debug lines). */
        virtual void DrawOverlay(CWorld* World,
                                 ImVec2 ViewportScreenOrigin,
                                 ImVec2 ViewportSize,
                                 const SCameraComponent& Camera)
        {
            (void)World; (void)ViewportScreenOrigin; (void)ViewportSize; (void)Camera;
        }

        /** Mode-specific buttons shown beneath the mode-selector bar while active. */
        virtual void DrawToolbar(CWorld* World, float ButtonSize)
        {
            (void)World; (void)ButtonSize;
        }

        /** When true, this mode owns viewport input fully (host suppresses select/marquee/gizmo).
         *  Selection mode returns false; specialized modes return true. */
        virtual bool ConsumesViewportInput() const { return true; }

        /** Host injects itself after construction so modes can reach editor services. */
        void SetContext(IEditorModeContext* InContext) { Context = InContext; }

    protected:

        IEditorModeContext* Context = nullptr;
    };

    // Default mode: select/gizmo/marquee handled by the host. Placeholder so the registry
    // has a stable entry; actual selection logic lives in FWorldEditorTool.
    class FSelectionEditorMode final : public IWorldEditorMode
    {
    public:
        const char* GetDisplayName() const override { return "Select"; }
        const char* GetIcon() const override { return LE_ICON_CURSOR_DEFAULT; }
        const char* GetTooltip() const override
        {
            return "Click to pick entities; drag to marquee-select; use the transform gizmo.";
        }
        bool ConsumesViewportInput() const override { return false; }
    };
}
