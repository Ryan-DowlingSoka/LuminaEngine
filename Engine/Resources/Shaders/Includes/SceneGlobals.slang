#pragma once
#include "Common.slang"

// ---------------------------------------------------------------------------------
//  Binding Set 0
// ---------------------------------------------------------------------------------

[[vk::binding(0, 0)]] ConstantBuffer<FSceneGlobalData>              uSceneData;
[[vk::binding(1, 0)]] StructuredBuffer<FLightData>                  uLightData;
// Unified 128B per-instance descriptor. Mirror of Lumina::FGPUInstance.
[[vk::binding(2, 0)]] RWStructuredBuffer<FGPUInstance>              uInstance;
[[vk::binding(3, 0)]] StructuredBuffer<float4x4>                    uBoneMatrices;
[[vk::binding(4, 0)]] RWStructuredBuffer<FCluster>                  uClusters;
[[vk::binding(5, 0)]] RWStructuredBuffer<FMaterialUniforms>         uMaterialUniforms;
[[vk::binding(6, 0)]] StructuredBuffer<FBillboardInstance>          uBillboards;

[[vk::binding(7,  0)]] Sampler2DArray                               uShadowCascade;
[[vk::binding(8, 0)]] Sampler2DArray                                uShadowAtlas;
[[vk::binding(9, 0)]] Sampler2D                                     uDepthPyramid;

// Unified per-view culling. CPU builds one FCullView per logical render view
// (main camera, each CSM cascade, each point face, each spot light) and
// packs them into uCullViews. CullMeshlets.slang tests each meshlet against
// every view and emits into per-view slices of uMeshletDrawList /
// uIndirectArgs, addressed through FCullView.DrawListOffset and
// IndirectArgsOffset so each draw pass sees only its own view.
[[vk::binding(10, 0)]] StructuredBuffer<FCullView>                   uCullViews;
[[vk::binding(11, 0)]] RWStructuredBuffer<FMeshletDraw>              uMeshletDrawList;
[[vk::binding(12, 0)]] RWStructuredBuffer<FDrawIndirectArguments>    uIndirectArgs;

// Same cascade image as binding 7 but with a point, non-comparison sampler
// so PCSS blocker search can read raw depth instead of PCF results.
[[vk::binding(13, 0)]] Sampler2DArray                                uShadowCascadePoint;

// Two-pass occlusion culling defer list. Phase 0 populates it with camera
// meshlets that failed the previous-frame HZB; phase 1 pops them and tests
// against the rebuilt current-frame HZB before emitting into the
// camera-late view slice of uMeshletDrawList / uIndirectArgs.
[[vk::binding(14, 0)]] RWStructuredBuffer<FMeshletDeferred>          uMeshletDeferList;
[[vk::binding(15, 0)]] globallycoherent RWStructuredBuffer<uint>     uDeferCount;

// ---------------------------------------------------------------------------------
//  Binding Set 1
// ---------------------------------------------------------------------------------

[[vk::binding(0, 1)]] Sampler2D uGlobalTextures[];

// ---------------------------------------------------------------------------------
//  Vertex Loading. Each meshlet owns a packed vertex slice in
//  MeshletHeader.Vertices; loaders dequantize position via the mesh-global
//  AABB carried on the header.
// ---------------------------------------------------------------------------------

FVertexData LoadStaticVertex(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx)
{
    FVertex V = reinterpret<FVertex*>(Header.Vertices)[M.VertexOffset + VertexLocalIdx];

    FVertexData Data;
    Data.Position = UnpackMeshletPosition(V.PositionXY, V.PositionZ, Header.MeshAABBMin.xyz, Header.MeshAABBScale.xyz);
    Data.Normal   = UnpackNormal(V.Normal);
    Data.UV       = UnpackUV(V.UV);
    Data.Color    = UnpackColor(V.Color);
    return Data;
}

FVertexData LoadSkinnedVertex(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx, uint BoneOffset)
{
    FSkinnedVertex V = reinterpret<FSkinnedVertex*>(Header.Vertices)[M.VertexOffset + VertexLocalIdx];

    float3 LocalPos    = UnpackMeshletPosition(V.PositionXY, V.PositionZ, Header.MeshAABBMin.xyz, Header.MeshAABBScale.xyz);
    float3 LocalNormal = UnpackNormal(V.Normal);

    uint4  JointIndices = UnpackUIntToUVec4(V.JointIndices);
    float4 Weights      = unpack_u8u32(V.JointWeights) / float4(255.0);

    float4x4 SkinMatrix =
        uBoneMatrices[BoneOffset + JointIndices.x] * Weights.x +
        uBoneMatrices[BoneOffset + JointIndices.y] * Weights.y +
        uBoneMatrices[BoneOffset + JointIndices.z] * Weights.z +
        uBoneMatrices[BoneOffset + JointIndices.w] * Weights.w;

    FVertexData Data;
    Data.Position = mul(SkinMatrix, float4(LocalPos, 1.0)).xyz;
    Data.Normal   = normalize(mul((float3x3)SkinMatrix, LocalNormal));
    Data.UV       = UnpackUV(V.UV);
    Data.Color    = UnpackColor(V.Color);
    return Data;
}

// Position-only variants for depth and shadow passes.
float3 LoadStaticPosition(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx)
{
    FVertex V = reinterpret<FVertex*>(Header.Vertices)[M.VertexOffset + VertexLocalIdx];
    return UnpackMeshletPosition(V.PositionXY, V.PositionZ, Header.MeshAABBMin.xyz, Header.MeshAABBScale.xyz);
}

float3 LoadSkinnedPosition(FMeshletHeader* Header, FMeshlet M, uint VertexLocalIdx, uint BoneOffset)
{
    FSkinnedVertex V = reinterpret<FSkinnedVertex*>(Header.Vertices)[M.VertexOffset + VertexLocalIdx];

    float3 LocalPos = UnpackMeshletPosition(V.PositionXY, V.PositionZ, Header.MeshAABBMin.xyz, Header.MeshAABBScale.xyz);

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

// Cold shadow-data accessors. Callers must check Light.ShadowDataIndex != INDEX_NONE
// before calling. Keeping the fetch behind a helper makes it easy to later move
// the shadow table to a dedicated binding without touching callsites.
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