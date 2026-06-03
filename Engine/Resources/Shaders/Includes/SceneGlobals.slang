#pragma once
#include "Common.slang"

// ---------------------------------------------------------------------------
//  Shader param model. The only bound descriptor set is the bindless texture
//  table (set 0). Scene data is reached by buffer device address through a single
//  per-frame root (gPC.Root); per-pass constants ride a second transient block at
//  gPC.PassAddr, accessed via `#define PC (*(FMyPassConstants*)gPC.PassAddr)`. The
//  field aliases below let shader bodies read uInstance[i] / uSceneData.X directly.
// ---------------------------------------------------------------------------
// Device addresses of every scene buffer + bindless indices for the IBL/shadow textures. Must match
// FSceneRoot in SceneRenderTypes.h (128 B, scalar layout). Reached through gPC.Root below.
struct FSceneRoot
{
    FSceneGlobalData*    SceneData;
    FLightData*          Lights;
    FGPUInstance*        Instances;
    FBoneTransform*      Bones;
    FCluster*            Clusters;
    FMaterialUniforms*   Materials;
    FBillboardInstance*  Billboards;
    FCullView*           CullViews;
    FMeshletDraw*        MeshletDrawListPtr;
    uint*                InstanceMeshletPrefixPtr;
    FPreSkinnedVertex*   PreSkinnedVerticesPtr;
    FSkinDescriptor*     SkinDescriptorsPtr;
    FWidgetInstance*     WidgetsPtr;
    uint  BRDFLutIndex;          // bindless 2D SRV
    uint  SkyIrradianceIndex;    // bindless cube SRV
    // [31:24] = prefilter cube mip count (see SamplePrefilter), [23:0] = bindless cube SRV index.
    // Packed so the per-pixel mip count rides along without growing FSceneRoot past the 128 B push budget.
    uint  SkyPrefilterIndex;
    uint  ShadowCascadeIndex;    // bindless 2D SRV (cascade atlas)
    uint  ShadowAtlasIndex;      // bindless 2D SRV (spot/point atlas)
    uint  SkyCubeIndex;          // bindless cube SRV (full-res sky; sharp near-mirror reflections)
};

struct FRootConstants
{
    ConstBufferPointer<FSceneRoot> Root;   // per-frame scene root (transient)
    uint64_t                       PassAddr; // per-pass constants block (transient), 0 if none
};
[[vk::push_constant]] FRootConstants gPC;

// The PC macro must deref (not copy by value): a by-value load of a struct with PhysicalStorageBuffer
// pointer fields trips spirv-val. gPC.PassAddr is 0 for passes that take no per-pass constants.

// Field aliases so shader bodies read uInstance[i] / uSceneData.X directly. Slang CSEs the repeated
// Root.get() within a function, so it's one root load per function.
#define uSceneData             (gPC.Root.get().SceneData[0])
#define uLightData             (gPC.Root.get().Lights)
#define uInstance              (gPC.Root.get().Instances)
#define uBoneMatrices          (gPC.Root.get().Bones)
#define uClusters              (gPC.Root.get().Clusters)
#define uMaterialUniforms      (gPC.Root.get().Materials)
#define uBillboards            (gPC.Root.get().Billboards)
#define uCullViews             (gPC.Root.get().CullViews)
#define uMeshletDrawList       (gPC.Root.get().MeshletDrawListPtr)
#define uInstanceMeshletPrefix (gPC.Root.get().InstanceMeshletPrefixPtr)
#define uPreSkinnedVertices    (gPC.Root.get().PreSkinnedVerticesPtr)
#define uSkinDescriptors       (gPC.Root.get().SkinDescriptorsPtr)
#define uWidgets               (gPC.Root.get().WidgetsPtr)


