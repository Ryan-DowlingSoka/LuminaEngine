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
    
    enum class ERenderSceneDebugFlags : uint8
    {
        None                = 0,
        Position            = 1,
        Normals             = 2,
        Albedo              = 3,
        SSAO                = 4,
		AmbientOcclusion    = 5,
		Roughness           = 6,
		Metallic            = 7,
		Specular            = 8,
        Depth               = 9,
        ShadowAtlas		    = 10,
        Cascade             = 11,
        Num                 = 12,
    };
    
    constexpr FStringView RenderFlagsAsString(ERenderSceneDebugFlags Flags)
    {
        switch (Flags)
        {
            case ERenderSceneDebugFlags::None: return "None";
            case ERenderSceneDebugFlags::Position: return "Position";
            case ERenderSceneDebugFlags::Normals: return "Normals";
            case ERenderSceneDebugFlags::Albedo: return "Albedo";
            case ERenderSceneDebugFlags::SSAO: return "SSAO";
            case ERenderSceneDebugFlags::AmbientOcclusion: return "Ambient Occlusion";
            case ERenderSceneDebugFlags::Roughness: return "Roughness";
            case ERenderSceneDebugFlags::Metallic: return "Metallic";
            case ERenderSceneDebugFlags::Specular: return "Specular";
            case ERenderSceneDebugFlags::Depth: return "Depth";
            case ERenderSceneDebugFlags::ShadowAtlas: return "Shadow Atlas";
            case ERenderSceneDebugFlags::Cascade: return "Cascades";
            default: return "None";
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
        int32       Padding;
    };
    
    VERIFY_SSBO_ALIGNMENT(FLightShadow)
    
    struct FLight
    {
        glm::vec3       Position;
        uint32          Color;
        
        float           Intensity;
        uint32          Padding0[3];
        
        glm::vec3       Direction;
        float           Radius;

        glm::mat4       ViewProjection[6];
        
        glm::vec2       Angles;
        uint32          Flags;
        float           Falloff;

        FLightShadow    Shadow[6];
    };

    static_assert(eastl::is_trivially_copyable_v<FLight>);

    VERIFY_SSBO_ALIGNMENT(FLight)
    
    struct FSkyLight
    {
        glm::vec4 Color;
    };
    
    struct FSceneLightData
    {
        uint32          NumLights;
        uint32          Padding0[3];

        glm::vec3       SunDirection;
        uint32          bHasSun;

        glm::vec4       CascadeSplits;

        glm::vec4       AmbientLight;
        
        FLight          Lights[MAX_LIGHTS];
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

    struct alignas(16) FGPUInstance
    {
        glm::mat4x4     Transform;
        glm::vec4       SphereBounds;
        
        uint64          VBAddress;
        uint64          IBAddress;

        uint32          EntityID;
        uint32          DrawIDAndFlags;
        uint32          BoneOffsetAndMaterialIndex;
        uint32          CustomData;
    };
    
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
        // Extruded camera frustum swept along the sun light direction. Used by
        // the shadow-cull compute pass so casters outside the camera frustum
        // still get written into the shadow indirect buffer. If there is no
        // directional light in the scene this mirrors Frustum (unused).
        FFrustum ShadowFrustum;
        glm::mat4 ViewMatrix;   // View matrix (not view-projection!)

        float P00;              // projection[0][0]
        float P11;              // projection[1][1]
        float zNear;
        float zFar;

        uint32 bFrustumCull;
        uint32 bOcclusionCull;
        uint32 InstanceNum;
        uint32 bHasDirectional;

        float PyramidWidth;
        float PyramidHeight;
        uint32 Padding1[2];
    };
    
    
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
    
    struct FSceneRenderStats
    {
        uint64 NumVertices = 0;
        uint64 NumBatches = 0;
        uint64 NumDraws = 0;
        uint64 NumTriangles = 0;          // Total triangles submitted
        uint64 NumInstances = 0;          // Total instances rendered
        uint64 NumMeshes = 0;             // Unique meshes
        uint64 NumMaterials = 0;          // Unique materials used
        uint64 NumDrawCallsCulled = 0;    // Draws culled by frustum/occlusion
        uint64 NumInstancesCulled = 0;    // Instances culled
        uint64 NumShadowDraws = 0;        // Shadow pass draws
        uint64 NumSkinnedMeshes = 0;      // Skinned vs static count
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
        uint8 bWireframe:1              = false;
        uint8 bDrawBillboards:1         = true;
        uint8 bUnlit:1                  = false;
        uint8 bLit:1                    = false;
    };
    
}