#pragma once
#include "Common.slang"

// ---------------------------------------------------------------------------------
//  Binding Set 0
// ---------------------------------------------------------------------------------

[[vk::binding(0, 0)]] ConstantBuffer<FSceneGlobalData>              uSceneData;
[[vk::binding(1, 0)]] StructuredBuffer<FLightData>                  uLightData;
[[vk::binding(2, 0)]] RWStructuredBuffer<FGPUInstance>              uInstance;
[[vk::binding(3, 0)]] StructuredBuffer<float4x4>                    uBoneMatrices;
[[vk::binding(4, 0)]] RWStructuredBuffer<FCluster>                  uClusters;
[[vk::binding(5, 0)]] RWStructuredBuffer<FMaterialUniforms>         uMaterialUniforms;
[[vk::binding(6, 0)]] StructuredBuffer<FBillboardInstance>          uBillboards;

// Cascade and shadow atlas live in the dedicated shadow-sampling set (set=3,
// see ShadowSampling.slang). They are NOT declared in this set so that shadow
// rendering passes (Point/Spot/Cascaded) can bind set 0 without triggering a
// shader-read state transition on images they're about to write as depth.
// Slot 9 (uDepthPyramid) freed: Hi-Z is sampled bindless via
// uSceneData.CullData.DepthPyramidIndex + SAMPLER_MIN_REDUCTION.

[[vk::binding(10, 0)]] StructuredBuffer<FCullView>                   uCullViews;
[[vk::binding(11, 0)]] RWStructuredBuffer<FMeshletDraw>              uMeshletDrawList;
[[vk::binding(12, 0)]] RWStructuredBuffer<FDrawIndirectArguments>    uIndirectArgs;

// Binding 13 (uShadowCascadePoint) moved to ShadowSampling.slang at set 3.

// Phase-0 deferred camera meshlets, retested by phase 1 against the rebuilt HZB.
[[vk::binding(14, 0)]] RWStructuredBuffer<FMeshletDeferred>          uMeshletDeferList;
[[vk::binding(15, 0)]] globallycoherent RWStructuredBuffer<uint>     uDeferCount;

// Inclusive per-instance meshlet prefix sum, sized [InstanceNum + 1].
// Entry [i] is the running total of meshlets across instances [0..i). The
// cull pass binary-searches this to recover (InstanceID, MeshletLocalIdx)
// from a flat thread index, sidestepping the old per-instance over-dispatch
// and the thread-bounds early-outs that wasted lanes on it.
[[vk::binding(16, 0)]] StructuredBuffer<uint>                        uInstanceMeshletPrefix;

// Pre-integrated BRDF LUT (Karis 2013 split-sum). Indexed by (NdotV, Roughness),
// returns (scale, bias) for F0. Sampled by the IBL ambient term in
// BasePixelPass; consumers pass the result through SampleBRDFLUT() in
// IBL.slang for clarity. Baked once at scene init.
[[vk::binding(17, 0)]] Sampler2D                                     uBRDFLut;

// IBL diffuse irradiance cube. Cosine-weighted convolution of the sky cube,
// indexed by surface normal. Stored as Irradiance / pi -- the consumer
// multiplies by Albedo without an extra /pi.
[[vk::binding(18, 0)]] SamplerCube                                   uSkyIrradiance;

// IBL pre-filtered specular cube. Each mip carries a different GGX roughness
// (mip 0 = mirror, last mip = fully rough). Sampled by the reflection vector
// at mip = Roughness * (NumMips - 1) and combined with the BRDF LUT for
// the specular ambient term.
[[vk::binding(19, 0)]] SamplerCube                                   uSkyPrefilter;


[[vk::binding(0, 1)]] Texture2D                                 uBindlessTex2D[];
[[vk::binding(0, 1)]] TextureCube                               uBindlessTexCube[];
[[vk::binding(0, 1)]] Texture3D                                 uBindlessTex3D[];
[[vk::binding(0, 1)]] Texture2DArray                            uBindlessTex2DArray[];
[[vk::binding(0, 1)]] RWTexture2D<float4>                       uBindlessRWTex2D[];
// Same descriptor array, coherent alias. Use this for cross-workgroup hand-offs
// (SPD mip-5 / mip-11). The SPIR-V Coherent decoration is per-OpVariable, so
// declaring a second variable at the same binding lets per-access coherency
// ride on the indexing expression.
[[vk::binding(0, 1)]] globallycoherent RWTexture2D<float4>      uBindlessRWTex2DCoherent[];
[[vk::binding(1, 1)]] SamplerState                              uBindlessSamplers[];

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

// Vertex loaders.

