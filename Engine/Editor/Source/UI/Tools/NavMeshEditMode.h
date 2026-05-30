#pragma once

#define USE_IMGUI_API
#include <imgui.h>

#include "WorldEditorMode.h"

namespace Lumina
{
    class CWorld;
    struct SCameraComponent;

    // Navigation viewport mode: draws bake-volume bounds + a toolbar. Components auto-bake
    // (SNavMeshComponent::bAutoBake); doesn't capture input so bounds stay selectable.
    class FNavigationEditMode final : public IWorldEditorMode
    {
    public:
        const char* GetDisplayName() const override { return "Navigation"; }
        const char* GetIcon() const override { return LE_ICON_VECTOR_POLYGON; }
        const char* GetTooltip() const override
        {
            return "Navigation: nav-mesh bounds auto-bake on move; toggle the overlay or rebuild here.";
        }

        void DrawOverlay(CWorld* World, ImVec2 ViewportScreenOrigin, ImVec2 ViewportSize, const SCameraComponent& Camera) override;
        void DrawToolbar(CWorld* World, float ButtonSize) override;

        /** Selection/gizmo stay live so the user can position the bounds volume. */
        bool ConsumesViewportInput() const override { return false; }
    };
}
