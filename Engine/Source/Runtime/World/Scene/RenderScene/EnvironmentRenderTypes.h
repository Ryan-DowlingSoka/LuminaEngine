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

    /**
     * CPU mirror of the per-frame environment constant buffer. Layout must
     * match FEnvironmentParams in Environment.slang exactly. The
     * EnvironmentPass uploads one of these per frame; the shader switches on
     * Misc.x (the sky mode) and reads the subset of fields each path needs.
     */
    struct alignas(16) FEnvironmentParams
    {
        glm::vec4   SolidSkyColor   = glm::vec4(0.45f, 0.65f, 1.0f, 0.0f); // rgb=color, w unused
        glm::vec4   ZenithColor     = glm::vec4(0.05f, 0.1f, 0.4f, 0.7f);  // rgb=color, w=horizonExponent
        glm::vec4   HorizonColor    = glm::vec4(0.6f, 0.8f, 1.0f, 0.0f);   // rgb=color, w unused
        glm::vec4   GroundColor     = glm::vec4(0.2f, 0.18f, 0.15f, 0.0f); // rgb=color, w unused
        glm::vec4   SunTint         = glm::vec4(1.0f, 1.0f, 1.0f, 20.0f);  // rgb=tint, w=sunIntensity
        // x=skyMode (uint cast to float), y=sunDiscScale, z=skyExposure, w=mieAnisotropy
        glm::vec4   Misc            = glm::vec4(2.0f, 1.0f, 1.5f, 0.76f);
    };
    VERIFY_SSBO_ALIGNMENT(FEnvironmentParams);
}
