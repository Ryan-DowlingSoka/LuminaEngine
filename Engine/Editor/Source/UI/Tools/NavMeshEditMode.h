#pragma once

#define USE_IMGUI_API
#include <imgui.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Lumina
{
    class CWorld;

    /**
     * World-editor sub-mode for placing / baking nav-mesh bounds. Mirrors
     * FTerrainEditMode's wiring: a toolbar button group ("+ NavMesh Bounds",
     * "Bake", "Show Mesh") and a viewport overlay that draws the bounds AABB
     * for every nav entity in the world so the user can position it via the
     * standard transform gizmo.
     */
    class FNavMeshEditMode
    {
    public:

        /** Drawn next to the terrain buttons in the world-editor viewport toolbar. */
        void DrawToolbar(CWorld* World, float ButtonSize);

        /** Editor-only AABB lines so the user can see + manipulate the bake volume. */
        void DrawOverlay(CWorld* World) const;

        /** Drop a fresh nav-bounds entity centered at WorldLocation. */
        static entt::entity CreateNavMeshBounds(CWorld* World, const glm::vec3& WorldLocation = glm::vec3(0.0f));
    };
}
