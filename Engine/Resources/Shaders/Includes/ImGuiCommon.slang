#pragma once

// Shared push block for the bindless ImGui backend. ImGui issues one draw per
// ImDrawCmd; only TextureID changes between draws in a list, so the cheap path
// is a 24 B push constant rather than a per-draw device-address buffer. The
// vertex shader maps framebuffer pixels -> NDC via Scale/Translate (classic
// ImGui ortho), the pixel shader samples the bindless table by TextureID.

struct FImGuiPushConstants
{
    float2 Scale;        // 2 / DisplaySize
    float2 Translate;    // -1 - DisplayPos * Scale
    uint   TextureID;    // bindless resource id (FRHIImage::GetResourceID / font atlas)
    uint   SamplerIndex; // stock bindless sampler index
};

[[vk::push_constant]] FImGuiPushConstants PC;