// The bindless table is the ONLY descriptor set scene passes bind, so it lives at set 0.
[[vk::binding(0, 0)]] Texture2D                                 uBindlessTex2D[];
[[vk::binding(0, 0)]] TextureCube                               uBindlessTexCube[];
[[vk::binding(0, 0)]] Texture3D                                 uBindlessTex3D[];
[[vk::binding(0, 0)]] Texture2DArray                            uBindlessTex2DArray[];
[[vk::binding(0, 0)]] RWTexture2D<float4>                       uBindlessRWTex2D[];
[[vk::binding(0, 0)]] RWTexture3D<float4>                       uBindlessRWTex3D[];
// Cube/cube-array storage images register their UAVs as 2D-array views (cube storage views are
// illegal in Vulkan), so cube writes index this array at GetMipUAVIndex(mip) with [int3(x,y,face)].
[[vk::binding(0, 0)]] RWTexture2DArray<float4>                  uBindlessRWTex2DArray[];
// Coherent alias of the same descriptor array for cross-workgroup hand-offs (SPD mip-5 / mip-11).
// SPIR-V Coherent is per-OpVariable, so a second variable at this binding rides coherency per-access.
[[vk::binding(0, 0)]] globallycoherent RWTexture2D<float4>      uBindlessRWTex2DCoherent[];
[[vk::binding(1, 0)]] SamplerState                              uBindlessSamplers[];
// Comparison-sampler alias of the same sampler table (VkSampler carries compareEnable; only
// uBindlessCmpSamplers[SAMPLER_SHADOW] is a comparison sampler, used for shadow SampleCmp).
[[vk::binding(1, 0)]] SamplerComparisonState                   uBindlessCmpSamplers[];

// Stock sampler indices. Keep in lockstep with EBindlessSampler in
// Engine/Source/Runtime/Renderer/TextureManager.h.
static const uint SAMPLER_LINEAR_WRAP        = 0;
static const uint SAMPLER_LINEAR_CLAMP       = 1;
static const uint SAMPLER_LINEAR_MIRROR      = 2;
static const uint SAMPLER_POINT_WRAP         = 3;
static const uint SAMPLER_POINT_CLAMP        = 4;
static const uint SAMPLER_ANISO_WRAP         = 5;
static const uint SAMPLER_ANISO_CLAMP        = 6;
static const uint SAMPLER_SHADOW             = 7;
static const uint SAMPLER_MIN_REDUCTION      = 8;
static const uint SAMPLER_MAX_REDUCTION      = 9;

// Convenience wrappers around the bindless table. ResourceID matches
// FRHIImage::GetResourceID() on the C++ side.
SamplerState GetBindlessSampler(uint SamplerIndex) { return uBindlessSamplers[SamplerIndex]; }

float4 SampleBindless2D(uint ResourceID, uint SamplerIndex, float2 UV)
{
    return uBindlessTex2D[ResourceID].Sample(uBindlessSamplers[SamplerIndex], UV);
}

float4 SampleBindless2DLevel(uint ResourceID, uint SamplerIndex, float2 UV, float Lod)
{
    return uBindlessTex2D[ResourceID].SampleLevel(uBindlessSamplers[SamplerIndex], UV, Lod);
}

float4 SampleBindlessCube(uint ResourceID, uint SamplerIndex, float3 Dir)
{
    return uBindlessTexCube[ResourceID].Sample(uBindlessSamplers[SamplerIndex], Dir);
}

float4 SampleBindlessCubeLevel(uint ResourceID, uint SamplerIndex, float3 Dir, float Lod)
{
    return uBindlessTexCube[ResourceID].SampleLevel(uBindlessSamplers[SamplerIndex], Dir, Lod);
}

float4 SampleBindless3D(uint ResourceID, uint SamplerIndex, float3 UVW)
{
    return uBindlessTex3D[ResourceID].Sample(uBindlessSamplers[SamplerIndex], UVW);
}

// HDR environment panoramas carry +Inf / NaN texels (half-float overflow of very bright suns or LED
// lights). Left unguarded they poison the IBL cubes -- Inf*weight and Inf-Inf produce NaN, which the BRDF
// renders as black/white texel blocks, and a single NaN reaching bloom spreads white across the screen.
// Drop non-finite channels to 0 and clamp into the R11G11B10 finite range so the convolution stays stable.
float SanitizeRadianceChannel(float X)
{
    if (!isfinite(X)) return 0.0;       // x != x catches NaN; |x| == inf caught by isfinite
    return clamp(X, 0.0, 64000.0);
}

