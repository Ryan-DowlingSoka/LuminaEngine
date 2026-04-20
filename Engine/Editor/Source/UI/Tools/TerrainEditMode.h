#pragma once

#define USE_IMGUI_API
#include <imgui.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "World/Entity/Components/TerrainBrushComponent.h"

namespace Lumina
{
    class CWorld;
    struct SCameraComponent;

    /**
     * Owns the terrain edit mode UI state for the world editor.
     */
    class FTerrainEditMode
    {
    public:
        bool IsActive() const { return bActive; }
        void SetActive(bool bInActive)  { bActive = bInActive; }

        /** Called from FWorldEditorTool::DrawViewportToolbar. Draws the mode toggle and brush settings. */
        void DrawToolbar(CWorld* World, float ButtonSize);

        /** Called from FWorldEditorTool::DrawViewportOverlayElements. Draws the brush cursor ring. */
        void DrawOverlay(CWorld* World, ImVec2 ViewportScreenOrigin, ImVec2 ViewportSize, const SCameraComponent& Camera);

        /** Called once per frame from FWorldEditorTool::Update while the viewport is focused. */
        void Tick(CWorld* World, float DeltaSeconds, const SCameraComponent& Camera, bool bViewportHovered, ImVec2 ViewportScreenOrigin, ImVec2 ViewportSize);

        /** Drop a fresh flat terrain entity at the world origin. */
        static entt::entity CreateDefaultTerrain(CWorld* World);

    private:
        bool  bActive       = false;
        bool  bShowSettings = true;

        ETerrainBrushMode Mode = ETerrainBrushMode::Sculpt;
        float Radius          = 512.0f;
        float Strength        = 2.0f;
        float Falloff         = 0.5f;
        float FlattenHeight   = 0.0f;
        int32 ActiveLayer     = 0;

        glm::vec3 LastHit     = glm::vec3(0.0f);
        bool      bHitValid   = false;

        // Reused so we act on whatever terrain entity is currently in the world.
        entt::entity FindPreferredTerrain(CWorld* World) const;
    };
}
