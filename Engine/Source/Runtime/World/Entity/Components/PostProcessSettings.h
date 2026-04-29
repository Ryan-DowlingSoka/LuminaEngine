#pragma once
#include "Core/Object/ObjectMacros.h"
#include "PostProcessSettings.generated.h"

namespace Lumina
{
    /**
     * Tone-mapping operator applied at the end of the color grading pass,
     * after parametric grading has run in linear HDR space.
     *
     *   None        -- pure clamp; skip tone mapping. Use for diagnostic
     *                  views or when the scene is already display-ready.
     *   ACES        -- Krzysztof Narkowicz's ACES Film fit. The legacy
     *                  Lumina default; saturated, contrasty, hue-shifts
     *                  bright reds toward orange.
     *   AGX         -- Troy Sobotka's AGX (bwrensch's polynomial fit).
     *                  Neutral, no hue skew, well-behaved highlights.
     *                  The recommended default for cinematic looks.
     *   AGXPunchy   -- AGX with extra slope, saturation, and a tiny hue
     *                  rotation. Closer to the look of a graded film
     *                  print without the heavy ACES contrast.
     *   AGXGolden   -- AGX biased warm; lifts shadows toward amber and
     *                  pulls highlights toward gold. Useful for sunset /
     *                  fantasy / candlelit scenes as a starting point.
     */
    REFLECT()
    enum class EToneMapper : uint8
    {
        None        = 0,
        ACES        = 1,
        AGX         = 2,
        AGXPunchy   = 3,
        AGXGolden   = 4,
    };

    /**
     * Per-camera color grading + tone mapping parameters. 
     * All grading runs in linear HDR space *before* the tone mapper. The
     * tone mapper is the final transform; the optional vignette is applied
     * in display space after that. Values were chosen so the all-defaults
     * struct produces an identity grade with AGX tone mapping (i.e. the
     * scene looks the same as the legacy ACES path at Exposure = 1, but
     * with cleaner highlights).
     */
    REFLECT()
    struct RUNTIME_API SPostProcessSettings
    {
        GENERATED_BODY()
     

        /** When false, the pass becomes a passthrough copy + tone map only --
         *  every grading knob below is ignored. Cheap kill-switch for A/B. */
        PROPERTY(Editable, Category = "Post Process")
        bool bEnabled = true;

        /** Tone mapper applied after grading. AGX is the recommended default
         *  -- it preserves hue and handles HDR highlights better than ACES. */
        PROPERTY(Editable, Category = "Post Process")
        EToneMapper ToneMapper = EToneMapper::AGX;
     

        /** Exposure compensation in stops (EV). +1 doubles scene brightness,
         *  -1 halves it. The scene's lighting still drives absolute values;
         *  this is the photographer's exposure dial on top. */
        PROPERTY(Editable, Category = "Post Process|Exposure", ClampMin = -8.0f, ClampMax = 8.0f)
        float ExposureCompensation = 0.0f;
     

        /** Color temperature shift, normalized [-1, 1]. Negative cools
         *  (toward blue), positive warms (toward orange). 0 == neutral
         *  (scene reference white preserved). Implemented as a chromatic
         *  adaptation in LMS so neutrals stay neutral after the shift. */
        PROPERTY(Editable, Category = "Post Process|White Balance", ClampMin = -1.0f, ClampMax = 1.0f)
        float Temperature = 0.0f;

        /** Green / magenta shift, normalized [-1, 1]. Pairs with
         *  Temperature on the standard photographer's white-balance cross. */
        PROPERTY(Editable, Category = "Post Process|White Balance", ClampMin = -1.0f, ClampMax = 1.0f)
        float Tint = 0.0f;
     
        /** Contrast around mid-grey (0.18). 1.0 == no change. Lower
         *  flattens; higher crushes shadows and lifts highlights. */
        PROPERTY(Editable, Category = "Post Process|Tone", ClampMin = 0.0f, ClampMax = 2.0f)
        float Contrast = 1.0f;

        /** Saturation. 0.0 == greyscale, 1.0 == unchanged, >1 boosts
         *  chroma. Computed against Rec.709 luma so neutrals are stable. */
        PROPERTY(Editable, Category = "Post Process|Tone", ClampMin = 0.0f, ClampMax = 4.0f)
        float Saturation = 1.0f;

        /** Display-space gamma applied right before output. 1.0 == no
         *  change. <1 brightens midtones, >1 darkens them. Combine with
         *  Contrast for a quick "grungy" pass. */
        PROPERTY(Editable, Category = "Post Process|Tone", ClampMin = 0.1f, ClampMax = 4.0f)
        float Gamma = 1.0f;
     

