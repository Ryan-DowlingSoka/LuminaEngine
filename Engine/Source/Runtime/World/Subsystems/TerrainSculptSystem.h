#pragma once

#include <glm/glm.hpp>
#include "Containers/Array.h"
#include "Platform/GenericPlatform.h"
#include "World/Entity/Components/TerrainBrushComponent.h"

namespace Lumina
{
    struct STerrainComponent;

    /**
     * Describes a single brush dab captured from the editor during a stroke.
     * A drag produces many of these per second; the sculpt system replays them
     * against the CPU heightmap and marks the touched region dirty.
     */
    struct FTerrainSculptDab
    {
        ETerrainBrushMode   Mode          = ETerrainBrushMode::Sculpt;
        glm::vec3           WorldPosition = glm::vec3(0.0f);
        /** Owning terrain entity's transform origin. The terrain is centered on this. */
        glm::vec3           TerrainOrigin = glm::vec3(0.0f);
        float               Radius        = 0.0f;
        float               Strength      = 0.0f;
        float               Falloff       = 0.5f;
        float               FlattenHeight = 0.0f;
        float               DeltaSeconds  = 0.0f;
        int32               ActiveLayer   = 0;
        int8                SculptSign    = 1;
    };

    /**
     * Stateless utility that applies brush dabs to a terrain component. The work
     * is parallelized across the brush footprint rows via GTaskSystem so large
     * radii don't block the main thread. Each apply() marks exactly the touched
     * rectangle dirty so the render scene can upload only that region to the GPU.
     *
     * Thread-safety: one stroke is assumed to be in flight at a time (the editor
     * holds the mouse). Callers must not apply dabs concurrently on the same
     * component.
     */
    class RUNTIME_API FTerrainSculptSystem
    {
    public:
        /** Apply a single dab, dispatching row updates to worker threads. */
        static void ApplyDab(STerrainComponent& Terrain, const FTerrainSculptDab& Dab);

        /** Convert a world-space XYZ point to heightmap sample coordinates. */
        static glm::ivec2 WorldToSample(const STerrainComponent& Terrain, const glm::vec3& WorldPos);

        /** Raycast a ray against the current heightmap and return the hit point if any.
         *  TerrainOrigin is the owning entity's world-space transform position; the terrain
         *  is centered on it, matching the renderer's OriginXZ = TerrainOrigin.xz - HalfSize. */
        static bool Raycast(const STerrainComponent& Terrain, const glm::vec3& TerrainOrigin, const glm::vec3& RayOrigin, const glm::vec3& RayDir, glm::vec3& OutHit);

    private:
        static void ApplySculpt (STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const glm::ivec4& Rect);
        static void ApplyFlatten(STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const glm::ivec4& Rect);
        static void ApplySmooth (STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const glm::ivec4& Rect);
        static void ApplyPaint  (STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const glm::ivec4& Rect);
    };
}
