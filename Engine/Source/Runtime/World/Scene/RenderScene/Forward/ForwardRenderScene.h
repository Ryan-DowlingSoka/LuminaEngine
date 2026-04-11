#pragma once
#include "Core/Delegates/Delegate.h"
#include "Renderer/BindingCache.h"
#include "Renderer/Vertex.h"
#include "World/Scene/RenderScene/MeshDrawCommand.h"
#include "World/Scene/RenderScene/RenderScene.h"


namespace Lumina
{
    class CWorld;

    /**
     * Scene rendering via Clustered Forward Rendering.
     */
    class FForwardRenderScene : public IRenderScene
    {
    public:
        
        FForwardRenderScene(CWorld* InWorld);
        ~FForwardRenderScene() override = default;
        LE_NO_COPYMOVE(FForwardRenderScene);
        
        struct FMeshPassDrawList
        {
            TVector<uint32> DrawCommandIndices;
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
        void EnvironmentPass(FRenderGraph& RenderGraph);
        void BatchedLineDraw(FRenderGraph& RenderGraph);
        void SelectionPass(FRenderGraph& RenderGraph);
        void ToneMappingPass(FRenderGraph& RenderGraph);
        void DebugDrawPass(FRenderGraph& RenderGraph);
        //~ End Render Passes
        
        void CompileDrawCommands(FRenderGraph& RenderGraph);
    
    private:
        
        
        FViewportState                      SceneViewportState;
        FDelegateHandle                     SwapchainResizedHandle;
        CWorld*                             World = nullptr;
        
        FSceneRenderStats                   RenderStats;
        FSceneRenderSettings                RenderSettings;
        FSceneLightData                     LightData;

        /** Packed array of all light shadows in the scene */
        TArray<TVector<FLightShadow>, (uint32)ELightType::Num>    PackedShadows;
        

        FBindingCache                       BindingCache;

        FRHIViewportRef                     SceneViewport;
        
        FSceneGlobalData                    SceneGlobalData;
        
        FRHIBindingSetRef                   SceneBindingSet;
        FRHIBindingLayoutRef                SceneBindingLayout;
        
        TVector<FSimpleElementVertex>       SimpleVertices;
        
        FRHIInputLayoutRef                  SimpleVertexLayoutInput;
        TVector<FLineBatch>                 LineBatches;

        TVector<FBillboardInstance>                 BillboardInstances;
        
        TArray<FRHIBufferRef, (int)ENamedBuffer::Num>   NamedBuffers = {};
        TArray<FRHIImageRef, (int)ENamedImage::Num>     NamedImages = {};
        
        /** Packed array of per-instance data */
        TVector<FGPUInstance>                   InstanceData;
        TVector<glm::mat4>                      BonesData;
        
        FShadowAtlas                            ShadowAtlas;
        
        FMeshPassDrawList                       PassDrawLists;
        
        /** Packed array of all cached mesh draw commands */
        TVector<FMeshDrawCommand>               DrawCommands;

        /** Indices into DrawCommands for translucent batches, rendered after opaque */
        TVector<uint32>                         TranslucentDrawList;

        /** Packed indirect draw arguments, gets sent directly to the GPU */
        TVector<FDrawIndirectArguments>         IndirectDrawArguments;
    };
}
