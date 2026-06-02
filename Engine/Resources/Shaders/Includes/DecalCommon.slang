#pragma once
#include "SceneGlobals.slang"

// Shared bindings/data for the DecalPass (vertex + pixel). Decals are drawn as unit cubes; the pixel
// shader reconstructs world position from scene depth, projects into decal-local space, and writes the
// material attributes into the DBuffer. Set 0 (scene) gives camera + material uniforms + bindless
// textures; set 2 (below) is the decal-pass-private depth + per-decal data. One draw per decal, the
// push constant selects its record.

struct FGPUDecal
{
    float4x4 WorldToDecal;   // world -> decal-local; inside the box iff all(abs(local) <= 0.5)
    float4x4 DecalToWorld;   // decal-local cube -> world (entity transform, scale = box extents)
    float    FadeAngleCos;   // cos(max angle) between surface normal and decal forward; below => fade
    float    Opacity;        // master coverage multiplier
    uint     MaterialIndex;  // index into uMaterialUniforms
    uint     Flags;          // reserved
};

[[vk::binding(0, 2)]] Sampler2D                        uDecalSceneDepth;   // opaque depth, raw reverse-Z NDC [0,1]
[[vk::binding(1, 2)]] StructuredBuffer<FGPUDecal>      uDecals;

// Unit cube corner table (12 triangles / 36 verts), local space [-0.5, 0.5]. Indexed by SV_VertexID.
static const float3 kDecalCubeVerts[36] = {
    // -X
    float3(-0.5,-0.5,-0.5), float3(-0.5,-0.5, 0.5), float3(-0.5, 0.5, 0.5),
    float3(-0.5,-0.5,-0.5), float3(-0.5, 0.5, 0.5), float3(-0.5, 0.5,-0.5),
    // +X
    float3( 0.5,-0.5,-0.5), float3( 0.5, 0.5, 0.5), float3( 0.5,-0.5, 0.5),
    float3( 0.5,-0.5,-0.5), float3( 0.5, 0.5,-0.5), float3( 0.5, 0.5, 0.5),
    // -Y
    float3(-0.5,-0.5,-0.5), float3( 0.5,-0.5,-0.5), float3( 0.5,-0.5, 0.5),
    float3(-0.5,-0.5,-0.5), float3( 0.5,-0.5, 0.5), float3(-0.5,-0.5, 0.5),
    // +Y
    float3(-0.5, 0.5,-0.5), float3( 0.5, 0.5, 0.5), float3( 0.5, 0.5,-0.5),
    float3(-0.5, 0.5,-0.5), float3(-0.5, 0.5, 0.5), float3( 0.5, 0.5, 0.5),
    // -Z
    float3(-0.5,-0.5,-0.5), float3(-0.5, 0.5,-0.5), float3( 0.5, 0.5,-0.5),
    float3(-0.5,-0.5,-0.5), float3( 0.5, 0.5,-0.5), float3( 0.5,-0.5,-0.5),
    // +Z
    float3(-0.5,-0.5, 0.5), float3( 0.5, 0.5, 0.5), float3(-0.5, 0.5, 0.5),
    float3(-0.5,-0.5, 0.5), float3( 0.5,-0.5, 0.5), float3( 0.5, 0.5, 0.5),
};

// Raw reverse-Z NDC depth [0,1] -> view-space linear distance (matches PostProcessPixelPass).
float DecalLinearizeDepth(float RawDepth)
{
    float ZNear = GetNearPlane();
    float ZFar  = GetFarPlane();
    return ZNear * ZFar / max(ZFar - RawDepth * (ZFar - ZNear), 1e-6);
}
