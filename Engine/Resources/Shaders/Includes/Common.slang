#pragma once

// ---------------------------------------------------------------------------------
//  Constants

static const float PHI    = 1.618033988749895;
static const float PI     = 3.1415926535897932384626433832795;
static const float TWO_PI = 6.28318530718;
static const float INV_PI = 0.31830988618;

static const int SSAO_KERNEL_SIZE = 32;

#define LIGHT_INDEX_MASK 0x1FFFu
#define LIGHTS_PER_UINT  2
#define LIGHTS_PER_CLUSTER 100
#define SHADOW_SAMPLE_COUNT 8

#define INDEX_NONE -1

#define COL_R_SHIFT 0
#define COL_G_SHIFT 8
#define COL_B_SHIFT 16
#define COL_A_SHIFT 24
#define COL_A_MASK  0xFF000000

#define MAX_LIGHTS   1728
#define MAX_SHADOWS  100
#define NUM_CASCADES 3

#define MAX_SCALARS  24
#define MAX_VECTORS  24
#define MAX_TEXTURES 24

#define BIT(X) (1u << (X))

static const uint LIGHT_FLAG_TYPE_DIRECTIONAL = BIT(0);
static const uint LIGHT_FLAG_TYPE_POINT       = BIT(1);
static const uint LIGHT_FLAG_TYPE_SPOT        = BIT(2);
static const uint LIGHT_FLAG_CASTSHADOW       = BIT(3);

enum ELightFlags
{
    None            = 0,
    Directional     = BIT(0),
    Point           = BIT(1),
    Spot            = BIT(2),
    
    Shadow          = BIT(3),
}

enum EInstanceFlags
{
    None            = 0,
    Billboard       = BIT(0),
    Skinned         = BIT(1),
    CastShadow      = BIT(2),
    ReceiveShadow   = BIT(3),
    Occluder        = BIT(4),
    NoCull          = BIT(5),
    Translucent     = BIT(6),
    Masked          = BIT(7),
}

// ---------------------------------------------------------------------------------
//  Structs

struct FVertex
{
    float3  Position;
    uint    Normal;
    uint    UV;
    uint    Color;
    uint2   Padding;
};

struct FSkinnedVertex
{
    float3 Position;
    uint   Normal;
    uint   UV;
    uint   Color;
    uint   JointIndices;
    uint   JointWeights;

    uint2   Padding;
};

struct FVertexData
{
    float3 Position;
    float3 Normal;
    float2 UV;
    float4 Color;
};

struct FDrawIndirectArguments
{
    uint VertexCount;
    uint InstanceCount;
    uint VertexOffset;
    uint FirstInstance;
};

struct FDrawIndexedIndirectArguments
{
    uint IndexCount;
    uint InstanceCount;
    uint FirstIndex;
    int  VertexOffset;
    uint FirstInstance;
};

struct FCameraView
{
    float4 CameraPosition;
    float4 CameraUp;
    float4 CameraRight;
    float4 CameraForward;
    float4x4 CameraView;
    float4x4 InverseCameraView;
    float4x4 CameraProjection;
    float4x4 InverseCameraProjection;
};

struct FMaterialPixelInputs
{
    float3 Diffuse;
    float  Metallic;
    float  Roughness;
    float  Specular;
    float3 Emissive;
    float  AmbientOcclusion;
    float3 Normal;
    float  Opacity;
};

struct FMaterialVertexInputs
{
    float3 WorldPositionOffset;
};

struct FEntityAABB
{
    uint  Entity;
    float MinX;
    float MinY;
    float MaxX;
    float MaxY;
};

struct FSSAOSettings
{
    float  Radius;
    float  Intensity;
    float  Power;
    float4 Samples[SSAO_KERNEL_SIZE];
};

static const uint MatFlag_None        = 0;
static const uint MatFlag_Masked      = BIT(0);
static const uint MatFlag_Translucent = BIT(1);
static const uint MatFlag_Additive    = BIT(2);
static const uint MatFlag_Unlit       = BIT(3);

struct FMaterialUniforms
{
    float4  Vectors[MAX_VECTORS];
    float   Scalars[MAX_SCALARS];
    uint    Textures[MAX_TEXTURES];
    uint    Flags;
    float   OpacityClipValue;
    uint    Padding[2];
};