        /** Multiplicative color filter applied in linear space. White
         *  (1, 1, 1) is a no-op. Tinted filter colors recolor the entire
         *  image -- great for quick "everything but the kitchen sink"
         *  mood passes (e.g. (0.5, 0.5, 0.7) for a cool blue wash). */
        PROPERTY(Editable, Color, Category = "Post Process|Color Filter")
        glm::vec3 ColorFilter = glm::vec3(1.0f);

        /** Strength of the color filter. 0 disables it, 1 is the full
         *  multiplied tint. Lets you keep the picker on a saturated color
         *  and dial intensity in/out without re-picking. */
        PROPERTY(Editable, Category = "Post Process|Color Filter", ClampMin = 0.0f, ClampMax = 2.0f)
        float ColorFilterIntensity = 1.0f;
     
        /** Shadow tint. Pulls the foot of the curve toward this color
         *  (lift). For a dark fantasy look try a deep blue or magenta. */
        PROPERTY(Editable, Color, Category = "Post Process|Lift Gamma Gain")
        glm::vec3 Shadows = glm::vec3(1.0f);

        /** Midtone tint. Power-style remap centered on midtones. The
         *  most useful knob for "warmer skin" or "sickly green forest". */
        PROPERTY(Editable, Color, Category = "Post Process|Lift Gamma Gain")
        glm::vec3 Midtones = glm::vec3(1.0f);

        /** Highlight tint. Multiplies the top of the curve. Tints
         *  windows, sky, sun bloom etc. Cool highlights + warm shadows
         *  is the cinematic "teal & orange" preset. */
        PROPERTY(Editable, Color, Category = "Post Process|Lift Gamma Gain")
        glm::vec3 Highlights = glm::vec3(1.0f);
     

        /** Vignette darkening at the corners. 0 == off. */
        PROPERTY(Editable, Category = "Post Process|Vignette", ClampMin = 0.0f, ClampMax = 1.0f)
        float VignetteIntensity = 0.0f;

        /** 0 == elliptical (matches viewport aspect), 1 == perfectly
         *  circular. */
        PROPERTY(Editable, Category = "Post Process|Vignette", ClampMin = 0.0f, ClampMax = 1.0f)
        float VignetteRoundness = 1.0f;

        /** Falloff width. 0 == hard ring (basically a porthole), 1 ==
         *  soft fall from center. */
        PROPERTY(Editable, Category = "Post Process|Vignette", ClampMin = 0.01f, ClampMax = 1.0f)
        float VignetteSmoothness = 0.5f;

        /** Vignette tint. Black is the classic darkening look; a deep
         *  blue or red can sell mood better than pure black. */
        PROPERTY(Editable, Color, Category = "Post Process|Vignette")
        glm::vec3 VignetteColor = glm::vec3(0.0f);

        /** Bloom strength. 0 == off (the bloom passes are skipped entirely
         *  so it's free when disabled). 0.04 - 0.12 is the cinematic range;
         *  >0.3 reads as obviously stylized. */
        PROPERTY(Editable, Category = "Post Process|Bloom", ClampMin = 0.0f, ClampMax = 1.0f, Delta = 0.005f)
        float BloomIntensity = 0.0f;

        /** Brightness above which a pixel begins contributing to bloom (in
         *  linear scene units, i.e. before tone mapping). Below this value
         *  the prefilter is zero. Values around 1.0 catch the sun, lights
         *  and bright emissives without bleeding off mid-grey skin / sky. */
        PROPERTY(Editable, Category = "Post Process|Bloom", ClampMin = 0.0f, ClampMax = 16.0f, Delta = 0.05f)
        float BloomThreshold = 1.0f;

        /** Soft-knee width around the threshold. 0 == hard cutoff (looks
         *  poppy and aliased on small bright pixels); 0.5 == standard
         *  cinematic soft knee. Higher values let dimmer mids leak in. */
        PROPERTY(Editable, Category = "Post Process|Bloom", ClampMin = 0.0f, ClampMax = 1.0f, Delta = 0.01f)
        float BloomSoftKnee = 0.5f;

        /** Tints the bloom contribution. White (1, 1, 1) is the natural
         *  look; a warm gold sells "magic hour" sun, a cool blue sells
         *  neon / sci-fi practicals. */
        PROPERTY(Editable, Color, Category = "Post Process|Bloom")
        glm::vec3 BloomTint = glm::vec3(1.0f);

        /** Strength of the radial RGB split applied in display space.
         *  0 == off; 0.001 - 0.005 is a subtle "anamorphic lens" feel,
         *  >0.01 reads as stylized / VHS. The offset scales with distance
         *  from the screen center so the image stays clean in the middle. */
        PROPERTY(Editable, Category = "Post Process|Chromatic Aberration", ClampMin = 0.0f, ClampMax = 0.05f, Delta = 0.0005f)
        float ChromaticAberration = 0.0f;

