#pragma once

#include "Containers/Array.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/Math.h"

namespace Lumina
{
    /**
     * Coarse per-frame rejection test built before the parallel mesh-processing
     * tasks. Holds every bounding volume an instance could possibly contribute
     * to this frame: the main camera frustum, the sun-swept shadow frustum
     * (camera frustum extruded along the sun direction so casters outside the
     * camera but inside the CSM sweep still render to shadows), and one sphere
     * per shadow-casting point / spotlight.
     *
     */
    struct FSceneCullContext
    {
        /** World-space sphere for a shadow-casting light's influence region. */
        struct FLightSphere
        {
            FVector3   Center;
            float       Radius;
        };

        /** Main camera frustum. Anything inside this passes unconditionally. */
        FFrustum Frustum;

        /**
         * Camera frustum extruded by ShadowSweepDistance along SunDirection.
         * Encloses every point that could cast a sun shadow into the visible
         * region. Identical construction to CullData::ShadowFrustum; prebuilt
         * here so it's available during parallel mesh gathering instead of
         * only after AllocateShadowTiles. Valid when bHasSun is true.
         */
        FFrustum SunShadowFrustum;

        FVector3 SunDirection = FVector3(0.0f);

        /**
         * One entry per shadow-casting point / spot light. Sized by the
         * parent renderer before dispatch; kept inline to avoid another
         * allocation on the hot path. Cap matches the typical shadow budget
         * (atlas can fit ~20 local lights at once) with slack.
         */
        TVector<FLightSphere> ShadowLights;

        /**
         * One frustum per active scene-capture (preview camera) view. An instance
         * outside the main camera but inside any capture frustum must still be
         * uploaded, or the GPU per-view cull has nothing to draw for that view.
         */
        TVector<FFrustum> CaptureFrusta;

        bool bEnabled  = true;
        bool bHasSun   = false;

        void Reset()
        {
            ShadowLights.clear();
            CaptureFrusta.clear();
            bEnabled = true;
            bHasSun  = false;
        }

        /**
         * Returns true if an instance with the given world-space sphere bounds
         * and per-component flags must be included in this frame's instance
         * upload. The test is conservative: it only rejects when the sphere is
         * definitively outside every view that could sample the instance.
         *
         * bCastsShadow should come from SMeshComponent::bCastShadow. If false,
         * we only need the mesh for its own visible coverage, so the shadow
         * tests are skipped and many off-screen instances can be dropped.
         *
         * MaxDrawDistance (0 = disabled) applies a distance cut regardless of
         * frustum; the existing SMeshComponent property was declared but never
         * enforced, so we honor it here as a nearly-free bonus.
         */
        FORCEINLINE bool ShouldKeep(
            const FVector3& Center,
            float            Radius,
            bool             bCastsShadow,
            float            MaxDrawDistance,
            const FVector3& CameraPosition) const
        {
            if (!bEnabled)
            {
                return true;
            }

            if (MaxDrawDistance > 0.0f)
            {
                const FVector3 ToCamera = Center - CameraPosition;
                const float     DistSq   = Math::Dot(ToCamera, ToCamera);
                const float     CutOff   = MaxDrawDistance + Radius;
                if (DistSq > CutOff * CutOff)
                {
                    return false;
                }
            }

            if (Frustum.IntersectsSphere(Center, Radius))
            {
                return true;
            }

            // Visible to a preview/capture camera: keep regardless of shadow casting.
            for (const FFrustum& CaptureFrustum : CaptureFrusta)
            {
                if (CaptureFrustum.IntersectsSphere(Center, Radius))
                {
                    return true;
                }
            }

            if (!bCastsShadow)
            {
                return false;
            }

            if (bHasSun && SunShadowFrustum.IntersectsSphere(Center, Radius))
            {
                return true;
            }

            for (const FLightSphere& Light : ShadowLights)
            {
                const FVector3 Delta  = Center - Light.Center;
                const float     DistSq = Math::Dot(Delta, Delta);
                const float     Sum    = Radius + Light.Radius;
                if (DistSq <= Sum * Sum)
                {
                    return true;
                }
            }

            return false;
        }

        /**
         * True only when the sphere is in the main camera view (frustum + draw
         * distance). Unlike ShouldKeep, this ignores shadow sweeps: a skeleton
         * kept solely to cast a shadow is NOT "rendered" for animation purposes,
         * so the anim systems can stop ticking its pose while it's off-screen.
         */
        FORCEINLINE bool IsCameraVisible(
            const FVector3& Center,
            float            Radius,
            float            MaxDrawDistance,
            const FVector3& CameraPosition) const
        {
            if (!bEnabled)
            {
                return true;
            }

            if (MaxDrawDistance > 0.0f)
            {
                const FVector3 ToCamera = Center - CameraPosition;
                const float     DistSq   = Math::Dot(ToCamera, ToCamera);
                const float     CutOff   = MaxDrawDistance + Radius;
                if (DistSq > CutOff * CutOff)
                {
                    return false;
                }
            }

            if (Frustum.IntersectsSphere(Center, Radius))
            {
                return true;
            }

            // A mesh visible only in a preview/capture view is still "rendered", so its
            // pose must keep ticking or the preview shows a frozen animation.
            for (const FFrustum& CaptureFrustum : CaptureFrusta)
            {
                if (CaptureFrustum.IntersectsSphere(Center, Radius))
                {
                    return true;
                }
            }

            return false;
        }
    };
}