struct FCustomPrimitiveData
{
    uint Packed;

    property float  AsFloat     { get { return asfloat(Packed); } }
    property int    AsInt       { get { return asint(Packed); } }
    property uint   AsUInt      { get { return Packed; } }
    property bool   AsBool      { get { return Packed != 0; } }
    property float4 AsFloat4    { get { return float4(unpack_u8u32(Packed)) / 255.0; } }
}

// Meshlet limits must match Lumina::MESHLET_MAX_VERTICES / MAX_TRIANGLES.
#define MESHLET_MAX_VERTICES  64
#define MESHLET_MAX_TRIANGLES 124
#define MESHLET_VERTICES_PER_DRAW (MESHLET_MAX_TRIANGLES * 3)  // 372

// Mirror of Lumina::FMeshlet.
struct FMeshlet
{
    uint VertexOffset;
    uint TriangleOffset;
    uint VertexCount;
    uint TriangleCount;
};

// Mirror of Lumina::FMeshletBounds. 16-byte aligned to match std430.
struct FMeshletBounds
{
    float3 Center;
    float  Radius;
    float3 ConeApex;
    float  ConeCutoff;
    float3 ConeAxis;
    float  _Pad0;
};

// Mirror of Lumina::FMeshletHeaderGPU. One entry per mesh; FGPUInstance carries
// a pointer to it so the cull pass and base VS can reach the four flat arrays
// through a single indirection.
struct FMeshletHeader
{
    FMeshlet*       Meshlets;
    FMeshletBounds* Bounds;
    uint*           Vertices;    // Indices into FMeshResource::Vertices
    uint*           Triangles;   // Packed 4x uint8 per uint32
};

// One entry per surviving meshlet emitted by MeshletCull.slang. The vertex
// shader uses SV_InstanceID to index into the MeshletDrawList slice owned by
// its draw (per-draw StartInstanceLocation + atomic-append counter).
struct FMeshletDraw
{
    uint InstanceID;         // Into uInstanceData
    uint MeshletLocalIndex;  // Local offset inside instance's surface meshlet range
};

struct FGPUInstance
{
    float4x4                ModelMatrix;
    float4                  SphereBounds;

    uint*                   VBAddress;
    uint*                   IBAddress;

    // Position-only shadow index stream. Shadow vertex pulling pulls through
    // this pointer so each shadow draw issues fewer unique VS invocations.
    uint*                   ShadowIBAddress;
    uint                    EntityID;
    private uint            DrawIDAndFlags;

    private uint            BoneOffsetAndMaterialIndex;
    FCustomPrimitiveData    CustomData;

    // Meshlet header pointer (zero when this instance skips meshlet culling,
    // e.g. skinned meshes). SurfaceMeshletOffset/Count index the surface's
    // meshlet slice inside the mesh's meshlet arrays.
    FMeshletHeader*         MeshletHeader;
    uint                    SurfaceMeshletOffset;
    uint                    SurfaceMeshletCount;

    bool HasFlag(EInstanceFlags Flag)
    {
        return HasAllFlags((uint)Flags, (uint)Flag);
    }

    property uint DrawID
    {
        get { return DrawIDAndFlags & 0x00FFFFFFu; }
        set { DrawIDAndFlags = (DrawIDAndFlags & 0xFF000000u) | (newValue & 0x00FFFFFFu); }
    }

    property EInstanceFlags Flags
    {
        get { return EInstanceFlags((DrawIDAndFlags >> 24) & 0xFFu); }
        set { DrawIDAndFlags = (DrawIDAndFlags & 0x00FFFFFFu) | ((newValue & 0xFFu) << 24); }
    }

    property uint BoneOffset
    {
        get { return BoneOffsetAndMaterialIndex & 0x0000FFFFu; }
        set { BoneOffsetAndMaterialIndex = (BoneOffsetAndMaterialIndex & 0xFFFF0000u) | (newValue & 0x0000FFFFu); }
    }

