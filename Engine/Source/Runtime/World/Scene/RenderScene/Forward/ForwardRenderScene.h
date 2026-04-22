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
            InstanceMappingShadow,
            IndirectShadow,
            Bone,
            Cluster,
            SimpleVertex,
            Billboards,
            MeshletDrawList,
            MeshletIndirect,
            MeshletDrawListCascade,
            MeshletIndirectCascade,

            Num,
        };
        
        enum class ENamedImage : uint8
        {
            HDR,
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
        //~ End Render Passes

        void CompileDrawCommands(ICommandList& CmdList);

        void ResolveDirtyTransforms();

        void ProcessStaticMeshEntityInternal(entt::entity Entity, const SStaticMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local);
        void ProcessSkeletalMeshEntityInternal(entt::entity Entity, const SSkeletalMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local);
        void MergeMeshDrawData(TVector<FThreadLocalDrawData>& ThreadLocal);
        
        void ProcessPointLight(const SPointLightComponent& PointLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount);
        void ProcessSpotLight(const SSpotLightComponent& SpotLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount);
        void ProcessDirectionalLight(const SDirectionalLightComponent& DirectionalLight, TAtomic<uint32>& LightCount);
        
        void ProcessBatchedLines(FLineBatcherComponent& Batcher);
        
        void NotifyMaxLightsHit();


    private:
        
        TArray<FRHIBufferRef, (int)ENamedBuffer::Num>   NamedBuffers = {};
        TArray<FRHIImageRef, (int)ENamedImage::Num>     NamedImages = {};
        
        /** Packed array of all light shadows in the scene */
        TArray<TVector<FLightShadow>, (uint32)ELightType::Num>    PackedShadows;

        /** Atomic cursor into FSceneLightData::Shadows[]. Reset every frame. */
        TAtomic<uint32>                         ShadowDataCount = 0;
        
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
         * Packed indirect draw arguments for the shadow pass. FirstInstance
         * points into uMappingDataShadow, populated atomically by
         * ShadowMeshCull.slang for each surviving caster / light pair.
         */
        TVector<FDrawIndirectArguments>         IndirectDrawArguments;

        /**
         * Per-draw meshlet indirect args. VertexCount = MESHLET_VERTICES_PER_DRAW
         * (372), InstanceCount starts at 0 (incremented by MeshletCull), and
         * FirstInstance is the prefix-summed offset into uMeshletDrawList where
         * this draw's meshlet slots live.
         */
        TVector<FDrawIndirectArguments>         IndirectDrawArgumentsMeshlet;

        /** Cascade-major: [c * NumDraws + d]. FirstInstance pre-shifted by c * TotalMeshletBound. */
        TVector<FDrawIndirectArguments>         IndirectDrawArgumentsMeshletCascade;

        /** Peak SurfaceMeshletCount across all instances this frame; sets MeshletCull's dispatch X. */
        uint32                                  MaxMeshletsPerInstance = 0;

        /** Per-cascade stride in uMeshletDrawListCascade. */
        uint32                                  TotalMeshletBound = 0;

    };
}
