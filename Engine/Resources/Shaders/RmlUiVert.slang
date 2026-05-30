// RmlUi vertex shader (fully-bindless, single-draw path). Each vertex carries a draw index into the
// device-address buffer (see RmlUiCommon); the shader just transforms by the per-draw MVP.
#pragma once
#include "Includes/RmlUiCommon.slang"

struct VSInput
{
    float2 Position  : POSITION;
    float4 Colour    : COLOR;       // premultiplied RGBA8
    float2 UV        : TEXCOORD0;
    uint   DrawIndex : TEXCOORD1;   // R32_UINT vertex attribute
};

struct VSOutput
{
    float4 Position : SV_Position;
    float4 Colour   : COLOR;
    float2 UV       : TEXCOORD0;
    nointerpolation uint DrawIndex : TEXCOORD1;
};

[shader("vertex")]
VSOutput VertexMain(VSInput Input)
{
    FUiDraw Draw = PC.Draws[Input.DrawIndex];

    VSOutput Output;
    Output.Position  = mul(Draw.MVP, float4(Input.Position, 0.0, 1.0));
    Output.Colour    = Input.Colour;
    Output.UV        = Input.UV;
    Output.DrawIndex = Input.DrawIndex;
    return Output;
}
