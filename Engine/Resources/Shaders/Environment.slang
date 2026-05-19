#pragma once
#include "Includes/SceneGlobals.slang"
#include "Includes/Sky.slang"

// Fullscreen sky pass. The C++ EnvironmentPass writes one FEnvironmentParams
// per frame; the shader switches on Misc.x to pick a path
// (SolidColor / Gradient / Dynamic / HDRI).
//
// All non-solid modes sample uSkyCube, which SkyCubeCapturePass has
// already filled this frame using the same EvaluateSky() that used to
// run per-fragment here. Doing the heavy sky math once at cube
// resolution and reading it through a single cube fetch per pixel is
// what makes the pass cheap on low-end hardware.

[[vk::binding(0, 2)]] ConstantBuffer<FEnvironmentParams> uEnvironment;
// Same SkyCube the IBL pipeline reads. SkyCubeCapturePass populates it
// each frame -- either from the procedural sky shader or from the
// imported HDRI via EquirectToCubemap.
[[vk::binding(1, 2)]] SamplerCube                        uSkyCube;

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
    float3 Color;
    uint SkyMode = asuint(uEnvironment.Misc.x);
    if (SkyMode == SKY_MODE_SOLID)
    {
        // Flat color: skip the cube fetch entirely.
        Color = uEnvironment.SolidSkyColor.rgb;
    }
    else
    {
        // Gradient / Dynamic / HDRI all live in uSkyCube now. The cube is
        // produced by SkyCubeCapturePass earlier this frame.
        float3 RayDir = GetViewRay(Input.UV);
        Color = uSkyCube.SampleLevel(RayDir, 0.0).rgb;

        // Per-pixel overlays (Dynamic mode only). Sun, moon, and stars are
        // pixel-scale features that look blocky baked into a 256 cube, so
        // they live here in the fullscreen pass instead. Each one early-
        // outs cheaply when its feature is not in view, so the typical
        // fragment pays only a couple of dot products plus the cube fetch.
        if (SkyMode == SKY_MODE_DYNAMIC)
        {
            float3 SunDir      = GetSunDirection();
            float  SunHeight   = SunDir.y;
            float  NightFactor = smoothstep(0.05, -0.15, SunHeight);
            float  UpMask      = smoothstep(-0.05, 0.1, RayDir.y);

            float3 Overlay = float3(0.0);

            // Sun disc + glow. Skip the full Sky_RenderSun() math when the
            // ray is nowhere near the sun -- a single dot product gate.
            float DiscScale = uEnvironment.Misc.y;
            float DiscAngle = kSunBaseAngle * max(DiscScale, 0.0);
            if (dot(RayDir, SunDir) > cos(DiscAngle * 1.8))
            {
                float3 SunColor = Sky_SunTimeOfDayColor(SunHeight)
                                * uEnvironment.SunTint.rgb
                                * uEnvironment.SunTint.w;
                Overlay += Sky_RenderSun(RayDir, SunDir, SunColor, DiscScale);
            }

            // Moon + stars only matter at night. Moon has its own dot-gate
            // inside Sky_RenderMoon; stars need an UpMask to avoid showing
            // through the lit ground band.
            if (NightFactor > 0.0)
            {
                Overlay += Sky_RenderMoon(RayDir, SunDir, uEnvironment, NightFactor);
                if (UpMask > 0.0)
                {
                    Overlay += Sky_Stars(RayDir, uEnvironment, GetTime(), NightFactor) * UpMask;
                }
            }

            // Match the cube's exposure + Reinhard tonemap so overlay
            // intensities read consistently against the sampled cube.
            float Exposure = max(uEnvironment.Misc.z, 1e-3);
            Overlay *= Exposure;
            Overlay = Overlay / (1.0 + Overlay);
            Color += Overlay;
        }
    }

    FragmentOutput Output;
    Output.Color = float4(Color, 1.0);
    return Output;
}
