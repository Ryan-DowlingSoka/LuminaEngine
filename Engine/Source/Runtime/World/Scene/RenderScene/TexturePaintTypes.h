#pragma once
#include "Renderer/RenderResource.h"

namespace Lumina
{
    // A single paint/clear request against a render-target texture. Enqueued from the
    // game thread (CWorld::PaintRenderTarget / ClearRenderTarget), drained into the
    // frame snapshot during Extract, executed as a compute dispatch in TexturePaintPass.
    struct FTexturePaintOp
    {
        enum class EMode : uint8 { Paint, Clear };

        // Holds the target image alive across the frame boundary (the source asset may be
        // destroyed between enqueue and the render thread consuming this op).
        FRHIImageRef    Target;

        EMode           Mode        = EMode::Paint;

        glm::vec4       Color       = glm::vec4(1.0f);   // RGBA brush/clear color
        glm::vec2       CenterUV    = glm::vec2(0.5f);   // brush center, 0..1
        float           RadiusUV    = 0.05f;             // brush radius in UV units
        float           Strength    = 1.0f;              // 0..1 opacity at the brush center
        float           Hardness    = 1.0f;              // edge falloff exponent; >1 sharper
        int32           BrushIndex  = -1;                // bindless SRV of an optional brush mask; -1 = solid radial
    };
}
