#pragma once

#include <glm/glm.hpp>

#include "Containers/Array.h"
#include "Core/Threading/Thread.h"
#include "Platform/GenericPlatform.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RHIGlobals.h"

#define MAX_LIGHTS 1728
#define MAX_SHADOWS 256
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

// CSM atlas: C0 (2048) takes left half; C1/C2 (1024 each) stack in right column.
// Single Texture2D atlas (~24 MB) replaces 192 MB D32 array.
constexpr int GCSMCascadeSizes[3]       = { 2048, 1024, 1024 };
constexpr int GCSMAtlasWidth            = 3072;
constexpr int GCSMAtlasHeight           = 2048;
constexpr int GCSMCascadeOriginX[3]     = { 0,    2048, 2048 };
constexpr int GCSMCascadeOriginY[3]     = { 0,    0,    1024 };

constexpr int GShadowAtlasResolution    = 4096;

// Hard cap on cull views: camera + NumCascades + 6/point + 1/spot.
constexpr int GMaxCullViews             = 128;

namespace Lumina
{
    class CMaterialInterface;
    struct FVertex;
    class CMaterial;
    class CStaticMesh;
}

namespace Lumina
{

    template<typename T>
    using TRenderVector = TFixedVector<T, 100>;
    
    // Mutually-exclusive debug viz; values must match DEBUG_MODE_* in Common.slang.
    enum class ERenderSceneDebugFlags : uint8
    {
        None                = 0,
        Unlit               = 1,
        Meshlets            = 2,
        WorldNormal         = 3,
        ShadingNormal       = 4,
        BaseColor           = 5,
        Roughness           = 6,
        Metallic            = 7,
        AmbientOcclusion    = 8,
        Emissive            = 9,
        UV                  = 10,
        LightComplexity     = 11,
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
        TwoSided                = BIT(4),  // Skip backface cone cull.
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

    // Mirror of ELightFlags in Common.slang -- keep values in lockstep.
    enum class ELightFlags : uint32
    {
        None        = 0,
        Directional = BIT(0),
        Point       = BIT(1),
        Spot        = BIT(2),
        CastShadow  = BIT(3),
        Volumetric  = BIT(4),
    };

    ENUM_CLASS_FLAGS(ELightFlags);

    struct FShadowAtlasConfig
    {
        uint32 AtlasResolution    = GShadowAtlasResolution;    // Atlas is square: AtlasResolution x AtlasResolution.
        uint32 MaxTileResolution  = 1024;                      // Largest tile a single shadow can claim. Must be pow2.
        uint32 MinTileResolution  = 128;                       // Smallest leaf the quad-tree will subdivide to. Must be pow2.
    };

    struct FShadowTile
    {
        glm::vec2 UVOffset;     // Normalized origin (0-1 range) of this tile in the atlas.
        glm::vec2 UVScale;      // Normalized size (square: UVScale.x == UVScale.y).
    };

    // Quad-tree shadow atlas allocator. Tiles sized by projected radius; reset per-frame via FreeTiles().
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
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.ArraySize         = 1;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::DepthAttachment, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName         = "Shadow Atlas";

            ShadowAtlas = GRenderContext->CreateImage(ImageDesc);

            MinLevel = Log2Floor(Config.MinTileResolution);
            MaxLevel = Log2Floor(Config.MaxTileResolution);
            NumLevels = (MaxLevel - MinLevel) + 1;
            FreeLists.resize(NumLevels);

            FreeTiles();
        }

        // Quantizes up to next pow2 and clamps to [Min,Max]. Returns INDEX_NONE if full.
        int32 AllocateTile(uint32 DesiredPixels)
        {
            FScopeLock Lock(AllocMutex);

            const uint32 ClampedSize = glm::clamp(RoundUpPow2(DesiredPixels), Config.MinTileResolution, Config.MaxTileResolution);
            const uint32 StartLevel  = Log2Floor(ClampedSize) - MinLevel;

            for (uint32 Level = StartLevel; Level < NumLevels; ++Level)
            {
                if (!FreeLists[Level].empty())
                {
                    FTileRect Rect = FreeLists[Level].front();
                    FreeLists[Level].pop();

                    // Split down to StartLevel; return last quadrant, push siblings back.
                    while (Level > StartLevel)
                    {
                        const uint32 Half = Rect.Size / 2;
                        const uint32 ChildLevel = Level - 1;
                        FreeLists[ChildLevel].push({ Rect.X + Half, Rect.Y,        Half });
                        FreeLists[ChildLevel].push({ Rect.X,        Rect.Y + Half, Half });
                        FreeLists[ChildLevel].push({ Rect.X + Half, Rect.Y + Half, Half });
                        Rect = { Rect.X, Rect.Y, Half };
                        --Level;
                    }

                    const int32 Handle = (int32)Tiles.size();
                    const float InvAtlas = 1.0f / (float)Config.AtlasResolution;
                    FShadowTile Tile;
                    Tile.UVOffset = glm::vec2(Rect.X * InvAtlas, Rect.Y * InvAtlas);
                    Tile.UVScale  = glm::vec2(Rect.Size * InvAtlas);
                    Tiles.push_back(Tile);
                    return Handle;
                }
            }
            return INDEX_NONE;
        }

        // Reseeds top-level free list with a grid of MaxTileResolution roots.
        void FreeTiles()
        {
            Tiles.clear();
            for (TQueue<FTileRect>& Q : FreeLists)
            {
                TQueue<FTileRect> Empty;
                std::swap(Q, Empty);
            }

            const uint32 RootSize = Config.MaxTileResolution;
            for (uint32 Y = 0; Y < Config.AtlasResolution; Y += RootSize)
            {
                for (uint32 X = 0; X < Config.AtlasResolution; X += RootSize)
                {
                    FreeLists[NumLevels - 1].push({ X, Y, RootSize });
                }
            }
        }

