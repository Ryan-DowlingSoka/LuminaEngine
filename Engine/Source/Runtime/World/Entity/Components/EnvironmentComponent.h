#pragma once
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "EnvironmentComponent.generated.h"

namespace Lumina
{
    class CTexture;

    /**
     * Sky background mode for SEnvironmentComponent. Controls what the
     * EnvironmentPass writes into the HDR target before opaque geometry
     * is composited on top.
     */
    REFLECT()
    enum class ESkyMode : uint8
    {
        // Flat color fill. Cheapest path; no atmospheric or sun rendering.
        SolidColor,

        // Two-tone vertical gradient (zenith / horizon / ground) with a
        // procedural sun disc. Cheap, art-direct-able, and consistent at
        // any time of day -- the sun direction still drives the disc
        // placement and the ambient if AmbientFromSky is on.
        Gradient,

        // Full atmospheric scattering (Rayleigh + Mie) with a time-of-day
        // sun color. The most expensive mode but gives a realistic sky
        // and natural twilight transitions.
        Dynamic,

        // Sample the imported HDR equirectangular panorama (assigned to
        // EnvironmentMap below) as the visible sky. The same texture
        // drives IBL ambient + reflections, so picking this mode keeps
        // the visible sky and the lit-surface response of the scene
        // perfectly consistent. Falls back to black when no
        // EnvironmentMap is set.
        HDRI,
    };

    /**
     * Singleton-style component placed on a level's environment entity.
     * Owns the sky background, sun cosmetic settings, and ambient skylight
     * contribution. The render scene reads at most one enabled
     * SEnvironmentComponent per frame.
     */
    REFLECT(Component)
    struct RUNTIME_API SEnvironmentComponent
    {
        GENERATED_BODY()

        // ====================================================================
        //  Sky
        // ====================================================================

        /** When false the EnvironmentPass is skipped entirely; the HDR target
         *  is left at its clear value (typically black). Useful for indoor
         *  scenes or anything that fully covers the framebuffer with opaque
         *  geometry. */
        PROPERTY(Editable, Category = "Sky")
        bool bRenderSky = true;

        /** Selects the sky background path the EnvironmentPass takes. Each
         *  mode reads a different subset of the properties below. */
        PROPERTY(Editable, Category = "Sky")
        ESkyMode SkyMode = ESkyMode::Dynamic;

        // -- Solid mode --

        /** Output color when SkyMode == SolidColor. HDR-range; values above
         *  1.0 are valid and feed bloom. */
        PROPERTY(Editable, Color, Category = "Sky|Solid")
        glm::vec3 SolidSkyColor = glm::vec3(0.45f, 0.65f, 1.0f);

        // -- Gradient mode --

        /** Color directly overhead. */
        PROPERTY(Editable, Color, Category = "Sky|Gradient")
        glm::vec3 ZenithColor = glm::vec3(0.05f, 0.1f, 0.4f);

        /** Color at the horizon line (Y == 0). */
        PROPERTY(Editable, Color, Category = "Sky|Gradient")
        glm::vec3 HorizonColor = glm::vec3(0.6f, 0.8f, 1.0f);

        /** Color directly below the horizon. */
        PROPERTY(Editable, Color, Category = "Sky|Gradient")
        glm::vec3 GroundColor = glm::vec3(0.2f, 0.18f, 0.15f);

        /** Curvature of the horizon-to-zenith transition. 1.0 is a clean
         *  cosine fade; smaller values (e.g. 0.4) push the sky color higher
         *  and keep a wider band of horizon. */
        PROPERTY(Editable, Category = "Sky|Gradient", ClampMin = 0.05f, ClampMax = 4.0f)
        float HorizonExponent = 0.7f;

        // -- Dynamic mode (and shared sun cosmetics) --

        /** Visual scale of the sun disc + glow on the sky background, in
         *  multiples of the sun's true angular size. Doesn't affect lighting. */
        PROPERTY(Editable, Category = "Sky|Sun", ClampMin = 0.0f, ClampMax = 8.0f)
        float SunDiscScale = 1.0f;

        /** Brightness of the sun disc and surrounding glow on the sky. */
        PROPERTY(Editable, Category = "Sky|Sun", ClampMin = 0.0f)
        float SunIntensity = 20.0f;

        /** Multiplicative tint applied to the time-of-day sun color in
         *  Dynamic mode and to the sun disc in Gradient mode. */
        PROPERTY(Editable, Color, Category = "Sky|Sun")
        glm::vec3 SunColorTint = glm::vec3(1.0f);

        /** Tone-mapping exposure applied to the dynamic sky's accumulated
         *  scattering before write. Higher values brighten the whole sky;
         *  lower values let the sun blow out without saturating the rest. */
        PROPERTY(Editable, Category = "Sky|Dynamic", ClampMin = 0.05f, ClampMax = 8.0f)
        float SkyExposure = 1.5f;

        /** Mie phase asymmetry (0.0 = isotropic, ~0.76 = strong forward
         *  scatter). Drives how tight the bright halo around the sun is. */
        PROPERTY(Editable, Category = "Sky|Dynamic", ClampMin = -0.99f, ClampMax = 0.99f)
        float MieAnisotropy = 0.76f;

        // -- HDRI environment --

        /** Optional HDR equirectangular panorama used as the source for
         *  IBL irradiance and prefilter convolution. When set, replaces
         *  the procedural sky cube capture with a live equirect->cubemap
         *  conversion, so all environment lighting (skylight ambient,
         *  metallic reflections) reads from this texture. The visible
         *  sky backdrop still uses the SkyMode above; assign an HDRI
         *  here to drive only the reflected/ambient contribution.
         *
         *  Texture must be imported with ColorSpace = Environment so it
         *  stays HDR (Basis-compressed LDR textures clip the highlights
         *  and break IBL energy). The factory auto-classifies any .hdr
         *  drag into Environment. */
        PROPERTY(Editable, Category = "Sky|HDRI")
        TObjectPtr<CTexture> EnvironmentMap;

        // ====================================================================
        //  Ambient skylight
        // ====================================================================

        /** Base ambient color applied to all surfaces. Multiplied by
         *  AmbientIntensity. Treated as the skylight contribution. */
        PROPERTY(Editable, Color, Category = "Ambient Light")
        glm::vec3 AmbientColor = glm::vec3(0.6f, 0.7f, 1.0f);

        /** Brightness multiplier for the ambient term. Tiny values
         *  (~0.005) preserve the old "barely there" look; 0.05+ gives a
         *  visible skylight wash. Capped at 1 because anything past
         *  ~0.3 already over-fills shadowed areas. */
        PROPERTY(Editable, Category = "Ambient Light", ClampMin = 0.0f, ClampMax = 1.0f, Delta = 0.001f)
        float AmbientIntensity = 0.05f;

        /** When true, AmbientColor is auto-derived from the active sky each
         *  frame instead of using the manual color: a blend of zenith and
         *  horizon (Gradient) or a sampled atmosphere color (Dynamic). The
         *  manual AmbientColor is ignored, AmbientIntensity still scales. */
        PROPERTY(Editable, Category = "Ambient Light")
        bool bAmbientFromSky = false;
    };
}
