#pragma once
#include "Includes/SceneGlobals.slang"

// Hemisphere SSAO from depth alone. View-space position is reconstructed from the depth buffer; the
// normal is reconstructed from neighbouring depth taps (no G-Buffer normal). A small tiled noise texture
// rotates the kernel per-pixel. Output is a single grayscale AO factor in [0,1] (1 = unoccluded).

struct FragmentInput
{
    float2 UV : TEXCOORD0;
};

struct FragmentOutput
{
    float4 Color : SV_Target0;
};

struct FData
{
    uint DepthIndex;   // bindless SRV of the scene depth buffer
    uint NoiseIndex;   // bindless SRV of the 4x4 RGBA32F rotation noise
};

[shader("fragment")]
FragmentOutput FragmentMain(FragmentInput Input)
{
    FragmentOutput Output;

    FData* Data = GetPassArgs<FData>();

    float2 ScreenSize = float2(Scene().ScreenSize.xy);
    float2 TexelSize  = 1.0 / ScreenSize;

    float4x4 Proj    = GetCameraProjection();
    float4x4 InvProj = GetInverseCameraProjection();

    float DepthC = SampleTexture2D(Data.DepthIndex, SAMPLER_POINT_CLAMP, Input.UV).r;

    // Reverse-Z: the far plane / sky is 0. Nothing to occlude there -> fully lit.
    if (DepthC <= 0.0)
    {
        Output.Color = float4(1.0, 1.0, 1.0, 1.0);
        return Output;
    }

    // View-space position of this pixel and its right/up neighbours, used to reconstruct the normal.
    float3 P  = ReconstructViewPos(Input.UV, DepthC, InvProj);

    float DepthR = SampleTexture2D(Data.DepthIndex, SAMPLER_POINT_CLAMP, Input.UV + float2(TexelSize.x, 0.0)).r;
    float DepthU = SampleTexture2D(Data.DepthIndex, SAMPLER_POINT_CLAMP, Input.UV + float2(0.0, TexelSize.y)).r;
    float3 Px = ReconstructViewPos(Input.UV + float2(TexelSize.x, 0.0), DepthR, InvProj);
    float3 Py = ReconstructViewPos(Input.UV + float2(0.0, TexelSize.y), DepthU, InvProj);

    float3 N = normalize(cross(Px - P, Py - P));
    // The camera sits at the view-space origin looking down -Z, so a visible surface's normal faces it.
    if (dot(N, -P) < 0.0)
    {
        N = -N;
    }

    // Tile the 4x4 noise across the screen (wrap point sampler) for a per-pixel TBN rotation.
    float2 NoiseScale = ScreenSize / 4.0;
    float3 RandomVec  = SampleTexture2D(Data.NoiseIndex, SAMPLER_POINT_WRAP, Input.UV * NoiseScale).xyz;

    // Gram-Schmidt: build an orthonormal tangent basis with the noise as the in-plane rotation.
    float3 Tangent   = normalize(RandomVec - N * dot(RandomVec, N));
    float3 Bitangent = cross(N, Tangent);
    float3x3 TBN     = float3x3(Tangent, Bitangent, N);   // rows; mul(v, TBN) = tangent -> view

    float Radius = Scene().SSAOSettings.Radius;
    float Bias   = Scene().SSAOSettings.Bias;

    float Occlusion = 0.0;
    for (int i = 0; i < SSAO_KERNEL_SIZE; ++i)
    {
        // Kernel sample: tangent-space hemisphere direction rotated into view space, placed around P.
        float3 SampleDir = mul(Scene().SSAOSettings.Samples[i].xyz, TBN);
        float3 SamplePos = P + SampleDir * Radius;

        // Project the sample into screen UV to look up the actual scene depth there.
        float4 Offset = mul(Proj, float4(SamplePos, 1.0));
        Offset.xyz /= Offset.w;
        float2 SampleUV = Offset.xy * 0.5 + 0.5;

        if (any(SampleUV < 0.0) || any(SampleUV > 1.0))
        {
            continue;
        }

        float SampleDepth = SampleTexture2D(Data.DepthIndex, SAMPLER_POINT_CLAMP, SampleUV).r;
        if (SampleDepth <= 0.0)
        {
            continue;
        }

        // View-space Z of the scene surface at the sample's screen location.
        float SceneZ = ReconstructViewPos(SampleUV, SampleDepth, InvProj).z;

        // This engine's view space is +Z forward, so a sample is occluded if the scene depth is less than its own (minus bias).
        float RangeCheck = smoothstep(0.0, 1.0, Radius / max(abs(P.z - SceneZ), 1e-4));
        Occlusion += ((SceneZ <= SamplePos.z - Bias) ? 1.0 : 0.0) * RangeCheck;
    }

    float AO = 1.0 - (Occlusion / float(SSAO_KERNEL_SIZE)) * Scene().SSAOSettings.Intensity;
    AO = pow(saturate(AO), Scene().SSAOSettings.Power);

    Output.Color = float4(AO, AO, AO, 1.0);
    return Output;
}