        const FShadowTile& GetTile(int32 TileIndex) const { return Tiles[TileIndex]; }
        FRHIImageRef GetImage() const { return ShadowAtlas; }

        const FShadowAtlasConfig& GetConfig() const { return Config; }
        const TVector<FShadowTile>& GetAllocatedTiles() const { return Tiles; }

    private:

        struct FTileRect
        {
            uint32 X;
            uint32 Y;
            uint32 Size;
        };

        static constexpr uint32 Log2Floor(uint32 V)
        {
            uint32 R = 0;
            while (V >>= 1) { ++R; }
            return R;
        }

        static constexpr uint32 RoundUpPow2(uint32 V)
        {
            if (V <= 1)
            {
                return 1;
            }
            --V;
            V |= V >> 1;  V |= V >> 2;  V |= V >> 4;
            V |= V >> 8;  V |= V >> 16;
            return V + 1;
        }

        FRHIImageRef ShadowAtlas;
        FShadowAtlasConfig Config;
        TVector<FShadowTile> Tiles;
        TVector<TQueue<FTileRect>> FreeLists;   // Indexed by (log2(size) - MinLevel).
        FMutex AllocMutex;
        uint32 MinLevel  = 0;
        uint32 MaxLevel  = 0;
        uint32 NumLevels = 0;
    };
    

    struct FLightShadow
    {
        glm::vec2   AtlasUVOffset;
        glm::vec2   AtlasUVScale;

        int32       ShadowMapIndex;
        int32       LightIndex;
        int32       ShadowDataIndex;    // Index into FSceneLightData::Shadows[]
        int32       _Padding;           // std430 16-byte alignment.
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

        ELightFlags     Flags;
        int32           ShadowDataIndex;    // INDEX_NONE if no shadow

        // Ignored unless Flags has ELightFlags::Volumetric.
        float           VolumetricIntensity;
        uint32          Padding0;
    };

    static_assert(sizeof(FLight) == 64, "FLight hot struct must fit a cache line");
    static_assert(eastl::is_trivially_copyable_v<FLight>);

    VERIFY_SSBO_ALIGNMENT(FLight)

    // Cold shadow-caster data; hot lighting loop never touches it.
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
        // Half-extent of each CSM cascade; used to convert shadow texel to world length.
        glm::vec4           CascadeRadii{};
        // Per-cascade shadow-map resolution; xyzw = cascades 0..3.
        glm::vec4           CascadeResolutions{};

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

    // 128B per-instance descriptor. Empty ctor skips zero-init on resize() (parallel writer overwrites everything).
    struct alignas(16) FGPUInstance
    {
        FGPUInstance() noexcept {}

        glm::mat4x4     Transform;
        glm::vec4       SphereBounds;

        uint64          VBAddress;

        uint32          ShadowMeshletOffset;
        uint32          ShadowMeshletCount;
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
        // Total meshlets this frame; flat thread-per-meshlet dispatch reads this.
        uint32 TotalMeshletBound;
        // Bindless ResourceID of the depth pyramid; HZB tap goes through uBindlessTex2D.
        uint32 DepthPyramidIndex;
    };

    // Bits inside FCullView::Flags. Must match CULL_VIEW_FLAG_* in Common.slang.
    namespace ECullViewFlags
    {
        enum Type : uint32
        {
            None            = 0,
            Frustum         = BIT(0),
            Cone            = BIT(1),
            Occlusion       = BIT(2),
            Distance        = BIT(3),
            CastShadowOnly  = BIT(4),
            SunAligned      = BIT(5),
            PhaseLate       = BIT(6),
        };
    }

    // Phase push-constant values. Must match CULL_PHASE_* in Common.slang.
    namespace ECullPhase
    {
        enum Type : uint32
        {
            Early = 0,
            Late  = 1,
        };
    }

    // Mirror of FCullView in Common.slang; one entry per render view.
    struct alignas(16) FCullView
    {
        glm::vec4   FrustumPlanes[6];           // 96 B
        glm::vec4   ViewOriginAndFlags;         // 16 B: xyz=origin, w=asfloat(flags)
        uint32      DrawListOffset;             // Into uMeshletDrawList
        uint32      DrawListCapacity;           // Max FMeshletDraw entries this view may emit
        uint32      IndirectArgsOffset;         // v * NumDraws
        uint32      NumDraws;                   // Number of indirect slots owned by this view
    };

    static_assert(sizeof(FCullView) == 128, "FCullView layout must match shader");
    VERIFY_SSBO_ALIGNMENT(FCullView)


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
    
    // CPU-side scene stats. Draw-time counters come from FPipelineStats / FGPUProfileFrame.
    struct FSceneRenderStats
    {
        uint64 NumBatches = 0;
        uint64 NumMeshes = 0;
        uint64 NumMaterials = 0;
        uint64 NumDrawCallsCulled = 0;
        uint64 NumInstancesCulled = 0;
        uint64 NumShadowDraws = 0;
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
        // Pre-upload reject of instances outside every contributing view.
        uint8 bCPUInstanceCull:1        = true;
        // Disabled = always LOD 0 (full detail).
        uint8 bUseLODs:1                = true;

        // Camera LOD + bias picks shadow LOD; capped at MAX_SHADOW_LOD. 0 = no saving, 1-2 typical.
        int8  ShadowLODBias             = 1;
    };
    
}