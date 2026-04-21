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
            FGPUInstance        Instance;            // DrawIDAndFlags holds only flags here; the merge fills in DrawID.
            EInstanceFlags      Flags;
            uint32              LocalBoneOffset;     // Offset into the owning thread's BonesData; ~0u for static meshes.
            uint16              MaterialIndex;
            uint16              LocalBatchIndex;     // Index into FThreadLocalDrawData::LocalBatches
            uint16              LocalDrawIndex;      // Index into FLocalBatchEntry::LocalDraws
        };

        /**
         * Per-thread local batch table. The worker linear-searches this small list to dedup
         * draws against batches it has already seen. The merge phase walks these instead of
         * walking every item, which is what makes the merge O(unique batches × threads)
         * rather than O(num instances).
         */
        struct FLocalBatchEntry
        {
            FDrawBatchKey       Key;
            FRHIVertexShader*   VertexShader = nullptr;
            FRHIPixelShader*    PixelShader  = nullptr;
            TVector<FDrawKey>   LocalDraws;
            TVector<uint32>     LocalDrawCounts;        // instance count per local draw
            uint32              GlobalBatchIndex = ~0u; // resolved during merge
            TVector<uint32>     LocalToGlobalDraw;      // resolved during merge
        };

        struct alignas(Threading::GCacheLineSize) FThreadLocalDrawData
        {
            TVector<FProcessedDrawItem> Items;
            TVector<FLocalBatchEntry>   LocalBatches;
            TVector<glm::mat4>          BonesData;
            FSceneRenderStats           Stats = {};
        };
        
        enum class ENamedBuffer : uint8
        {
            Scene,
            Light,
            Instance,
            InstanceMapping,
            InstanceMappingShadow,
            InstanceMappingCascade,
            Indirect,
            IndirectShadow,
            IndirectCascade,
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
        
        void RenderView(FRenderGraph& RenderGraph, const FViewVolume& ViewVolume) override;
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
        void ResetPass(FRenderGraph& RenderGraph);
        void CullPass(FRenderGraph& RenderGraph);
        void DepthPrePass(FRenderGraph& RenderGraph);
        void DepthPyramidPass(FRenderGraph& RenderGraph);
        void ClusterBuildPass(FRenderGraph& RenderGraph);
        void LightCullPass(FRenderGraph& RenderGraph);
        void PointShadowPass(FRenderGraph& RenderGraph);
        void SpotShadowPass(FRenderGraph& RenderGraph);
        void CascadedShowPass(FRenderGraph& RenderGraph);
        void BasePass(FRenderGraph& RenderGraph);
        void BillboardPass(FRenderGraph& RenderGraph);
        void ParticleSimulatePass(FRenderGraph& RenderGraph);
        void ParticleRenderPass(FRenderGraph& RenderGraph);
        void TerrainUpdatePass(FRenderGraph& RenderGraph);
        void TerrainRenderPass(FRenderGraph& RenderGraph);
        void TransparentPass(FRenderGraph& RenderGraph);
        void OITResolvePass(FRenderGraph& RenderGraph);
        void EnvironmentPass(FRenderGraph& RenderGraph);
        void BatchedLineDraw(FRenderGraph& RenderGraph);
        void ToneMappingPass(FRenderGraph& RenderGraph);
        //~ End Render Passes
        
        void CompileDrawCommands(FRenderGraph& RenderGraph);

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
        
        /** Packed array of per-instance data */
        TVector<FGPUInstance>                   InstanceData;
        TVector<glm::mat4>                      BonesData;
        
        FShadowAtlas                            ShadowAtlas;
        
        /** Packed array of all cached mesh draw commands */
        TVector<FMeshDrawCommand>               DrawCommands;

        /** Indices into DrawCommands for opaque batches */
        TVector<uint32>                         OpaqueDrawList;

        /** Indices into DrawCommands for translucent batches, rendered after opaque */
        TVector<uint32>                         TranslucentDrawList;

        /** Packed indirect draw arguments, gets sent directly to the GPU */
        TVector<FDrawIndirectArguments>         IndirectDrawArguments;

        /**
         * Cascade-major duplicate of IndirectDrawArguments. Layout:
         *   [c * NumDraws + d] = args for draw d in cascade c.
         * FirstInstance in each cascade slice is pre-shifted by c * NumInstances
         * so the cascade mapping buffer can be bound as a single flat UAV.
         */
        TVector<FDrawIndirectArguments>         IndirectDrawArgumentsCascade;

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
