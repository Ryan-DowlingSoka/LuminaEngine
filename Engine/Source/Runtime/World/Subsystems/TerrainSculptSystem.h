#pragma once

#include "Core/Math/Math.h"
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
        FVector3           WorldPosition = FVector3(0.0f);
        /** Owning terrain entity's transform origin. The terrain is centered on this. */
        FVector3           TerrainOrigin = FVector3(0.0f);
        float               Radius        = 0.0f;
        float               Strength      = 0.0f;
        float               Falloff       = 0.5f;
        float               FlattenHeight = 0.0f;
        float               DeltaSeconds  = 0.0f;
        int32               ActiveLayer   = 0;
        int8                SculptSign    = 1;

        /** Noise mode parameters (world-space frequency in 1/units, octaves >= 1). */
        float               NoiseFrequency = 1.0f / 512.0f;
        int32               NoiseOctaves   = 4;

        /** Ramp mode endpoints in world space; only XZ used. Heights are sampled from
         *  the heightmap at the endpoints unless RampUseExplicitHeights is set. */
        FVector3           RampStart      = FVector3(0.0f);
        FVector3           RampEnd        = FVector3(0.0f);
        bool                RampUseExplicitHeights = false;
        float               RampStartHeight = 0.0f;
        float               RampEndHeight   = 0.0f;
        /** Half-width of the ramp's linear core (world units). Beyond this, falloff applies. */
        float               RampHalfWidth   = 0.0f;
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
        static FIntVector2 WorldToSample(const STerrainComponent& Terrain, const FVector3& WorldPos);

        /** Raycast a ray against the current heightmap and return the hit point if any.
         *  TerrainOrigin is the owning entity's world-space transform position; the terrain
         *  is centered on it, matching the renderer's OriginXZ = TerrainOrigin.xz - HalfSize. */
        static bool Raycast(const STerrainComponent& Terrain, const FVector3& TerrainOrigin, const FVector3& RayOrigin, const FVector3& RayDir, FVector3& OutHit);

    private:
        static void ApplySculpt (STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const FIntVector4& Rect);
        static void ApplyFlatten(STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const FIntVector4& Rect);
        static void ApplySmooth (STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const FIntVector4& Rect);
        static void ApplyNoise  (STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const FIntVector4& Rect);
        static void ApplyRamp   (STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const FIntVector4& Rect);
        static void ApplyPaint  (STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const FIntVector4& Rect);
    };
}
