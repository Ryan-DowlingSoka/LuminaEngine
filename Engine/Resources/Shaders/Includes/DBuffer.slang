#pragma once
#include "Common.slang"

// DBuffer decals (forward-renderer deferred decals). The DecalPass projects decal boxes onto scene
// depth and writes BaseColor / WorldNormal / Roughness-Metallic-AO into these three unorm targets,
// blended so each target's alpha holds TRANSMITTANCE = product of (1 - coverage) over all decals
// (cleared to a=1, blend RGB = SrcAlpha/InvSrcAlpha, A = Zero/InvSrcAlpha). The base pass composites
// the surface under the accumulated decal layer: result = surface * transmittance + premultipliedDecal.
//
// Bound at the opaque base pass's set 2 (which is otherwise an empty placeholder). Kept out of the
// scene set so the DecalPass -- which writes these as render targets while binding the scene set -- never
// references them as SRVs. A pixel untouched by any decal reads a=1, rgb=0, so ApplyDBuffer is a no-op.

[[vk::binding(0, 2)]] Sampler2D uDBufferA;   // rgb: BaseColor,                 a: transmittance
[[vk::binding(1, 2)]] Sampler2D uDBufferB;   // rg:  WorldNormal (OctEncode01),  a: transmittance
[[vk::binding(2, 2)]] Sampler2D uDBufferC;   // r:Roughness g:Metallic b:AO,     a: transmittance

// Blend the accumulated decal layer at ScreenUV into the surface material attributes, in place.
// Call after the material is resolved and BEFORE lighting so decals receive lights/shadows/IBL.
void ApplyDBuffer(inout float3 Albedo, inout float3 WorldNormal, inout float Roughness,
                  inout float Metallic, inout float AO, float2 ScreenUV)
{
    float4 A = uDBufferA.SampleLevel(ScreenUV, 0.0);
    float4 B = uDBufferB.SampleLevel(ScreenUV, 0.0);
    float4 C = uDBufferC.SampleLevel(ScreenUV, 0.0);

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
