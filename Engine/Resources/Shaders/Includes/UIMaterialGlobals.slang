#pragma once
#include "Common.slang"

// Minimal globals for the UI material domain (fullscreen pass into an offscreen brush, no scene).
// Declares only what node-emitted code touches; deliberately does NOT include SceneGlobals.slang.

// Set 0 -- material uniform buffer (read-only twin of the scene's slot 5).
[[vk::binding(5, 0)]] StructuredBuffer<FMaterialUniforms> uMaterialUniforms;

float  GetMaterialScalar (uint MatIndex, uint Index) { return uMaterialUniforms[MatIndex].Scalars[Index];  }
float4 GetMaterialVec4   (uint MatIndex, uint Index) { return uMaterialUniforms[MatIndex].Vectors[Index];  }
uint   GetMaterialTexture(uint MatIndex, uint Index) { return uMaterialUniforms[MatIndex].Textures[Index]; }

// Set 1 -- bindless texture table (mirrors SceneGlobals.slang layout/indices).
[[vk::binding(0, 1)]] Texture2D     uBindlessTex2D[];
[[vk::binding(1, 1)]] SamplerState  uBindlessSamplers[];

static const uint SAMPLER_LINEAR_WRAP  = 0;
static const uint SAMPLER_LINEAR_CLAMP = 1;

float4 SampleBindless2D(uint ResourceID, uint SamplerIndex, float2 UV)
{
    return uBindlessTex2D[ResourceID].Sample(uBindlessSamplers[SamplerIndex], UV);
}

float4 SampleBindless2DLevel(uint ResourceID, uint SamplerIndex, float2 UV, float Lod)
{
    return uBindlessTex2D[ResourceID].SampleLevel(uBindlessSamplers[SamplerIndex], UV, Lod);
}

// Push constant named uSceneData so emitted ScreenUV/Resolution/AspectRatio nodes compile unchanged.
struct FUIMaterialPushConstants
{
    uint4 ScreenSize;    // .xy = brush resolution
    float Time;
    uint  MaterialIndex;
    uint2 _Pad;
};
[[vk::push_constant]] ConstantBuffer<FUIMaterialPushConstants> uSceneData;

// Scene-dependent helpers the graph may emit. UI has no camera/world, so these
// return neutral values (matches how the PostProcess template zeroes them).
float  GetTime()           { return uSceneData.Time; }
float3 GetCameraPosition() { return float3(0.0); }
