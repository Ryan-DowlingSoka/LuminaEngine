// ImGui vertex shader (bindless backend). Pulls native ImDrawVert from the transient ring by device
// address (PC.VertexAddr) -- no input layout, no bound vertex buffer. Scale/Translate map framebuffer
// pixels to NDC (no Y-flip; Vulkan viewport is +Y-down).
#pragma once
#include "Includes/ImGuiCommon.slang"

// Mirrors ImDrawVert (pos float2 @0, uv float2 @8, col RGBA8 @16), stride 20.
struct ImDrawVert
{
    float2 Position;
    float2 UV;
    uint   Colour;   // packed RGBA8 (ImU32; R in byte 0), the IA used to unpack this for us
};

struct VSOutput
{
    float4 Position : SV_Position;
    float4 Colour   : COLOR;
    float2 UV       : TEXCOORD0;
};

[shader("vertex")]
VSOutput VertexMain(uint VertexID : SV_VulkanVertexID)
{
    // Indexed draw: SV_VulkanVertexID is indexBuffer[i] + VtxOffset, so it indexes the full vertex array.
    ImDrawVert V = ((ImDrawVert*)PC.VertexAddr)[VertexID];

    VSOutput Output;
    Output.Position = float4(V.Position * PC.Scale + PC.Translate, 0.0, 1.0);
    Output.Colour   = float4( (V.Colour        & 0xFFu),
                              ((V.Colour >>  8) & 0xFFu),
                              ((V.Colour >> 16) & 0xFFu),
                              ((V.Colour >> 24) & 0xFFu)) / 255.0;
    Output.UV       = V.UV;
    return Output;
}