    property uint MaterialIndex
    {
        get { return (BoneOffsetAndMaterialIndex >> 16) & 0x0000FFFFu; }
        set { BoneOffsetAndMaterialIndex = (BoneOffsetAndMaterialIndex & 0x0000FFFFu) | ((newValue & 0x0000FFFFu) << 16); }
    }

    property float3 BoundsCenter { get { return SphereBounds.xyz; } }
    property float BoundsRadius { get { return SphereBounds.w; } }
};

struct FLightShadow
{
    float2 AtlasUVOffset;
    float2 AtlasUVScale;
    int    ShadowMapIndex;
    int    ShadowMapLayer;
    int    LightIndex;
    int    ShadowDataIndex;
};

struct FBillboardInstance
{
    float3          Position;
    float           Size;

    private uint    ColorPack;
    uint            TextureIndex;
    uint            EntityID;

    property float3 ColorTint { get { return UnpackColor(ColorPack).rgb; }}
};

// Hot per-light data. Sized to a single 64B cache line so the lighting loop
// can stream lights with one L2 miss per light instead of reading through the
// old 640B struct (which wasted ~90% of each fetched cache line on data only
// used by the shadow passes). ShadowDataIndex points into the cold
// FLightShadowData[] table, or INDEX_NONE if this light has no shadow.
struct FLight
{
    float3 Position;
    uint   Color;

    float3 Direction;
    float  Radius;

    float  Intensity;
    float  Falloff;
    float2 Angles;

    uint   Flags;
    int    ShadowDataIndex;
    uint   Padding[2];
};

// Cold shadow data. Only fetched by the shadow rendering passes and by the
// lit pixel shader *after* we know a light actually has a shadow
// (`ShadowDataIndex != INDEX_NONE`), so it stays out of the hot loop's cache
// footprint. One entry per shadow-casting light, capped at MAX_SHADOWS.
struct FLightShadowData
{
    float4x4     ViewProjection[6];
    FLightShadow Shadow[6];
};

struct FLightData
{
    uint   NumLights;
    uint   Padding[3];
    float3 SunDirection;
    uint   bHasSun;
    float4 CascadeSplits;
    float4 AmbientLight;
    FLight Lights[MAX_LIGHTS];
    FLightShadowData Shadows[MAX_SHADOWS];
};

struct FCluster
{
    float4 MinPoint;
    float4 MaxPoint;
    uint   LightIndices[LIGHTS_PER_CLUSTER];
    uint   Count;
};

struct FFrustum
{
    float4 Planes[6];
};

struct FCullData
{
    FFrustum Frustum;
    FFrustum ShadowFrustum;
    FFrustum CascadeFrustum[NUM_CASCADES];
    float4x4 View;
    float    P00;
    float    P11;
    float    zNear;
    float    zFar;
    uint     bFrustumCull;
    uint     bOcclusionCull;
    uint     InstanceNum;
    uint     bHasDirectional;
    float    PyramidWidth;
    float    PyramidHeight;
    float    ShadowMaxDistance;
    uint     bShadowOcclusionCull;
    uint     NumDraws;
    // Debug visualization mode. Values mirror Lumina::ERenderSceneDebugFlags —
    // keep DEBUG_MODE_* constants below in sync with the C++ enum.
    uint     DebugMode;
};

// Debug visualization modes. Must match Lumina::ERenderSceneDebugFlags.
#define DEBUG_MODE_NONE     0u
#define DEBUG_MODE_MESHLETS 1u

struct FSceneGlobalData
{
    FCameraView   CameraView;
    uint4         ScreenSize;
    uint4         GridSize;
    float         Time;
    float         DeltaTime;
    float         NearPlane;
    float         FarPlane;
    FSSAOSettings SSAOSettings;
    FCullData     CullData;
};

// ---------------------------------------------------------------------------------
//  Functions

float3 UnpackNormal(uint Packed)
{
    float3 Normal;
    Normal.x = float(int(Packed << 22) >> 22) / 511.0;
    Normal.y = float(int(Packed << 12) >> 22) / 511.0;
    Normal.z = float(int(Packed << 2)  >> 22) / 511.0;
    return normalize(Normal);
}

float2 UnpackUV(uint Packed)
{
    uint Lo = Packed & 0xFFFFu;
    uint Hi = Packed >> 16u;
    return float2(f16tof32(Lo), f16tof32(Hi));
}

