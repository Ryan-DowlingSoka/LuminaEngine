#pragma once

#include <glm/glm.hpp>
#include "Platform/GenericPlatform.h"

#ifndef VERIFY_SSBO_ALIGNMENT
#define VERIFY_SSBO_ALIGNMENT(Type) \
    static_assert(sizeof(Type) % 16 == 0, #Type " must be 16-byte aligned");
#endif

namespace Lumina
{
    // Sky mode constants. Must match SKY_MODE_* in Environment.slang.
    constexpr uint32 GSkyMode_SolidColor = 0u;
    constexpr uint32 GSkyMode_Gradient   = 1u;
    constexpr uint32 GSkyMode_Dynamic    = 2u;
    // Visible sky reads from the SkyCube (which itself was filled by the
    // imported HDRI in the Equirect->Cube pass). When SkyMode is HDRI but
    // no EnvironmentMap is bound, the env shader falls back to a black
    // sky -- documented as such in SEnvironmentComponent.
    constexpr uint32 GSkyMode_HDRI       = 3u;

    /**
     * CPU mirror of the per-frame environment constant buffer. Layout must
     * match FEnvironmentParams in Environment.slang exactly. The
     * EnvironmentPass uploads one of these per frame; the shader switches on
     * Misc.x (the sky mode) and reads the subset of fields each path needs.
     */
    struct alignas(16) FEnvironmentParams
    {
        glm::vec4   SolidSkyColor    = glm::vec4(0.45f, 0.65f, 1.0f, 0.0f); // rgb=color, w unused
        glm::vec4   ZenithColor      = glm::vec4(0.05f, 0.1f, 0.4f, 0.7f);  // rgb=color, w=horizonExponent
        glm::vec4   HorizonColor     = glm::vec4(0.6f, 0.8f, 1.0f, 0.0f);   // rgb=color, w unused
        glm::vec4   GroundColor      = glm::vec4(0.2f, 0.18f, 0.15f, 0.0f); // rgb=color, w unused
        glm::vec4   SunTint          = glm::vec4(1.0f, 1.0f, 1.0f, 20.0f);  // rgb=tint, w=sunIntensity
        // x=skyMode (uint cast to float), y=sunDiscScale, z=skyExposure, w=mieAnisotropy
        glm::vec4   Misc             = glm::vec4(2.0f, 1.0f, 1.5f, 0.76f);

        // -- Procedural night additions (Dynamic mode only) --
        // rgb = night zenith tint (deep blue/purple), w = brightness scalar
        glm::vec4   NightSkyColor    = glm::vec4(0.012f, 0.018f, 0.04f, 0.4f);
        // x=density, y=brightness, z=twinkleSpeed, w=size
        glm::vec4   StarParams       = glm::vec4(0.55f, 1.0f, 2.5f, 0.5f);
        // x=size (multiples of 0.5deg), y=glowSize, z=brightness, w=autoOpposeSun (>=0.5 = auto)
        glm::vec4   MoonParams       = glm::vec4(3.0f, 0.4f, 0.6f, 1.0f);
        // xyz = manual moon direction (used when MoonParams.w < 0.5), w unused
        glm::vec4   MoonDirection    = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
        // x = milky-way band intensity, y = band tilt (radians), z/w reserved
        glm::vec4   GalaxyParams     = glm::vec4(0.06f, 0.45f, 0.0f, 0.0f);
    };
    VERIFY_SSBO_ALIGNMENT(FEnvironmentParams);

    /**
     * CPU mirror of the per-frame exponential-height-fog constant buffer. Layout
     * must match FExponentialHeightFogParams in Includes/Fog.slang exactly. Read by
     * the froxel volumetric fog passes (inject builds density + scattering from it;
     * apply reads FogMaxOpacity).
     */
    struct alignas(16) FExponentialHeightFogParams
    {
        // rgb = fog inscattering color, w = fog density at base height
        glm::vec4   InscatteringColor = glm::vec4(0.5f, 0.6f, 0.7f, 0.02f);
        // x = height falloff, y = base height (world Y), z = start distance, w = max opacity
        glm::vec4   HeightParams      = glm::vec4(0.2f, 0.0f, 0.0f, 1.0f);
        // rgb = directional (sun) inscatter color, w = directional exponent
        glm::vec4   DirectionalColor  = glm::vec4(1.0f, 0.9f, 0.7f, 4.0f);
        // x = volumetric scattering intensity, y = volumetric anisotropy,
        // z = volumetric max distance, w = directional inscatter start distance
        glm::vec4   VolumetricParams  = glm::vec4(1.0f, 0.6f, 200.0f, 0.0f);
    };
    VERIFY_SSBO_ALIGNMENT(FExponentialHeightFogParams);
}
