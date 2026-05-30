// RmlUi pixel shader (fully-bindless, single-draw path). Per-draw data comes from the device-address
// buffer by flat draw index; scissor is an axis-aligned clip test here. Colors are premultiplied.
#pragma once
#include "Includes/RmlUiCommon.slang"

[[vk::binding(0, 0)]] Texture2D     uBindlessTex2D[];
[[vk::binding(1, 0)]] SamplerState  uBindlessSamplers[];

struct PSInput
{
    float4 Position : SV_Position;
    float4 Colour   : COLOR;
    float2 UV       : TEXCOORD0;
    nointerpolation uint DrawIndex : TEXCOORD1;
};

[shader("fragment")]
float4 FragmentMain(PSInput Input) : SV_Target0
{
    FUiDraw Draw = PC.Draws[Input.DrawIndex];

    // Shader-side scissor (axis-aligned, framebuffer pixels).
    float2 P = Input.Position.xy;
    if (P.x < Draw.ClipRect.x || P.y < Draw.ClipRect.y || P.x >= Draw.ClipRect.z || P.y >= Draw.ClipRect.w)
    {
        discard;
    }

    float4 Texel = uBindlessTex2D[Draw.TextureID].Sample(uBindlessSamplers[Draw.SamplerIndex], Input.UV);
    return Input.Colour * Texel;
}