float3x3 CalculateNormalMatrix(float4x4 ModelMatrix)
{
    float3 C0 = ModelMatrix[0].xyz;
    float3 C1 = ModelMatrix[1].xyz;
    float3 C2 = ModelMatrix[2].xyz;

    float3 R0 = cross(C1, C2);
    float3 R1 = cross(C2, C0);
    float3 R2 = cross(C0, C1);

    float InvDet = 1.0 / dot(C0, R0);

    return float3x3(R0 * InvDet, R1 * InvDet, R2 * InvDet);
}

float2 VogelDiskSample(int SampleIndex, int SamplesCount, float Angle)
{
    float GoldenAngle = 2.0 * PI * (1.0 - 1.0 / PHI);
    float r           = sqrt((float(SampleIndex) + 0.5) / float(SamplesCount));
    float theta       = float(SampleIndex) * GoldenAngle + Angle;
    return float2(r * cos(theta), r * sin(theta));
}

float3 ScreenSpaceDither(float2 ScreenPos, float Time)
{
    float3 Dither = float3(dot(float2(171.0, 231.0), ScreenPos.xy + Time));
    Dither.rgb = frac(Dither.rgb / float3(103.0, 71.0, 97.0)) - float3(0.5, 0.5, 0.5);
    return (Dither.rgb / 255.0) * 0.375;
}

uint4 UnpackUIntToUVec4(uint Packed)
{
    return uint4(
        (Packed      ) & 0xFFu,
        (Packed >>  8) & 0xFFu,
        (Packed >> 16) & 0xFFu,
        (Packed >> 24) & 0xFFu
    );
}

float4 UnpackColor(uint ColorMask)
{
    return float4(
        float((ColorMask >> COL_R_SHIFT) & 0xFFu) / 255.0,
        float((ColorMask >> COL_G_SHIFT) & 0xFFu) / 255.0,
        float((ColorMask >> COL_B_SHIFT) & 0xFFu) / 255.0,
        float((ColorMask >> COL_A_SHIFT) & 0xFFu) / 255.0
    );
}

float4 GetLightColor(FLight Light)
{
    return UnpackColor(Light.Color);
}

bool IsApproximatelyEqual(float a, float b, float EPSILON)
{
    return abs(a - b) <= (abs(a) < abs(b) ? abs(b) : abs(a)) * EPSILON;
}

float max3(float3 V)
{
    return max(max(V.x, V.y), V.z);
}

bool HasFlag(uint Value, uint Flag)
{
    return (Value & Flag) != 0u;
}

bool HasAllFlags(uint Value, uint Flag)
{
    return (Value & Flag) == Flag;
}

uint SetFlag(uint Value, uint Flag)
{
    return Value | Flag;
}

uint ClearFlag(uint Value, uint Flag)
{
    return Value & (~Flag);
}

uint ToggleFlag(uint Value, uint Flag)
{
    return Value ^ Flag;
}

float3 ReconstructWorldPosition(float2 UV, float Depth, float4x4 InvProjection, float4x4 InvView)
{
    float  Z            = Depth * 2.0 - 1.0;
    float4 ClipSpacePos = float4(UV * 2.0 - 1.0, Z, 1.0);
    float4 ViewSpacePos = mul(InvProjection, ClipSpacePos);
    ViewSpacePos       /= ViewSpacePos.w;
    float4 WorldPos     = mul(InvView, ViewSpacePos);
    return WorldPos.xyz;
}

float3 ReconstructViewPosition(float2 UV, float Depth, float4x4 InvProjection)
{
    float4 ClipSpace = float4(UV * 2.0 - 1.0, Depth * 2.0 - 1.0, 1.0);
    float4 ViewSpace = mul(InvProjection, ClipSpace);
    return ViewSpace.xyz / ViewSpace.w;
}

float LinearizeDepth(float Depth, float Near, float Far)
{
    return (Near * Far) / (Far - Depth * (Far - Near)) / Far;
}

float LinearizeDepthReverseZ(float Depth, float Near, float Far)
{
    return Near * Far / (Far + Depth * (Near - Far));
}