float3 SanitizeRadiance(float3 C)
{
    return float3(SanitizeRadianceChannel(C.r),
                  SanitizeRadianceChannel(C.g),
                  SanitizeRadianceChannel(C.b));
}

float Luminance(float3 C)
{
    return dot(C, float3(0.2126, 0.7152, 0.0722));
}

// Karis "tone map then average" firefly suppression for importance-sampled IBL convolution. A lone very
// bright texel (a city light, the sun) hit by only a few GGX samples otherwise produces high-variance
// blocky/sparkly reflections. Weighting by 1/(1+luma) in tonemapped space and un-tonemapping the mean
// preserves energy while killing the variance. Identity for a single sample, so mirror reflections are
// untouched -- only multi-sample (rough) gathers get smoothed.
float3 FireflyTonemap(float3 C)   { return C / (1.0 + Luminance(C)); }
float3 FireflyUntonemap(float3 C) { return C / max(1.0 - Luminance(C), 1e-4); }

// Vertex loaders.

FVertexData LoadStaticVertex(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx)
{
    FVertex V = reinterpret<FVertex*>(Header.Vertices)[M.VertexOffset + VertexLocalIdx];

    FVertexData Data;
    Data.Position = UnpackMeshletPosition(V.Position, M.LoInt, GetMeshOrigin(Header, M.LODIndex), GetMeshGridStep(Header, M.LODIndex));
    Data.Normal   = UnpackNormal(V.Normal);
    Data.Tangent  = UnpackTangent(V.Tangent);
    Data.UV       = UnpackUV(V.UV);
    Data.Color    = UnpackColor(V.Color);
    return Data;
}

// Skinned reads come entirely from the pre-skinned buffer (Skinning.slang did the blend this frame)
// -- a single structured load like the static path, no per-pass skinning.
FVertexData LoadSkinnedVertex(FMeshlet M, uint VertexLocalIdx, uint SkinnedVertexBase)
{
    FPreSkinnedVertex S = uPreSkinnedVertices[SkinnedVertexBase + M.VertexOffset + VertexLocalIdx];

    FVertexData Data;
    Data.Position = float3(S.Px, S.Py, S.Pz);
    Data.Normal   = UnpackNormal(S.Normal);
    Data.Tangent  = UnpackTangent(S.Tangent);
    Data.UV       = UnpackUV(S.UV);
    Data.Color    = UnpackColor(S.Color);
    return Data;
}

float3 LoadStaticPosition(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx)
{
    FVertex V = reinterpret<FVertex*>(Header.Vertices)[M.VertexOffset + VertexLocalIdx];
    return UnpackMeshletPosition(V.Position, M.LoInt, GetMeshOrigin(Header, M.LODIndex), GetMeshGridStep(Header, M.LODIndex));
}

float3 LoadSkinnedPosition(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx, uint SkinnedVertexBase)
{
    FPreSkinnedVertex S = uPreSkinnedVertices[SkinnedVertexBase + M.VertexOffset + VertexLocalIdx];
    return float3(S.Px, S.Py, S.Pz);
}

