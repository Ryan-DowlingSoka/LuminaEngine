#pragma once

#include "Core/Math/Math.h"

#include "Containers/Array.h"
#include "Core/Threading/Thread.h"
#include "Platform/GenericPlatform.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RHIGlobals.h"

#define MAX_LIGHTS 8192
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

constexpr int NumCascades = 4;
constexpr int ClusterGridSizeX = 16;
constexpr int ClusterGridSizeY = 9;
constexpr int ClusterGridSizeZ = 24;

constexpr int NumClusters = ClusterGridSizeX * ClusterGridSizeY * ClusterGridSizeZ;

// 4 equal cascades in a 4096x4096 atlas (2x2). Equal resolution avoids starving the far cascades that
// cover the most ground; total D32 atlas is 67MB (vs the old 6144x4096 = 100MB with a 4096/2048/1024
// geometric falloff that spent most of the budget on the near cascade).
constexpr int GCSMCascadeSizes[NumCascades]   = { 2048, 2048, 2048, 2048 };
constexpr int GCSMAtlasWidth                  = 4096;
constexpr int GCSMAtlasHeight                 = 4096;
constexpr int GCSMCascadeOriginX[NumCascades] = { 0,    2048, 0,    2048 };
constexpr int GCSMCascadeOriginY[NumCascades] = { 0,    0,    2048, 2048 };

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
        ClusterGrid         = 12,
        ShadowCascades      = 13,
        ShadowPenumbra      = 14,
        Num                 = 15,
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
            case ERenderSceneDebugFlags::ClusterGrid:       return "Light Clusters";
            case ERenderSceneDebugFlags::ShadowCascades:    return "Shadow Cascades";
            case ERenderSceneDebugFlags::ShadowPenumbra:    return "Shadow Penumbra";
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
        FVector4 Location          = {};
        FVector4 Up                = {};
        FVector4 Right             = {};
        FVector4 Forward           = {};
        FMatrix4 View              = {};
        FMatrix4 InverseView       = {};
        FMatrix4 Projection        = {};
        FMatrix4 InverseProjection = {};
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
        FVector2 UVOffset;     // Normalized origin (0-1 range) of this tile in the atlas.
        FVector2 UVScale;      // Normalized size (square: UVScale.x == UVScale.y).
    };

    // Quad-tree shadow atlas allocator. Tiles sized by projected radius; reset per-frame via FreeTiles().
    class FShadowAtlas
    {
    public:

        FShadowAtlas(const FShadowAtlasConfig& InConfig)
            : Config(InConfig)
        {
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = FUIntVector2(InConfig.AtlasResolution);
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

            const uint32 ClampedSize = Math::Clamp(RoundUpPow2(DesiredPixels), Config.MinTileResolution, Config.MaxTileResolution);
            const uint32 StartLevel  = Log2Floor(ClampedSize) - MinLevel;

            for (uint32 Level = StartLevel; Level < NumLevels; ++Level)
            {
                if (!FreeLists[Level].empty())
                {
                    FTileRect Rect = FreeLists[Level].back();
                    FreeLists[Level].pop_back();

                    // Split down to StartLevel; return last quadrant, push siblings back.
                    while (Level > StartLevel)
                    {
                        const uint32 Half = Rect.Size / 2;
                        const uint32 ChildLevel = Level - 1;
                        FreeLists[ChildLevel].push_back({ Rect.X + Half, Rect.Y,        Half });
                        FreeLists[ChildLevel].push_back({ Rect.X,        Rect.Y + Half, Half });
                        FreeLists[ChildLevel].push_back({ Rect.X + Half, Rect.Y + Half, Half });
                        Rect = { Rect.X, Rect.Y, Half };
                        --Level;
                    }

                    const int32 Handle = (int32)Tiles.size();
                    const float InvAtlas = 1.0f / (float)Config.AtlasResolution;
                    FShadowTile Tile;
                    Tile.UVOffset = FVector2(Rect.X * InvAtlas, Rect.Y * InvAtlas);
                    Tile.UVScale  = FVector2(Rect.Size * InvAtlas);
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
            for (TVector<FTileRect>& Q : FreeLists)
            {
                Q.clear();   // keeps capacity -- avoids reallocating the free lists every frame
            }

            const uint32 RootSize = Config.MaxTileResolution;
            for (uint32 Y = 0; Y < Config.AtlasResolution; Y += RootSize)
            {
                for (uint32 X = 0; X < Config.AtlasResolution; X += RootSize)
                {
                    FreeLists[NumLevels - 1].push_back({ X, Y, RootSize });
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
        TVector<TVector<FTileRect>> FreeLists;   // Indexed by (log2(size) - MinLevel). Used LIFO; cleared (keeps capacity) per frame.
        FMutex AllocMutex;
        uint32 MinLevel  = 0;
        uint32 MaxLevel  = 0;
        uint32 NumLevels = 0;
    };
    

    struct FLightShadow
    {
        FVector2   AtlasUVOffset;
        FVector2   AtlasUVScale;

        int32       ShadowMapIndex;
        int32       LightIndex;
        int32       ShadowDataIndex;    // Index into FSceneLightData::Shadows[]
        int32       _Padding;           // std430 16-byte alignment.
    };

    VERIFY_SSBO_ALIGNMENT(FLightShadow)
    
    // Hot per-light data. Keeping it at 64 bytes cuts the L2 footprint of the inner loop ~10x.
    struct FLight
    {
        FVector3       Position;
        uint32          Color;

        FVector3       Direction;
        float           Radius;

        float           Intensity;
        float           Falloff;
        FVector2       Angles;

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
        FMatrix4       ViewProjection[6];  // 384 B
        FLightShadow    Shadow[6];          // 192 B
    };

    static_assert(sizeof(FLightShadowData) == 576, "FLightShadowData layout must match shader");
    VERIFY_SSBO_ALIGNMENT(FLightShadowData)

    struct FSkyLight
    {
        FVector4 Color;
    };

    struct FSceneLightData
    {
        uint32              NumLights{};
        uint32              Padding0[3];

        FVector3           SunDirection{};
        uint32              bHasSun{};

        FVector4           CascadeSplits{};
        // Half-extent of each CSM cascade; used to convert shadow texel to world length.
        FVector4           CascadeRadii{};
        // Per-cascade shadow-map resolution; xyzw = cascades 0..3.
        FVector4           CascadeResolutions{};

        // Directional shadow tuning (SDirectionalLightComponent): x = normal-bias scale, y = depth bias,
        // z = PCSS softness (light size), w = cascade cross-fade fraction.
        FVector4           ShadowParams{ 1.0f, 0.0f, 0.05f, 0.20f };
        // x = far-cascade distance-fade fraction; yzw reserved.
        FVector4           ShadowParams2{ 0.15f, 0.0f, 0.0f, 0.0f };

        FVector4           AmbientLight{};

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

    struct FSolidBatch
    {
        uint32  StartVertex;
        uint32  VertexCount;
        bool    bDepthTest;
    };

    struct FSSAOSettings
    {
        float Radius = 1.0f;
        float Intensity = 2.0f;
        float Power = 1.5f;

        uint32 Padding;

        FVector4 Samples[SSAO_KERNEL_SIZE];
    };

    struct FGBuffer
    {
        FRHIImageRef Normals;
        FRHIImageRef Material;
        FRHIImageRef AlbedoSpec;
    };
    
    struct alignas(16) FBillboardInstance
    {
        FVector3       Position;
        float           Size;

        uint32          ColorPack;
        uint32          TextureIndex;
        uint32          EntityID;
    };

    // World-space UI widget quad. Matches FWidgetInstance in Common.slang (96B, dense).
    struct alignas(16) FWidgetInstance
    {
        FMatrix4       Transform;      // entity world matrix
        FVector2       WorldSize;      // quad size in world units
        uint32          TextureIndex;   // bindless ResourceID of the widget RT
        uint32          Flags;          // bit0 = billboard (face camera)
        uint32          ColorPack;      // tint, PackColor()
        uint32          EntityID;
        uint32          Pad0;
        uint32          Pad1;
    };

    static constexpr uint32 WIDGET_FLAG_BILLBOARD = 1u << 0;

    // One projected decal. Drawn as a unit cube; the decal pixel shader reconstructs the surface from
    // depth, projects into decal-local space, and writes the DBuffer. Must match FGPUDecal in DecalCommon.slang.
    struct alignas(16) FGPUDecal
    {
        FMatrix4    WorldToDecal;       // world -> decal-local ([-0.5,0.5]^3 inside the box)
        FMatrix4    DecalToWorld;       // decal-local cube -> world (entity transform)
        float       FadeAngleCos;       // cos(max angle) of surface normal vs decal forward; below => fades out
        float       Opacity;            // master coverage multiplier
        uint32      MaterialIndex;      // slot into the material uniform buffer
        uint32      Flags;              // reserved
    };

    static_assert(sizeof(FGPUDecal) == 144, "FGPUDecal layout must match DecalCommon.slang");
    VERIFY_SSBO_ALIGNMENT(FGPUDecal)

    struct alignas(16) FCluster
    {
        FVector4 MinPoint;
        FVector4 MaxPoint;
        uint32 LightIndices[LIGHTS_PER_CLUSTER];
        uint32 Count;
    };
    
    VERIFY_SSBO_ALIGNMENT(FCluster)
    
    struct FLightClusterPC
    {
        FMatrix4 InverseProjection;
        FVector2 zNearFar;
        FUIntVector2 ScreenSize;
        FUIntVector4 GridSize;
    };

    // 128B per-instance descriptor. Empty ctor skips zero-init on resize() (parallel writer overwrites everything).
    struct alignas(16) FGPUInstance
    {
        FGPUInstance() noexcept {}

        FMatrix4     Transform;
        FVector4       SphereBounds;

        uint32          ShadowMeshletOffset;
        uint32          ShadowMeshletCount;
        uint64          MeshletHeaderAddress;

        uint32          DrawIDAndFlags;
        uint32          SurfaceMeshletOffset;
        uint32          SurfaceMeshletCount;
        uint32          CustomData;

        // Full 32-bit bone index (was packed into 16 bits with MaterialIndex, which capped
        // the scene at 64k total bones).
        uint32          BoneOffset;
        uint32          MaterialIndex;
        uint32          EntityID;
        // Base index into the pre-skinned vertex buffer; the skinning pass writes there, the draw VS
        // reads instead of re-skinning. 0 for static instances.
        uint32          SkinnedVertexBase;
    };

    static_assert(sizeof(FGPUInstance) == 128, "FGPUInstance layout must match shader");
    VERIFY_SSBO_ALIGNMENT(FGPUInstance)

    // One per skinned vertex, produced by the skinning pass and read by every draw VS. Holds the COMPLETE
    // vertex so the VS never touches the source. Position full-precision; normal/tangent octahedral. 28 B.
    struct FPreSkinnedVertex
    {
        float       Px;
        float       Py;
        float       Pz;
        uint32      Normal;     // PackNormal
        uint32      Tangent;    // PackTangent
        uint32      UV;
        uint32      Color;
    };
    static_assert(sizeof(FPreSkinnedVertex) == 28, "FPreSkinnedVertex must match shader");

    // One per rendered-LOD meshlet; drives the skinning compute dispatch (one workgroup each).
    // Flattened from per-entity so every meshlet skins concurrently (no serial meshlet loop).
    struct FSkinDescriptor
    {
        uint64      MeshletHeaderAddress;   // FMeshletHeader* (BDA)
        uint32      BoneOffset;             // global index into the bone-matrix buffer
        // Combined base = (compacted slice base) - (vertex span start), so that
        // SkinnedVertexBase + M.VertexOffset lands in the compacted slice (uint wraps).
        uint32      SkinnedVertexBase;
        uint32      MeshletIndex;           // index into Header.Meshlets (one descriptor per meshlet)
        uint32      Pad;
    };
    static_assert(sizeof(FSkinDescriptor) == 24, "FSkinDescriptor must match shader");

    // Bone skinning matrix with its always-(0,0,0,1) last row dropped: first 3 rows of the 4x4.
    // 48 B vs 64 B, lossless for affine transforms. Read only by the skinning compute.
    struct FBoneTransform
    {
        FVector4   Row0;
        FVector4   Row1;
        FVector4   Row2;
    };
    static_assert(sizeof(FBoneTransform) == 48, "FBoneTransform must match shader");
    VERIFY_SSBO_ALIGNMENT(FBoneTransform)

    // Drop the redundant 4th row of an affine skinning matrix. p' = M*p == dot(Row_r, p4).
    FORCEINLINE FBoneTransform PackBoneTransform(const FMatrix4& M)
    {
        return {
            FVector4(M[0][0], M[1][0], M[2][0], M[3][0]),
            FVector4(M[0][1], M[1][1], M[2][1], M[3][1]),
            FVector4(M[0][2], M[1][2], M[2][2], M[3][2]),
        };
    }

    constexpr uint32 PackDrawIDAndFlags(uint32 DrawID, EInstanceFlags Flags)
    {
        return (DrawID & 0x00FFFFFF) | (((uint32)Flags & 0xFF) << 24);
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
        FVector4   FrustumPlanes[6];           // 96 B
        FVector4   ViewOriginAndFlags;         // 16 B: xyz=origin, w=asfloat(flags)
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
        FVector4  EmitterPosition;
        FVector4  EmitterForward;
        FVector4  EmitterRight;
        FVector4  EmitterUp;
        FUIntVector4 Counts;              // x=MaxParticles, y=SpawnCount, z=FrameSeed, w=SimFlags
        FUIntVector4 Modes;               // x=Shape, y=VelocityMode
        FVector4  ShapeSize;           // xyz dims; w=cone half-angle (radians)
        FVector4  VelocityMin;
        FVector4  VelocityMax;
        FVector4  SpeedAndLifetime;    // x=speedMin, y=speedMax, z=lifeMin, w=lifeMax
        FVector4  Gravity;             // xyz=gravity, w=drag
        FVector4  StartColor;
        FVector4  EndColor;
        FVector4  SizeRange;           // xy=start(min,max); zw=end(min,max)
        FVector4  RotationRange;       // xy=rot(min,max); zw=rotSpeed(min,max)
        FVector4  NoiseStrength;       // xyz=strength; w=scale
        FVector4  NoiseParams;         // x=speed
        FVector4  Timing;              // x=DeltaTime, y=TotalTime, z=SystemAge
    };
    static_assert(sizeof(FParticleSimParamsGPU) == 288, "FParticleSimParamsGPU layout must match shader");

    // 48 byte layout. must match FParticleRenderParams in ParticleVertex.slang.
    struct alignas(16) FParticleRenderParamsGPU
    {
        FUIntVector4 Flags;       // x=TextureIndex, y=BillboardToCamera
        FVector4  Tint;        // xyz=color, w=intensity
        FVector4  UVParams;    // reserved
    };
    static_assert(sizeof(FParticleRenderParamsGPU) == 48, "FParticleRenderParamsGPU layout must match shader");
    
    struct FGPUSceneSettings
    {
        EGPUSceneSettingFlags Flags;
    };

    // Device addresses of every scene buffer + bindless indices for the IBL/shadow textures. Built into
    // a per-view transient each frame; its address rides in FRootConstants.RootAddr. Must match
    // FSceneRoot in SceneGlobals.slang (128 B). No alignas: natural 8-byte packing matches Slang's
    // scalar field offsets (alignas(16) would pad the tail and break the size check).
    struct FSceneRoot
    {
        uint64 SceneData             = 0;  // FSceneGlobalData (per-view camera/scene)
        uint64 Lights                = 0;
        uint64 Instances             = 0;
        uint64 Bones                 = 0;
        uint64 Clusters              = 0;  // per-view, GPU-written
        uint64 Materials             = 0;  // non-dynamic
        uint64 Billboards            = 0;
        uint64 CullViews             = 0;
        uint64 MeshletDrawList       = 0;  // ring, GPU-written
        uint64 InstanceMeshletPrefix = 0;
        uint64 PreSkinnedVertices    = 0;  // GPU-written
        uint64 SkinDescriptors       = 0;
        uint64 Widgets               = 0;
        uint32 BRDFLutIndex          = 0;
        uint32 SkyIrradianceIndex    = 0;
        uint32 SkyPrefilterIndex     = 0;
        uint32 ShadowCascadeIndex    = 0;  // bindless 2D SRV (cascade atlas)
        uint32 ShadowAtlasIndex      = 0;  // bindless 2D SRV (spot/point atlas)
        uint32 SkyCubeIndex          = 0;  // bindless cube SRV (full-res sky; sharp near-mirror reflections)
    };
    static_assert(sizeof(FSceneRoot) == 128, "FSceneRoot must match SceneGlobals.slang");

    // The one engine-wide push constant. RootAddr -> FSceneRoot transient; PassAddr -> per-pass
    // constants transient (0 if the pass has none). Matches FRootConstants in SceneGlobals.slang.
    struct FRootConstants
    {
        uint64 RootAddr = 0;
        uint64 PassAddr = 0;
    };

    struct FSceneGlobalData
    {
        FCameraData     CameraData;
        FUIntVector4      ScreenSize;
        FUIntVector4      GridSize;

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
