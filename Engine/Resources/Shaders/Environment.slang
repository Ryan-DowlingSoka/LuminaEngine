#pragma once
#include "Includes/SceneGlobals.slang"
#include "Includes/Sky.slang"

// Fullscreen sky pass. The C++ EnvironmentPass writes one FEnvironmentParams
// per frame; the shader switches on Misc.x via Sky.slang to pick a path
// (SolidColor / Gradient / Dynamic).
//
// The IBL cubemap capture uses the same Sky.slang functions but feeds them
// directional rays from face / texel coordinates instead of unprojecting
// from screen space, so it does not depend on the camera projection at all.

[[vk::binding(0, 2)]] ConstantBuffer<FEnvironmentParams> uEnvironment;

struct FragmentInput  { float2 UV : TEXCOORD0; };
struct FragmentOutput { float4 Color : SV_Target0; };

// Reconstruct a world-space ray from a fullscreen-quad UV. The Y-flip baked
// into the camera projection (Vulkan +Y-down NDC) is undone by
// GetInverseCameraProjection() since the inverse of a Y-flipped matrix is
// also Y-flipped, so feeding the +Y-down NDC back through it lands in the
// correct view space.
//
// RayClip.z = 1 picks the near plane (which in reverse-Z is the geometric
// near). Either Z value would give the same direction once we normalize, so
// any sky-distant Z works; we keep 1.0 here for parity with the original.
float3 GetViewRay(float2 UV)
{
    float2 NDC      = UV * 2.0 - 1.0;
    float4 RayClip  = float4(NDC, 1.0, 1.0);
    float4 RayView  = mul(GetInverseCameraProjection(), RayClip);
    RayView.xyz    /= RayView.w;
    float3 RayWorld = mul(GetInverseCameraView(), float4(RayView.xyz, 0.0)).xyz;
    return normalize(RayWorld);
}

[shader("fragment")]
FragmentOutput FragmentMain(FragmentInput Input)
{
    float3 RayDir = GetViewRay(Input.UV);
    float3 SunDir = GetSunDirection();
    float3 Color  = EvaluateSky(uEnvironment, SunDir, RayDir);

    FragmentOutput Output;
    Output.Color = float4(Color, 1.0);
    return Output;
}
