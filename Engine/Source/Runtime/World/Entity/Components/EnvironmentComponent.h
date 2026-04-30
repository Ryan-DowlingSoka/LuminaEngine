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
     
        /** Output color when SkyMode == SolidColor. HDR-range; values above
         *  1.0 are valid and feed bloom. */
        PROPERTY(Editable, Color, Category = "Sky|Solid")
        glm::vec3 SolidSkyColor = glm::vec3(0.45f, 0.65f, 1.0f);

     
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
        float SkyExposure = 0.5f;

        /** Mie phase asymmetry (0.0 = isotropic, ~0.76 = strong forward
         *  scatter). Drives how tight the bright halo around the sun is. */
        PROPERTY(Editable, Category = "Sky|Dynamic", ClampMin = -0.99f, ClampMax = 0.99f)
        float MieAnisotropy = 0.76f;
     
        /** Base color the sky settles to once the sun is well below the
         *  horizon. Multiplied by NightBrightness. Pick a desaturated,
         *  slightly-purple deep blue for a believable night. */
        PROPERTY(Editable, Color, Category = "Sky|Night")
        glm::vec3 NightSkyColor = glm::vec3(0.012f, 0.018f, 0.04f);

        /** Overall brightness of the night sky color + its contribution to
         *  the atmosphere blend. Independent of star/moon brightness. */
        PROPERTY(Editable, Category = "Sky|Night", ClampMin = 0.0f, ClampMax = 8.0f)
        float NightBrightness = 0.01f;
     
        /** Fraction of cells in the procedural starfield grid that contain
         *  a star. 0 = no stars, 1 = densely packed (overkill). The grid
         *  resolution is fixed; density scales how many cells emit. */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.0f, ClampMax = 1.0f)
        float StarDensity = 0.55f;

        /** Brightness scalar for the starfield. Stars are HDR so values
         *  above 1 are fine, they will tonemap and bloom naturally. */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.0f, ClampMax = 8.0f)
        float StarBrightness = 0.16f;

        /** Angular speed (rad/s) of the per-star twinkle modulation. Set
         *  to 0 to freeze the starfield. */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.0f, ClampMax = 16.0f)
        float StarTwinkleSpeed = 2.5f;

        /** Visual size of each star inside its cell. Larger values produce
         *  visible discs; smaller values produce sharp pinpoints. */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.05f, ClampMax = 1.0f)
        float StarSize = 0.5f;

        /** Brightness of the procedural Milky-Way band. The band is
         *  oriented around a tilted galactic plane and fades with the
         *  night factor. Set to 0 to disable. */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.0f, ClampMax = 4.0f)
        float GalaxyIntensity = 0.06f;

        /** Tilt (radians) of the galactic plane relative to the world XZ
         *  plane. Lets you reorient where the Milky Way sits in the sky. */
        PROPERTY(Editable, Category = "Sky|Stars", ClampMin = 0.0f, ClampMax = 6.2832f)
        float GalaxyTilt = 0.45f;
     
        /** Visual scale of the moon disc, in multiples of the real moon's
         *  angular size (~0.5 degrees). 1 is realistic; 3-4 reads better
         *  as a backdrop element. */
        PROPERTY(Editable, Category = "Sky|Moon", ClampMin = 0.0f, ClampMax = 16.0f)
        float MoonSize = 1.5f;

        /** Multiplier on the moon's surrounding glow halo. 0 disables. */
        PROPERTY(Editable, Category = "Sky|Moon", ClampMin = 0.0f, ClampMax = 4.0f)
        float MoonGlowSize = 0.3f;

        /** Brightness scalar for the moon disc + glow. */
        PROPERTY(Editable, Category = "Sky|Moon", ClampMin = 0.0f, ClampMax = 8.0f)
        float MoonBrightness = 0.6f;

        /** When true, the moon is anchored opposite the sun and its phase
         *  is shaded automatically by the sun's position. Turn off to
         *  drive MoonDirection directly (useful for cinematics). */
        PROPERTY(Editable, Category = "Sky|Moon")
        bool bMoonOpposeSun = true;

        /** Manual moon direction (FROM viewer TO moon, normalized) used
         *  when bMoonOpposeSun is false. Phase is still computed from the
         *  sun direction so moving this around walks through the phases. */
        PROPERTY(Editable, Category = "Sky|Moon")
        glm::vec3 MoonDirection = glm::vec3(0.0f, -1.0f, 0.0f);
     
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
     

        /** Base ambient color applied to all surfaces. Multiplied by
         *  AmbientIntensity. Treated as the skylight contribution. */
        PROPERTY(Editable, Color, Category = "Ambient Light")
        glm::vec3 AmbientColor = glm::vec3(0.6f, 0.7f, 1.0f);

        /** Brightness multiplier for the ambient term. Tiny values
         *  (~0.005) preserve the old "barely there" look; 0.05+ gives a
         *  visible skylight wash. Capped at 1 because anything past
         *  ~0.3 already over-fills shadowed areas. */
        PROPERTY(Editable, Category = "Ambient Light", ClampMin = 0.0f, ClampMax = 1.0f, Delta = 0.001f)
        float AmbientIntensity = 0.30f;

        /** When true, AmbientColor is auto-derived from the active sky each
         *  frame instead of using the manual color: a blend of zenith and
         *  horizon (Gradient) or a sampled atmosphere color (Dynamic). The
         *  manual AmbientColor is ignored, AmbientIntensity still scales. */
        PROPERTY(Editable, Category = "Ambient Light")
        bool bAmbientFromSky = false;
    };
}
