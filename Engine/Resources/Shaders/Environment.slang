#pragma once
#include "Includes/SceneGlobals.slang"
#include "Includes/Sky.slang"

// Fullscreen sky pass; switches on Misc.x (SolidColor / Gradient / Dynamic / HDRI). Non-solid modes
// read uSkyCube, which SkyCubeCapturePass fills once per frame -- one cube fetch/pixel instead of the full sky math.

[[vk::binding(0, 2)]] ConstantBuffer<FEnvironmentParams> uEnvironment;
// Same SkyCube the IBL pipeline reads (procedural sky or HDRI via EquirectToCubemap).
[[vk::binding(1, 2)]] SamplerCube                        uSkyCube;
// Imported HDRI equirect, sampled directly so the visible sky shows full resolution (not the 256 cube).
[[vk::binding(2, 2)]] Sampler2D                          uEquirect;

struct FragmentInput  { float2 UV : TEXCOORD0; };
struct FragmentOutput { float4 Color : SV_Target0; };

// Reconstruct a world-space ray from fullscreen-quad UV. The camera projection's Y-flip is undone by
// GetInverseCameraProjection (the inverse is also Y-flipped). RayClip.z is arbitrary post-normalize.
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
        // Gradient / Dynamic read uSkyCube; HDRI reads its source equirect directly (the cube is
        // fine for IBL but too coarse full-screen).
        float3 RayDir = GetViewRay(Input.UV);
        if (SkyMode == SKY_MODE_HDRI)
        {
            // World ray -> equirect UV. Matches EquirectToCubemap.slang so the
            // visible sky lines up with the cube-derived IBL / reflections.
            float u = 0.5 + atan2(-RayDir.z, RayDir.x) * (1.0 / TWO_PI);
            float v = 0.5 - asin(clamp(RayDir.y, -1.0, 1.0)) * INV_PI;
            Color = uEquirect.SampleLevel(float2(u, v), 0.0).rgb;
        }
        else
        {
            Color = uSkyCube.SampleLevel(RayDir, 0.0).rgb;
        }

        // Per-pixel overlays (Dynamic only): sun/moon/stars are pixel-scale and blocky in a 256 cube,
        // so they run here. Each early-outs cheaply when off-screen.
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

            // Moon + stars only matter at night; stars need UpMask to stay out of the lit ground band.
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
