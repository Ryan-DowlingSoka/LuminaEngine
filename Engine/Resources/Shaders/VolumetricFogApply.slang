#include "Includes/SceneGlobals.slang"
#include "Includes/Fog.slang"
#include "Includes/Froxel.slang"

// Froxel volumetric fog -- apply pass. Per pixel, find the froxel slice from depth,
// trilinearly sample the integrated volume, and composite: out = inScatter + scene*T.
// Implemented with (One, InvSrcAlpha) blend by writing alpha = 1 - transmittance, so
// the scene HDR is never read back. Sky (far plane) takes full aerial perspective.

struct FPushConstants
{
    uint  GridZ;
    float NearPlane;
    float FogRange;
    float _Pad;
};
[[vk::push_constant]] FPushConstants PC;

[[vk::binding(0, 2)]] Sampler2D                                   uSceneDepth;
[[vk::binding(1, 2)]] Sampler3D                                   uFroxelIntegrated;
[[vk::binding(2, 2)]] ConstantBuffer<FExponentialHeightFogParams> uFog;

struct PSInput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

// Interleaved gradient noise -- a stable, per-pixel screen-space hash. Used to dither
// the froxel depth sample by < one slice so the exponential Z banding breaks into fine
// full-resolution grain (far less objectionable than coarse per-froxel noise).
float IGN(float2 PixelPos)
{
    return frac(52.9829189 * frac(dot(PixelPos, float2(0.06711056, 0.00583715))));
}

[shader("fragment")]
float4 FragmentMain(PSInput Input) : SV_Target0
{
    float2 UV    = Input.UV;
    float  Depth = uSceneDepth.SampleLevel(UV, 0.0).r;

    // Reverse-Z: Depth == 0 is the far plane (sky) -> full aerial perspective to range.
    float ViewDist;
    if (Depth > 0.0)
    {
        float3 ViewPos = ReconstructViewPos(UV, Depth, GetInverseCameraProjection());
        ViewDist = -ViewPos.z;   // view space looks down -Z
    }
    else
    {
        ViewDist = PC.FogRange;
    }

    float W = Froxel_DistanceToW(ViewDist, PC.NearPlane, PC.FogRange);
    // Dither the depth sample by < half a slice to break exponential-Z banding into
    // fine per-pixel grain instead of visible color steps.
    W = saturate(W + (IGN(Input.Position.xy) - 0.5) / float(PC.GridZ));

    float4 Fog = uFroxelIntegrated.SampleLevel(float3(UV, W), 0.0);

    float Transmittance = saturate(Fog.a);
    float Opacity       = min(1.0 - Transmittance, uFog.HeightParams.w);  // clamp by FogMaxOpacity

    // (One, InvSrcAlpha): result = Fog.rgb + HDR * (1 - Opacity).
    return float4(Fog.rgb, Opacity);
}
