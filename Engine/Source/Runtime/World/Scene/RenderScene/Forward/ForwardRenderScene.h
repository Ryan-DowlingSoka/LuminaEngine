#pragma once
#include "Core/Delegates/Delegate.h"
#include "Renderer/BindingCache.h"
#include "Renderer/Vertex.h"
#include "World/Scene/RenderScene/MeshDrawCommand.h"
#include "World/Scene/RenderScene/RenderScene.h"


namespace Lumina
{
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
        * Per-entity output of the parallel mesh-processing phase. The hot work
        * (transforms, bounds, material resolution, instance packing) happens on a worker thread;
        * each item carries indices into its owning thread's local batch table so the merge phase
        * never has to recompute or hash a batch key.
        */
        struct FProcessedDrawItem
        {
            // Scattered into the per-instance SSBO during the merge pass.
            // Instance.DrawIDAndFlags holds only flags here; the merge fills
            // in DrawID once the global draw index is known.
            FGPUInstance        Instance;
            EInstanceFlags      Flags;
            uint32              LocalBoneOffset;   // Offset into the owning thread's BonesData; ~0u for static meshes.
            uint16              MaterialIndex;
            uint16              LocalBatchIndex;   // Index into FThreadLocalDrawData::LocalBatches
            uint16              LocalDrawIndex;    // Index into FLocalBatchEntry::LocalDraws
        };

        /**
         * Per-thread local batch table. The worker linear-searches this small list to dedup
         * draws against batches it has already seen. The merge phase walks these instead of
         * walking every item, which is what makes the merge O(unique batches × threads)
         * rather than O(num instances).
         */
        struct alignas(64) FLocalBatchEntry
        {
            FDrawBatchKey       Key;
            FRHIVertexShader*   VertexShader = nullptr;
            FRHIPixelShader*    PixelShader  = nullptr;
            TVector<FDrawKey>   LocalDraws;
            TVector<uint32>     LocalDrawCounts;        // instance count per local draw
            // Sum of SurfaceMeshletCount for every instance sharing this local draw; the merge
            // pass reduces this into the global meshlet prefix sum without walking Instances.
            TVector<uint32>     LocalMeshletCounts;
            uint32              GlobalBatchIndex = ~0u; // resolved during merge
            TVector<uint32>     LocalToGlobalDraw;      // resolved during merge
            // Merge stamps one write-cursor per local draw. Parallel writer increments it in
            // place; each worker only touches its own LocalBatches so no atomics.
            TVector<uint32>     LocalDrawWriteBase;
        };

        struct alignas(64) FThreadLocalDrawData
        {
            TVector<FProcessedDrawItem> Items;
            TVector<FLocalBatchEntry>   LocalBatches;
            TVector<glm::mat4>          BonesData;
            FSceneRenderStats           Stats = {};
            uint32                      MaxMeshletsPerInstance = 0;
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
            MeshletDrawList,
            // Shared indirect draw args. NumViews * NumDraws slots — each
            // view addresses its own range via FCullView.IndirectArgsOffset.
            IndirectArgs,

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
        void CullPass(ICommandList& CmdList);
        void DepthPrePass(ICommandList& CmdList);
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
        void MergeMeshDrawData(TVector<FThreadLocalDrawData>& ThreadLocal);
        
        void ProcessPointLight(const SPointLightComponent& PointLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount);
        void ProcessSpotLight(const SSpotLightComponent& SpotLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount);
        void ProcessDirectionalLight(const SDirectionalLightComponent& DirectionalLight, TAtomic<uint32>& LightCount);

        /**
         * Serial post-pass: fits every accumulated shadow request into the atlas
         * budget (shrinks the largest desired tiles until Σ area ≤ capacity) then
         * allocates tiles and populates FLightShadowData / PackedShadows. Runs
         * after the parallel Process*Light tasks have written into ShadowRequests.
         */
        void AllocateShadowTiles();

        /**
         * Builds the per-frame FCullView array and seeds IndirectArgs for every
         * view's indirect-draw slice. Runs after AllocateShadowTiles (so every
         * shadow-casting light has its ViewProjection[6] filled) and before the
         * scene buffer upload so CullMeshlets has everything it needs.
         *
         * View layout:
         *   0              — main camera (frustum + cone + occlusion + micropoly)
         *   1..NumCascades — CSM cascade views (frustum + sun-aligned cone, cast-shadow-only, distance)
         *   N..            — 6 views per shadow-casting point light
         *   ...            — 1 view per shadow-casting spot light
         */
        void BuildCullViews(const FViewVolume& ViewVolume);
        
        void ProcessBatchedLines(FLineBatcherComponent& Batcher);
        
        void NotifyMaxLightsHit();

        /**
         * CPU-side early-out for shadow requests. Returns false when the
         * light's attenuation sphere falls entirely outside the camera
         * frustum — nothing the camera can see would be shadowed by the light,
         * so the shadow view, atlas tile and draw-list slice are all skipped.
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
         * shrink the largest tiles uniformly when over budget — the old greedy
         * first-come allocator failed the Nth light the moment the 4×2048 atlas
         * was full, no matter how close it was.
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
        
        FShadowAtlas                            ShadowAtlas;
        
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

        /** Peak SurfaceMeshletCount across all instances this frame; sets CullMeshlets' dispatch X. */
        uint32                                  MaxMeshletsPerInstance = 0;

        /** Per-view stride in uMeshletDrawList (CullMeshlets atomic writes stay within this span). */
        uint32                                  TotalMeshletBound = 0;

        /** Per-view stride into IndirectArgs (NumDraws). Cached so draw passes can index without recomputing. */
        uint32                                  NumDrawsPerView = 0;

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

    };
}
