#pragma once

struct FPrismPush
{
    float2 InvScreenSize;
    float2 _Pad;
};

[[vk::push_constant]] ConstantBuffer<FPrismPush> uPush;

// Bindless sampler table shared with the rest of the engine. UI textures
// register with FTextureManager and their index is baked into each vertex.
[[vk::binding(0, 0)]] Sampler2D uGlobalTextures[];

struct VSInput
{
    float2 Position  : POSITION;
    float2 UV        : TEXCOORD0;
    float4 Color     : COLOR0;
    float4 ClipRect  : TEXCOORD1;
    float2 LocalSize : TEXCOORD2;
    float4 Params    : TEXCOORD3;
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float2 UV        : TEXCOORD0;
    float4 Color     : COLOR0;
    float4 ClipRect  : TEXCOORD1;
    float2 ScreenPos : TEXCOORD2;
    float2 LocalSize : TEXCOORD3;
    float4 Params    : TEXCOORD4;
};

[shader("vertex")]
VSOutput VertexMain(VSInput Input)
{
    VSOutput Output;

    // Pixels -> NDC. Vulkan's native NDC has +Y pointing down, so a direct
    // [0,size] -> [-1,1] mapping puts pixel (0,0) at the top-left without a
    // Y flip. Flipping here inverted asymmetric content like text glyphs.
    float2 Ndc = Input.Position * uPush.InvScreenSize * 2.0 - 1.0;

    Output.Position  = float4(Ndc, 0.0, 1.0);
    Output.UV        = Input.UV;
    Output.Color     = Input.Color;
    Output.ClipRect  = Input.ClipRect;
    Output.ScreenPos = Input.Position;
    Output.LocalSize = Input.LocalSize;
    Output.Params    = Input.Params;
    return Output;
}