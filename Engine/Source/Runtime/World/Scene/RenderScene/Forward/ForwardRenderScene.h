#pragma once
#include <condition_variable>
#include "Core/Delegates/Delegate.h"
#include "Memory/Allocators/Allocator.h"
#include "Memory/SmartPtr.h"
#include "Renderer/BindingCache.h"
#include "Renderer/Vertex.h"
#include "World/Scene/RenderScene/EnvironmentRenderTypes.h"
#include "World/Scene/RenderScene/MeshDrawCommand.h"
#include "World/Scene/RenderScene/RenderScene.h"
#include "World/Scene/RenderScene/SceneCullContext.h"
#include "World/Entity/Components/PostProcessSettings.h"
#include "World/Subsystems/WorldSettings.h"


namespace Lumina
{
    template <typename T>
    using TFrameVector = TVector<T, FFrameArenaAllocator>;

    template <typename K, typename V>
    using TFrameHashMap = THashMap<K, V, eastl::hash<K>, eastl::equal_to<K>, FFrameArenaAllocator>;

    struct FLineBatcherComponent;
    struct SDirectionalLightComponent;
    struct SSpotLightComponent;
    struct SPointLightComponent;
    class CWorld;
    struct SStaticMeshComponent;
    struct SSkeletalMeshComponent;
    struct STransformComponent;
    struct STerrainComponent;
    class CMaterialInterface;

    /** Scene rendering via Clustered Forward Rendering. */
    class FForwardRenderScene : public IRenderScene
    {
    public:
        
        FForwardRenderScene(CWorld* InWorld);
        ~FForwardRenderScene() override = default;
        LE_NO_COPYMOVE(FForwardRenderScene);
        
        // Per-entity shared data. FProcessedDrawItem carries an EntityRecordIndex
        // into this table so Transform/Bounds aren't duplicated per surface.
        struct FEntityRecord
        {
            glm::mat4               Transform;
            glm::vec4               SphereBounds;
            uint64                  MeshletHeaderAddress;
            uint32                  CustomData;
            uint32                  EntityID;
            uint32                  LocalBoneOffset;  // ~0u for static meshes.
            uint32                  _Pad;
        };

        // Per-(entity, surface) item. Composed into an FGPUInstance during the
        // parallel write phase by reading the shared FEntityRecord.
        struct FProcessedDrawItem
        {
            uint32              EntityRecordIndex;
            uint32              SurfaceMeshletOffset;
            uint32              SurfaceMeshletCount;
            uint32              ShadowMeshletOffset;
            uint32              ShadowMeshletCount;
            EInstanceFlags      Flags;
            uint16              MaterialIndex;
            uint16              LocalBatchIndex;
            uint16              LocalDrawIndex;
            uint16              _Pad;
        };

        // Per-thread local batch table; worker linear-searches it to dedup draws.
        // Merge walks these, making it O(unique batches x threads), not O(instances).
        struct alignas(64) FLocalBatchEntry
        {
            FDrawBatchKey                       Key;
            FRHIVertexShader*                   VertexShader = nullptr;
            FRHIPixelShader*                    PixelShader  = nullptr;
            // Per-material depth-prepass / shadow VS. Null for non-WPO materials
            // -- renderer falls back to the global.
            FRHIVertexShader*                   DepthVertexShader  = nullptr;
            FRHIVertexShader*                   ShadowVertexShader = nullptr;
            TFrameVector<FDrawKey>              LocalDraws;
            TFrameVector<uint32>                LocalDrawCounts;
            TFrameVector<uint32>                LocalMeshletCounts;
            // O(1) lookup into LocalDraws.
            TFrameHashMap<FDrawKey, uint16>     DrawIndexByKey;
            uint32                              GlobalBatchIndex = ~0u;
            TFrameVector<uint32>                LocalToGlobalDraw;
            TFrameVector<uint32>                LocalDrawWriteBase;

