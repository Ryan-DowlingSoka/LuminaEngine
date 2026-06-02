#include "Includes/DecalCommon.slang"

// Decal box vertex stage. Emits the unit cube transformed by the decal's world matrix. Back faces are
// rasterized (the pipeline culls front faces) so the box still covers the screen when the camera is
// inside it; the pixel shader reconstructs the shaded surface from depth and rejects out-of-box pixels.
//
// The decal index comes from SV_VulkanInstanceID (the draw's FirstInstance), NOT a push constant -- a
// push constant declared in both VS and PS lays out unpredictably across stages and fed the VS garbage.

struct VSOutput
{
    nointerpolation uint DecalIndex : DECAL_INDEX;
    float4 Position : SV_Position;
};

[shader("vertex")]
VSOutput VertexMain(uint VertexID : SV_VulkanVertexID, uint DecalIndex : SV_VulkanInstanceID)
{
    FGPUDecal Decal = uDecals[DecalIndex];

    float3 LocalPos = kDecalCubeVerts[VertexID];
    float4 WorldPos = mul(Decal.DecalToWorld, float4(LocalPos, 1.0));

    VSOutput Output;
    Output.DecalIndex = DecalIndex;
    Output.Position   = WorldToClip(WorldPos.xyz);
    return Output;
}
