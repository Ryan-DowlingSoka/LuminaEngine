#pragma once

// Shared push block for the bindless ImGui backend; only TextureID changes between draws, so a 32 B
// push constant beats a per-draw buffer. VS pulls ImDrawVert from VertexAddr and maps pixels -> NDC via
// Scale/Translate, PS samples by TextureID.

struct FImGuiPushConstants
{
    float2   Scale;        // 2 / DisplaySize
    float2   Translate;    // -1 - DisplayPos * Scale
    uint     TextureID;    // bindless resource id (FRHIImage::GetResourceID / font atlas)
    uint     SamplerIndex; // stock bindless sampler index
    uint64_t VertexAddr;   // device address of the transient ImDrawVert array (vertex pulling)
};

[[vk::push_constant]] FImGuiPushConstants PC;