            FLocalBatchEntry() = default;
            explicit FLocalBatchEntry(FFrameArenaAllocator A)
                : LocalDraws(A), LocalDrawCounts(A), LocalMeshletCounts(A)
                , DrawIndexByKey(A), LocalToGlobalDraw(A), LocalDrawWriteBase(A) {}
        };

        // Material-pure portion of a resolved draw slot, cached per-thread keyed
        // by material. Per-entity bits (CastShadow / bDrawInDepthPass) added in ResolveSlot.
        struct FCachedMaterialResolve
        {
            FRHIVertexShader*   VertexShader;
            FRHIPixelShader*    PixelShader;
            FRHIVertexShader*   DepthVertexShader;
            FRHIVertexShader*   ShadowVertexShader;
            uint64              MaterialID;
            uint16              MaterialIdx;
            bool                bTranslucent;
            bool                bMasked;
            bool                bAdditive;
            bool                bTwoSided;
            bool                bMaterialCastsShadows;
        };

        struct FMaterialCacheEntry
        {
            CMaterialInterface*     Key;
            FCachedMaterialResolve  Resolve;
        };

        struct alignas(64) FThreadLocalDrawData
        {
            TFrameVector<FProcessedDrawItem>    Items;
            TFrameVector<FEntityRecord>         EntityRecords;
            TFrameVector<FLocalBatchEntry>      LocalBatches;
            TFrameVector<glm::mat4>             BonesData;
            TFrameHashMap<class CMesh*, uint8>  PinnedMeshDedupe;
            // Heap-backed so refs survive the per-thread arena reset.
            TVector<FRHIBufferRef>              PinnedMeshBuffers;
            // Per-thread material resolve cache; linear-scanned (few unique materials per thread).
            TFrameVector<FMaterialCacheEntry>   MaterialCache;
            // Fast path for PinnedMeshDedupe: skip the hash for consecutive same-mesh entities.
            class CMesh*                        LastPinnedMesh = nullptr;
            FFrameArenaAllocator                Arena;
            FSceneRenderStats                   Stats = {};

            FThreadLocalDrawData() = default;
            explicit FThreadLocalDrawData(FFrameArenaAllocator A)
                : Items(A), EntityRecords(A), LocalBatches(A), BonesData(A), PinnedMeshDedupe(A)
                , MaterialCache(A), Arena(A) {}
            
            void ResetForFrame(FFrameArenaAllocator A)
            {
                Items            = TFrameVector<FProcessedDrawItem>(A);
                EntityRecords    = TFrameVector<FEntityRecord>(A);
                LocalBatches     = TFrameVector<FLocalBatchEntry>(A);
                BonesData        = TFrameVector<glm::mat4>(A);
                PinnedMeshDedupe = TFrameHashMap<CMesh*, uint8>(A);
                MaterialCache    = TFrameVector<FMaterialCacheEntry>(A);
                PinnedMeshBuffers.clear();
                LastPinnedMesh   = nullptr;
                Arena            = A;
                Stats            = {};
            }
        };

        // Shadow tile request captured during parallel light processing;
        // resolved + shrunk-to-fit by AllocateShadowTiles.
        struct FShadowRequest
        {
            uint32      LightIndex;
            ELightType  Type;
            uint32      DesiredPixels;
            float       DistanceToCamera;
            glm::vec3   Position;
            glm::vec3   Direction;      // Spot only
            glm::vec3   Up;             // Spot only
            float       Attenuation;
            float       OuterFOVDegrees;
        };

        // Per-frame extracted snapshot. FRAMES_IN_FLIGHT instances in FrameRing;
        // game thread writes one in Extract, render thread reads it in RenderView.
        struct FFrameData
        {
            FViewVolume                      ViewVolume = {};
            FSceneGlobalData                 SceneGlobalData = {};
            SDefaultWorldSettings            CachedWorldSettings = {};
            float                            CachedWorldDeltaTime = 0.0f;
            // False = shader compiler still warming up; RenderView no-ops.
            bool                             bExtractedThisFrame = false;

