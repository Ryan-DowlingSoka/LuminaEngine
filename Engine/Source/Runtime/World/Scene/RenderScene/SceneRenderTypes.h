#pragma once

#include <glm/glm.hpp>

#include "Containers/Array.h"
#include "Platform/GenericPlatform.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RHIGlobals.h"

#define MAX_LIGHTS 1728
#define MAX_SHADOWS 100
#define SSAO_KERNEL_SIZE 32
#define LIGHT_INDEX_MASK 0x1FFFu
#define LIGHTS_PER_UINT 2
#define LIGHTS_PER_CLUSTER 100

#define SCENE_MAX_BOUNDS UINT64_MAX

#define COL_R_SHIFT 0
#define COL_G_SHIFT 8
#define COL_B_SHIFT 16
#define COL_A_SHIFT 24
#define COL_A_MASK 0xFF000000

#define VERIFY_SSBO_ALIGNMENT(Type) \
static_assert(sizeof(Type) % 16 == 0, #Type " must be 16-byte aligned");

constexpr int NumCascades = 3;
constexpr int ClusterGridSizeX = 16;
constexpr int ClusterGridSizeY = 9;
constexpr int ClusterGridSizeZ = 24;

constexpr int NumClusters = ClusterGridSizeX * ClusterGridSizeY * ClusterGridSizeZ;

constexpr int GCSMResolution            = 4096;
constexpr int GShadowAtlasResolution    = 4096;

namespace Lumina
{
    class FDeferredRenderScene;
    class CMaterialInterface;
    struct FVertex;
    class CMaterial;
    class CStaticMesh;
}

namespace Lumina
{

    template<typename T>
    using TRenderVector = TFixedVector<T, 100>;
    
    // Mutually-exclusive debug visualization mode for the forward scene. The
    // enum value is forwarded to the GPU via FCullData::DebugMode; keep the
    // numeric values in sync with DEBUG_MODE_* in Common.slang.
    enum class ERenderSceneDebugFlags : uint8
    {
        None                = 0,    // Normal lit rendering.
        Unlit               = 1,    // Albedo + emissive only, skip lighting.
        Meshlets            = 2,    // Per-meshlet hash color.
        WorldNormal         = 3,    // World-space geometric normal remapped to [0,1].
        ShadingNormal       = 4,    // World-space normal-mapped shading normal.
        BaseColor           = 5,    // Raw albedo with no lighting or emissive.
        Roughness           = 6,    // Roughness as greyscale.
        Metallic            = 7,    // Metallic as greyscale.
        AmbientOcclusion    = 8,    // Material AO as greyscale.
        Emissive            = 9,    // Emissive term only.
        UV                  = 10,   // frac(UV0) -> rg channels.
        LightComplexity     = 11,   // Cluster light count heatmap.
        Num                 = 12,
    };

    constexpr FStringView RenderFlagsAsString(ERenderSceneDebugFlags Flags)
    {
        switch (Flags)
        {
            case ERenderSceneDebugFlags::None:              return "Lit";
            case ERenderSceneDebugFlags::Unlit:             return "Unlit";
            case ERenderSceneDebugFlags::Meshlets:          return "Meshlets";
            case ERenderSceneDebugFlags::WorldNormal:       return "World Normal";
            case ERenderSceneDebugFlags::ShadingNormal:     return "Shading Normal";
            case ERenderSceneDebugFlags::BaseColor:         return "Base Color";
            case ERenderSceneDebugFlags::Roughness:         return "Roughness";
            case ERenderSceneDebugFlags::Metallic:          return "Metallic";
            case ERenderSceneDebugFlags::AmbientOcclusion:  return "Ambient Occlusion";
            case ERenderSceneDebugFlags::Emissive:          return "Emissive";
            case ERenderSceneDebugFlags::UV:                return "UV";
            case ERenderSceneDebugFlags::LightComplexity:   return "Light Complexity";
            default:                                        return "Lit";
        }
    }

    enum class ELightType : uint8
    {
        Directional,
        Point,
        Spot,

        Num,
    };
    
    enum class EGPUSceneSettingFlags : uint16
    {
        None    = 0,
        Unlit   = BIT(0),
        Lit     = BIT(1),
    };
    
    ENUM_CLASS_FLAGS(EGPUSceneSettingFlags);
    
    enum class EInstanceFlags : uint32
    {
        None                    = 0,
        Billboard               = BIT(0),
        Skinned                 = BIT(1),
        CastShadow              = BIT(2),
        ReceiveShadow           = BIT(3),
		Occluder                = BIT(4),
        IgnoreOcclusionCulling  = BIT(5),
        Translucent             = BIT(6),
        Masked                  = BIT(7),
    };
    
    ENUM_CLASS_FLAGS(EInstanceFlags);
    
    struct FCameraData
    {
        glm::vec4 Location          = {};
        glm::vec4 Up                = {};
        glm::vec4 Right             = {};
        glm::vec4 Forward           = {};
        glm::mat4 View              = {};
        glm::mat4 InverseView       = {};
        glm::mat4 Projection        = {};
        glm::mat4 InverseProjection = {};
    };

    constexpr uint32 LIGHT_TYPE_MASK      = 0x0000FFFF; // lower 16 bits
    constexpr uint32 LIGHT_SHADOW_MASK    = 0xFFFF0000; // upper 16 bits
    constexpr int    LIGHT_SHADOW_SHIFT   = 16;
    
    constexpr uint32 LIGHT_TYPE_DIRECTIONAL = 1 << 0;
    constexpr uint32 LIGHT_TYPE_POINT       = 1 << 1;
    constexpr uint32 LIGHT_TYPE_SPOT        = 1 << 2;

    struct FShadowAtlasConfig
    {
        uint32 AtlasResolution = GShadowAtlasResolution;
        uint32 TileResolution = 512;
        uint32 NumLayers = 7;

        constexpr uint32 TilesPerRow() const { return AtlasResolution / TileResolution; }
        constexpr uint32 MaxTiles() const { return TilesPerRow() * TilesPerRow(); }
    };

    struct FShadowTile
    {
        glm::vec2 UVOffset;     // Normalized offset (0-1 range)
        glm::vec2 UVScale;      // Normalized scale (1.0 / TilesPerRow)
    };
    
    class FShadowAtlas
    {
    public:
        
        FShadowAtlas(const FShadowAtlasConfig& InConfig)
            : Config(InConfig)
        {
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = glm::uvec2(InConfig.AtlasResolution);
            ImageDesc.Format            = EFormat::D32;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.InitialState      = EResourceStates::DepthWrite;
            ImageDesc.Dimension         = EImageDimension::Texture2DArray;
            ImageDesc.ArraySize         = (uint16)Config.NumLayers;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::DepthAttachment, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName         = "Shadow Atlas";

            ShadowAtlas = GRenderContext->CreateImage(ImageDesc);
            
            Tiles.resize(Config.MaxTiles());
            float Scale = 1.0f / Config.TilesPerRow();
        
            for (uint32 y = 0; y < Config.TilesPerRow(); ++y)
            {
                for (uint32 x = 0; x < Config.TilesPerRow(); ++x)
                {
                    uint32 Index = y * Config.TilesPerRow() + x;
                    Tiles[Index].UVOffset = glm::vec2(x * Scale, y * Scale);
                    Tiles[Index].UVScale = glm::vec2(Scale, Scale);
                    Free.push((int32)Index);
                }
            }
        }
    
        int32 AllocateTile()
        {
            if (Free.empty())
            {
                return INDEX_NONE;
            }
            
            int32 TileIndex = Free.front();
            Free.pop();
            return TileIndex;
        }

        void FreeTiles()
        {
            while (!Free.empty())
            {
                Free.pop();
            }
            
            for (uint32 i = 0; i < Tiles.size(); ++i)
            {
                Free.push(i);
            }
        }
        
        const FShadowTile& GetTile(int32 TileIndex) const { return Tiles[TileIndex]; }
        FRHIImageRef GetImage() const { return ShadowAtlas; }

    private:

        FRHIImageRef ShadowAtlas;
        FShadowAtlasConfig Config;
        TVector<FShadowTile> Tiles;
        TQueue<int32> Free;
    };
    

    struct FLightShadow
    {
        glm::vec2   AtlasUVOffset;
        glm::vec2   AtlasUVScale;

        int32       ShadowMapIndex;
        int32       ShadowMapLayer;
        int32       LightIndex;
        int32       ShadowDataIndex;    // Index into FSceneLightData::Shadows[]
    };

    VERIFY_SSBO_ALIGNMENT(FLightShadow)
    
    // Hot per-light data. Keeping it at 64 bytes cuts the L2 footprint of the inner loop ~10x.
    struct FLight
    {
        glm::vec3       Position;
        uint32          Color;

        glm::vec3       Direction;
        float           Radius;

        float           Intensity;
        float           Falloff;
        glm::vec2       Angles;

        uint32          Flags;
        int32           ShadowDataIndex;    // INDEX_NONE if no shadow
        uint32          Padding0[2];
    };

    static_assert(sizeof(FLight) == 64, "FLight hot struct must fit a cache line");
    static_assert(eastl::is_trivially_copyable_v<FLight>);

    VERIFY_SSBO_ALIGNMENT(FLight)

    // Cold shadow-caster data. Only shadow-rendering passes and the lit
    // pixel shader's shadow branch touch this, so the hot lighting loop
    // never pays the miss.
    struct FLightShadowData
    {
        glm::mat4       ViewProjection[6];  // 384 B
        FLightShadow    Shadow[6];          // 192 B
    };

    static_assert(sizeof(FLightShadowData) == 576, "FLightShadowData layout must match shader");
    VERIFY_SSBO_ALIGNMENT(FLightShadowData)

    struct FSkyLight
    {
        glm::vec4 Color;
    };

    struct FSceneLightData
    {
        uint32              NumLights{};
        uint32              Padding0[3];

        glm::vec3           SunDirection{};
        uint32              bHasSun{};

        glm::vec4           CascadeSplits{};
        // World-space half-extent (sphere radius) of each CSM cascade. Pixel
        // shaders use this to convert a shadow texel into a world-space length
        // for normal-offset bias — the offset must shrink as the cascade
        // tightens or it grows visibly large in cascade 0.
        glm::vec4           CascadeRadii{};

        glm::vec4           AmbientLight{};

        FLight              Lights[MAX_LIGHTS]{};
        FLightShadowData    Shadows[MAX_SHADOWS]{};
    };
    
    struct FLineBatch
    {
        uint32  StartVertex;
        uint32  VertexCount;
        float   Thickness;
        bool    bDepthTest;
    };
    
    struct FSSAOSettings
    {
        float Radius = 1.0f;
        float Intensity = 2.0f;
        float Power = 1.5f;

        uint32 Padding;

        glm::vec4 Samples[SSAO_KERNEL_SIZE];
    };

    struct FGBuffer
    {
        FRHIImageRef Normals;
        FRHIImageRef Material;
        FRHIImageRef AlbedoSpec;
    };
    
    struct alignas(16) FBillboardInstance
    {
        glm::vec3       Position;
        float           Size;
        
        uint32          ColorPack;
        uint32          TextureIndex;
        uint32          EntityID;
    };
    
    struct alignas(16) FCluster
    {
        glm::vec4 MinPoint;
        glm::vec4 MaxPoint;
        uint32 LightIndices[LIGHTS_PER_CLUSTER];
        uint32 Count;
    };
    
    VERIFY_SSBO_ALIGNMENT(FCluster)
    
    struct FLightClusterPC
    {
        glm::mat4 InverseProjection;
        glm::vec2 zNearFar;
        glm::uvec2 ScreenSize;
        glm::uvec4 GridSize;
    };

    // Unified 128B per-instance descriptor. One SSBO, one binding, one fetch per
    // instance. MeshletHeaderAddress is zero when the instance skips the meshlet
    // path (skinned meshes, legacy assets without meshlets).
    struct alignas(16) FGPUInstance
    {
        glm::mat4x4     Transform;
        glm::vec4       SphereBounds;

        uint64          VBAddress;
        uint64          ShadowIBAddress;
        uint64          MeshletHeaderAddress;

        uint32          DrawIDAndFlags;
        uint32          SurfaceMeshletOffset;
        uint32          SurfaceMeshletCount;
        uint32          CustomData;

        uint32          BoneOffsetAndMaterialIndex;
        uint32          EntityID;
    };

    static_assert(sizeof(FGPUInstance) == 128, "FGPUInstance layout must match shader");
    VERIFY_SSBO_ALIGNMENT(FGPUInstance)
    
    constexpr uint32 PackDrawIDAndFlags(uint32 DrawID, EInstanceFlags Flags)
    {
        return (DrawID & 0x00FFFFFF) | (((uint32)Flags & 0xFF) << 24);
    }
    
    constexpr uint32 PackBoneOffsetAndMaterial(uint16 BoneOffset, uint16 MaterialIndex)
    {
        return (uint32)BoneOffset | ((uint32)MaterialIndex << 16);
    }
    
    struct FCullData
    {
        FFrustum Frustum;
        FFrustum ShadowFrustum;

        FFrustum CascadeFrustum[NumCascades];

        uint32 bFrustumCull;
        uint32 bOcclusionCull;
        uint32 InstanceNum;
        uint32 bHasDirectional;

        float PyramidWidth;
        float PyramidHeight;

        float  ShadowMaxDistance;
        uint32 bShadowOcclusionCull;

        uint32 NumDraws;
        uint32 DebugMode;
        uint32 Padding[2];
    };
    
    
    // Sim flag bitmask, must match constants in ParticleSimulate(.Template).slang
    static constexpr uint32 PARTICLE_SIM_FLAG_LOOP          = 1u << 0;
    static constexpr uint32 PARTICLE_SIM_FLAG_BURST_PENDING = 1u << 1;

    // 288 byte layout, must match FParticleSimParams in ParticleSimulate.slang / ParticleSimulateTemplate.slang.
    struct alignas(16) FParticleSimParamsGPU
    {
        glm::vec4  EmitterPosition;
        glm::vec4  EmitterForward;
        glm::vec4  EmitterRight;
        glm::vec4  EmitterUp;
        glm::uvec4 Counts;              // x=MaxParticles, y=SpawnCount, z=FrameSeed, w=SimFlags
        glm::uvec4 Modes;               // x=Shape, y=VelocityMode
        glm::vec4  ShapeSize;           // xyz dims; w=cone half-angle (radians)
        glm::vec4  VelocityMin;
        glm::vec4  VelocityMax;
        glm::vec4  SpeedAndLifetime;    // x=speedMin, y=speedMax, z=lifeMin, w=lifeMax
        glm::vec4  Gravity;             // xyz=gravity, w=drag
        glm::vec4  StartColor;
        glm::vec4  EndColor;
        glm::vec4  SizeRange;           // xy=start(min,max); zw=end(min,max)
        glm::vec4  RotationRange;       // xy=rot(min,max); zw=rotSpeed(min,max)
        glm::vec4  NoiseStrength;       // xyz=strength; w=scale
        glm::vec4  NoiseParams;         // x=speed
        glm::vec4  Timing;              // x=DeltaTime, y=TotalTime, z=SystemAge
    };
    static_assert(sizeof(FParticleSimParamsGPU) == 288, "FParticleSimParamsGPU layout must match shader");

    // 48 byte layout. must match FParticleRenderParams in ParticleVertex.slang.
    struct alignas(16) FParticleRenderParamsGPU
    {
        glm::uvec4 Flags;       // x=TextureIndex, y=BillboardToCamera
        glm::vec4  Tint;        // xyz=color, w=intensity
        glm::vec4  UVParams;    // reserved
    };
    static_assert(sizeof(FParticleRenderParamsGPU) == 48, "FParticleRenderParamsGPU layout must match shader");
    
    struct FGPUSceneSettings
    {
        EGPUSceneSettingFlags Flags;
    };

    struct FSceneGlobalData
    {
        FCameraData     CameraData;
        glm::uvec4      ScreenSize;
        glm::uvec4      GridSize;

        float           Time;
        float           DeltaTime;
        float           NearPlane;
        float           FarPlane;
        
        FSSAOSettings   SSAOSettings;
        FCullData       CullData;
    };

    struct FMeshPass
    {
        uint32 MeshDrawOffset;
        uint32 MeshDrawSize;
        uint32 IndirectDrawOffset;
    };
    
    // CPU-side scene stats. Only tracks counters the GPU can't see directly
    // (batch/cull/material bookkeeping); draw-time counters like triangles,
    // VS/FS invocations, and draw/instance counts now come from Vulkan
    // pipeline-statistics queries — see FPipelineStats / FGPUProfileFrame.
    struct FSceneRenderStats
    {
        uint64 NumBatches = 0;            // Unique pipeline/material batches built this frame
        uint64 NumMeshes = 0;             // Unique meshes referenced
        uint64 NumMaterials = 0;          // Unique materials referenced
        uint64 NumDrawCallsCulled = 0;    // Draws culled by frustum/occlusion (CPU path)
        uint64 NumInstancesCulled = 0;    // Instances culled
        uint64 NumShadowDraws = 0;        // Shadow pass draws
        uint64 NumSkinnedMeshes = 0;
        uint64 NumStaticMeshes = 0;
    };
    
    struct FSceneRenderSettings
    {
        ERenderSceneDebugFlags Flags    = ERenderSceneDebugFlags::None;
        uint8 bUseInstancing:1          = true;
        uint8 bHasEnvironment:1         = false;
        uint8 bDrawAABB:1               = false;
        uint8 bSSAO:1                   = false;
        uint8 bFrustumCull:1            = true;
        uint8 bOcclusionCull:1          = true;
        uint8 bShadowOcclusionCull:1    = true;
        uint8 bWireframe:1              = false;
        uint8 bDrawBillboards:1         = true;
    };
    
}