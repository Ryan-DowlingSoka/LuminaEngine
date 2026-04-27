// RmlUi vertex shader.
//
// Vertex layout matches Rml::Vertex exactly (RG32_FLOAT pos / RGBA8_UNORM
// premultiplied colour / RG32_FLOAT uv). The CPU side feeds:
//
//   PC.MVP         = Ortho(pixel -> NDC) * (user transform from SetTransform)
//   PC.Translation = per-RenderGeometry pixel-space offset
//
// We add Translation to the vertex's pixel position, then transform by MVP.
// Doing the projection on CPU keeps the shader trivial and works uniformly
// for both untransformed UI and CSS-3D-transformed elements.
#pragma once

struct VSInput
{
    float2 Position : POSITION;
    float4 Colour   : COLOR;
    float2 UV       : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float4 Colour   : COLOR;
    float2 UV       : TEXCOORD0;
};

struct FRmlUiPushConstants
{
    float4x4 MVP;          // 64 B
    float2   Translation;  //  8 B
    float2   _Pad;         //  8 B  (16-byte align for the next push constant block)
};

[[vk::push_constant]] ConstantBuffer<FRmlUiPushConstants> PC;

[shader("vertex")]
VSOutput VertexMain(VSInput Input)
{
    float2 PixelPos = Input.Position + PC.Translation;

    VSOutput Output;
    Output.Position = mul(PC.MVP, float4(PixelPos, 0.0, 1.0));
    Output.Colour   = Input.Colour;
    Output.UV       = Input.UV;
    return Output;
}
