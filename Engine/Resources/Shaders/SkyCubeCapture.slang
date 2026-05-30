#include "Includes/Sky.slang"

// Bakes the procedural sky into a 6-face cubemap for the IBL passes; one thread per texel computes
// its world-space ray from (face, texel), camera-independent. Written via a Texture2DArray UAV view.

[[vk::binding(0, 0)]] RWTexture2DArray<float4>                uSkyCube;
[[vk::binding(1, 0)]] ConstantBuffer<FEnvironmentParams>      uEnvironment;

struct FPushConstants
{
    float3 SunDirection;
    float  Time;
};
[[vk::push_constant]] FPushConstants PC;

#define SKYCAP_TILE 8

[shader("compute")]
[numthreads(SKYCAP_TILE, SKYCAP_TILE, 1)]
void ComputeMain(uint3 DTid : SV_DispatchThreadID)
{
    uint Width, Height, Layers;
    uSkyCube.GetDimensions(Width, Height, Layers);

    if (DTid.x >= Width || DTid.y >= Height || DTid.z >= Layers)
    {
        return;
    }

    // Texel-centered face UV in [0, 1]. Sky.slang::CubeFaceDirection remaps
    // to [-1, 1] internally for the face-axis math.
    float2 UV = (float2(DTid.xy) + 0.5) / float2(Width, Height);

    float3 RayDir = CubeFaceDirection(DTid.z, UV);
    float3 Color  = EvaluateSky(uEnvironment, PC.SunDirection, RayDir, PC.Time);

    uSkyCube[DTid] = float4(Color, 1.0);
}