            TVector<FGPUInstance>            Instances;
            TVector<glm::mat4>               BonesData;
            TVector<FMeshDrawCommand>        DrawCommands;
            TVector<uint32>                  OpaqueDrawList;
            TVector<uint32>                  OpaqueOccluderDrawList;
            TVector<uint32>                  TranslucentDrawList;
            TVector<FRHIBufferRef>           PinnedMeshBuffersThisFrame;
            FSceneCullContext                SceneCullContext;

            TVector<FCullView>               CullViews;
            TVector<FDrawIndirectArguments>  IndirectArgs;
            TVector<uint32>                  DrawMeshletStartOffsets;
            TVector<uint32>                  InstanceMeshletPrefix;
            uint32                           TotalMeshletBound   = 0;
            uint32                           NumDrawsPerView     = 0;
            uint32                           CameraLateViewIndex = ~0u;
            uint32                           CascadeViewBase     = ~0u;
            TVector<uint32>                  PointShadowCullViewBases;
            TVector<uint32>                  SpotShadowCullViewBases;

            FSceneLightData                  LightData = {};
            TArray<TVector<FLightShadow>, (uint32)ELightType::Num> PackedShadows = {};
            TAtomic<uint32>                  ShadowDataCount = 0;
            TVector<FShadowRequest>          ShadowRequests;
            FMutex                           ShadowRequestMutex;
            // Per-slot copy of FShadowAtlas::Tiles -- render passes index this.
            TVector<FShadowTile>             AtlasTiles;

            FEnvironmentParams               EnvironmentParams = {};
            FRHIImage*                       EnvironmentMapImage = nullptr;
            bool                             bIBLDirty            = false;
            bool                             bIBLConvolutionDirty = false;
            SPostProcessSettings             ActivePostProcessStorage = {};
            bool                             bHasActivePostProcess = false;
            TVector<CMaterialInterface*>     ActivePostProcessMaterials;

            TVector<FSimpleElementVertex>    SimpleVertices;
            TVector<FLineBatch>              LineBatches;
            TVector<FBillboardInstance>      BillboardInstances;

            FSceneRenderStats                FrameStats = {};
        };

        enum class ENamedBuffer : uint8
        {
            Scene,
            Light,
            // Unified per-instance descriptor. Bound at binding 2.
            Instance,
            Bone,
            Cluster,
            SimpleVertex,
            Billboards,
            // Per-view cull descriptors; one per logical render view
            // (main camera, each CSM cascade, each point face, each spot).
            CullView,
            // Shared meshlet draw list, NumViews * TotalMeshletBound; each view owns a
            // slice. Ringed per frame-in-flight: use GetMeshletDrawList(), not GetNamedBuffer.
            MeshletDrawList,
            // Shared indirect draw args (NumViews * NumDraws); per-view range.
            // Ringed per frame-in-flight: use GetIndirectArgs(), not GetNamedBuffer.
            IndirectArgs,
            // Two-pass cull defer list: phase 0 appends prev-frame-Hi-Z rejects, phase 1
            // re-tests them. Ringed per frame-in-flight: use GetMeshletDeferList().
            MeshletDeferList,
            // Atomic counter paired with MeshletDeferList; reset per frame by ResetPass.
            // Ringed per frame-in-flight: use GetDeferCount(), not GetNamedBuffer.
            DeferCount,
            // SPD atomic counter; last phase-1 workgroup runs phase 2.
            // Ringed per frame-in-flight: use GetSpdCounter(), not GetNamedBuffer.
            SpdCounter,
            // Per-frame environment params read by Environment.slang at binding (2, 0).
            Environment,
            // Inclusive prefix sum of per-instance SurfaceMeshletCount, sized [InstanceNum+1].
            // Cull binary-searches it to recover (InstanceID, MeshletLocalIdx) from a flat dispatch.
            InstanceMeshletPrefix,
            // Color grading + tone mapping constants. 144 B exceeds AMD RDNA's 128 B
            // push-constant cap, so it lives in a UBO at (set 2, binding 2).
            ColorGrading,

