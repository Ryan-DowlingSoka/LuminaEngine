#pragma once
#include "Common.slang"


struct SkinnedVertexBuffer
{
    FSkinnedVertex* Vertices;
};

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

// Parallel shadow-caster indirect/mapping buffers for point + spot lights.
// Directional shadows are driven by the meshlet cascade buffers below.
[[vk::binding(10, 0)]] RWStructuredBuffer<uint>                      uMappingDataShadow;
[[vk::binding(11, 0)]] RWStructuredBuffer<FDrawIndirectArguments>    uIndirectDrawDataShadow;

// Meshlet-driven camera-pass rendering. MeshletCull.slang atomically appends
// surviving meshlets to uMeshletDrawList and increments InstanceCount in
// uIndirectDrawDataMeshlet. The base vertex shader uses SV_InstanceID to look
// up (InstanceID, MeshletLocalIndex) and pull vertices through the meshlet
// header rather than the flat index buffer.
[[vk::binding(12, 0)]] RWStructuredBuffer<FMeshletDraw>              uMeshletDrawList;
[[vk::binding(13, 0)]] RWStructuredBuffer<FDrawIndirectArguments>    uIndirectDrawDataMeshlet;

// Per-cascade meshlet draw list + indirect for the CSM pass.
[[vk::binding(14, 0)]] RWStructuredBuffer<FMeshletDraw>              uMeshletDrawListCascade;
[[vk::binding(15, 0)]] RWStructuredBuffer<FDrawIndirectArguments>    uIndirectDrawDataMeshletCascade;

// ---------------------------------------------------------------------------------
//  Binding Set 1
// ---------------------------------------------------------------------------------

[[vk::binding(0, 1)]] Sampler2D uGlobalTextures[];

// ---------------------------------------------------------------------------------
//  Vertex Loading
// ---------------------------------------------------------------------------------

FVertexData LoadStaticVertex(uint* VBO, uint* IBO, uint VertexID)
{
    uint Index = IBO[VertexID];
    FVertex V = reinterpret<FVertex*>(VBO)[Index];

    FVertexData Data;
    Data.Position = V.Position;
    Data.Normal   = UnpackNormal(V.Normal);
    Data.UV       = UnpackUV(V.UV);
    Data.Color    = UnpackColor(V.Color);
    return Data;
}

FVertexData LoadSkinnedVertex(uint* VBO, uint* IBO, uint VertexID, uint BoneOffset)
{
    uint Index = IBO[VertexID];
    FSkinnedVertex V = reinterpret<FSkinnedVertex*>(VBO)[Index];

    float3 LocalPos    = V.Position;
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

// Position-only fetch. DepthPrePass / ShadowMappingVert only consume
// VertexData.Position, so avoid loading Normal/UV/Color from the vertex record
// and skip the unpack calls entirely. Reading just the float3 keeps the VS off
// the Unpack* ALU path that shows up hot in profiling.
float3 LoadStaticPositionByIndex(uint* VBO, uint Index)
{
    return reinterpret<FVertex*>(VBO)[Index].Position;
}

float3 LoadSkinnedPositionByIndex(uint* VBO, uint Index, uint BoneOffset)
{
    FSkinnedVertex V = reinterpret<FSkinnedVertex*>(VBO)[Index];

    uint4  JointIndices = UnpackUIntToUVec4(V.JointIndices);
    float4 Weights      = unpack_u8u32(V.JointWeights) / float4(255.0);

    float4x4 SkinMatrix =
        uBoneMatrices[BoneOffset + JointIndices.x] * Weights.x +
        uBoneMatrices[BoneOffset + JointIndices.y] * Weights.y +
        uBoneMatrices[BoneOffset + JointIndices.z] * Weights.z +
        uBoneMatrices[BoneOffset + JointIndices.w] * Weights.w;

    return mul(SkinMatrix, float4(V.Position, 1.0)).xyz;
}

// IBO-driven position-only variants for the spot/point shadow path.
float3 LoadStaticPosition(uint* VBO, uint* IBO, uint VertexID)
{
    return LoadStaticPositionByIndex(VBO, IBO[VertexID]);
}

float3 LoadSkinnedPosition(uint* VBO, uint* IBO, uint VertexID, uint BoneOffset)
{
    return LoadSkinnedPositionByIndex(VBO, IBO[VertexID], BoneOffset);
}

// Meshlet-driven vertex fetch. The meshlet path has already resolved the final
// vertex index (via the meshlet header's Vertices array), so these helpers skip
// the IBO indirection and pull the vertex record directly.
FVertexData LoadStaticVertexByIndex(uint* VBO, uint Index)
{
    FVertex V = reinterpret<FVertex*>(VBO)[Index];

    FVertexData Data;
    Data.Position = V.Position;
    Data.Normal   = UnpackNormal(V.Normal);
    Data.UV       = UnpackUV(V.UV);
    Data.Color    = UnpackColor(V.Color);
    return Data;
}

FVertexData LoadSkinnedVertexByIndex(uint* VBO, uint Index, uint BoneOffset)
{
    FSkinnedVertex V = reinterpret<FSkinnedVertex*>(VBO)[Index];

    float3 LocalPos    = V.Position;
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

// Shadow-pass accessor. Shadow draws are indexed from a dedicated mapping
// buffer populated by ShadowMeshCull.slang.
uint ShadowDrawIDToInstanceID(uint ID)
{
    return uMappingDataShadow[ID];
}

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