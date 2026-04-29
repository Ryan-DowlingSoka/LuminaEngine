#include "Includes/Sky.slang"
#include "Includes/IBL.slang"

// Diffuse irradiance convolution. For each output texel of each face the
// shader integrates incoming radiance L(omega) weighted by max(N . omega, 0)
// over the unit hemisphere around N, where N is the world-space direction
// implied by (face, texelUV). Cosine-weighted importance sampling (Malley's
// method) gives PDF p(omega) = (N . omega) / pi, so the importance-weighted
// monte carlo estimator collapses to:
//
//      Irradiance(N) / pi  =  (1 / NumSamples) * sum L(omega_i)
//
// We bake this "diffuse-BRDF-convolved" form so the runtime shader does
// `Diffuse = SampleIrradiance(N) * Albedo` without an explicit / pi.

[[vk::binding(0, 0)]] SamplerCube                              uSrcCube;
[[vk::binding(1, 0)]] RWTexture2DArray<float4>                 uOutCube;

#define IRRADIANCE_TILE 8

[shader("compute")]
[numthreads(IRRADIANCE_TILE, IRRADIANCE_TILE, 1)]
void ComputeMain(uint3 DTid : SV_DispatchThreadID)
{
    uint Width, Height, Layers;
    uOutCube.GetDimensions(Width, Height, Layers);

    if (DTid.x >= Width || DTid.y >= Height || DTid.z >= Layers)
    {
        return;
    }

    float2 UV = (float2(DTid.xy) + 0.5) / float2(Width, Height);
    float3 N  = CubeFaceDirection(DTid.z, UV);

    // Tangent frame around N. Same Up-axis fallback ImportanceSampleGGX
    // uses to avoid a degenerate cross product when N is near +Z.
    float3 Up        = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 Tangent   = normalize(cross(Up, N));
    float3 Bitangent = cross(N, Tangent);

    const uint NumSamples = 1024u;
    float3     Sum        = float3(0.0, 0.0, 0.0);

    for (uint i = 0u; i < NumSamples; ++i)
    {
        // Malley's method: a uniform unit-disk sample lifted onto the
        // hemisphere yields a cosine-weighted direction.
        float2 Xi       = Hammersley(i, NumSamples);
        float  Phi      = 2.0 * PI * Xi.x;
        float  CosTheta = sqrt(1.0 - Xi.y);
        float  SinTheta = sqrt(Xi.y);

        float3 LocalDir = float3(SinTheta * cos(Phi), SinTheta * sin(Phi), CosTheta);
        float3 L        = normalize(LocalDir.x * Tangent + LocalDir.y * Bitangent + LocalDir.z * N);

        Sum += uSrcCube.SampleLevel(L, 0.0).rgb;
    }

    uOutCube[DTid] = float4(Sum / float(NumSamples), 1.0);
}
