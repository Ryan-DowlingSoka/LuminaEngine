#pragma once
#include "Includes/SceneGlobals.slang"

struct VSInput
{
    float3 Position : POSITION;
    uint   Color    : COLOR;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float4 Color    : COLOR;
};

[shader("vertex")]
VSOutput VertexMain(VSInput Input)
{
    VSOutput Output;
    Output.Position = mul(GetCameraProjection(), mul(GetCameraView(), float4(Input.Position, 1.0f)));
    Output.Color    = UnpackColor(Input.Color);
    return Output;
}