#pragma once
#include "GlobalRHI.slang"

// Bindless ImGui pass: vertices pulled by device address, textures sampled from the
// global heap by ResourceID. Args re-point per draw (TextureID changes).

struct FImGuiArgs
{
    float2   Scale;        // 2 / DisplaySize
    float2   Translate;    // -1 - DisplayPos * Scale
    uint     TextureID;    // heap ResourceID (gTextures2D[])
    uint     SamplerIndex; // SAMPLER_* heap slot
    uint64_t VertexAddr;   // device address of the transient ImDrawVert array
};

// Mirrors ImDrawVert (pos float2 @0, uv float2 @8, col RGBA8 @16), stride 20.
struct ImDrawVert
{
    float2 Position;
    float2 UV;
    uint   Color;
};

struct FImGuiInterpolants
{
    float4 Position : SV_Position;
    float4 Color    : COLOR;
    float2 UV       : TEXCOORD0;
};
