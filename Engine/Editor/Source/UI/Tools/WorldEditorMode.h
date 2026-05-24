#pragma once

#define USE_IMGUI_API
#include <imgui.h>

namespace Lumina
{
    class CWorld;
    struct SCameraComponent;

    /**
     * Host-provided services a mode can call into. Lets a mode wrap an interaction
     * (e.g. a terrain sculpt stroke) in the editor's undo transaction without taking
     * a hard dependency on the concrete tool class.
     */
    class IEditorModeContext
    {
    public:
        virtual ~IEditorModeContext() = default;

        /** Capture the pre-interaction state for undo. Pair with EndModeTransaction. */
        virtual void BeginModeTransaction() = 0;

        /** Capture the post-interaction state and push it onto the undo stack. */
        virtual void EndModeTransaction(const char* Name) = 0;
    };

    /**
     * Mutually-exclusive viewport input mode for the world editor.
     *
     * Exactly one mode is active at a time. The host (FWorldEditorTool) routes
     * per-frame Tick, viewport overlay, and toolbar drawing to the active mode
     * and suppresses its own click-to-select, drag-marquee, and transform
     * gizmo handling when ConsumesViewportInput() is true. This is the seam
     * for future modes (foliage, painting, splines, etc.) — they only need to
     * implement this interface and register themselves on the tool.
     */
    class IWorldEditorMode
    {
    public:
        virtual ~IWorldEditorMode() = default;

        /** Short label shown on the mode-selector button. */
        virtual const char* GetDisplayName() const = 0;

        /** Optional hover tooltip shown on the mode-selector button. */
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

        /** When true, the host suppresses its built-in click-to-select, drag-marquee
         *  and transform-gizmo handling so this mode owns the viewport input fully.
         *  The default Selection mode returns false; specialized modes (terrain,
         *  foliage paint, etc.) return true. */
        virtual bool ConsumesViewportInput() const { return true; }

        /** Host injects itself after construction so modes can reach editor services. */
        void SetContext(IEditorModeContext* InContext) { Context = InContext; }

    protected:

        IEditorModeContext* Context = nullptr;
    };

    /**
     * Default mode: standard select / gizmo / drag-marquee interaction handled by
     * the host. This mode is a placeholder so the mode registry has a stable
     * "click stuff" entry; all the actual selection logic lives in
     * FWorldEditorTool because it shares state with the outliner, details
     * panel, and undo system.
     */
    class FSelectionEditorMode final : public IWorldEditorMode
    {
    public:
        const char* GetDisplayName() const override { return "Select"; }
        const char* GetTooltip() const override
        {
            return "Click to pick entities; drag to marquee-select; use the transform gizmo.";
        }
        bool ConsumesViewportInput() const override { return false; }
    };
}
