// ImGui vertex shader (bindless backend). Consumes native ImDrawVert directly; Scale/Translate map
// framebuffer pixels to NDC (no Y-flip; Vulkan viewport is +Y-down).
#pragma once
#include "Includes/ImGuiCommon.slang"

struct VSInput
{
    float2 Position : POSITION;
    float2 UV       : TEXCOORD0;
    float4 Colour   : COLOR;     // RGBA8_UNORM (ImU32, not premultiplied)
};

struct VSOutput
{
    float4 Position : SV_Position;
    float4 Colour   : COLOR;
    float2 UV       : TEXCOORD0;
};

[shader("vertex")]
VSOutput VertexMain(VSInput Input)
{
    VSOutput Output;
    Output.Position = float4(Input.Position * PC.Scale + PC.Translate, 0.0, 1.0);
    Output.Colour   = Input.Colour;
    Output.UV       = Input.UV;
    return Output;
}
