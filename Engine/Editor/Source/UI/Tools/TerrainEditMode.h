#pragma once

#define USE_IMGUI_API
#include <imgui.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "WorldEditorMode.h"
#include "World/Entity/Components/TerrainBrushComponent.h"

namespace Lumina
{
    class CWorld;
    struct SCameraComponent;
    struct STerrainComponent;

    /**
     * Viewport mode for sculpting + painting terrain. Owns the brush settings
     * window, cursor ring overlay, and per-frame stroke integration. Activation
     * is managed by the host's mode registry; the mode itself no longer carries
     * an on/off toggle.
     */
    class FTerrainEditMode final : public IWorldEditorMode
    {
    public:
        const char* GetDisplayName() const override { return "Terrain"; }
        const char* GetTooltip() const override
        {
            return "Sculpt, smooth, ramp, noise, and paint layers on the active terrain.";
        }

        void OnEnter(CWorld* World) override;

        /** Mode-specific brush toolbar shown beneath the mode-selector bar. */
        void DrawToolbar(CWorld* World, float ButtonSize) override;

        /** Brush cursor ring + ramp preview. */
        void DrawOverlay(CWorld* World, ImVec2 ViewportScreenOrigin, ImVec2 ViewportSize, const SCameraComponent& Camera) override;

        /** Cursor raycast, key shortcuts, stroke dispatch. */
        void Tick(CWorld* World, const SCameraComponent& Camera, bool bViewportHovered, ImVec2 ViewportScreenOrigin, ImVec2 ViewportSize) override;

        /** Drop a fresh flat terrain entity at the world origin. */
        static entt::entity CreateDefaultTerrain(CWorld* World);

    private:
        bool  bShowSettings = true;

        ETerrainBrushMode Mode = ETerrainBrushMode::Sculpt;
        float Radius          = 512.0f;
        float Strength        = 2.0f;
        float Falloff         = 0.5f;
        float FlattenHeight   = 0.0f;
        int32 ActiveLayer     = 0;

        // Sculpt direction sign; held key (LAlt) momentarily flips it during a stroke.
        // Replaces the older Shift-modifier so Shift remains free for other shortcuts.
        bool  bInvertSculpt   = false;

        // Dab spacing as a fraction of brush radius. A new dab fires once the cursor
        // has moved at least this many radii from the last dab, keeping strength
        // independent of mouse polling rate / FPS.
        float Spacing         = 0.25f;

        // Noise brush controls.
        float NoiseFrequency  = 1.0f / 512.0f;
        int32 NoiseOctaves    = 4;

        // Ramp brush state. Stroke captures Start on mousedown and updates End each
        // frame until release; dabs apply the current Start->End line.
        bool      bRampStarted   = false;
        glm::vec3 RampStart      = glm::vec3(0.0f);
        glm::vec3 RampEnd        = glm::vec3(0.0f);
        float     RampHalfWidth  = 256.0f;

        glm::vec3 LastHit     = glm::vec3(0.0f);
        bool      bHitValid   = false;

        // Stroke continuity tracked across frames for spacing.
        bool      bStrokeActive   = false;
        bool      bHasLastDabPos  = false;
        glm::vec3 LastDabWorldPos = glm::vec3(0.0f);

        // Reused so we act on whatever terrain entity is currently in the world.
        entt::entity FindPreferredTerrain(CWorld* World) const;

        void DrawLayerPanel(STerrainComponent& Terrain);
        static void EnsureLayerWeightStorage(STerrainComponent& Terrain);
        static void SwapLayers(STerrainComponent& Terrain, int32 A, int32 B);
        static void RemoveLayer(STerrainComponent& Terrain, int32 Index);
    };
}
