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

    /**
     * Scene rendering via Clustered Forward Rendering.
     */
    class FForwardRenderScene : public IRenderScene
    {
    public:
        
        FForwardRenderScene(CWorld* InWorld);
        ~FForwardRenderScene() override = default;
        LE_NO_COPYMOVE(FForwardRenderScene);
        
        /**
         * Per-entity shared data. One record per processed entity; surface-level
         * FProcessedDrawItem entries carry an EntityRecordIndex into this table
         * so we don't duplicate Transform/Bounds/etc. for every surface.
         */
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

        /**
         * Per-(entity, surface) item. Composed into a full FGPUInstance during
         * the parallel write phase by reading the shared FEntityRecord.
         */
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

        /**
         * Per-thread local batch table. The worker linear-searches this small list to dedup
         * draws against batches it has already seen. The merge phase walks these instead of
         * walking every item, which is what makes the merge O(unique batches × threads)
         * rather than O(num instances).
         */
        struct alignas(64) FLocalBatchEntry
        {
            FDrawBatchKey                       Key;
            FRHIVertexShader*                   VertexShader = nullptr;
            FRHIPixelShader*                    PixelShader  = nullptr;
            // Per-material depth-prepass / shadow vertex shaders. Null for
            // non-WPO materials -- the renderer falls back to the global.
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

        struct alignas(64) FThreadLocalDrawData
        {
            TFrameVector<FProcessedDrawItem>    Items;
            TFrameVector<FEntityRecord>         EntityRecords;
            TFrameVector<FLocalBatchEntry>      LocalBatches;
            TFrameVector<glm::mat4>             BonesData;
            TFrameHashMap<class CMesh*, uint8>  PinnedMeshDedupe;
            // Heap-backed so refs survive the per-thread arena reset.
            TVector<FRHIBufferRef>              PinnedMeshBuffers;
            FFrameArenaAllocator                Arena;
            FSceneRenderStats                   Stats = {};

            FThreadLocalDrawData() = default;
            explicit FThreadLocalDrawData(FFrameArenaAllocator A)
                : Items(A), EntityRecords(A), LocalBatches(A), BonesData(A), PinnedMeshDedupe(A), Arena(A) {}
        };

        // Pending shadow tile request captured during parallel light processing;
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

        // Per-frame extracted snapshot. FRAMES_IN_FLIGHT instances live in
        // FrameRing; game thread writes one during Extract, render thread reads
        // the matching one during RenderView.
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
            // Per-view cull descriptors. One entry per logical render view
            // (main camera, each CSM cascade, each point face, each spot).
            CullView,
            // Shared meshlet draw list. Sized NumViews * TotalMeshletBound;
            // each view owns a slice addressed by FCullView.DrawListOffset.
            // NumViews includes the camera-late phase view, so a camera
            // meshlet deferred by phase 1 lands in its own disjoint slice.
            MeshletDrawList,
            // Shared indirect draw args (NumViews * NumDraws slots); each
            // view addresses its own range via FCullView.IndirectArgsOffset.
            IndirectArgs,
            // Two-pass culling defer list. Phase 0 appends camera meshlets
            // rejected by the *previous-frame* Hi-Z; phase 1 pops them and
            // re-tests against the rebuilt current-frame Hi-Z. Sized for the
            // worst case (every camera meshlet occluded).
            MeshletDeferList,
            // Single-uint atomic counter paired with MeshletDeferList. Reset
            // to zero every frame by ResetPass.
            DeferCount,
            // Single-uint atomic counter for the Single-Pass Downsampler that
            // builds the depth pyramid. Each phase-1 SPD workgroup increments
            // it; the last one to do so runs phase 2. FillBuffer zeros it at
            // the top of DepthPyramidPass; phase 2 also resets it before the
            // dispatch ends, so a single zero either way keeps it clean.
            SpdCounter,
            // Per-frame environment params (sky mode, gradient/dynamic
            // colors, sun cosmetics, exposure). Read by Environment.slang
            // at binding (2, 0); written once per frame from the active
            // SEnvironmentComponent.
            Environment,
            // Inclusive prefix sum of per-instance SurfaceMeshletCount.
            // Sized [InstanceNum + 1]; entry [i] is the running total of
            // meshlets across instances [0..i). The cull pass dispatches a
            // flat thread-per-meshlet layout sized to TotalMeshletBound and
            // binary-searches this buffer to recover (InstanceID,
            // MeshletLocalIdx) without the old per-instance over-dispatch
            // and thread-bounds early-outs.
            InstanceMeshletPrefix,
            // Color grading + tone mapping constants. 144 B exceeds AMD RDNA's
            // 128 B push-constant cap, so the block lives in a UBO bound at
            // (set 2, binding 2) for the tone-mapping pass.
            ColorGrading,

            Num,
        };
        
        enum class ENamedImage : uint8
        {
            HDR,
            LDR,
            // Ping-pong scratch for the post-process material chain. Same
            // size and format as LDR; the pass alternates between LDR and
            // this image, sampling one and writing the other so each
            // material reads the previous output. Allocated even when no
            // post-process materials are bound -- it's cheap and keeps the
            // resize path simple.
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

            // Half-res scattering buffer for volumetric lighting. The march
            // pass writes here; a depth-aware bilateral upsample composites
            // the result into HDR. Half-res cuts the per-pixel ray-march cost
            // by 4x and the bandwidth by another factor via R11G11B10_FLOAT.
            VolumetricHalfRes,

            // MSAA scratch render targets. Allocated only when MSAASampleCount > 1.
            // Geometry passes write into these and resolve into the matching 1x
            // image (HDR / DepthAttachment / Picker) at end-of-pass.
            HDR_MS,
            Depth_MS,
            Picker_MS,

            // Pre-integrated BRDF LUT for the split-sum IBL approximation
            // (Karis 2013). Baked once at scene init by BRDFIntegration.slang
            // and never regenerated -- it is independent of swapchain size,
            // sky state, and any per-frame data, so it lives outside the
            // InitImages / SwapchainResized rebuild path.
            BRDFLut,

            // Sky captured into a cubemap by SkyCubeCapture.slang, fed into
            // the irradiance + prefilter convolution to drive IBL ambient.
            // Persistent image (size constant, decoupled from swapchain), but
            // the *contents* are refreshed per frame the sky is enabled.
            SkyCube,

            // Diffuse-BRDF-convolved sky (cosine-weighted hemisphere
            // integration). Sampled by surface normal N at runtime to
            // compute IBL diffuse: kD * Albedo * SkyIrradiance(N) * AO.
            SkyIrradiance,

            // GGX-prefiltered sky with one mip per roughness step. Sampled
            // by reflection vector R at mip = Roughness * (NumMips - 1) and
            // combined with the BRDF LUT for IBL specular.
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

        // Ringed accessor for IndirectArgs (see IndirectArgsRing).
        FRHIBuffer* GetIndirectArgs() const { return IndirectArgsRing[CurrentFrameSlot]; }

        /** Returns the MSAA scratch RT when MSAA is enabled, otherwise the 1x image.
         *  Use these for the *render target* binding on geometry passes that should
         *  participate in MSAA. The 1x image is the resolve target. */
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
        // Game thread: clear per-frame CPU scene state (Instances, LightData,
        // DrawCommands, ...). Must run before the parallel ECS gather repopulates.
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
        // Populates Instances, LightData, EnvironmentParams, CullViews, etc.
        void CompileDrawCommands_GameThread();

        // Render-thread half: buffer resize + WriteBuffer commands. Reads the
        // state populated by the matching CompileDrawCommands_GameThread call.
        void CompileDrawCommands_RenderThread(ICommandList& CmdList);

        void ProcessStaticMeshEntityInternal(entt::entity Entity, const SStaticMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local);
        void ProcessSkeletalMeshEntityInternal(entt::entity Entity, const SSkeletalMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local);

        /**
         * Populates SceneCullContext with the main camera frustum, the sun-
         * swept shadow frustum (if a directional light is present), and one
         * sphere per shadow-casting point / spot light. Runs serially before
         * the parallel mesh gather so the context is immutable during the
         * parallel phase.
         *
         * Thread-safe reads of ECS components: called on the render thread
         * while no ECS mutation is in flight.
         */
        void BuildSceneCullContext();
        void MergeMeshDrawData(TVector<FThreadLocalDrawData>& ThreadLocal);
        
        void ProcessPointLight(const SPointLightComponent& PointLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount);
        void ProcessSpotLight(const SSpotLightComponent& SpotLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount);
        void ProcessDirectionalLight(const SDirectionalLightComponent& DirectionalLight, TAtomic<uint32>& LightCount);

        /**
         * Serial post-pass: fits every accumulated shadow request into the atlas
         * budget (shrinks the largest desired tiles until sum(area) <= capacity) then
         * allocates tiles and populates FLightShadowData / PackedShadows. Runs
         * after the parallel Process*Light tasks have written into ShadowRequests.
         */
        void AllocateShadowTiles();

        /**
         * Builds the per-frame FCullView array and seeds IndirectArgs for every
         * view's indirect-draw slice. Runs after AllocateShadowTiles (so every
         * shadow-casting light has its ViewProjection[6] filled) and before the
         * scene buffer upload so CullMeshlets has everything it needs.
         */
        void BuildCullViews(const FViewVolume& ViewVolume);
        
        void ProcessBatchedLines(FLineBatcherComponent& Batcher);
        
        void NotifyMaxLightsHit();

        /**
         * CPU-side early-out for shadow requests. Returns false when the
         * light's attenuation sphere falls entirely outside the camera
         * frustum, skipping the shadow view, atlas tile, and draw-list slice.
         */
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

        // Bloom mip chain. Single image with NumMips = BLOOM_MIP_COUNT mips.
        // Mip 0 is half-res of HDR; each successive mip halves again. The
        // downsample chain (single SPD compute dispatch) writes mips 0..N-1
        // from HDR; the upsample chain (one compute dispatch per mip step)
        // walks mip N-1 back to mip 0 with additive accumulation in-shader.
        // Tone-mapping samples mip 0.
        static constexpr uint32                         BLOOM_MIP_COUNT = 5;
        FRHIImageRef                                    BloomChain;
        
        FViewportState                          SceneViewportState;
        FDelegateHandle                         SwapchainResizedHandle;
        CWorld*                                 World = nullptr;

        // Mirror of last-rendered frame's FrameStats; rendered immediately
        // visible to GetRenderStats() callers (editor UI panels).
        FSceneRenderStats                       RenderStats;
        FSceneRenderSettings                    RenderSettings;

        // IBL change-tracking. SkyCube / Irradiance / Prefilter outputs
        // are persistent across frames and only need to be rebuilt when
        // their inputs actually change. The snapshot fields hold what
        // the IBL output was last baked from; we re-bake when any of
        // them differs. bIBLValid = false forces the first-frame bake.
        //
        // bIBLDirty drives the sky-cube capture and tracks every sun delta
        // so the visible sky stays smooth under animated time-of-day.
        //
        // bIBLConvolutionDirty drives the irradiance + GGX prefilter passes,
        // which are far more expensive (256 GGX samples per texel per mip,
        // 5 mips). It only fires when the sun has moved by more than
        // GIBLConvolutionSunCosThreshold or when env params / HDRI change,
        // so continuous TOD animation does not pay full convolution cost
        // every frame.
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

        // Mirrors EnvironmentParams from the last frame we uploaded. The
        // env CB is 96 B but uploaded every frame through
        // CompileDrawCommands; gating on a memcmp saves the WriteBuffer
        // for the (common) case of a static environment.
        FEnvironmentParams                      LastUploadedEnvironmentParams = {};
        bool                                    bEnvironmentParamsUploaded    = false;

        // Cluster-grid change tracking. The cluster AABBs ClusterBuildPass
        // produces live in view space and depend only on the projection and
        // the render-target size -- not the camera transform -- so the pass
        // is skipped while those are unchanged. bClusterGridDirty forces a
        // rebuild on the first frame and after a buffer reallocation.
        glm::mat4                               LastClusterInvProjection = glm::mat4(0.0f);
        glm::vec2                               LastClusterNearFar       = glm::vec2(0.0f);
        glm::uvec2                              LastClusterScreenSize    = glm::uvec2(0);
        bool                                    bClusterGridDirty        = true;
        
        FBindingCache                           BindingCache;

        FRHIViewportRef                         SceneViewport;

        // Latest post-process material list handed by the world. Captured into
        // FFrameData::ActivePostProcessMaterials at the start of Extract; not
        // read by render passes directly.
        TVector<CMaterialInterface*>            PendingPostProcessMaterials;

        // SceneBindingSet is a non-owning view of SceneBindingSets[CurrentFrameSlot],
        // refreshed at the top of RenderView_RenderThread. Every per-pass bind site
        // reads through this pointer so call sites don't need to know about ringing.
        // Refs live in SceneBindingSets; the raw pointer here is safe for the duration
        // of the render frame because the ring keeps them alive.
        FRHIBindingSet*                                                 SceneBindingSet = nullptr;
        TArray<FRHIBindingSetRef, FRAMES_IN_FLIGHT>                     SceneBindingSets = {};
        FRHIBindingLayoutRef                                            SceneBindingLayout;

        // IndirectArgs is GPU-atomic-written by CullMeshlets and consumed by
        // DrawIndirect calls, so it can't live in BUF_Dynamic (the bind path
        // doesn't carry dynamic offsets to vkCmdDrawIndirect). Manual N-buffer:
        // GetIndirectArgs() returns the slot bound by the current frame's
        // SceneBindingSets entry.
        TArray<FRHIBufferRef, FRAMES_IN_FLIGHT>                         IndirectArgsRing = {};

        // Frame slot index for ringed scene resources. Set at the top of
        // RenderView_RenderThread from GRenderManager->GetCurrentFrameIndex().
        uint8                                                           CurrentFrameSlot = 0;

        // Set 3 — shadow textures bound only by passes that SAMPLE shadows. Kept
        // out of SceneBindingSet so the shadow rendering passes (Point/Spot/
        // Cascaded) don't trigger a shader-read state transition on the same
        // image they're about to write as a depth attachment.
        FRHIBindingSetRef                       ShadowSamplingBindingSet;
        FRHIBindingLayoutRef                    ShadowSamplingBindingLayout;

        // Empty layout/set used as a contiguous-pipeline-layout placeholder for set 2
        // in pipelines that use set 3 (shadow sampling) but no set 2 (BasePass and
        // TransparentPass). Vulkan requires pSetLayouts to be contiguous up to the
        // highest used set index.
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

        // One bump arena per worker thread. Lifetime is the scene; reset at
        // the start of every BuildPerFrameRenderData. Backs all per-frame
        // TFrameVector / TFrameHashMap members on FThreadLocalDrawData.
        TVector<TUniquePtr<FBlockLinearAllocator>> FrameArenas;

        // MergeMeshDrawData scratch. Persisted across frames so capacity is
        // reused; resized up only, never shrunk. Each frame the active prefix
        // [0..NumBatches) / [0..TotalDrawArgs) is overwritten in place.
        TVector<FDrawBatchKey>                  MergeGlobalBatchKeys;
        TVector<TVector<FLocalBatchEntry*>>     MergeBatchToLocals;
        TVector<TVector<FDrawKey>>              MergeGlobalDrawsPerBatch;
        TVector<uint32>                         MergeBatchDrawArgBase;
        TVector<uint32>                         MergeDrawInstanceCounts;
        TVector<uint32>                         MergeMeshletCountsPerDraw;
        TVector<uint32>                         MergeDrawInstanceOffsets;
        TVector<uint32>                         MergeDrawCursor;

        // Per-frame extracted-state ring. Extract waits on
        // Consumed[Slot] >= Produced[Slot] before overwriting a slot;
        // SignalFrameConsumed (called from the render lambda tail) advances
        // Consumed.
        TArray<FFrameData, FRAMES_IN_FLIGHT>          FrameRing;
        TArray<TAtomic<uint64>, FRAMES_IN_FLIGHT>     SlotConsumedCount;
        TArray<uint64,         FRAMES_IN_FLIGHT>      SlotProducedCount = {};
        FMutex                                        SlotMutex;
        std::condition_variable                       SlotCV;

        FFrameData*                             ExtractFrame = nullptr;  // game thread
        FFrameData*                             RenderFrame  = nullptr;  // render thread

        void WaitForSlotConsumed(uint8 Slot, uint64 Target);
        void SignalSlotConsumed(uint8 Slot);

#if USING(WITH_EDITOR)
        static constexpr uint32                 PickerReadbackRingSize = FRAMES_IN_FLIGHT + 1;
        struct FPickerReadbackSlot
        {
            FRHIStagingImageRef Staging;
            uint32              Width = 0;
            uint32              Height = 0;
            uint64              SubmittedFrame = 0;
            bool                bPending = false;
        };
        mutable TArray<FPickerReadbackSlot, PickerReadbackRingSize> PickerReadbackRing;
        uint64                                  PickerReadbackFrame = 0;
        uint32                                  PickerReadbackWriteIndex = 0;

        // Schedules the per-frame picker -> staging copy on CmdList. Called
        // from RenderView after the last pass that writes to the picker RT.
        void IssuePickerReadback(ICommandList& CmdList);
#endif

    };
}
