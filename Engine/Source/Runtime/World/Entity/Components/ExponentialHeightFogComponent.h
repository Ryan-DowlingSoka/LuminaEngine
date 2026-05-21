#pragma once

#include "glm/glm.hpp"
#include "ExponentialHeightFogComponent.generated.h"

namespace Lumina
{
    /**
     * Exponential height fog. Density falls off exponentially with world height,
     * giving thick fog in valleys and clear air on peaks. Drives an analytic
     * composite (distance + directional sun inscatter) and, when bVolumetricFog
     * is set, the same density profile shapes the volumetric light-shaft march.
     *
     * Singleton-style: only one enabled instance per frame is read.
     */
    REFLECT(Component, Category = "Environment")
    struct RUNTIME_API SExponentialHeightFogComponent
    {
        GENERATED_BODY()

        /** When false, no fog passes run this frame. */
        PROPERTY(Editable, Category = "Fog")
        bool bEnabled = true;

        /** Overall fog thickness at FogBaseHeight; 0 disables the analytic fog. */
        PROPERTY(Editable, Category = "Fog", ClampMin = 0.0f, Delta = 0.001f)
        float FogDensity = 0.02f;

        /** How quickly density drops with altitude; larger = thinner aloft, sharper layer. */
        PROPERTY(Editable, Category = "Fog", ClampMin = 0.0f, ClampMax = 4.0f, Delta = 0.001f)
        float FogHeightFalloff = 0.2f;

        /** World-space Y at which FogDensity applies (the base of the fog layer). */
        PROPERTY(Editable, Category = "Fog")
        float FogBaseHeight = 0.0f;

        /** Distance from the camera before fog begins to accumulate. */
        PROPERTY(Editable, Category = "Fog", ClampMin = 0.0f)
        float FogStartDistance = 0.0f;

        /** Upper bound on fog opacity so distant geometry never fully disappears. */
        PROPERTY(Editable, Category = "Fog", ClampMin = 0.0f, ClampMax = 1.0f, Delta = 0.001f)
        float FogMaxOpacity = 1.0f;

        /** Base fog color (HDR; values >1 feed bloom). */
        PROPERTY(Editable, Color, Category = "Fog")
        glm::vec3 FogInscatteringColor = glm::vec3(0.5f, 0.6f, 0.7f);

        /** Fog tint blended in when looking toward the sun (light shafts / haze glow). */
        PROPERTY(Editable, Color, Category = "Directional Inscatter")
        glm::vec3 DirectionalInscatteringColor = glm::vec3(1.0f, 0.9f, 0.7f);

        /** Tightness of the sun-facing inscatter lobe; larger = smaller, sharper glow. */
        PROPERTY(Editable, Category = "Directional Inscatter", ClampMin = 1.0f, ClampMax = 64.0f)
        float DirectionalInscatteringExponent = 4.0f;

        /** Distance before directional inscatter ramps in. */
        PROPERTY(Editable, Category = "Directional Inscatter", ClampMin = 0.0f)
        float DirectionalInscatteringStartDistance = 0.0f;

        /** When true, the fog's height-density profile drives the volumetric light-shaft march. */
        PROPERTY(Editable, Category = "Volumetric")
        bool bVolumetricFog = true;

        /** Brightness multiplier on the volumetric scattering (god rays). Decoupled from
        FogDensity, so raise this to make shafts pop without thickening the fog. */
        PROPERTY(Editable, Category = "Volumetric", ClampMin = 0.0f)
        float VolumetricScatteringIntensity = 3.0f;

        /** Henyey-Greenstein phase asymmetry for shafts (0 = isotropic, ~0.8 = strong forward). */
        PROPERTY(Editable, Category = "Volumetric", ClampMin = -0.95f, ClampMax = 0.95f)
        float VolumetricAnisotropy = 0.8f;

        /** Maximum ray-march distance for the volumetric shafts. */
        PROPERTY(Editable, Category = "Volumetric", ClampMin = 1.0f)
        float VolumetricMaxDistance = 200.0f;

        /** Ray-march step count; higher is smoother but costlier. */
        PROPERTY(Editable, Category = "Volumetric", ClampMin = 4, ClampMax = 128)
        int32 VolumetricStepCount = 16;
    };
}