            Num,
        };
        
        enum class ENamedImage : uint8
        {
            HDR,
            LDR,
            // Ping-pong scratch for the post-process material chain; same size/format
            // as LDR. Pass alternates LDR/this so each material reads the previous output.
            PostProcessScratch,
            SMAAEdges,
            SMAABlend,
            SMAAArea,
            SMAASearch,
            Cascade,
            DepthAttachment,
            DepthPyramid,
            Picker,
            Accum,
            Revealage,

            // Half-res volumetric scattering buffer; depth-aware bilateral upsample
            // composites it into HDR. Half-res cuts ray-march cost 4x.
            VolumetricHalfRes,

            // MSAA scratch RTs, allocated only when MSAASampleCount > 1. Geometry passes
            // write here and resolve into the matching 1x image at end-of-pass.
            HDR_MS,
            Depth_MS,
            Picker_MS,

            // Pre-integrated BRDF LUT for split-sum IBL (Karis 2013). Baked once at
            // scene init; swapchain-independent, so outside the InitImages rebuild path.
            BRDFLut,

            // Sky cubemap from SkyCubeCapture.slang, feeding IBL convolution. Persistent
            // image (decoupled from swapchain) but contents refresh each frame sky is on.
            SkyCube,

            // Diffuse-BRDF-convolved sky; sampled by normal N for IBL diffuse.
            SkyIrradiance,

            // GGX-prefiltered sky, one mip per roughness step; sampled by reflection R.
            SkyPrefilter,

            #if USING(WITH_EDITOR)
            PointLightIcon,
            DirectionalLightIcon,
            SkyLightIcon,
            SpotLightIcon,
            CameraIcon,
            CharacterIcon,
            ParticleSystemIcon,
            #endif
            
            Num,
        };
        
        void Init() override;
        void Shutdown() override;
        
        void BeginFrame() override { }
        void EndFrame() override { }
        
        void Extract(const FViewVolume& ViewVolume, const SPostProcessSettings* PostProcess) override;
        void RenderView_RenderThread(ICommandList& CmdList, uint8 FrameIndex) override;
        void SignalFrameConsumed(uint8 FrameIndex) override;
        void SetActivePostProcessMaterials(const TVector<CMaterialInterface*>& Materials) override { PendingPostProcessMaterials = Materials; }
        void SwapchainResized(glm::vec2 NewSize);
        void Resize(const glm::uvec2& NewSize) override { SwapchainResized(glm::vec2(NewSize)); }
        
        void DrawBillboard(FRHIImage* Image, const glm::vec3& Location, float Scale) override;
        void DrawLine(const glm::vec3& Start, const glm::vec3& End, const glm::vec4& Color, float Thickness, bool bDepthTest, float Duration) override { }

        static FViewportState MakeViewportStateFromImage(const FRHIImage* Image);
        
        FRHIBuffer* GetNamedBuffer(ENamedBuffer Buffer) const { return NamedBuffers[(int)Buffer]; }
        FRHIImage* GetNamedImage(ENamedImage Image) const { return NamedImages[(int)Image];}

        // Ringed accessors for the cull-pass scratch (see IndirectArgsRing).
        FRHIBuffer* GetIndirectArgs()     const { return IndirectArgsRing[CurrentFrameSlot]; }
        FRHIBuffer* GetMeshletDrawList()  const { return MeshletDrawListRing[CurrentFrameSlot]; }
        FRHIBuffer* GetMeshletDeferList() const { return MeshletDeferListRing[CurrentFrameSlot]; }
        FRHIBuffer* GetDeferCount()       const { return DeferCountRing[CurrentFrameSlot]; }
        FRHIBuffer* GetSpdCounter()       const { return SpdCounterRing[CurrentFrameSlot]; }

