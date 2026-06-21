#pragma once
#include "Common.slang"
#include "SceneGlobals.slang"

// DBuffer decals (forward-renderer deferred decals). The DecalPass projects decal boxes onto scene
// depth and writes BaseColor / WorldNormal / Roughness-Metallic-AO into these three unorm targets,
// blended so each target's alpha holds TRANSMITTANCE = product of (1 - coverage) over all decals
// (cleared to a=1, blend RGB = SrcAlpha/InvSrcAlpha, A = Zero/InvSrcAlpha). The base pass composites
// the surface under the accumulated decal layer: result = surface * transmittance + premultipliedDecal.
//
// No pass-local set: the three DBuffer targets are read through the bindless table via indices in this
// push constant (opaque base pass only). A pixel untouched by any decal reads a=1, rgb=0, so it's a no-op.
struct FDBufferPushConstants
{
    uint MeshletBase;     // mesh path: MeshletDrawList base for this draw (read by MeshletMesh.slang); unused by the PS
    uint DBufferAIndex;   // rgb BaseColor,                a transmittance
    uint DBufferBIndex;   // rg  WorldNormal (OctEncode01), a transmittance
    uint DBufferCIndex;   // r Roughness g Metallic b AO,  a transmittance
};
FDBufferPushConstants* DBufferArgs() { return GetPassArgs<FDBufferPushConstants>(); }

// Blend the accumulated decal layer at ScreenUV into the surface material attributes, in place.
// Call after the material is resolved and BEFORE lighting so decals receive lights/shadows/IBL.
void ApplyDBuffer(inout float3 Albedo, inout float3 WorldNormal, inout float Roughness,
                  inout float Metallic, inout float AO, float2 ScreenUV)
{
    // Sentinel index (no decals this frame) -> skip the three DBuffer samples; the composite is a no-op.
    if (DBufferArgs().DBufferAIndex == 0xFFFFFFFFu)
    {
        return;
    }

    SamplerState Smp = gSamplers[SAMPLER_POINT_CLAMP];
    float4 A = gTextures2D[DBufferArgs().DBufferAIndex].SampleLevel(Smp, ScreenUV, 0.0);
    float4 B = gTextures2D[DBufferArgs().DBufferBIndex].SampleLevel(Smp, ScreenUV, 0.0);
    float4 C = gTextures2D[DBufferArgs().DBufferCIndex].SampleLevel(Smp, ScreenUV, 0.0);

    // BaseColor: standard "over" composite of the decal layer (rgb premultiplied over black) on top of
    // the surface attenuated by the layer transmittance.
    if (A.a < 0.9999)
    {
        Albedo = Albedo * A.a + A.rgb;
    }

    // Normal: composite in octahedral space (approximate but stable), then re-decode to a unit normal.
    if (B.a < 0.9999)
    {
        float2 OctSurface   = OctEncode01(WorldNormal);
        float2 OctComposite = B.rg + OctSurface * B.a;
        WorldNormal = OctDecode01(OctComposite);
    }

    // Roughness / Metallic / AO: same transmittance composite per channel.
    if (C.a < 0.9999)
    {
        Roughness = saturate(Roughness * C.a + C.r);
        Metallic  = saturate(Metallic  * C.a + C.g);
        AO        = saturate(AO        * C.a + C.b);
    }
}
