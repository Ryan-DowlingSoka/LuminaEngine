#include "Includes/SceneGlobals.slang"
#include "Includes/Fog.slang"
#include "Includes/Froxel.slang"

// Froxel volumetric fog apply pass: sample the integrated volume by depth and composite via
// (One, InvSrcAlpha) blend (alpha = 1 - transmittance), so the scene HDR is never read back.

// No pass-local set: scene depth (2D) + integrated volume (3D) read via bindless indices, fog params
// via a device-address pointer, all in the push constant.
struct FPushConstants
{
    ConstBufferPointer<FExponentialHeightFogParams> Fog;
    uint  DepthIndex;        // bindless 2D SRV: scene depth
    uint  IntegratedIndex;   // bindless 3D SRV: integrated froxel volume
    uint  GridZ;
    float NearPlane;
    float FogRange;
    uint  _Pad0;
};
#define PC (*(FPushConstants*)gPC.PassAddr)

struct PSInput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

// Interleaved gradient noise: stable per-pixel hash used to dither the depth sample by < one slice,
// breaking exponential-Z banding into fine full-resolution grain.
float IGN(float2 PixelPos)
{
    return frac(52.9829189 * frac(dot(PixelPos, float2(0.06711056, 0.00583715))));
}

[shader("fragment")]
float4 FragmentMain(PSInput Input) : SV_Target0
{
    FExponentialHeightFogParams uFog = PC.Fog.get();

    float2 UV    = Input.UV;
    float  Depth = uBindlessTex2D[PC.DepthIndex].SampleLevel(uBindlessSamplers[SAMPLER_POINT_CLAMP], UV, 0.0).r;

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

    float4 Fog = uBindlessTex3D[PC.IntegratedIndex].SampleLevel(uBindlessSamplers[SAMPLER_LINEAR_CLAMP], float3(UV, W), 0.0);

    float Transmittance = saturate(Fog.a);
    float Opacity       = min(1.0 - Transmittance, uFog.HeightParams.w);  // clamp by FogMaxOpacity

    // (One, InvSrcAlpha): result = Fog.rgb + HDR * (1 - Opacity).
    return float4(Fog.rgb, Opacity);
}
