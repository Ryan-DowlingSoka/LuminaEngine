#include "Includes/SceneGlobals.slang"

// Post-process material template; fullscreen pass after tone mapping, ping-ponging LDR target and a
// scratch image so each material reads the previous output via uSceneColor. Returns Material.Emissive.
// No pass-local set: the three scene inputs are read through the bindless table via push-constant
// indices. The codegen emits `uSceneColor.Sample(uv)` etc., so each input is a small wrapper that
// mirrors the Sampler2D interface; FragmentMain builds them from the indices before $MATERIAL_INPUTS.
struct FSceneTex
{
    Texture2D    Tex;
    SamplerState Smp;
    float4 Sample(float2 uv)               { return Tex.Sample(Smp, uv); }
    float4 SampleLevel(float2 uv, float l)  { return Tex.SampleLevel(Smp, uv, l); }
};

// Raw reverse-Z NDC depth [0,1] -> linear view-space distance (world units along view -Z).
float LinearizeSceneDepth(float RawDepth)
{
    float ZNear = GetNearPlane();
    float ZFar  = GetFarPlane();
    return ZNear * ZFar / max(ZFar - RawDepth * (ZFar - ZNear), 1e-6);
}

struct PSInput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

struct FPostProcessPushConstants
{
    uint MaterialIndex;
    uint SceneColorIndex;   // bindless 2D SRV: ping-pong source (linear-clamp)
    uint SceneDepthIndex;   // bindless 2D SRV: opaque depth (point-clamp)
    uint HDRIndex;          // bindless 2D SRV: pre-tone-map HDR (linear-clamp)
};

#define PC (*(FPostProcessPushConstants*)gPC.PassAddr)

[shader("fragment")]
float4 FragmentMain(PSInput Input) : SV_Target0
{
    // Scene inputs as wrappers named to match the codegen's `uSceneColor.Sample(uv)` references.
    FSceneTex uSceneColor;    uSceneColor.Tex    = uBindlessTex2D[PC.SceneColorIndex]; uSceneColor.Smp    = uBindlessSamplers[SAMPLER_LINEAR_CLAMP];
    FSceneTex uSceneDepth;    uSceneDepth.Tex    = uBindlessTex2D[PC.SceneDepthIndex]; uSceneDepth.Smp    = uBindlessSamplers[SAMPLER_POINT_CLAMP];
    FSceneTex uHDRSceneColor; uHDRSceneColor.Tex = uBindlessTex2D[PC.HDRIndex];        uHDRSceneColor.Smp = uBindlessSamplers[SAMPLER_LINEAR_CLAMP];

    uint   MaterialIndex = PC.MaterialIndex;
    float2 UV0           = Input.UV;
    float4 SceneColor    = uSceneColor.Sample(Input.UV);

    // Surface-base-pass aliases. Zeroed because there is no rasterized
    // geometry feeding this pass; node-emit code may still reference them.
    float3 WorldPosition = float3(0.0);
    float3 WorldNormal   = float3(0.0, 0.0, 1.0);
    float3 ViewPosition  = float3(0.0);
    float4 VertexColor   = float4(1.0);
    uint   EntityID      = 0;

    $MATERIAL_INPUTS

    return float4(Material.Emissive, 1.0);
}
