#pragma once
#include "Core/Delegates/Delegate.h"
#include "Renderer/BindingCache.h"
#include "Renderer/Vertex.h"
#include "World/Scene/RenderScene/MeshDrawCommand.h"
#include "World/Scene/RenderScene/RenderScene.h"


namespace Lumina
{
    class CWorld;
    struct SStaticMeshComponent;
    struct SSkeletalMeshComponent;
    struct STransformComponent;

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

        struct FThreadLocalDrawData
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
            Indirect,
            Bone,
            Cluster,
            SimpleVertex,
            Billboards,
            
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
        void TransparentPass(FRenderGraph& RenderGraph);
        void OITResolvePass(FRenderGraph& RenderGraph);
        void EnvironmentPass(FRenderGraph& RenderGraph);
        void BatchedLineDraw(FRenderGraph& RenderGraph);
        void SelectionPass(FRenderGraph& RenderGraph);
        void ToneMappingPass(FRenderGraph& RenderGraph);
        void DebugDrawPass(FRenderGraph& RenderGraph);
        //~ End Render Passes
        
        void CompileDrawCommands(FRenderGraph& RenderGraph);

        // ~ Begin Parallel Draw Command Compilation ~

        void ProcessStaticMeshEntityInternal(entt::entity Entity, const SStaticMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local);
        void ProcessSkeletalMeshEntityInternal(entt::entity Entity, const SSkeletalMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local);
        void MergeMeshDrawData(TVector<FThreadLocalDrawData>& ThreadLocal);

        // ~ End Parallel Draw Command Compilation ~

    private:
        
        TArray<FRHIBufferRef, (int)ENamedBuffer::Num>   NamedBuffers = {};
        TArray<FRHIImageRef, (int)ENamedImage::Num>     NamedImages = {};
        
        /** Packed array of all light shadows in the scene */
        TArray<TVector<FLightShadow>, (uint32)ELightType::Num>    PackedShadows;
        
        FViewportState                      SceneViewportState;
        FDelegateHandle                     SwapchainResizedHandle;
        CWorld*                             World = nullptr;
        
        FSceneRenderStats                   RenderStats;
        FSceneRenderSettings                RenderSettings;
        FSceneLightData                     LightData;
        

        FBindingCache                       BindingCache;

        FRHIViewportRef                     SceneViewport;
        
        FSceneGlobalData                    SceneGlobalData;
        
        FRHIBindingSetRef                   SceneBindingSet;
        FRHIBindingLayoutRef                SceneBindingLayout;
        
        TVector<FSimpleElementVertex>       SimpleVertices;
        
        FRHIInputLayoutRef                  SimpleVertexLayoutInput;
        TVector<FLineBatch>                 LineBatches;

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
    };
}
