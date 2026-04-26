#pragma once
#include "Core/Delegates/Delegate.h"
#include "Memory/Allocators/Allocator.h"
#include "Memory/SmartPtr.h"
#include "Renderer/BindingCache.h"
#include "Renderer/Vertex.h"
#include "World/Scene/RenderScene/EnvironmentRenderTypes.h"
#include "World/Scene/RenderScene/MeshDrawCommand.h"
#include "World/Scene/RenderScene/RenderScene.h"
#include "World/Scene/RenderScene/SceneCullContext.h"


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
            FFrameArenaAllocator                Arena;
            FSceneRenderStats                   Stats = {};

            FThreadLocalDrawData() = default;
            explicit FThreadLocalDrawData(FFrameArenaAllocator A)
                : Items(A), EntityRecords(A), LocalBatches(A), BonesData(A), Arena(A) {}
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

            Num,
        };
        
        enum class ENamedImage : uint8
        {
            HDR,
            LDR,
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
            
            #if USING(WITH_EDITOR)
            PointLightIcon,
            DirectionalLightIcon,
            SpotLightIcon,
            CameraIcon,
            CharacterIcon,
            #endif
            
            Num,
        };
        
        void Init() override;
        void Shutdown() override;
        
        void BeginFrame() override { }
        void EndFrame() override { }
        
        void RenderView(ICommandList& CmdList, const FViewVolume& ViewVolume) override;
        void SwapchainResized(glm::vec2 NewSize);
        
        void DrawBillboard(FRHIImage* Image, const glm::vec3& Location, float Scale) override;
        void DrawLine(const glm::vec3& Start, const glm::vec3& End, const glm::vec4& Color, float Thickness, bool bDepthTest, float Duration) override { }

        static FViewportState MakeViewportStateFromImage(const FRHIImage* Image);
        
        FRHIBuffer* GetNamedBuffer(ENamedBuffer Buffer) const { return NamedBuffers[(int)Buffer]; }
        FRHIImage* GetNamedImage(ENamedImage Image) const { return NamedImages[(int)Image];}
        
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
        
        //~ Begin Render Passes
        void ResetPass(ICommandList& CmdList);
        
        // Two-phase meshlet cull. Early dispatch runs every (instance,
        // meshlet) pair against every non-late view; camera meshlets that
        // fail the *previous-frame* Hi-Z are routed to the defer list
        // instead of being dropped. Late dispatch pops the defer list and
        // re-tests against the freshly built Hi-Z so disoccluded geometry
        // (fast camera motion) still reaches the base pass.
        void CullPassEarly(ICommandList& CmdList);
        void CullPassLate(ICommandList& CmdList);
        // Depth-only rasterization of opaque occluders. Early runs over the
        // camera (view 0) slice right after CullEarly; late runs over the
        // camera-late slice after CullLate so re-added meshlets contribute
        // to the final depth buffer before the base pass draws lighting.
        
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
        void ParticleSimulatePass(ICommandList& CmdList);
        void ParticleRenderPass(ICommandList& CmdList);
        void TerrainUpdatePass(ICommandList& CmdList);
        // Per-terrain GPU cull. One dispatch per active terrain entity, one
        // workgroup per chunk, one thread per meshlet. Survivors land in
        // FTerrainGPUState::VisibleMeshletBuffer + IndirectDrawBuffer; the
        // render pass consumes both via DrawIndirect.
        void TerrainCullPass(ICommandList& CmdList);
        void TerrainRenderPass(ICommandList& CmdList);
        void TransparentPass(ICommandList& CmdList);
        void OITResolvePass(ICommandList& CmdList);
        void EnvironmentPass(ICommandList& CmdList);
        void BatchedLineDraw(ICommandList& CmdList);
        void ToneMappingPass(ICommandList& CmdList);
        void SMAAEdgeDetectionPass(ICommandList& CmdList);
        void SMAABlendWeightPass(ICommandList& CmdList);
        void SMAANeighborhoodBlendPass(ICommandList& CmdList);
        //~ End Render Passes

        void CompileDrawCommands(ICommandList& CmdList);

        void ResolveDirtyTransforms();

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
        
        /** Packed array of all light shadows in the scene */
        TArray<TVector<FLightShadow>, (uint32)ELightType::Num>    PackedShadows;

        /** Atomic cursor into FSceneLightData::Shadows[]. Reset every frame. */
        TAtomic<uint32>                         ShadowDataCount = 0;

        /**
         * Pending per-light shadow tile requests queued during parallel light
         * processing. Finalized into ShadowAtlas tiles + FLightShadowData by
         * AllocateShadowTiles after Graph.Wait. Request capture has to be
         * deferred so we can fit the whole set against the atlas budget and
         * shrink the largest tiles uniformly when over budget. The old
         * greedy allocator failed the Nth light once the atlas was full.
         */
        struct FShadowRequest
        {
            uint32      LightIndex;        // Into LightData.Lights
            ELightType  Type;
            uint32      DesiredPixels;     // Unclamped; clamped + shrunk by the fit pass
            float       DistanceToCamera;  // Used by the view-budget drop pass (farthest first)
            glm::vec3   Position;
            glm::vec3   Direction;         // Spot only
            glm::vec3   Up;                // Spot only
            float       Attenuation;       // Spot far plane
            float       OuterFOVDegrees;   // Spot
        };
        TVector<FShadowRequest>                 ShadowRequests;
        FMutex                                  ShadowRequestMutex;
        
        FViewportState                          SceneViewportState;
        FDelegateHandle                         SwapchainResizedHandle;
        CWorld*                                 World = nullptr;
        
        FSceneRenderStats                       RenderStats;
        FSceneRenderSettings                    RenderSettings;
        FSceneLightData                         LightData;
        // Per-frame copy of the active SEnvironmentComponent's GPU params.
        // Populated during environment processing, uploaded with the scene
        // buffers, and read by Environment.slang at binding (2, 0).
        FEnvironmentParams                      EnvironmentParams;
        
        FBindingCache                           BindingCache;

        FRHIViewportRef                         SceneViewport;
        
        FSceneGlobalData                        SceneGlobalData;
        
        FRHIBindingSetRef                       SceneBindingSet;
        FRHIBindingLayoutRef                    SceneBindingLayout;
        
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
        
        TVector<FSimpleElementVertex>           SimpleVertices;
        
        FRHIInputLayoutRef                      SimpleVertexLayoutInput;
        TVector<FLineBatch>                     LineBatches;

        TVector<FBillboardInstance>             BillboardInstances;
        
        /** Packed array of per-instance descriptors uploaded to ENamedBuffer::Instance. */
        TVector<FGPUInstance>                   Instances;
        TVector<glm::mat4>                      BonesData;

        /**
         * Per-frame CPU reject volumes. Built at the top of CompileDrawCommands
         * (serially; only needs sun direction and shadow-caster positions)
         * and consumed by the parallel Process*Mesh tasks. Entries are
         * read-only during parallel gather.
         */
        FSceneCullContext                       SceneCullContext;

        FShadowAtlas                            ShadowAtlas;

        // One bump arena per worker thread. Lifetime is the scene; reset at
        // the start of every BuildPerFrameRenderData. Backs all per-frame
        // TFrameVector / TFrameHashMap members on FThreadLocalDrawData.
        TVector<TUniquePtr<FBlockLinearAllocator>> FrameArenas;

        /** Packed array of all cached mesh draw commands */
        TVector<FMeshDrawCommand>               DrawCommands;

        /** Indices into DrawCommands for opaque batches */
        TVector<uint32>                         OpaqueDrawList;

        /**
         * Subset of OpaqueDrawList whose batches have bDrawInDepthPass set.
         * Only these batches are rasterized in the depth pre-pass; the full
         * opaque set is drawn in the base pass with GREATER_EQUAL so non-
         * occluders still contribute to the final depth buffer.
         */
        TVector<uint32>                         OpaqueOccluderDrawList;

        /** Indices into DrawCommands for translucent batches, rendered after opaque */
        TVector<uint32>                         TranslucentDrawList;

        /**
         * Per-view indirect draw arguments. Sized NumCullViews * NumDraws,
         * view-major: slot (v, d) = v * NumDraws + d. VertexCount =
         * MESHLET_VERTICES_PER_DRAW (372), InstanceCount starts at 0 (incremented
         * by CullMeshlets), FirstInstance = v * TotalMeshletBound + prefix[d] so
         * each per-view atomic append stays inside that view's draw-list slice.
         */
        TVector<FDrawIndirectArguments>         IndirectArgs;

        /**
         * Per-view cull descriptors. One FCullView per logical render view
         * (main camera at 0, then NumCascades CSM views, then 6 per point
         * light, then 1 per spot). Packed tightly into the CullView SSBO
         * and consumed by CullMeshlets.slang.
         */
        TVector<FCullView>                      CullViews;

        /**
         * Per-draw meshlet start offset (prefix sum of meshlet counts).
         * IndirectArgs for view v, draw d is seeded with
         *   FirstInstance = v * TotalMeshletBound + DrawMeshletStartOffsets[d]
         * so every view writes into its own disjoint slice of uMeshletDrawList.
         * Filled by MergeMeshDrawData; consumed by BuildCullViews.
         */
        TVector<uint32>                         DrawMeshletStartOffsets;

        /** Per-view stride in uMeshletDrawList (CullMeshlets atomic writes stay within this span). */
        uint32                                  TotalMeshletBound = 0;

        /** Per-view stride into IndirectArgs (NumDraws). Cached so draw passes can index without recomputing. */
        uint32                                  NumDrawsPerView = 0;

        /**
         * Inclusive prefix sum of per-instance SurfaceMeshletCount.
         * Sized [Instances.size() + 1]; entry [i] is the running total of
         * meshlets across instances [0..i). Built on CPU after the parallel
         * instance write, uploaded to ENamedBuffer::InstanceMeshletPrefix
         * each frame, and used by the cull pass to recover (InstanceID,
         * MeshletLocalIdx) from a flat thread index.
         */
        TVector<uint32>                         InstanceMeshletPrefix;

        /**
         * View index of the first CSM cascade. Only valid if bHasSun and the
         * sun has a valid ShadowDataIndex; otherwise UINT32_MAX. Cascade c's
         * view index is CascadeViewBase + c.
         */
        uint32                                  CascadeViewBase = ~0u;

        /**
         * View index of each point shadow's first (face 0) view. Indexed
         * parallel to PackedShadows[Point]; face F's view index is
         * PointShadowCullViewBases[i] + F.
         */
        TVector<uint32>                         PointShadowCullViewBases;

        /** View index of each spot shadow. Indexed parallel to PackedShadows[Spot]. */
        TVector<uint32>                         SpotShadowCullViewBases;

        /**
         * Index of the camera-late cull view. Phase 1 (CullPassLate) emits
         * into this view's slice after re-testing the defer list against
         * the rebuilt Hi-Z; DepthPrePassLate and BasePass each issue a
         * second DrawIndirect per batch against this offset to pick up the
         * meshlets that the early HZB pass mis-occluded. UINT32_MAX when
         * no cull is active (no instances this frame).
         */
        uint32                                  CameraLateViewIndex = ~0u;

    };
}