// Performs the bone blend for one source vertex; used only by Skinning.slang to fill
// the pre-skinned buffer. Output is object-space (the draw VS still applies ModelMatrix).
FPreSkinnedVertex SkinVertex(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx, uint BoneOffset)
{
    FSkinnedVertex V = reinterpret<FSkinnedVertex*>(Header.Vertices)[M.VertexOffset + VertexLocalIdx];

    float3 LocalPos     = UnpackMeshletPosition(V.Position, M.LoInt, GetMeshOrigin(Header, M.LODIndex), GetMeshGridStep(Header, M.LODIndex));
    float3 LocalNormal  = UnpackNormal(V.Normal);
    float4 LocalTangent = UnpackTangent(V.Tangent);

    uint4  JointIndices = UnpackUIntToUVec4(V.JointIndices);
    float4 Weights      = unpack_u8u32(V.JointWeights) / float4(255.0);

    FBoneTransform B0 = uBoneMatrices[BoneOffset + JointIndices.x];
    FBoneTransform B1 = uBoneMatrices[BoneOffset + JointIndices.y];
    FBoneTransform B2 = uBoneMatrices[BoneOffset + JointIndices.z];
    FBoneTransform B3 = uBoneMatrices[BoneOffset + JointIndices.w];

    // Linear blend of the row-stored skinning matrices, then transform by explicit row dots.
    // Rigid per bone, so the same 3x3 rows rotate Tangent and its handedness sign is preserved.
    float4 R0 = B0.Row0 * Weights.x + B1.Row0 * Weights.y + B2.Row0 * Weights.z + B3.Row0 * Weights.w;
    float4 R1 = B0.Row1 * Weights.x + B1.Row1 * Weights.y + B2.Row1 * Weights.z + B3.Row1 * Weights.w;
    float4 R2 = B0.Row2 * Weights.x + B1.Row2 * Weights.y + B2.Row2 * Weights.z + B3.Row2 * Weights.w;

    float4 P = float4(LocalPos, 1.0);
    float3 SkinnedPos     = float3(dot(R0, P), dot(R1, P), dot(R2, P));
    float3 SkinnedNormal  = normalize(float3(dot(R0.xyz, LocalNormal),      dot(R1.xyz, LocalNormal),      dot(R2.xyz, LocalNormal)));
    float3 SkinnedTangent = normalize(float3(dot(R0.xyz, LocalTangent.xyz), dot(R1.xyz, LocalTangent.xyz), dot(R2.xyz, LocalTangent.xyz)));

    FPreSkinnedVertex Out;
    Out.Px      = SkinnedPos.x;
    Out.Py      = SkinnedPos.y;
    Out.Pz      = SkinnedPos.z;
    Out.Normal  = PackNormal(SkinnedNormal);
    Out.Tangent = PackTangent(float4(SkinnedTangent, LocalTangent.w));
    Out.UV      = V.UV;     // skin-invariant; carried so the draw VS never reads the source
    Out.Color   = V.Color;
    return Out;
}

float  GetMaterialScalar (uint MatIndex, uint Index) { return uMaterialUniforms[MatIndex].Scalars[Index];  }
float4 GetMaterialVec4   (uint MatIndex, uint Index) { return uMaterialUniforms[MatIndex].Vectors[Index];  }
uint   GetMaterialTexture(uint MatIndex, uint Index) { return uMaterialUniforms[MatIndex].Textures[Index]; }

FGPUInstance GetInstance(uint InstanceID) { return uInstance[InstanceID]; }

FLight GetLightAt(uint Index)       { return uLightData[0].Lights[Index]; }
uint   GetNumLights()               { return uLightData[0].NumLights;     }
float3 GetSunDirection()            { return uLightData[0].SunDirection;  }
float3 GetAmbientLightColor()       { return uLightData[0].AmbientLight.xyz; }
float  GetAmbientLightIntensity()   { return uLightData[0].AmbientLight.w;   }

// Callers must check Light.ShadowDataIndex != INDEX_NONE first.
float4x4 GetShadowViewProjection(int ShadowDataIndex, uint ViewIndex)
{
    return uLightData[0].Shadows[ShadowDataIndex].ViewProjection[ViewIndex];
}

FLightShadow GetShadowTile(int ShadowDataIndex, uint Face)
{
    return uLightData[0].Shadows[ShadowDataIndex].Shadow[Face];
}

float    GetTime()                  { return uSceneData.Time;       }
float    GetDeltaTime()             { return uSceneData.DeltaTime;  }
float    GetNearPlane()             { return uSceneData.NearPlane;  }
float    GetFarPlane()              { return uSceneData.FarPlane;   }

float3   GetCameraPosition()        { return uSceneData.CameraView.CameraPosition.xyz; }
float3   GetCameraUp()              { return uSceneData.CameraView.CameraUp.xyz;       }
float3   GetCameraRight()           { return uSceneData.CameraView.CameraRight.xyz;    }
float3   GetCameraForward()         { return uSceneData.CameraView.CameraForward.xyz;  }
float4x4 GetCameraView()            { return uSceneData.CameraView.CameraView;         }
float4x4 GetInverseCameraView()     { return uSceneData.CameraView.InverseCameraView;  }
float4x4 GetCameraProjection()      { return uSceneData.CameraView.CameraProjection;   }
float4x4 GetInverseCameraProjection(){ return uSceneData.CameraView.InverseCameraProjection; }

