#pragma once

// Shared layout for the fully-bindless RmlUi pass. All per-draw data lives in a
// device-address buffer indexed by a per-vertex draw index, so the whole UI is
// drawn with a single DrawIndexed -- no per-draw push constants, scissor, or
// state changes. Scissor is done in the fragment shader (ClipRect).

struct FUiDraw
{
    float4x4 MVP;
    float4   ClipRect;     // minX, minY, maxX, maxY in framebuffer pixels
    uint     TextureID;    // bindless resource id (FRHIImage::GetResourceID)
    uint     SamplerIndex; // stock bindless sampler index
    uint2    _Pad;
};

struct FRmlUiPushConstants
{
    FUiDraw* Draws;        // device address of the per-draw array (FTransientAlloc::Gpu)
};

[[vk::push_constant]] FRmlUiPushConstants PC;
