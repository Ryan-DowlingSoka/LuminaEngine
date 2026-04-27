// RmlUi pixel shader.
//
// RmlUi vertex colours arrive as ColourbPremultiplied (premultiplied RGBA8).
// The vertex stage passes them through unchanged and we modulate by the
// sampled texture, so the final pixel is `vertexColour * texelColour`. With
// premultiplied source colours, the pipeline blend state should be:
//   srcRGB = One,  dstRGB = OneMinusSrcAlpha
//   srcA   = One,  dstA   = OneMinusSrcAlpha
// (handled in the C++ pipeline-state setup, not the shader).
//
// Untextured geometry binds a 1x1 white default texture so the modulate is a
// no-op; that's cleaner than branching on a "no texture" flag.
#pragma once

struct PSInput
{
    float4 Position : SV_Position;
    float4 Colour   : COLOR;
    float2 UV       : TEXCOORD0;
};

[[vk::binding(0, 0)]] Sampler2D uTexture;

[shader("fragment")]
float4 FragmentMain(PSInput Input) : SV_Target0
{
    float4 Texel = uTexture.Sample(Input.UV);
    return Input.Colour * Texel;
}
