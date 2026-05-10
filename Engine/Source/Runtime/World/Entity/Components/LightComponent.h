#pragma once

#include "glm/glm.hpp"
#include "LightComponent.generated.h"

namespace Lumina
{
    REFLECT(Component, Category = "Lights")
    struct RUNTIME_API SPointLightComponent
    {
        GENERATED_BODY()

        /** RGB color of the light emission. */
        PROPERTY(Editable, Color, Category = "Light")
        glm::vec3 LightColor = glm::vec3(1.0f);

        /** Brightness of the light in lux. */
        PROPERTY(Editable, Category = "Light", ClampMin = 0.0f)
        float Intensity = 10.0f;

        /** Radius in meters within which the light affects objects. */
        PROPERTY(Editable, Category = "Light")
        float Attenuation = 10.0f;

        /** Controls the steepness of the intensity falloff curve toward the attenuation radius. */
        PROPERTY(Editable, Category = "Light")
        float Falloff = 0.8f;

        /** When true, this light contributes to the shadow pass. */
        PROPERTY(Editable, Category = "Shadows")
        bool bCastShadows = false;

        /** When true, the light scatters through participating media (fog/atmosphere). */
        PROPERTY(Editable, Category = "Advanced")
        bool bVolumetric = false;

        /** Strength of the volumetric scattering contribution. */
        PROPERTY(Editable, Category = "Advanced")
        float VolumetricIntensity = 0.5f;
    };

    REFLECT(Component, Category = "Lights")
    struct RUNTIME_API SSpotLightComponent
    {
        GENERATED_BODY()

        /** RGB color of the light emission. */
        PROPERTY(Editable, Color, Category = "Light")
        glm::vec3 LightColor = glm::vec3(1.0f);

        /** Brightness of the light in lux. */
        PROPERTY(Editable, Category = "Light", ClampMin = 0.0f, ClampMax = 1000.0f)
        float Intensity = 10.0f;

        /** Angle (degrees) of the fully-lit inner cone, no falloff inside this region. */
        PROPERTY(Editable, Category = "Light", ClampMin = 0.0f)
        float InnerConeAngle = 20.0f;

        /** Angle (degrees) of the outer cone edge, light fades from inner to outer. */
        PROPERTY(Editable, Category = "Light", ClampMin = 0.0f)
        float OuterConeAngle = 30.0f;

        /** Radius in meters within which the spotlight affects objects. */
        PROPERTY(Editable, Category = "Light", ClampMin = 0.0f)
        float Attenuation = 10.0f;

        /** Controls the steepness of the intensity falloff curve toward the attenuation radius. */
        PROPERTY(Editable, Category = "Light")
        float Falloff = 0.8f;

        /** When true, this light contributes to the shadow pass. */
        PROPERTY(Editable, Category = "Shadows")
        bool bCastShadows = false;

        /** Depth bias to prevent shadow acne on receiving surfaces. */
        PROPERTY(Editable, Category = "Shadows")
        float ShadowBias = 0.005f;

        /** World-space radius used to build the shadow map frustum. */
        PROPERTY(Editable, Category = "Shadows")
        float ShadowRadius = 1.0f;

        /** When true, the light scatters through participating media (fog/atmosphere). */
        PROPERTY(Editable, Category = "Advanced")
        bool bVolumetric = false;

        /** Strength of the volumetric scattering contribution. */
        PROPERTY(Editable, Category = "Advanced")
        float VolumetricIntensity = 0.5f;

        /** Index into the shadow map atlas, managed by the renderer. */
        int32 ShadowMapIndex = -1;
    };


    REFLECT(Component, Category = "Lights")
    struct RUNTIME_API SDirectionalLightComponent
    {
        GENERATED_BODY()

        /** RGB color of the directional light. */
        PROPERTY(Editable, Color, Category = "Light")
        glm::vec3 Color = glm::vec4(1.0f);

        /** Normalized world-space direction the light travels (points away from the source). */
        PROPERTY(Editable, Category = "Light")
        glm::vec3 Direction = glm::vec3(0.0f, 0.3f, 0.8f);

        /** Brightness multiplier of the directional light. */
        PROPERTY(Editable, Category = "Light", ClampMin = 0.0f)
        float Intensity = 1.5f;

        /** When true, this light contributes to the shadow pass. */
        PROPERTY(Editable)
        bool bCastShadows = true;

        /** When true, the sun scatters through participating media (god rays / light shafts). */
        PROPERTY(Editable, Category = "Advanced")
        bool bVolumetric = false;

        /** Strength of the volumetric scattering contribution. */
        PROPERTY(Editable, Category = "Advanced")
        float VolumetricIntensity = 0.5f;
    };
}