float3 WorldToView(float3 WorldPos)
{
    return mul(GetCameraView(), float4(WorldPos, 1.0)).xyz;
}

float3 NormalWorldToView(float3 Normal)
{
    return mul((float3x3)GetCameraView(), Normal);
}

float4 ViewToClip(float3 ViewPos)
{
    return mul(GetCameraProjection(), float4(ViewPos, 1.0));
}

float4 WorldToClip(float3 WorldPos)
{
    return mul(GetCameraProjection(), mul(GetCameraView(), float4(WorldPos, 1.0)));
}

float SineWave(float Speed, float Amplitude)
{
    return Amplitude * sin(GetTime() * Speed);
}

float3 ReconstructWorldPos(float2 UV, float Depth, float4x4 InvProj, float4x4 InvView)
{
    float4 Clip  = float4(UV * 2.0 - 1.0, Depth, 1.0);
    float4 View  = mul(InvProj, Clip);
    View        /= View.w;
    float4 World = mul(InvView, View);
    return World.xyz;
}

float3 ReconstructViewPos(float2 UV, float Depth, float4x4 InvProj)
{
    float4 Clip = float4(UV * 2.0 - 1.0, Depth, 1.0);
    float4 View = mul(InvProj, Clip);
    return View.xyz / View.w;
}

// Froxel index for a fragment from view-space depth + screen position. Mirrors the tile math in
// ClusterBuild/LightCull; clamps stop edge fragments below ZNear from indexing past uClusters.
uint ComputeClusterIndex(float ViewZ, float2 ScreenPos)
{
    uint3  Grid       = uSceneData.GridSize.xyz;
    uint2  ScreenSize = uSceneData.ScreenSize.xy;
    float  ZNear      = GetNearPlane();
    float  ZFar       = GetFarPlane();

    float  ZForTile = max(abs(ViewZ), ZNear);
    float  ZTileF   = (log(ZForTile / ZNear) * float(Grid.z)) / log(ZFar / ZNear);
    uint   ZTile    = (uint)clamp(ZTileF, 0.0, float(Grid.z) - 1.0);
    float2 TileSize = float2(ScreenSize) / float2(Grid.xy);
    uint2  TileXY   = (uint2)clamp(ScreenPos / TileSize, float2(0.0, 0.0), float2(Grid.xy) - float2(1.0, 1.0));
    return TileXY.x + TileXY.y * Grid.x + ZTile * Grid.x * Grid.y;
}

// Discrete heatmap for the "Light Complexity" view: how many lights a froxel
// has to shade. Bands chosen so the transition between counts reads at a glance.
float3 LightComplexityColor(uint Count)
{
    if (Count == 0u)  return float3(0.0, 0.0, 0.0);
    if (Count <= 2u)  return float3(0.0, 0.25, 0.6);
    if (Count <= 4u)  return float3(0.0, 0.6, 0.3);
    if (Count <= 8u)  return float3(0.7, 0.8, 0.0);
    if (Count <= 16u) return float3(1.0, 0.5, 0.0);
    return float3(1.0, 0.0, 0.0);
}

// Per-froxel hashed hue for the "Light Clusters" view. Empty froxels are dimmed; froxels at the
// LIGHTS_PER_CLUSTER write cap turn white so light-list saturation is obvious.
float3 ClusterDebugColor(uint ClusterIndex, uint LightCount)
{
    if (LightCount >= (uint)LIGHTS_PER_CLUSTER)
    {
        return float3(1.0, 1.0, 1.0);
    }

    uint H = ClusterIndex * 2654435761u;
    H ^= H >> 16; H *= 2246822519u;
    H ^= H >> 13; H *= 3266489917u;
    H ^= H >> 16;

    float3 Hue = float3(
        float( H        & 0xFFu) / 255.0,
        float((H >>  8) & 0xFFu) / 255.0,
        float((H >> 16) & 0xFFu) / 255.0);

    // Dim empty froxels so populated ones pop against the grid.
    return Hue * ((LightCount == 0u) ? 0.25 : 1.0);
}