        // MSAA scratch RT when enabled, else the 1x image; use for the render-target
        // binding on geometry passes that participate in MSAA. 1x image is the resolve target.
        FRHIImage* GetSceneColorRT() const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::HDR_MS) : GetNamedImage(ENamedImage::HDR); }
        FRHIImage* GetSceneDepthRT() const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::Depth_MS) : GetNamedImage(ENamedImage::DepthAttachment); }
        FRHIImage* GetPickerRT()     const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::Picker_MS) : GetNamedImage(ENamedImage::Picker); }

        /** Resolve target — null when MSAA off (no resolve needed). Caller adds via FAttachment::SetResolveImage. */
        FRHIImage* GetSceneColorResolve() const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::HDR) : nullptr; }
        FRHIImage* GetSceneDepthResolve() const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::DepthAttachment) : nullptr; }
        FRHIImage* GetPickerResolve()     const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::Picker) : nullptr; }

        uint8 GetMSAASampleCount() const { return MSAASampleCount; }
        
        FRHIImage* GetRenderTarget() const override;
        const FSceneRenderStats& GetRenderStats() const override;
        FSceneRenderSettings& GetSceneRenderSettings() override;
        entt::entity GetEntityAtPixel(uint32 X, uint32 Y) const override;
        void SetPickerCursor(uint32 X, uint32 Y, bool bOverViewport) override;
        const FShadowAtlas* GetShadowAtlas() const override { return &ShadowAtlas; }
        
        
    private:
        
        void InitBuffers();
        void InitImages();
        void InitFrameResources();
        void CreateLayouts();
        
        void InitBRDFLUT();
        
        void InitSkyCube();
        
        void InitIBLConvolutionTargets();
        
        //~ Begin Render Passes
        // Game thread: clear per-frame CPU scene state before the parallel gather repopulates.
        void ResetPass_GameThread();

        // Render thread: depth + shadow atlas clears recorded onto the cmd list.
        void ResetPass_RenderThread(ICommandList& CmdList);
        
        void CullPassEarly(ICommandList& CmdList);
        void CullPassLate(ICommandList& CmdList);

        
        void DepthPrePassEarly(ICommandList& CmdList);
        void DepthPrePassLate(ICommandList& CmdList);
        void DepthPyramidPass(ICommandList& CmdList);
        void ClusterBuildPass(ICommandList& CmdList);
        void LightCullPass(ICommandList& CmdList);
        void PointShadowPass(ICommandList& CmdList);
        void SpotShadowPass(ICommandList& CmdList);
        void CascadedShowPass(ICommandList& CmdList);
        void BasePass(ICommandList& CmdList);
        void BillboardPass(ICommandList& CmdList);

        uint32 ParticleSimulatePass(ICommandList& CmdList);
        
        void ParticleRenderPass(ICommandList& CmdList);
        void TerrainUpdatePass(ICommandList& CmdList);

        void TerrainCullPass(ICommandList& CmdList);
        void TerrainRenderPass(ICommandList& CmdList);
        void TransparentPass(ICommandList& CmdList);
        void OITResolvePass(ICommandList& CmdList);
        void VolumetricLightingPass(ICommandList& CmdList);
        void EnvironmentPass(ICommandList& CmdList);
        void SkyCubeCapturePass(ICommandList& CmdList);
        void IrradianceConvolutionPass(ICommandList& CmdList);
        void PrefilterEnvMapPass(ICommandList& CmdList);
        void BatchedLineDraw(ICommandList& CmdList);
        void BloomPass(ICommandList& CmdList);
        void ToneMappingPass(ICommandList& CmdList);
        void PostProcessMaterialPass(ICommandList& CmdList);
        void SMAAEdgeDetectionPass(ICommandList& CmdList);
        void SMAABlendWeightPass(ICommandList& CmdList);
        void SMAANeighborhoodBlendPass(ICommandList& CmdList);
        //~ End Render Passes

        // Game-thread half: ECS reads + parallel Process* tasks + cull/shadow setup.
        void CompileDrawCommands_GameThread();

        // Render-thread half: buffer resize + WriteBuffer commands; reads game-thread state.
        void CompileDrawCommands_RenderThread(ICommandList& CmdList);

        void ProcessStaticMeshEntityInternal(entt::entity Entity, const SStaticMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local);
        void ProcessSkeletalMeshEntityInternal(entt::entity Entity, const SSkeletalMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local);

        // Populates SceneCullContext with camera/shadow frustums and per-light spheres.
        // Runs serially before the parallel gather so the context is immutable during it.
        void BuildSceneCullContext();
        void MergeMeshDrawData(TVector<FThreadLocalDrawData>& ThreadLocal);
        
        void ProcessPointLight(const SPointLightComponent& PointLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount);
        void ProcessSpotLight(const SSpotLightComponent& SpotLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount);
        void ProcessDirectionalLight(const SDirectionalLightComponent& DirectionalLight, TAtomic<uint32>& LightCount);

        // Serial post-pass: fits shadow requests into the atlas budget (shrinks largest
        // tiles until sum(area) <= capacity), then allocates tiles + PackedShadows.
        void AllocateShadowTiles();

        // Builds the per-frame FCullView array and seeds IndirectArgs per view. Runs
        // after AllocateShadowTiles (ViewProjection filled) and before the buffer upload.
        void BuildCullViews(const FViewVolume& ViewVolume);
        
        void ProcessBatchedLines(FLineBatcherComponent& Batcher);
        
        void NotifyMaxLightsHit();

        // CPU early-out for shadow requests: false when the light's attenuation sphere
        // is fully outside the camera frustum, skipping its shadow view/tile/slice.
        bool ShouldRequestShadow(const glm::vec3& LightPosition, float LightRadius) const;


    private:
        
        TArray<FRHIBufferRef, (int)ENamedBuffer::Num>   NamedBuffers = {};
        TArray<FRHIImageRef, (int)ENamedImage::Num>     NamedImages = {};

        /** MSAA sample count cached from world settings. 1 == disabled (no overhead). */
        uint8                                           MSAASampleCount = 1;

        /** Allocate the MS-only scratch images (HDR_MS, Depth_MS, Picker_MS). No-op when MSAA is off. */
        void AllocateMSAAImages(const glm::uvec2& Extent);

        /** Reconcile cached sample count with the world setting; reallocates MS images when it changes. */
        void SyncMSAAState();

        // Bloom mip chain (one image, BLOOM_MIP_COUNT mips). SPD downsample writes
        // mips 0..N-1 from HDR; upsample walks back additively. Tone-mapping samples mip 0.
        static constexpr uint32                         BLOOM_MIP_COUNT = 5;
        FRHIImageRef                                    BloomChain;
        
        FViewportState                          SceneViewportState;
        FDelegateHandle                         SwapchainResizedHandle;
        CWorld*                                 World = nullptr;

        // Mirror of last-rendered frame's FrameStats; rendered immediately
        // visible to GetRenderStats() callers (editor UI panels).
        FSceneRenderStats                       RenderStats;
        FSceneRenderSettings                    RenderSettings;

        // IBL change-tracking: persistent SkyCube/Irradiance/Prefilter rebuilt only when
        // these last-baked inputs change. bIBLConvolutionDirty gates the costly convolution.
        FEnvironmentParams                      LastIBLEnvironmentParams = {};
        FRHIImage*                              LastIBLEnvironmentMap    = nullptr;
        glm::vec3                               LastIBLSunDirection      = glm::vec3(0.0f);
        bool                                    bLastIBLHasSun           = false;
        bool                                    bIBLValid                = false;

        FEnvironmentParams                      LastConvolvedEnvironmentParams = {};
        FRHIImage*                              LastConvolvedEnvironmentMap    = nullptr;
        glm::vec3                               LastConvolvedSunDirection      = glm::vec3(0.0f);
        bool                                    bLastConvolvedHasSun           = false;
        bool                                    bIBLConvolutionValid           = false;

        // Mirrors last-uploaded EnvironmentParams; a memcmp gate skips the WriteBuffer
        // for the common static-environment case.
        FEnvironmentParams                      LastUploadedEnvironmentParams = {};
        bool                                    bEnvironmentParamsUploaded    = false;

        // Cluster-grid change tracking: view-space AABBs depend only on projection +
        // RT size, so the pass is skipped while those are unchanged.
        glm::mat4                               LastClusterInvProjection = glm::mat4(0.0f);
        glm::vec2                               LastClusterNearFar       = glm::vec2(0.0f);
        glm::uvec2                              LastClusterScreenSize    = glm::uvec2(0);
        bool                                    bClusterGridDirty        = true;
        
        FBindingCache                           BindingCache;

        FRHIViewportRef                         SceneViewport;

        // Latest post-process material list from the world; captured into
        // FFrameData::ActivePostProcessMaterials at the start of Extract.
        TVector<CMaterialInterface*>            PendingPostProcessMaterials;

        // Non-owning view of SceneBindingSets[CurrentFrameSlot], refreshed at the top of
        // RenderView; bind sites read through it so they don't need to know about ringing.
        FRHIBindingSet*                                                 SceneBindingSet = nullptr;
        TArray<FRHIBindingSetRef, FRAMES_IN_FLIGHT>                     SceneBindingSets = {};
        FRHIBindingLayoutRef                                            SceneBindingLayout;

        // Read-only twin of SceneBindingSet (shared layout) binding GPU-written buffers as
        // SRV; read-only passes use it so the tracker skips the per-pass UAV barrier.
        FRHIBindingSet*                                                 SceneBindingSetReadOnly = nullptr;
        TArray<FRHIBindingSetRef, FRAMES_IN_FLIGHT>                     SceneBindingSetReadOnlys = {};

        // GPU-atomic-written by CullMeshlets, consumed by DrawIndirect, so it can't be
        // BUF_Dynamic (no dynamic offset to vkCmdDrawIndirect). Manual N-buffer ring.
        TArray<FRHIBufferRef, FRAMES_IN_FLIGHT>                         IndirectArgsRing = {};

        // Cull-pass scratch, ringed like IndirectArgsRing so frame N+1's cull overlaps
        // frame N's draws instead of stalling on a WAR barrier on one shared buffer.
        TArray<FRHIBufferRef, FRAMES_IN_FLIGHT>                         MeshletDrawListRing = {};
        TArray<FRHIBufferRef, FRAMES_IN_FLIGHT>                         MeshletDeferListRing = {};
        TArray<FRHIBufferRef, FRAMES_IN_FLIGHT>                         DeferCountRing = {};
        TArray<FRHIBufferRef, FRAMES_IN_FLIGHT>                         SpdCounterRing = {};

        // Frame slot index for ringed scene resources. Set at the top of
        // RenderView_RenderThread from GRenderManager->GetCurrentFrameIndex().
        uint8                                                           CurrentFrameSlot = 0;

        // Set 3 -- shadow textures bound only by passes that SAMPLE shadows. Kept out of
        // SceneBindingSet so shadow passes don't shader-read an image they're about to write.
        FRHIBindingSetRef                       ShadowSamplingBindingSet;
        FRHIBindingLayoutRef                    ShadowSamplingBindingLayout;

        // Placeholder for set 2 in pipelines that use set 3 but no set 2 (BasePass,
        // TransparentPass). Vulkan requires pSetLayouts contiguous to the highest set.
        FRHIBindingSetRef                       EmptySet2BindingSet;
        FRHIBindingLayoutRef                    EmptySet2Layout;


        FRHIBindingSetRef                       ComposeBindingSet;
        FRHIBindingLayoutRef                    ComposeBindingLayout;

        FRHIBindingSetRef                       SMAAEdgeBindingSet;
        FRHIBindingLayoutRef                    SMAAEdgeBindingLayout;

        FRHIBindingSetRef                       SMAABlendWeightBindingSet;
        FRHIBindingLayoutRef                    SMAABlendWeightBindingLayout;

        FRHIBindingSetRef                       SMAANeighborhoodBindingSet;
        FRHIBindingLayoutRef                    SMAANeighborhoodBindingLayout;

        FRHIBindingSetRef                       OITBindingSet;
        FRHIBindingLayoutRef                    OITBindingLayout;
        
        FRHIInputLayoutRef                      SimpleVertexLayoutInput;

        FShadowAtlas                            ShadowAtlas;

        // One bump arena per worker thread, reset each frame; backs all per-frame
        // TFrameVector / TFrameHashMap members on FThreadLocalDrawData.
        TVector<TUniquePtr<FBlockLinearAllocator>> FrameArenas;

        // MergeMeshDrawData scratch, persisted across frames (grown, never shrunk);
        // the active prefix is overwritten in place each frame.
        TVector<FDrawBatchKey>                  MergeGlobalBatchKeys;
        TVector<TVector<FLocalBatchEntry*>>     MergeBatchToLocals;
        TVector<TVector<FDrawKey>>              MergeGlobalDrawsPerBatch;
        TVector<uint32>                         MergeBatchDrawArgBase;
        TVector<uint32>                         MergeDrawInstanceCounts;
        TVector<uint32>                         MergeMeshletCountsPerDraw;
        TVector<uint32>                         MergeDrawInstanceOffsets;
        TVector<uint32>                         MergeDrawCursor;
        TVector<uint32>                         MergeThreadBoneBase;

        // Per-worker draw-gather scratch, persisted so outer storage keeps capacity;
        // arena-backed members are reset each frame (ResetForFrame) to avoid aliasing.
        TVector<FThreadLocalDrawData>           ThreadLocalStorage;

        TArray<FFrameData,      FRAMES_IN_FLIGHT>       FrameRing;
        TArray<TAtomic<uint64>, FRAMES_IN_FLIGHT>       SlotConsumedCount;
        TArray<uint64,          FRAMES_IN_FLIGHT>       SlotProducedCount = {};
        TArray<TAtomic<bool>,   FRAMES_IN_FLIGHT>       SlotHasPendingConsume = {};
        FMutex                                          SlotMutex;
        std::condition_variable                         SlotCV;

        FFrameData*                             ExtractFrame = nullptr;  // game thread
        FFrameData*                             RenderFrame  = nullptr;  // render thread

        void WaitForSlotConsumed(uint8 Slot, uint64 Target);
        void SignalSlotConsumed(uint8 Slot);