FVertexData LoadStaticVertex(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx)
{
    FVertex V = reinterpret<FVertex*>(Header.Vertices)[M.VertexOffset + VertexLocalIdx];

    FVertexData Data;
    Data.Position = UnpackMeshletPosition(V.Position, M.LoInt, Header.MeshOrigin.xyz, Header.MeshGridStep.xyz);
    Data.Normal   = UnpackNormal(V.Normal);
    Data.Tangent  = UnpackTangent(V.Tangent);
    Data.UV       = UnpackUV(V.UV);
    Data.Color    = UnpackColor(V.Color);
    return Data;
}

FVertexData LoadSkinnedVertex(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx, uint BoneOffset)
{
    FSkinnedVertex V = reinterpret<FSkinnedVertex*>(Header.Vertices)[M.VertexOffset + VertexLocalIdx];

    float3 LocalPos     = UnpackMeshletPosition(V.Position, M.LoInt, Header.MeshOrigin.xyz, Header.MeshGridStep.xyz);
    float3 LocalNormal  = UnpackNormal(V.Normal);
    float4 LocalTangent = UnpackTangent(V.Tangent);

    uint4  JointIndices = UnpackUIntToUVec4(V.JointIndices);
    float4 Weights      = unpack_u8u32(V.JointWeights) / float4(255.0);

    float4x4 SkinMatrix =
        uBoneMatrices[BoneOffset + JointIndices.x] * Weights.x +
        uBoneMatrices[BoneOffset + JointIndices.y] * Weights.y +
        uBoneMatrices[BoneOffset + JointIndices.z] * Weights.z +
        uBoneMatrices[BoneOffset + JointIndices.w] * Weights.w;

    // Skinning is rigid (rotation + translation) per bone, so the same 3x3
    // submatrix that takes Normal to skinned space takes Tangent there too.
    // The handedness sign rides through unchanged -- it describes the UV
    // parameterization, not the geometry.
    float3x3 SkinRot = (float3x3)SkinMatrix;

    FVertexData Data;
    Data.Position = mul(SkinMatrix, float4(LocalPos, 1.0)).xyz;
    Data.Normal   = normalize(mul(SkinRot, LocalNormal));
    Data.Tangent  = float4(normalize(mul(SkinRot, LocalTangent.xyz)), LocalTangent.w);
    Data.UV       = UnpackUV(V.UV);
    Data.Color    = UnpackColor(V.Color);
    return Data;
}

float3 LoadStaticPosition(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx)
{
    FVertex V = reinterpret<FVertex*>(Header.Vertices)[M.VertexOffset + VertexLocalIdx];
    return UnpackMeshletPosition(V.Position, M.LoInt, Header.MeshOrigin.xyz, Header.MeshGridStep.xyz);
}

float3 LoadSkinnedPosition(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx, uint BoneOffset)
{
    FSkinnedVertex V = reinterpret<FSkinnedVertex*>(Header.Vertices)[M.VertexOffset + VertexLocalIdx];

    float3 LocalPos = UnpackMeshletPosition(V.Position, M.LoInt, Header.MeshOrigin.xyz, Header.MeshGridStep.xyz);

    uint4  JointIndices = UnpackUIntToUVec4(V.JointIndices);
    float4 Weights      = unpack_u8u32(V.JointWeights) / float4(255.0);

    float4x4 SkinMatrix =
        uBoneMatrices[BoneOffset + JointIndices.x] * Weights.x +
        uBoneMatrices[BoneOffset + JointIndices.y] * Weights.y +
        uBoneMatrices[BoneOffset + JointIndices.z] * Weights.z +
        uBoneMatrices[BoneOffset + JointIndices.w] * Weights.w;

    return mul(SkinMatrix, float4(LocalPos, 1.0)).xyz;
}

// ---------------------------------------------------------------------------------
//  Material Accessors
// ---------------------------------------------------------------------------------

float  GetMaterialScalar (uint MatIndex, uint Index) { return uMaterialUniforms[MatIndex].Scalars[Index];  }
float4 GetMaterialVec4   (uint MatIndex, uint Index) { return uMaterialUniforms[MatIndex].Vectors[Index];  }
uint   GetMaterialTexture(uint MatIndex, uint Index) { return uMaterialUniforms[MatIndex].Textures[Index]; }

// ---------------------------------------------------------------------------------
//  Instance Accessors
// ---------------------------------------------------------------------------------

FGPUInstance GetInstance(uint InstanceID) { return uInstance[InstanceID]; }

// ---------------------------------------------------------------------------------
//  Light Accessors
// ---------------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------------
//  Scene / Camera Accessors
// ---------------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------------
//  Transform Helpers
// ---------------------------------------------------------------------------------

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