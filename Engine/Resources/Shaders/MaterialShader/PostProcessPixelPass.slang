#include "Includes/SceneGlobals.slang"

// Post-process material template. Runs as a fullscreen pass after tone
// mapping; the chain ping-pongs between the LDR target and a scratch
// image so each material can read the previous output via uSceneColor.
// The fragment shader returns Material.Emissive as the new scene color.

[[vk::binding(0, 2)]] Sampler2D uSceneColor;       // Current pass input (ping-pong source)
[[vk::binding(1, 2)]] Sampler2D uSceneDepth;       // Opaque depth (raw NDC z, reverse-Z 0..1)
[[vk::binding(2, 2)]] Sampler2D uHDRSceneColor;    // Pre-tone-mapping HDR scene color

// Convert raw NDC depth into linear view-space distance from the camera.
// Matches the project's reverse-Z perspective. Caller passes raw [0, 1]
// depth; output is in world units along view -Z.
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
    uint _Pad0;
    uint _Pad1;
    uint _Pad2;
};

[shader("fragment")]
float4 FragmentMain(PSInput Input, uniform FPostProcessPushConstants PC) : SV_Target0
{
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
