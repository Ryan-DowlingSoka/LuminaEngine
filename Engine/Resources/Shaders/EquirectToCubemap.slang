#include "Includes/Common.slang"
#include "Includes/Sky.slang"

// Bakes an HDR equirectangular panorama (the imported .hdr) into the
// scene's 6-face cube map. Replaces SkyCubeCapture.slang for HDRI-driven
// environments; the downstream IBL passes (irradiance convolution, GGX
// prefilter) read from the same SkyCube image and don't care which path
// produced its contents.
//
// Equirect addressing (Y up, left-handed +Z forward to match the rest
// of the engine):
//   u = 0.5 + atan2(-z, x) / (2 * PI)   -- longitude
//   v = 0.5 - asin(y)      /  PI        -- latitude
//
// The atan2 takes -z (not +z) because a left-handed yaw clockwise sweeps
// world rays through +X, -Z, -X, +Z; we want u to *increase* through
// that sweep so the texture's left-to-right direction matches the
// camera's right-rotation direction. atan2(z, x) sweeps the opposite
// way and produces a horizontally-mirrored sky (text reads backward).
//
// The asin form keeps the poles non-singular under bilinear sampling --
// the alternative `acos(y) / PI` puts the singular point at v = 0 / 1
// where the HDRI's row-0 / row-(H-1) pixels stretch, producing visible
// banding on rough surfaces that read those mips. asin's symmetry around
// v = 0.5 avoids that.

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

    // Texel-centered face UV in [0, 1]. Same convention SkyCubeCapture uses
    // so the irradiance / prefilter passes see identical addressing whether
    // the source was procedural or HDRI.
    float2 UV     = (float2(DTid.xy) + 0.5) / float2(Width, Height);
    float3 RayDir = CubeFaceDirection(DTid.z, UV);

    // World ray -> equirect UV. clamp v away from the exact pole row so
    // bilinear sampling of float16 storage doesn't pull a single garbage
    // texel into the hemisphere taps near +Y / -Y.
    float u = 0.5 + atan2(-RayDir.z, RayDir.x) * (1.0 / TWO_PI);
    float v = 0.5 - asin(clamp(RayDir.y, -1.0, 1.0)) * INV_PI;

    float3 Color = uEquirect.SampleLevel(float2(u, v), 0.0).rgb;

    uSkyCube[DTid] = float4(Color, 1.0);
}
