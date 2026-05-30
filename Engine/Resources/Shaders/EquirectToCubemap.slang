#include "Includes/Common.slang"
#include "Includes/Sky.slang"

// Bakes an HDR equirect panorama into the scene's 6-face cube map (HDRI path; downstream IBL is identical).
// Addressing (Y up, left-handed +Z fwd): u = 0.5 + atan2(-z, x)/2PI; v = 0.5 - asin(y)/PI. The -z keeps u
// increasing with camera right-rotation (else the sky mirrors); asin keeps the poles non-singular under bilinear.

[[vk::binding(0, 0)]] Sampler2D                     uEquirect;
[[vk::binding(1, 0)]] RWTexture2DArray<float4>      uSkyCube;

#define EQUIRECT_TILE 8

[shader("compute")]
[numthreads(EQUIRECT_TILE, EQUIRECT_TILE, 1)]
void ComputeMain(uint3 DTid : SV_DispatchThreadID)
{
    uint Width, Height, Layers;
    uSkyCube.GetDimensions(Width, Height, Layers);

    if (DTid.x >= Width || DTid.y >= Height || DTid.z >= Layers)
    {
        return;
    }

    // Texel-centered face UV; same convention as SkyCubeCapture so IBL addressing matches either source.
    float2 UV     = (float2(DTid.xy) + 0.5) / float2(Width, Height);
    float3 RayDir = CubeFaceDirection(DTid.z, UV);

    // World ray -> equirect UV; asin clamps v off the exact pole row to avoid garbage bilinear taps.
    float u = 0.5 + atan2(-RayDir.z, RayDir.x) * (1.0 / TWO_PI);
    float v = 0.5 - asin(clamp(RayDir.y, -1.0, 1.0)) * INV_PI;

    float3 Color = uEquirect.SampleLevel(float2(u, v), 0.0).rgb;

    uSkyCube[DTid] = float4(Color, 1.0);
}
