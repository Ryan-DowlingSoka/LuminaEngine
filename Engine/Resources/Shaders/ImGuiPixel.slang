// ImGui pixel shader (bindless backend).
//
// The texture is sampled straight out of the engine bindless table by the
// push-constant resource id, so there are no per-texture descriptor sets --
// the table is bound once for the whole UI. ImGui vertex colours are straight
// (not premultiplied); final pixel = vertexColour * texel.
#pragma once
#include "Includes/ImGuiCommon.slang"

[[vk::binding(0, 0)]] Texture2D     uBindlessTex2D[];
[[vk::binding(1, 0)]] SamplerState  uBindlessSamplers[];

struct PSInput
{
    float4 Position : SV_Position;
    float4 Colour   : COLOR;
    float2 UV       : TEXCOORD0;
};

[shader("fragment")]
float4 FragmentMain(PSInput Input) : SV_Target0
{
    float4 Texel = uBindlessTex2D[PC.TextureID].Sample(uBindlessSamplers[PC.SamplerIndex], Input.UV);
    return Input.Colour * Texel;
}