#if USING(WITH_EDITOR)
        static constexpr uint32                 PickerReadbackRingSize = FRAMES_IN_FLIGHT + 1;
        // Side of the square copied around the cursor; large enough to still contain
        // the click pixel after the few-frame cursor drift before readback consumes it.
        static constexpr uint32                 PickerRegionExtent = 64;
        struct FPickerReadbackSlot
        {
            FRHIStagingImageRef Staging;
            uint32              OriginX = 0;        // top-left of the copied region, in picker texels
            uint32              OriginY = 0;
            uint32              Width = 0;          // region dimensions == staging image extent
            uint32              Height = 0;
            uint64              SubmittedFrame = 0;
            bool                bPending = false;
        };
        mutable TArray<FPickerReadbackSlot, PickerReadbackRingSize> PickerReadbackRing;
        uint64                                  PickerReadbackFrame = 0;
        uint32                                  PickerReadbackWriteIndex = 0;

        // Pick-cursor published by editor, read by readback. Packed into one atomic to
        // avoid torn reads: bit 0 = over-viewport, bits 1..21 = X, bits 22..42 = Y.
        TAtomic<uint64>                         PickerCursorPacked = 0;

        // Schedules the per-frame picker -> staging copy after the last picker RT write.
        void IssuePickerReadback(ICommandList& CmdList);
#endif

    };
}