        /** Film grain strength. 0 == off; 0.05 - 0.15 reads as a film
         *  stock without obscuring detail; >0.3 is stylized / found-footage.
         *  Animated per-frame so the grain shimmers like real silver halide. */
        PROPERTY(Editable, Category = "Post Process|Film Grain", ClampMin = 0.0f, ClampMax = 1.0f, Delta = 0.005f)
        float FilmGrainIntensity = 0.0f;

        /** Grain cell size in pixels. 1.0 == per-pixel salt-and-pepper
         *  (digital sensor noise); 2 - 4 reads as 35mm; >6 looks like
         *  Super 8. */
        PROPERTY(Editable, Category = "Post Process|Film Grain", ClampMin = 0.5f, ClampMax = 8.0f, Delta = 0.05f)
        float FilmGrainSize = 1.5f;

        /** How much grain biases toward shadows. 0 == uniform across the
         *  image; 1 == grain only in dark regions (mimics how film silver
         *  halide is most visible in underexposed areas). 0.6 is a good
         *  cinematic default. */
        PROPERTY(Editable, Category = "Post Process|Film Grain", ClampMin = 0.0f, ClampMax = 1.0f, Delta = 0.01f)
        float FilmGrainResponse = 0.6f;
    };

    /**
     * Blend the float / vec3 fields of `In` onto `InOut` weighted by
     * `Weight` in [0, 1]. Categorical fields (the ToneMapper enum) snap
     * to `In` when Weight >= 0.5 so volumes don't produce nonsense
     * intermediate operators. The master `bEnabled` toggle is intentionally
     * left untouched -- it's a per-source switch, not a blendable value.
     *
     * Defined inline so the renderer and the post-process volume system
     * can share the exact same merge rules without an extra .cpp.
     */
    inline void BlendPostProcessSettings(SPostProcessSettings& InOut, const SPostProcessSettings& In, float Weight)
    {
        if (Weight <= 0.0f || !In.bEnabled)
        {
            return;
        }
        Weight = (Weight < 1.0f) ? Weight : 1.0f;

        const auto LerpF  = [Weight](float A, float B)         { return A + (B - A) * Weight; };
        const auto LerpV3 = [Weight](glm::vec3 A, glm::vec3 B) { return A + (B - A) * Weight; };

        InOut.ExposureCompensation = LerpF (InOut.ExposureCompensation, In.ExposureCompensation);
        InOut.Temperature          = LerpF (InOut.Temperature,          In.Temperature);
        InOut.Tint                 = LerpF (InOut.Tint,                 In.Tint);
        InOut.Contrast             = LerpF (InOut.Contrast,             In.Contrast);
        InOut.Saturation           = LerpF (InOut.Saturation,           In.Saturation);
        InOut.Gamma                = LerpF (InOut.Gamma,                In.Gamma);
        InOut.ColorFilter          = LerpV3(InOut.ColorFilter,          In.ColorFilter);
        InOut.ColorFilterIntensity = LerpF (InOut.ColorFilterIntensity, In.ColorFilterIntensity);
        InOut.Shadows              = LerpV3(InOut.Shadows,              In.Shadows);
        InOut.Midtones             = LerpV3(InOut.Midtones,             In.Midtones);
        InOut.Highlights           = LerpV3(InOut.Highlights,           In.Highlights);
        InOut.VignetteIntensity    = LerpF (InOut.VignetteIntensity,    In.VignetteIntensity);
        InOut.VignetteRoundness    = LerpF (InOut.VignetteRoundness,    In.VignetteRoundness);
        InOut.VignetteSmoothness   = LerpF (InOut.VignetteSmoothness,   In.VignetteSmoothness);
        InOut.VignetteColor        = LerpV3(InOut.VignetteColor,        In.VignetteColor);
        InOut.BloomIntensity       = LerpF (InOut.BloomIntensity,       In.BloomIntensity);
        InOut.BloomThreshold       = LerpF (InOut.BloomThreshold,       In.BloomThreshold);
        InOut.BloomSoftKnee        = LerpF (InOut.BloomSoftKnee,        In.BloomSoftKnee);
        InOut.BloomTint            = LerpV3(InOut.BloomTint,            In.BloomTint);
        InOut.ChromaticAberration  = LerpF (InOut.ChromaticAberration,  In.ChromaticAberration);
        InOut.FilmGrainIntensity   = LerpF (InOut.FilmGrainIntensity,   In.FilmGrainIntensity);
        InOut.FilmGrainSize        = LerpF (InOut.FilmGrainSize,        In.FilmGrainSize);
        InOut.FilmGrainResponse    = LerpF (InOut.FilmGrainResponse,    In.FilmGrainResponse);

        if (Weight >= 0.5f)
        {
            InOut.ToneMapper = In.ToneMapper;
        }
    }
}
