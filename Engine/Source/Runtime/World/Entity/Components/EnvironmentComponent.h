#pragma once
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "EnvironmentComponent.generated.h"

namespace Lumina
{
    class CTexture;

    /** Sky background mode. */
    REFLECT()
    enum class ESkyMode : uint8
    {
        // Flat color fill; cheapest path, no atmospherics.
        SolidColor,

        // Zenith/horizon/ground gradient + procedural sun disc.
        Gradient,

        // Full atmospheric scattering (Rayleigh + Mie) with TOD sun color.
        Dynamic,

        // Imported HDR equirect panorama; same texture drives IBL ambient + reflections.
        HDRI,
    };

    /** Singleton-style environment component; only one enabled instance per frame is read. */
    REFLECT(Component)
    struct RUNTIME_API SEnvironmentComponent
    {
        GENERATED_BODY()

        /** When false, EnvironmentPass is skipped; HDR target stays at its clear value. */
        PROPERTY(Editable, Category = "Sky")
        bool bRenderSky = true;

        /** Selects sky background path; each mode reads a different subset of properties below. */
        PROPERTY(Editable, Category = "Sky")
        ESkyMode SkyMode = ESkyMode::Dynamic;

        /** SolidColor mode output; HDR-range, values >1 feed bloom. */
        PROPERTY(Editable, Color, Category = "Sky|Solid")
        glm::vec3 SolidSkyColor = glm::vec3(0.45f, 0.65f, 1.0f);


        /** Color directly overhead. */
        PROPERTY(Editable, Color, Category = "Sky|Gradient")
        glm::vec3 ZenithColor = glm::vec3(0.05f, 0.1f, 0.4f);

        /** Color at the horizon (Y == 0). */
        PROPERTY(Editable, Color, Category = "Sky|Gradient")
        glm::vec3 HorizonColor = glm::vec3(0.6f, 0.8f, 1.0f);

        /** Color directly below the horizon. */
        PROPERTY(Editable, Color, Category = "Sky|Gradient")
        glm::vec3 GroundColor = glm::vec3(0.2f, 0.18f, 0.15f);

        /** Horizon-to-zenith curvature; 1.0 = clean cosine, smaller widens horizon band. */
        PROPERTY(Editable, Category = "Sky|Gradient", ClampMin = 0.05f, ClampMax = 4.0f)
        float HorizonExponent = 0.7f;

        /** Sun disc visual scale (cosmetic; doesn't affect lighting). */
        PROPERTY(Editable, Category = "Sky|Sun", ClampMin = 0.0f, ClampMax = 8.0f)
        float SunDiscScale = 1.0f;

        /** Sun disc + glow brightness on the sky. */
        PROPERTY(Editable, Category = "Sky|Sun", ClampMin = 0.0f)
        float SunIntensity = 20.0f;

        /** Tint on TOD sun color (Dynamic) and sun disc (Gradient). */
        PROPERTY(Editable, Color, Category = "Sky|Sun")
        glm::vec3 SunColorTint = glm::vec3(1.0f);

        /** Exposure on dynamic sky scattering before write. */
        PROPERTY(Editable, Category = "Sky|Dynamic", ClampMin = 0.05f, ClampMax = 8.0f)
        float SkyExposure = 0.5f;

        /** Mie phase asymmetry (0 = isotropic, ~0.76 = strong forward scatter). */
        PROPERTY(Editable, Category = "Sky|Dynamic", ClampMin = -0.99f, ClampMax = 0.99f)
        float MieAnisotropy = 0.76f;

        /** Night sky base color (multiplied by NightBrightness). */
        PROPERTY(Editable, Color, Category = "Sky|Night")
        glm::vec3 NightSkyColor = glm::vec3(0.012f, 0.018f, 0.04f);

        /** Night sky brightness; independent of star/moon brightness. */
        PROPERTY(Editable, Category = "Sky|Night", ClampMin = 0.0f, ClampMax = 8.0f)
        float NightBrightness = 0.01f;

        /** Star cell density [0, 1]. */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.0f, ClampMax = 1.0f)
        float StarDensity = 0.55f;

        /** Star brightness; HDR-friendly. */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.0f, ClampMax = 8.0f)
        float StarBrightness = 0.16f;

        /** Twinkle angular speed (rad/s); 0 freezes the field. */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.0f, ClampMax = 16.0f)
        float StarTwinkleSpeed = 2.5f;

        /** Star size within its cell. */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.05f, ClampMax = 1.0f)
        float StarSize = 0.5f;

        /** Milky-Way band brightness; 0 disables. */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.0f, ClampMax = 4.0f)
        float GalaxyIntensity = 0.002f;

        /** Galactic plane tilt vs world XZ (radians). */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.0f, ClampMax = 6.2832f)
        float GalaxyTilt = 0.45f;

        /** Moon disc scale (multiples of real angular size ~0.5 deg). */
        PROPERTY(Editable, Category = "Sky|Moon", ClampMin = 0.0f, ClampMax = 16.0f)
        float MoonSize = 1.5f;

        /** Moon glow halo multiplier; 0 disables. */
        PROPERTY(Editable, Category = "Sky|Moon", ClampMin = 0.0f, ClampMax = 4.0f)
        float MoonGlowSize = 0.3f;

        /** Moon disc + glow brightness. */
        PROPERTY(Editable, Category = "Sky|Moon", ClampMin = 0.0f, ClampMax = 8.0f)
        float MoonBrightness = 0.6f;

        /** When true, moon anchored opposite sun; phase auto-shaded from sun. */
        PROPERTY(Editable, Category = "Sky|Moon")
        bool bMoonOpposeSun = true;

        /** Manual moon direction (FROM viewer TO moon) used when bMoonOpposeSun is false. */
        PROPERTY(Editable, Category = "Sky|Moon")
        glm::vec3 MoonDirection = glm::vec3(0.0f, -1.0f, 0.0f);

        /** HDR equirect for IBL irradiance/prefilter. Texture must use ColorSpace = Environment. */
        PROPERTY(Editable, Category = "Sky|HDRI")
        TObjectPtr<CTexture> EnvironmentMap;


        /** Base ambient (skylight) color; multiplied by AmbientIntensity. */
        PROPERTY(Editable, Color, Category = "Ambient Light")
        glm::vec3 AmbientColor = glm::vec3(0.6f, 0.7f, 1.0f);

        /** Ambient brightness; >0.3 over-fills shadows. */
        PROPERTY(Editable, Category = "Ambient Light", ClampMin = 0.0f, ClampMax = 1.0f, Delta = 0.001f)
        float AmbientIntensity = 0.05f;

        /** When true, AmbientColor is auto-derived from active sky; AmbientIntensity still scales. */
        PROPERTY(Editable, Category = "Ambient Light")
        bool bAmbientFromSky = false;
    };
}
