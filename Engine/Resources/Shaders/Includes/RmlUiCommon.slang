#pragma once

// Shared layout for the fully-bindless RmlUi pass: per-draw data lives in a device-address buffer
// indexed per-vertex, so the whole UI draws in one DrawIndexed. Scissor is done in the FS (ClipRect).

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
