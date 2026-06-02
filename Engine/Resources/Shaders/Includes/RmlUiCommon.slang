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

// Mirrors FUiVertex in RmlUiRenderer.h (stride 24); pulled by device address (no input layout).
// Order matters: the two float2s are kept adjacent (offsets 0 and 8) so neither vector straddles a
// 16-byte boundary under PhysicalStorageBuffer layout rules. Colour between them would put UV at
// offset 12, which spirv-val rejects (VUID-VkShaderModuleCreateInfo-pCode-08737).
struct FUiVertex
{
    float2 Position;   // @0
    float2 UV;         // @8
    uint   Colour;     // @16  premultiplied RGBA8
    uint   DrawIndex;  // @20  index into Draws[]
};

struct FRmlUiPushConstants
{
    FUiDraw*   Draws;      // device address of the per-draw array (FTransientAlloc::Gpu)
    FUiVertex* Vertices;   // device address of the resident batch vertex buffer (vertex pulling)
};

[[vk::push_constant]] FRmlUiPushConstants PC;
