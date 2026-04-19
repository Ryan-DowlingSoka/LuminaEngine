#include "pch.h"
#include "ForwardRenderScene.h"
#include <execution>
#include "Assets/AssetTypes/Material/Material.h"
#include "Assets/AssetTypes/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "assets/assettypes/mesh/skeleton/skeleton.h"
#include "Assets/AssetTypes/Textures/Texture.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Windows/Window.h"
#include "Paths/Paths.h"
#include "Renderer/RendererUtils.h"
#include "Renderer/RHIStaticStates.h"
#include "Renderer/ShaderCompiler.h"
#include "Renderer/RenderGraph/RenderGraphDescriptor.h"
#include "TaskSystem/TaskGraph.h"
#include "TaskSystem/TaskSystem.h"
#include "Tools/Import/ImportHelpers.h"
#include "World/World.h"
#include "World/Entity/Components/BillboardComponent.h"
#include "world/entity/components/charactercontrollercomponent.h"
#include "World/Entity/Components/EditorComponent.h"
#include "world/entity/components/entitytags.h"
#include "world/entity/components/environmentcomponent.h"
#include "world/entity/components/lightcomponent.h"
#include "World/Entity/Components/LineBatcherComponent.h"
#include "World/Entity/Components/SkeletalMeshComponent.h"
#include "world/entity/components/staticmeshcomponent.h"
#include "World/Scene/RenderScene/MeshDrawCommand.h"

namespace Lumina
{
    static TConsoleVar CVarSelectionThickness("r.SelectionThickness", 5, "Changes thickness of entity selection.");
    
    FForwardRenderScene::FForwardRenderScene(CWorld* InWorld)
        : World(InWorld)
        , LightData()
        , SceneGlobalData()
        , ShadowAtlas(FShadowAtlasConfig())
    {
    }

    void FForwardRenderScene::Init()
    {
        LOG_TRACE("Initializing Forward Render Scene");
        
        SceneViewport = GRenderContext->CreateViewport(Windowing::GetPrimaryWindowHandle()->GetExtent(), "Forward Renderer Viewport");

        InitBuffers();
        InitFrameResources();

        SwapchainResizedHandle = FRenderManager::OnSwapchainResized.AddMember(this, &FForwardRenderScene::SwapchainResized); 
        
        #if USING(WITH_EDITOR)
        NamedImages[(int)ENamedImage::PointLightIcon]       = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/PointLight.png", true);  
        NamedImages[(int)ENamedImage::DirectionalLightIcon] = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/SkyLight.png", true);  
        NamedImages[(int)ENamedImage::SpotLightIcon]        = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/SpotLight.png", true);  
        NamedImages[(int)ENamedImage::CameraIcon]           = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/CameraIcon.png", true);  
        NamedImages[(int)ENamedImage::CharacterIcon]        = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/PersonIcon.png", true);  

        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::PointLightIcon]);
        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::DirectionalLightIcon]);
        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::SpotLightIcon]);
        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::CameraIcon]);
        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::CharacterIcon]);
        #endif
    }

    void FForwardRenderScene::Shutdown()
    {
        GRenderContext->ClearCommandListCache();
        GRenderContext->ClearBindingCaches();

        #if USING(WITH_EDITOR)
        GRenderManager->GetTextureManager().RemoveTexture(NamedImages[(int)ENamedImage::PointLightIcon]);
        GRenderManager->GetTextureManager().RemoveTexture(NamedImages[(int)ENamedImage::DirectionalLightIcon]);
        GRenderManager->GetTextureManager().RemoveTexture(NamedImages[(int)ENamedImage::SpotLightIcon]);
        GRenderManager->GetTextureManager().RemoveTexture(NamedImages[(int)ENamedImage::CameraIcon]);
        GRenderManager->GetTextureManager().RemoveTexture(NamedImages[(int)ENamedImage::CharacterIcon]);
        #endif
        
        FRenderManager::OnSwapchainResized.Remove(SwapchainResizedHandle);
        
        LOG_TRACE("Shutting down Forward Render Scene");
    }

    void FForwardRenderScene::RenderView(FRenderGraph& RenderGraph, const FViewVolume& ViewVolume)
    {
        LUMINA_PROFILE_SCOPE();
        
        SceneViewport->SetViewVolume(ViewVolume);
        
        SceneGlobalData.CameraData.Location             = glm::vec4(SceneViewport->GetViewVolume().GetViewPosition(), 1.0f);
        SceneGlobalData.CameraData.Up                   = glm::vec4(SceneViewport->GetViewVolume().GetUpVector(), 1.0f);
        SceneGlobalData.CameraData.Right                = glm::vec4(SceneViewport->GetViewVolume().GetRightVector(), 1.0f);
        SceneGlobalData.CameraData.Forward              = glm::vec4(SceneViewport->GetViewVolume().GetForwardVector(), 1.0f);
        SceneGlobalData.CameraData.View                 = SceneViewport->GetViewVolume().GetViewMatrix();
        SceneGlobalData.CameraData.InverseView          = SceneViewport->GetViewVolume().GetInverseViewMatrix();
        SceneGlobalData.CameraData.Projection           = SceneViewport->GetViewVolume().GetProjectionMatrix();
        SceneGlobalData.CameraData.InverseProjection    = SceneViewport->GetViewVolume().GetInverseProjectionMatrix();
        SceneGlobalData.ScreenSize                      = glm::vec4(SceneViewport->GetSize().x, SceneViewport->GetSize().y, 0.0f, 0.0f);
        SceneGlobalData.GridSize                        = glm::vec4(ClusterGridSizeX, ClusterGridSizeY, ClusterGridSizeZ, 0.0f);
        SceneGlobalData.Time                            = (float)World->GetTimeSinceWorldCreation();
        SceneGlobalData.DeltaTime                       = (float)World->GetWorldDeltaTime();
        SceneGlobalData.FarPlane                        = SceneViewport->GetViewVolume().GetFar();
        SceneGlobalData.NearPlane                       = SceneViewport->GetViewVolume().GetNear();
        SceneGlobalData.CullData.Frustum                = SceneViewport->GetViewVolume().GetFrustum();
        SceneGlobalData.CullData.ShadowFrustum          = SceneGlobalData.CullData.Frustum; // Rebuilt after directional light is processed.
        SceneGlobalData.CullData.bHasDirectional        = 0u;
        SceneGlobalData.CullData.ViewMatrix             = SceneViewport->GetViewVolume().GetViewMatrix();
        SceneGlobalData.CullData.P00                    = SceneViewport->GetViewVolume().GetProjectionMatrix()[0][0];
        SceneGlobalData.CullData.P11                    = SceneViewport->GetViewVolume().GetProjectionMatrix()[1][1];
        SceneGlobalData.CullData.zNear                  = SceneViewport->GetViewVolume().GetNear();
        SceneGlobalData.CullData.zFar                   = SceneViewport->GetViewVolume().GetFar();
        SceneGlobalData.CullData.InstanceNum            = (uint32)InstanceData.size();
        SceneGlobalData.CullData.bFrustumCull           = RenderSettings.bFrustumCull;
        SceneGlobalData.CullData.bOcclusionCull         = RenderSettings.bOcclusionCull;
        SceneGlobalData.CullData.PyramidWidth           = (float)GetNamedImage(ENamedImage::DepthPyramid)->GetSizeX();
        SceneGlobalData.CullData.PyramidHeight          = (float)GetNamedImage(ENamedImage::DepthPyramid)->GetSizeY();
        SceneGlobalData.CullData.ShadowMaxDistance      = RenderSettings.ShadowMaxDistance;
        SceneGlobalData.CullData.bShadowOcclusionCull   = RenderSettings.bShadowOcclusionCull;
        
        
        // Wait for shader tasks.
        if(GRenderContext->GetShaderCompiler()->HasPendingRequests())
        {
            return;
        }

        ResetPass(RenderGraph);
        CompileDrawCommands(RenderGraph);
        CullPass(RenderGraph);
        DepthPrePass(RenderGraph);
        DepthPyramidPass(RenderGraph);
        ClusterBuildPass(RenderGraph);
        LightCullPass(RenderGraph);
        PointShadowPass(RenderGraph);
        SpotShadowPass(RenderGraph);
        CascadedShowPass(RenderGraph);
        EnvironmentPass(RenderGraph);
        BasePass(RenderGraph);
        TransparentPass(RenderGraph);
        OITResolvePass(RenderGraph);
        BatchedLineDraw(RenderGraph);
        BillboardPass(RenderGraph);
        ToneMappingPass(RenderGraph);
        DebugDrawPass(RenderGraph);
    }
    
    void FForwardRenderScene::SwapchainResized(glm::vec2 NewSize)
    {
        GRenderContext->ClearCommandListCache();
        GRenderContext->ClearBindingCaches();
        BindingCache.ReleaseResources();
        
        SceneViewport = GRenderContext->CreateViewport(NewSize, "Forward Renderer Viewport");
        
        InitFrameResources();
    }

    void FForwardRenderScene::CompileDrawCommands(FRenderGraph& RenderGraph)
    {
        LUMINA_PROFILE_SCOPE();
        
        {
            LUMINA_PROFILE_SECTION("Compile Draw Commands");
            FEntityRegistry& Registry = World->GetEntityRegistry();
            TAtomic<uint32> LightCount{0};
            

            auto DirectionalView    = Registry.view<SDirectionalLightComponent>(entt::exclude<SDisabledTag>);
            auto SpotLightView      = Registry.view<SSpotLightComponent, STransformComponent>(entt::exclude<SDisabledTag>);
            auto PointLightView     = Registry.view<SPointLightComponent, STransformComponent>(entt::exclude<SDisabledTag>);
            auto CharacterView      = Registry.view<SCharacterControllerComponent, STransformComponent>(entt::exclude<SDisabledTag>);
            auto CameraView         = Registry.view<SCameraComponent, STransformComponent>(entt::exclude<SDisabledTag>);
            auto BillboardView      = Registry.view<SBillboardComponent, STransformComponent>(entt::exclude<SDisabledTag>);
            auto LineBatcherView    = Registry.view<FLineBatcherComponent>();
            auto EnvironmentView    = Registry.view<SEnvironmentComponent>(entt::exclude<SDisabledTag>);
            auto TransformView      = Registry.view<STransformComponent, FNeedsTransformUpdate>();
            auto StaticView         = Registry.view<SStaticMeshComponent, STransformComponent>(entt::exclude<SDisabledTag>);
            auto SkeletalView       = Registry.view<SSkeletalMeshComponent, STransformComponent>(entt::exclude<SDisabledTag>);
            
            ResolveDirtyTransforms();

            const size_t EntityCount       = StaticView.size_hint() + SkeletalView.size_hint();
            const size_t EstimatedProxies  = EntityCount * 2;

            InstanceData.reserve(EstimatedProxies);
            IndirectDrawArguments.reserve(EstimatedProxies);
            DrawCommands.reserve(EstimatedProxies);

            // One thread-local accumulator per scheduler thread (workers + the calling thread).
            const uint32 NumThreads = GTaskSystem->GetScheduler().GetNumTaskThreads();
            TVector<FThreadLocalDrawData> ThreadLocal(NumThreads);
            const uint32 ReservePerThread = (uint32)((EstimatedProxies + NumThreads - 1) / std::max(1u, NumThreads));
            for (FThreadLocalDrawData& Local : ThreadLocal)
            {
                Local.Items.reserve(ReservePerThread);
            }
            
            
            FTaskGraph Graph;
            
            FTaskGraph::FNodeHandle StaticNode = Graph.AddParallelFor((uint32)StaticView.handle()->size(), 64, [&](const Task::FParallelRange& Range)
            {
                LUMINA_PROFILE_SECTION("Process Static Mesh Range");
                FThreadLocalDrawData& Local = ThreadLocal[Range.Thread];
                auto Handle = StaticView.handle();
                for (uint32 i = Range.Start; i < Range.End; ++i)
                {
                    entt::entity Entity = (*Handle)[i];
                    if (StaticView.contains(Entity))
                    {
                        const SStaticMeshComponent& MeshComponent      = StaticView.get<SStaticMeshComponent>(Entity);
                        const STransformComponent&  TransformComponent = StaticView.get<STransformComponent>(Entity);
                        ProcessStaticMeshEntityInternal(Entity, MeshComponent, TransformComponent, Local);
                    }
                }
            });

            FTaskGraph::FNodeHandle SkeletalNode = Graph.AddParallelFor((uint32)SkeletalView.handle()->size(), 32, [&](const Task::FParallelRange& Range)
            {
                LUMINA_PROFILE_SECTION("Process Skeletal Mesh Range");
                FThreadLocalDrawData& Local = ThreadLocal[Range.Thread];
                auto Handle = SkeletalView.handle();
                for (uint32 i = Range.Start; i < Range.End; ++i)
                {
                    entt::entity Entity = (*Handle)[i];
                    if (SkeletalView.contains(Entity))
                    {
                        const SSkeletalMeshComponent& MeshComponent    = SkeletalView.get<SSkeletalMeshComponent>(Entity);
                        const STransformComponent&  TransformComponent = SkeletalView.get<STransformComponent>(Entity);
                        ProcessSkeletalMeshEntityInternal(Entity, MeshComponent, TransformComponent, Local);
                    }
                }
            });

            FTaskGraph::FNodeHandle MergeNode = Graph.Add([&]
            {
                MergeMeshDrawData(ThreadLocal);
            });
            
            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Environment Processing");
                
                RenderSettings.bHasEnvironment = false;
                LightData.AmbientLight = glm::vec4(0.0f);
                RenderSettings.bSSAO = false;
                EnvironmentView.each([this] (const SEnvironmentComponent& EnvironmentComponent)
                {
                    LightData.AmbientLight          = glm::vec4(EnvironmentComponent.AmbientColor, EnvironmentComponent.Intensity);
                    RenderSettings.bHasEnvironment  = true;
                    RenderSettings.bSSAO            = false;
                }); 
            });
            
            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Batched Line Processing");

                LineBatcherView.each([&](FLineBatcherComponent& LineBatcherComponent)
                {
                    ProcessBatchedLines(LineBatcherComponent);
                }); 
            });
            
            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Process Billboard Primitives");
                
                BillboardView.each([this](entt::entity Entity, const SBillboardComponent& BillboardComponent, const STransformComponent& TransformComponent)
                {
                    if (!BillboardComponent.Texture.IsValid() || !BillboardComponent.Texture->GetRHIRef()->IsValid())
                    {
                        return;
                    }
                    
                    FBillboardInstance& Billboard   = BillboardInstances.emplace_back();
                    Billboard.TextureIndex          = BillboardComponent.Texture->GetRHIRef()->GetTextureCacheIndex();
                    Billboard.Position              = TransformComponent.WorldTransform.Location;
                    Billboard.Size                  = BillboardComponent.Scale;
                    Billboard.EntityID              = entt::to_integral(Entity);
                
                    //RenderStats.NumVertices         += 6;
                });
                
                #if USING(WITH_EDITOR)
                {
                    if (!World->IsGameWorld())
                    {
                        CameraView.each([this](entt::entity Entity, SCameraComponent&, STransformComponent& Transform)
                        {
                            if (World->GetEntityRegistry().all_of<FEditorComponent>(Entity))
                            {
                                return;
                            }
                            
                            FBillboardInstance& Billboard   = BillboardInstances.emplace_back();
                            Billboard.TextureIndex          = GetNamedImage(ENamedImage::CameraIcon)->GetTextureCacheIndex();
                            Billboard.ColorPack             = PackColor(FColor::White);
                            Billboard.Position              = Transform.WorldTransform.Location;
                            Billboard.Size                  = 0.35f;
                            Billboard.EntityID              = entt::to_integral(Entity);
                        });
                    }
                }
                
                CharacterView.each([this](entt::entity Entity, SCharacterControllerComponent&, STransformComponent& Transform)
                {
                    if (!World->IsGameWorld())
                    {
                        FBillboardInstance& Billboard   = BillboardInstances.emplace_back();
                        Billboard.TextureIndex          = GetNamedImage(ENamedImage::CharacterIcon)->GetTextureCacheIndex();
                        Billboard.ColorPack             = PackColor(FColor::White);
                        Billboard.Position              = Transform.WorldTransform.Location;
                        Billboard.Size                  = 0.35f;
                        Billboard.EntityID              = entt::to_integral(Entity);
                    }
                });
                
                PointLightView.each([&] (entt::entity Entity, const SPointLightComponent& PointLightComponent, const STransformComponent& TransformComponent)
                {
                    if (!World->IsGameWorld())
                    {
                        FBillboardInstance& Billboard   = BillboardInstances.emplace_back();
                        Billboard.TextureIndex          = GetNamedImage(ENamedImage::PointLightIcon)->GetTextureCacheIndex();
                        Billboard.ColorPack             = PackColor({PointLightComponent.LightColor, 1.0f});
                        Billboard.Position              = TransformComponent.WorldTransform.Location;
                        Billboard.Size                  = 0.35f;
                        Billboard.EntityID              = entt::to_integral(Entity);
                    }
                });
                
                SpotLightView.each([&] (entt::entity Entity, SSpotLightComponent& SpotLightComponent, STransformComponent& Transform)
                {
                    if (!World->IsGameWorld())
                    {
                        FBillboardInstance& Billboard   = BillboardInstances.emplace_back();
                        Billboard.TextureIndex          = GetNamedImage(ENamedImage::PointLightIcon)->GetTextureCacheIndex();
                        Billboard.ColorPack             = PackColor({SpotLightComponent.LightColor, 1.0f});
                        Billboard.Position              = Transform.WorldTransform.Location;
                        Billboard.Size                  = 0.35f;
                        Billboard.EntityID              = entt::to_integral(Entity);
                    }
                });
                
                #endif 
            });
            
            auto DLightTask = Graph.AddParallelFor(DirectionalView.handle()->size(), 32, [&](Task::FParallelRange Range)
            {
                LUMINA_PROFILE_SECTION("Process Directional Light");
                auto Handle = DirectionalView.handle();
                for (uint32 i = Range.Start; i < Range.End; ++i)
                {
                    entt::entity Entity = (*Handle)[i];
                    if (DirectionalView.contains(Entity))
                    {
                        auto& DirectionalLight = DirectionalView.get<SDirectionalLightComponent>(Entity);
                        ProcessDirectionalLight(DirectionalLight, LightCount);
                    }
                }
            });
            
            auto PointLightTask = Graph.AddParallelFor(PointLightView.handle()->size(), 32, [&](Task::FParallelRange Range)
            {
                LUMINA_PROFILE_SECTION("Process Point Light Range");

                auto Handle = PointLightView.handle();
                for (uint32 i = Range.Start; i < Range.End; ++i)
                {
                    entt::entity Entity = (*Handle)[i];
                    if (PointLightView.contains(Entity))
                    {
                        auto& PointLight = PointLightView.get<SPointLightComponent>(Entity);
                        auto& Transform = PointLightView.get<STransformComponent>(Entity);
                        ProcessPointLight(PointLight, Transform, LightCount);
                    }
                }
            });
            
            auto SpotLightTask = Graph.AddParallelFor(SpotLightView.handle()->size(), 32, [&](Task::FParallelRange Range)
            {
                LUMINA_PROFILE_SECTION("Process Spot Light Range");

                auto Handle = SpotLightView.handle();
                for (uint32 i = Range.Start; i < Range.End; ++i)
                {
                    entt::entity Entity = (*Handle)[i];
                    if (SpotLightView.contains(Entity))
                    {
                        auto& SpotLight = SpotLightView.get<SSpotLightComponent>(Entity);
                        auto& Transform = SpotLightView.get<STransformComponent>(Entity);
                        ProcessSpotLight(SpotLight, Transform, LightCount);
                    }
                }
            });
            
            Graph.AddDependency(PointLightTask, DLightTask);
            Graph.AddDependency(SpotLightTask, DLightTask);
            Graph.AddDependency(MergeNode, StaticNode);
            Graph.AddDependency(MergeNode, SkeletalNode);

            Graph.Dispatch();
            Graph.Wait();
            
            
            LightData.NumLights = LightCount.load(std::memory_order_acquire);
        }
        
        
        //========================================================================================================================
        
        {
            FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
            RenderGraph.AddPass(RG_Raster, "Write Scene Buffers", Descriptor, [this](ICommandList& CmdList)
            {
                LUMINA_PROFILE_SECTION_COLORED("Write Scene Buffers", tracy::Color::OrangeRed3);
                
                SceneGlobalData.CullData.InstanceNum = (uint32)InstanceData.size();

                // Build the shadow-cull frustum: the camera frustum swept along the sun
                // direction so casters behind / above / beside the camera still write into
                // the shadow indirect buffer. Distance is matched to the CSM far plane used
                // in ProcessDirectionalLight (ShadowMaxDistance = 1000) with headroom for the
                // cascade pullback.
                if (LightData.bHasSun)
                {
                    const glm::vec3 SunDir = glm::normalize(LightData.SunDirection);
                    constexpr float ShadowSweepDistance = 2000.0f;
                    SceneGlobalData.CullData.ShadowFrustum   = SceneGlobalData.CullData.Frustum.Extruded(SunDir, ShadowSweepDistance);
                    SceneGlobalData.CullData.bHasDirectional = 1u;
                }
                else
                {
                    SceneGlobalData.CullData.ShadowFrustum   = SceneGlobalData.CullData.Frustum;
                    SceneGlobalData.CullData.bHasDirectional = 0u;
                }
                
                const SIZE_T SimpleVertexSize   = SimpleVertices.size() * sizeof(FSimpleElementVertex);
                const SIZE_T InstanceDataSize   = InstanceData.size() * sizeof(FGPUInstance);
                const SIZE_T BoneDataSize       = BonesData.size() * sizeof(glm::mat4);
                const SIZE_T IndirectArgsSize   = IndirectDrawArguments.size() * sizeof(FDrawIndirectArguments);
                const SIZE_T ActiveLightsSize   = LightData.NumLights * sizeof(FLight);
                const SIZE_T LightUploadSize    = offsetof(FSceneLightData, Lights) + ActiveLightsSize;
                const SIZE_T BillboardSize      = BillboardInstances.size() * sizeof(FBillboardInstance);
                
                bool bAnyBufferResized = false;
                
                if (RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::Instance], (uint32)InstanceDataSize, 1.2f))
                {
                    bAnyBufferResized = true;
                }
                
                if (RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::InstanceMapping], sizeof(uint32) * InstanceData.size(), 1.2f))
                {
                    bAnyBufferResized = true;
                }

                if (RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::InstanceMappingShadow], sizeof(uint32) * InstanceData.size(), 1.2f))
                {
                    bAnyBufferResized = true;
                }
                
                if (RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::SimpleVertex], (uint32)SimpleVertexSize, 1.2f))
                {
                    bAnyBufferResized = true;
                }
                
                if (RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::Bone], (uint32)BoneDataSize, 1.2f))
                {
                    bAnyBufferResized = true;
                }
                
                if (RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::Indirect], (uint32)IndirectArgsSize, 1.2f))
                {
                    bAnyBufferResized = true;
                }

                if (RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::IndirectShadow], (uint32)IndirectArgsSize, 1.2f))
                {
                    bAnyBufferResized = true;
                }
                
                if (RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::Light], (uint32)LightUploadSize, 1.2f))
                {
                    bAnyBufferResized = true;
                }
                
                if (RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::Billboards], (uint32)BillboardSize, 1.2f))
                {
                    bAnyBufferResized = true;
                }
                
                if (bAnyBufferResized)
                {
                    CreateLayouts();
                }
                
                CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Scene), EResourceStates::CopyDest);
                CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Instance), EResourceStates::CopyDest);
                CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Bone), EResourceStates::CopyDest);
                CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Indirect), EResourceStates::CopyDest);
                CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::IndirectShadow), EResourceStates::CopyDest);
                CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::SimpleVertex), EResourceStates::CopyDest);
                CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Light), EResourceStates::CopyDest);
                CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Billboards), EResourceStates::CopyDest);
                CmdList.CommitBarriers();
                
                CmdList.DisableAutomaticBarriers();
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Scene), &SceneGlobalData, sizeof(FSceneGlobalData));
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Instance), InstanceData.data(), InstanceDataSize);
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Bone), BonesData.data(),  BoneDataSize);
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Indirect), IndirectDrawArguments.data(), IndirectArgsSize);
                // Shadow indirect buffer uses the identical per-batch layout (same FirstInstance
                // offsets, InstanceCount == 0) so ShadowMeshCull.slang can atomically fill it
                // in parallel with the camera-view cull pass.
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::IndirectShadow), IndirectDrawArguments.data(), IndirectArgsSize);
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::SimpleVertex), SimpleVertices.data(), SimpleVertexSize);
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Light), &LightData, LightUploadSize);
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Billboards), BillboardInstances.data(), BillboardSize);
                CmdList.EnableAutomaticBarriers();
            });
        }
    }

    void FForwardRenderScene::ResolveDirtyTransforms()
    {
        FEntityRegistry& Registry = World->GetEntityRegistry();
        auto SingleView = Registry.view<FNeedsTransformUpdate, STransformComponent>(entt::exclude<FRelationshipComponent>);
        auto RelationshipGroup = Registry.group<FNeedsTransformUpdate, FRelationshipComponent>(entt::get<STransformComponent>);
        
        if (!RelationshipGroup.empty())
        {
            TFixedVector<entt::entity, 100> DirtyEntities;
            DirtyEntities.reserve(RelationshipGroup.size());
            for (auto entity : RelationshipGroup)
            {
                DirtyEntities.push_back(entity);
            }
            
            auto RelationshipTransformCallable = [&](uint32 Index)
            {
                entt::entity DirtyEntity = DirtyEntities[Index];
                
                auto& DirtyTransform = RelationshipGroup.get<STransformComponent>(DirtyEntity);
                auto& DirtyRelationship = RelationshipGroup.get<FRelationshipComponent>(DirtyEntity);
                
                if (DirtyRelationship.Parent != entt::null && Registry.valid(DirtyRelationship.Parent))
                {
                    glm::mat4 ParentWorldTransform         = Registry.get<STransformComponent>(DirtyRelationship.Parent).WorldTransform.GetMatrix();
                    glm::mat4 LocalTransform               = DirtyTransform.LocalTransform.GetMatrix();
                    DirtyTransform.WorldTransform          = FTransform(ParentWorldTransform * LocalTransform);
                }
                else
                {
                    DirtyTransform.WorldTransform = DirtyTransform.LocalTransform;
                }
                
                DirtyTransform.CachedMatrix = DirtyTransform.WorldTransform.GetMatrix();
                
                TFunction<void(entt::entity)> UpdateChildrenRecursive;
                UpdateChildrenRecursive = [&](entt::entity ParentEntity)
                {
                    ECS::Utils::ForEachChild(Registry, ParentEntity, [&](entt::entity Child)
                    {
                        auto& ParentTransform = Registry.get<STransformComponent>(ParentEntity);
                        auto& ChildTransform = Registry.get<STransformComponent>(Child);

                        glm::mat4 ParentWorldTransform = ParentTransform.WorldTransform.GetMatrix();
                        glm::mat4 ChildLocalTransform = ChildTransform.LocalTransform.GetMatrix();
                        
                        ChildTransform.WorldTransform = FTransform(ParentWorldTransform * ChildLocalTransform);
                        ChildTransform.CachedMatrix = ChildTransform.WorldTransform.GetMatrix();
                        
                        UpdateChildrenRecursive(Child);
                    });
                };
                
                UpdateChildrenRecursive(DirtyEntity);
            };
            
            if (DirtyEntities.size() > 1000)
            {
                Task::ParallelFor((uint32)DirtyEntities.size(), RelationshipTransformCallable);
            }
            else
            {
                for (uint32 i = 0; i < (uint32)DirtyEntities.size(); ++i)
                {
                    RelationshipTransformCallable(i);
                }
            }
        }

        if (SingleView.size_hint() < 1000)
        {
            SingleView.each([&](STransformComponent& TransformComponent)
            {
                TransformComponent.WorldTransform = TransformComponent.LocalTransform;
                TransformComponent.CachedMatrix = TransformComponent.WorldTransform.GetMatrix();  
            });
        }
        else
        {
            auto WorkFunctor = [](STransformComponent& Transform)
            {
                Transform.WorldTransform = Transform.LocalTransform;
                Transform.CachedMatrix = Transform.WorldTransform.GetMatrix();
            };

            auto Handle = SingleView.handle();
            Task::ParallelFor(Handle->size(), [&](uint32 Index)
            {
                entt::entity Entity = (*Handle)[Index];
                
                if (SingleView.contains(Entity))
                {
                    std::apply(WorkFunctor, SingleView.get(Entity));
                }
            });
        }
        
        Registry.clear<FNeedsTransformUpdate>();
    }

    static uint16 FindOrAddLocalBatch(FForwardRenderScene::FThreadLocalDrawData& Local, const FDrawBatchKey& Key, FRHIVertexShader* VS, FRHIPixelShader* PS)
    {
        // Linear scan: per-thread batch counts are tiny (typically <30) and the table is hot in L1.
        // A hash map would probably (maybe?) be slower at this size and would also fragment the cache.
        const uint32 Count = (uint32)Local.LocalBatches.size();
        for (uint32 i = 0; i < Count; ++i)
        {
            if (Local.LocalBatches[i].Key == Key)
            {
                return (uint16)i;
            }
        }
        
        FForwardRenderScene::FLocalBatchEntry& Entry = Local.LocalBatches.emplace_back();
        Entry.Key          = Key;
        Entry.VertexShader = VS;
        Entry.PixelShader  = PS;
        
        return (uint16)Count;
    }

    static uint16 FindOrAddLocalDraw(FForwardRenderScene::FLocalBatchEntry& Batch, const FDrawKey& Key)
    {
        const uint32 Count = (uint32)Batch.LocalDraws.size();
        for (uint32 i = 0; i < Count; ++i)
        {
            if (Batch.LocalDraws[i] == Key)
            {
                Batch.LocalDrawCounts[i]++;
                return (uint16)i;
            }
        }
        Batch.LocalDraws.push_back(Key);
        Batch.LocalDrawCounts.push_back(1);
        return (uint16)Count;
    }

    void FForwardRenderScene::ProcessStaticMeshEntityInternal(entt::entity Entity, const SStaticMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local)
    {
        CMesh* Mesh = MeshComponent.StaticMesh;
        if (!IsValid(Mesh))
        {
            return;
        }

        const FMeshResource& Resource = Mesh->GetMeshResource();
        const uint64 VBAddress = Mesh->GetVertexBuffer()->GetAddress();
        const uint64 IBAddress = Mesh->GetIndexBuffer()->GetAddress();

        Local.Stats.NumVertices  += Resource.GetNumVertices();
        Local.Stats.NumTriangles += Resource.GetNumTriangles();

        const glm::mat4& TransformMatrix = TransformComponent.CachedMatrix;

        const FAABB     BoundingBox = Mesh->GetAABB().ToWorld(TransformMatrix);
        const glm::vec3 Center      = (BoundingBox.Min + BoundingBox.Max) * 0.5f;
        const glm::vec3 Extents     = BoundingBox.Max - Center;
        const float     Radius      = glm::length(Extents);
        const glm::vec4 SphereBounds = glm::vec4(Center, Radius);

        EInstanceFlags BaseFlags = EInstanceFlags::None;
        if (MeshComponent.bReceiveShadow)
        {
            BaseFlags |= EInstanceFlags::ReceiveShadow;
        }
        if (MeshComponent.bIgnoreOcclusionCulling)
        {
            BaseFlags |= EInstanceFlags::IgnoreOcclusionCulling;
        }

        const uint32 EntityIDPacked = entt::to_integral(Entity);

        for (const FGeometrySurface& Surface : Resource.GeometrySurfaces)
        {
            CMaterialInterface* Material = MeshComponent.GetMaterialForSlot(Surface.MaterialIndex);
            if (!IsValid(Material) || !IsValid(Material->GetMaterial()) || !Material->IsReadyForRender())
            {
                Material = CMaterial::GetDefaultMaterial();
            }

            EInstanceFlags Flags = BaseFlags;
            if (MeshComponent.bCastShadow && Material->DoesCastShadows())
            {
                Flags |= EInstanceFlags::CastShadow;
            }

            const EBlendMode BlendMode  = Material->GetBlendMode();
            const bool bIsTranslucent   = BlendMode == EBlendMode::Translucent || BlendMode == EBlendMode::Additive;
            const bool bIsMasked        = BlendMode == EBlendMode::Masked;
            const bool bIsAdditive      = BlendMode == EBlendMode::Additive;
            const bool bDrawInDepthPass = MeshComponent.bUseAsOccluder && !bIsTranslucent;

            if (bIsTranslucent)
            {
                Flags |= EInstanceFlags::Translucent;
            }
            if (bIsMasked)
            {
                Flags |= EInstanceFlags::Masked;
            }

            FDrawBatchKey BatchKey
            {
                .MaterialID       = (uint64)Material->GetMaterial(),
                .bDrawInDepthPass = (uint32)(bDrawInDepthPass ? 1u : 0u),
                .bTranslucent     = (uint32)(bIsTranslucent   ? 1u : 0u),
                .bMasked          = (uint32)(bIsMasked        ? 1u : 0u),
                .bAdditive        = (uint32)(bIsAdditive      ? 1u : 0u),
            };
            const uint16 LocalBatchIdx = FindOrAddLocalBatch(Local, BatchKey, Material->GetVertexShader(), Material->GetPixelShader());
            const uint16 LocalDrawIdx  = FindOrAddLocalDraw(Local.LocalBatches[LocalBatchIdx], FDrawKey{ Surface.StartIndex, Surface.IndexCount });

            FProcessedDrawItem& Item = Local.Items.emplace_back();
            Item.Instance.Transform                  = TransformMatrix;
            Item.Instance.SphereBounds               = SphereBounds;
            Item.Instance.VBAddress                  = VBAddress;
            Item.Instance.IBAddress                  = IBAddress;
            Item.Instance.EntityID                   = EntityIDPacked;
            Item.Instance.DrawIDAndFlags             = PackDrawIDAndFlags(0, Flags); // DrawID patched at write-out
            Item.Instance.BoneOffsetAndMaterialIndex = PackBoneOffsetAndMaterial(0, (uint16)Material->GetMaterialIndex());
            Item.Instance.CustomData                 = MeshComponent.CustomPrimitiveData.Data.Packed;

            Item.Flags           = Flags;
            Item.MaterialIndex   = (uint16)Material->GetMaterialIndex();
            Item.LocalBatchIndex = LocalBatchIdx;
            Item.LocalDrawIndex  = LocalDrawIdx;
            Item.LocalBoneOffset = ~0u;
        }
    }

    void FForwardRenderScene::ProcessSkeletalMeshEntityInternal(entt::entity Entity, const SSkeletalMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local)
    {
        CMesh* Mesh = MeshComponent.SkeletalMesh;
        if (!IsValid(Mesh))
        {
            return;
        }

        const FMeshResource& Resource = Mesh->GetMeshResource();

        const uint32 LocalBoneOffset = (uint32)Local.BonesData.size();
        Local.BonesData.insert(Local.BonesData.end(), MeshComponent.BoneTransforms.begin(), MeshComponent.BoneTransforms.end());

        Local.Stats.NumVertices  += Resource.GetNumVertices();
        Local.Stats.NumTriangles += Resource.GetNumTriangles();

        const glm::mat4 TransformMatrix = TransformComponent.GetWorldMatrix();

        const FAABB     BoundingBox = Mesh->GetAABB().ToWorld(TransformMatrix);
        const glm::vec3 Center      = (BoundingBox.Min + BoundingBox.Max) * 0.5f;
        const glm::vec3 Extents     = BoundingBox.Max - Center;
        const float     Radius      = glm::length(Extents);
        const glm::vec4 SphereBounds = glm::vec4(Center, Radius);

        EInstanceFlags BaseFlags = EInstanceFlags::Skinned;
        if (MeshComponent.bReceiveShadow)
        {
            BaseFlags |= EInstanceFlags::ReceiveShadow;
        }

        const uint64 VBAddress = Mesh->GetVertexBuffer()->GetAddress();
        const uint64 IBAddress = Mesh->GetIndexBuffer()->GetAddress();
        const uint32 EntityIDPacked = entt::to_integral(Entity);

        for (const FGeometrySurface& Surface : Resource.GeometrySurfaces)
        {
            CMaterialInterface* Material = MeshComponent.GetMaterialForSlot(Surface.MaterialIndex);
            if (!IsValid(Material) || !IsValid(Material->GetMaterial()) || !Material->IsReadyForRender())
            {
                Material = CMaterial::GetDefaultMaterial();
            }

            EInstanceFlags Flags = BaseFlags;
            if (MeshComponent.bCastShadow && Material->DoesCastShadows())
            {
                Flags |= EInstanceFlags::CastShadow;
            }

            const EBlendMode BlendMode  = Material->GetBlendMode();
            const bool bIsTranslucent   = BlendMode == EBlendMode::Translucent || BlendMode == EBlendMode::Additive;
            const bool bIsMasked        = BlendMode == EBlendMode::Masked;
            const bool bIsAdditive      = BlendMode == EBlendMode::Additive;
            const bool bDrawInDepthPass = MeshComponent.bUseAsOccluder && !bIsTranslucent;

            if (bIsTranslucent)
            {
                Flags |= EInstanceFlags::Translucent;
            }
            if (bIsMasked)
            {
                Flags |= EInstanceFlags::Masked;
            }

            FDrawBatchKey BatchKey
            {
                .MaterialID       = (uint64)Material->GetMaterial(),
                .bDrawInDepthPass = (uint32)(bDrawInDepthPass ? 1u : 0u),
                .bTranslucent     = (uint32)(bIsTranslucent   ? 1u : 0u),
                .bMasked          = (uint32)(bIsMasked        ? 1u : 0u),
                .bAdditive        = (uint32)(bIsAdditive      ? 1u : 0u),
            };
            const uint16 LocalBatchIdx = FindOrAddLocalBatch(Local, BatchKey, Material->GetVertexShader(), Material->GetPixelShader());
            const uint16 LocalDrawIdx  = FindOrAddLocalDraw(Local.LocalBatches[LocalBatchIdx], FDrawKey{ Surface.StartIndex, Surface.IndexCount });

            FProcessedDrawItem& Item = Local.Items.emplace_back();
            Item.Instance.Transform                  = TransformMatrix;
            Item.Instance.SphereBounds               = SphereBounds;
            Item.Instance.VBAddress                  = VBAddress;
            Item.Instance.IBAddress                  = IBAddress;
            Item.Instance.EntityID                   = EntityIDPacked;
            Item.Instance.DrawIDAndFlags             = PackDrawIDAndFlags(0, Flags);
            Item.Instance.BoneOffsetAndMaterialIndex = PackBoneOffsetAndMaterial(0, (uint16)Material->GetMaterialIndex());
            Item.Instance.CustomData                 = MeshComponent.CustomPrimitiveData.Data.Packed;

            Item.Flags           = Flags;
            Item.MaterialIndex   = (uint16)Material->GetMaterialIndex();
            Item.LocalBatchIndex = LocalBatchIdx;
            Item.LocalDrawIndex  = LocalDrawIdx;
            Item.LocalBoneOffset = LocalBoneOffset;
        }
    }

    void FForwardRenderScene::MergeMeshDrawData(TVector<FThreadLocalDrawData>& ThreadLocal)
    {
        LUMINA_PROFILE_SECTION("Merge Mesh Draw Data");

        const uint32 NumThreads = (uint32)ThreadLocal.size();

        // Bones still have to be merged serially because skinned meshes reference them by absolute index.
        // It's a few KB even at 500K instances, so it's fine.
        TVector<uint32> ThreadBoneBase(NumThreads, 0u);
        uint32 TotalInstances = 0;
        for (uint32 t = 0; t < NumThreads; ++t)
        {
            FThreadLocalDrawData& Local = ThreadLocal[t];
            ThreadBoneBase[t] = (uint32)BonesData.size();
            BonesData.insert(BonesData.end(), Local.BonesData.begin(), Local.BonesData.end());
            RenderStats.NumVertices  += Local.Stats.NumVertices;
            RenderStats.NumTriangles += Local.Stats.NumTriangles;
            TotalInstances += (uint32)Local.Items.size();
        }

        if (TotalInstances == 0)
        {
            return;
        }

        // We walk per-thread *batch* tables (a few dozen entries each).
        // Linear search is the right structure for unique-batch counts in the tens.
        TVector<FDrawBatchKey> GlobalBatchKeys;
        GlobalBatchKeys.reserve(64);
        DrawCommands.reserve(64);

        for (FThreadLocalDrawData& Local : ThreadLocal)
        {
            for (FLocalBatchEntry& LocalBatch : Local.LocalBatches)
            {
                uint32 GlobalIdx = ~0u;
                const uint32 NumGlobal = (uint32)GlobalBatchKeys.size();
                for (uint32 g = 0; g < NumGlobal; ++g)
                {
                    if (GlobalBatchKeys[g] == LocalBatch.Key)
                    {
                        GlobalIdx = g;
                        break;
                    }
                }
                if (GlobalIdx == ~0u)
                {
                    GlobalIdx = NumGlobal;
                    GlobalBatchKeys.push_back(LocalBatch.Key);

                    FMeshDrawCommand& NewCmd = DrawCommands.emplace_back();
                    NewCmd.VertexShader     = LocalBatch.VertexShader;
                    NewCmd.PixelShader      = LocalBatch.PixelShader;
                    NewCmd.IndirectDrawOffset = 0;
                    NewCmd.DrawCount        = 0;
                    NewCmd.bDrawInDepthPass = LocalBatch.Key.bDrawInDepthPass;
                    NewCmd.bTranslucent     = LocalBatch.Key.bTranslucent;
                    NewCmd.bMasked          = LocalBatch.Key.bMasked;
                    NewCmd.bAdditive        = LocalBatch.Key.bAdditive;
                }
                LocalBatch.GlobalBatchIndex = GlobalIdx;
            }
        }

        const uint32 NumBatches = (uint32)GlobalBatchKeys.size();

        // For each global batch we collect the unique (StartIndex, IndexCount) draws from every thread
        // and stamp each thread's local-draw indices with the resolved global-draw position.
        TVector<TVector<FDrawKey>> GlobalDrawsPerBatch(NumBatches);

        for (FThreadLocalDrawData& Local : ThreadLocal)
        {
            for (FLocalBatchEntry& LocalBatch : Local.LocalBatches)
            {
                TVector<FDrawKey>& GlobalDraws = GlobalDrawsPerBatch[LocalBatch.GlobalBatchIndex];
                const uint32 NumLocal = (uint32)LocalBatch.LocalDraws.size();
                LocalBatch.LocalToGlobalDraw.resize(NumLocal);

                for (uint32 ld = 0; ld < NumLocal; ++ld)
                {
                    const FDrawKey& K = LocalBatch.LocalDraws[ld];
                    const uint32 NumGlobal = (uint32)GlobalDraws.size();
                    uint32 GlobalDraw = ~0u;
                    for (uint32 g = 0; g < NumGlobal; ++g)
                    {
                        if (GlobalDraws[g] == K)
                        {
                            GlobalDraw = g;
                            break;
                        }
                    }
                    if (GlobalDraw == ~0u)
                    {
                        GlobalDraw = NumGlobal;
                        GlobalDraws.push_back(K);
                    }
                    LocalBatch.LocalToGlobalDraw[ld] = GlobalDraw;
                }
            }
        }

        // Each batch gets a contiguous block of draw args. Compute a global "draw" index =
        // batch base + draw offset within batch. Cull pass writes InstanceCount; we leave it 0.
        TVector<uint32> BatchDrawArgBase(NumBatches);
        uint32 TotalDrawArgs = 0;
        for (uint32 b = 0; b < NumBatches; ++b)
        {
            BatchDrawArgBase[b]                 = TotalDrawArgs;
            DrawCommands[b].IndirectDrawOffset  = TotalDrawArgs;
            DrawCommands[b].DrawCount           = (uint32)GlobalDrawsPerBatch[b].size();
            RenderStats.NumDraws                += DrawCommands[b].DrawCount;
            TotalDrawArgs                       += (uint32)GlobalDrawsPerBatch[b].size();
        }

        IndirectDrawArguments.resize(TotalDrawArgs);

        // We need both totals (to compute StartInstanceLocation) and per-thread counts (so each
        // thread knows where it can write its slice without contention in phase 6).
        TVector<uint32> DrawInstanceCounts(TotalDrawArgs, 0u);
        // ThreadDrawCounts[t * TotalDrawArgs + d]: how many instances thread t contributes to draw d.
        TVector<uint32> ThreadDrawCounts((size_t)NumThreads * TotalDrawArgs, 0u);

        for (uint32 t = 0; t < NumThreads; ++t)
        {
            FThreadLocalDrawData& Local = ThreadLocal[t];
            const size_t Row = (size_t)t * TotalDrawArgs;
            for (FLocalBatchEntry& LocalBatch : Local.LocalBatches)
            {
                const uint32 BatchBase  = BatchDrawArgBase[LocalBatch.GlobalBatchIndex];
                const uint32 NumLocal   = (uint32)LocalBatch.LocalDrawCounts.size();
                for (uint32 ld = 0; ld < NumLocal; ++ld)
                {
                    const uint32 GlobalDraw = BatchBase + LocalBatch.LocalToGlobalDraw[ld];
                    const uint32 Count      = LocalBatch.LocalDrawCounts[ld];
                    ThreadDrawCounts[Row + GlobalDraw] += Count;
                    DrawInstanceCounts[GlobalDraw]      += Count;
                }
            }
        }

        // DrawInstanceOffsets[d] = where draw d's instances start in InstanceData.
        // ThreadDrawWriteBase[t * TotalDrawArgs + d] = where thread t's instances for draw d start.
        TVector<uint32> DrawInstanceOffsets(TotalDrawArgs);
        {
            uint32 Running = 0;
            for (uint32 d = 0; d < TotalDrawArgs; ++d)
            {
                DrawInstanceOffsets[d] = Running;
                Running += DrawInstanceCounts[d];
            }
            DEBUG_ASSERT(Running == TotalInstances);
        }

        TVector<uint32> ThreadDrawWriteBase((size_t)NumThreads * TotalDrawArgs);
        {
            // For each draw, hand each thread a slice starting at the running cursor.
            TVector<uint32> Running(TotalDrawArgs);
            for (uint32 d = 0; d < TotalDrawArgs; ++d)
            {
                Running[d] = DrawInstanceOffsets[d];
            }
            for (uint32 t = 0; t < NumThreads; ++t)
            {
                const size_t Row = (size_t)t * TotalDrawArgs;
                for (uint32 d = 0; d < TotalDrawArgs; ++d)
                {
                    ThreadDrawWriteBase[Row + d] = Running[d];
                    Running[d] += ThreadDrawCounts[Row + d];
                }
            }
        }

        // Fill IndirectDrawArguments with their final values.
        {
            uint32 DrawCursor = 0;
            for (uint32 b = 0; b < NumBatches; ++b)
            {
                const TVector<FDrawKey>& GlobalDraws = GlobalDrawsPerBatch[b];
                for (uint32 i = 0; i < GlobalDraws.size(); ++i, ++DrawCursor)
                {
                    FDrawIndirectArguments& Args = IndirectDrawArguments[DrawCursor];
                    Args.VertexCount           = GlobalDraws[i].IndexCount;
                    Args.InstanceCount         = 0u; // cull pass increments
                    Args.StartVertexLocation   = GlobalDraws[i].StartIndex;
                    Args.StartInstanceLocation = DrawInstanceOffsets[DrawCursor];
                    RenderStats.NumInstances  += DrawInstanceCounts[DrawCursor];
                }
            }
        }

        // Each thread owns a disjoint set of (thread, draw) slices in the global InstanceData,
        // so there are no atomics. Cursors are local to each worker.
        InstanceData.resize(TotalInstances);

        {
            LUMINA_PROFILE_SECTION("Parallel Instance Write");

            FTaskGraph WriteGraph;
            WriteGraph.AddParallelFor(NumThreads, 1, [&](const Task::FParallelRange& Range)
            {
                for (uint32 t = Range.Start; t < Range.End; ++t)
                {
                    FThreadLocalDrawData& Local = ThreadLocal[t];
                    const uint32 BoneBase = ThreadBoneBase[t];
                    const size_t Row      = (size_t)t * TotalDrawArgs;

                    // Per-thread cursors. Sized to TotalDrawArgs but the worker only ever touches
                    // entries for draws it actually contributes to (most are read-once).
                    TVector<uint32> Cursors(TotalDrawArgs);
                    for (uint32 d = 0; d < TotalDrawArgs; ++d)
                    {
                        Cursors[d] = ThreadDrawWriteBase[Row + d];
                    }

                    for (FProcessedDrawItem& Item : Local.Items)
                    {
                        const FLocalBatchEntry& LocalBatch = Local.LocalBatches[Item.LocalBatchIndex];
                        const uint32 GlobalDraw = BatchDrawArgBase[LocalBatch.GlobalBatchIndex]
                                                + LocalBatch.LocalToGlobalDraw[Item.LocalDrawIndex];

                        const uint32 WriteIdx = Cursors[GlobalDraw]++;

                        Item.Instance.DrawIDAndFlags = PackDrawIDAndFlags(GlobalDraw, Item.Flags);

                        if (Item.LocalBoneOffset != ~0u)
                        {
                            Item.Instance.BoneOffsetAndMaterialIndex = PackBoneOffsetAndMaterial(
                                (uint16)(BoneBase + Item.LocalBoneOffset),
                                Item.MaterialIndex);
                        }

                        InstanceData[WriteIdx] = Item.Instance;
                    }
                }
            });
            WriteGraph.Dispatch();
            WriteGraph.Wait();
        }

        RenderStats.NumBatches = NumBatches;
        OpaqueDrawList.reserve(NumBatches);
        TranslucentDrawList.reserve(NumBatches);
        for (uint32 i = 0; i < NumBatches; ++i)
        {
            if (DrawCommands[i].bTranslucent)
            {
                TranslucentDrawList.push_back(i);
            }
            else
            {
                OpaqueDrawList.push_back(i);
            }
        }
    }

    void FForwardRenderScene::ProcessPointLight(const SPointLightComponent& PointLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount)
    {
        auto Lights = LightCount.fetch_add(1, std::memory_order_acquire);
        if (LightCount >= MAX_LIGHTS - 1)
        {
            NotifyMaxLightsHit();
            return;
        }
        
        FLight Light;
        Light.Flags                 = LIGHT_TYPE_POINT;
        Light.Falloff               = PointLight.Falloff;
        Light.Color                 = PackColor(glm::vec4(PointLight.LightColor, 1.0));
        Light.Intensity             = PointLight.Intensity;
        Light.Radius                = PointLight.Attenuation;
        Light.Position              = TransformComponent.WorldTransform.Location;
        
        FViewVolume LightView(90.0f, 1.0f, 0.01f, Light.Radius);
        
        auto SetView = [&Light](FViewVolume& View, uint32 Index)
        {
            switch (Index)
            {
            case 0: // +X
                View.SetView(Light.Position, FViewVolume::RightAxis, FViewVolume::DownAxis);
                break;
            case 1: // -X
                View.SetView(Light.Position, FViewVolume::LeftAxis, FViewVolume::DownAxis);
                break;
            case 2: // +Y
                View.SetView(Light.Position, FViewVolume::UpAxis, FViewVolume::ForwardAxis);
                break;
            case 3: // -Y
                View.SetView(Light.Position, FViewVolume::DownAxis, FViewVolume::BackwardAxis);
                break;
            case 4: // +Z
                View.SetView(Light.Position, FViewVolume::ForwardAxis, FViewVolume::DownAxis);
                break;
            case 5: // -Z
                View.SetView(Light.Position, FViewVolume::BackwardAxis, FViewVolume::DownAxis);
                break;
            default:
                UNREACHABLE();
            }
        };
        
        if (PointLight.bCastShadows)
        {
            int32 TileIndex = ShadowAtlas.AllocateTile();
            
            if (TileIndex != INDEX_NONE)
            {
                const FShadowTile& Tile = ShadowAtlas.GetTile(TileIndex);
        
                for (int Face = 0; Face < 6; ++Face)
                {
                    SetView(LightView, Face);
                    
                    Light.ViewProjection[Face]              = LightView.ToReverseDepthViewProjectionMatrix();
                    Light.Shadow[Face].ShadowMapIndex       = TileIndex;
                    Light.Shadow[Face].ShadowMapLayer       = Face;
                    Light.Shadow[Face].AtlasUVOffset        = Tile.UVOffset;
                    Light.Shadow[Face].AtlasUVScale         = Tile.UVScale;
                    Light.Shadow[Face].LightIndex           = (int32)Lights;
                }
                
                PackedShadows[(uint32)ELightType::Point].push_back(Light.Shadow[0]);
            }
        }
        else
        {
            Light.Shadow[0].ShadowMapIndex = INDEX_NONE;
        }
        
        LightData.Lights[Lights] = Light;
    }

    void FForwardRenderScene::ProcessSpotLight(const SSpotLightComponent& SpotLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount)
    {
        auto Lights = LightCount.fetch_add(1, std::memory_order_acquire);
        if (LightCount >= MAX_LIGHTS - 1)
        {
            NotifyMaxLightsHit();
            return;
        }
        
        glm::vec3 UpdatedForward    = TransformComponent.GetRotation() * FViewVolume::ForwardAxis;
        glm::vec3 UpdatedUp         = TransformComponent.GetRotation() * FViewVolume::UpAxis;
                
        float InnerDegrees = SpotLight.InnerConeAngle;
        float OuterDegrees = SpotLight.OuterConeAngle;
        
        float InnerCos = glm::cos(glm::radians(InnerDegrees));
        float OuterCos = glm::cos(glm::radians(OuterDegrees));
        
        FViewVolume ViewVolume(OuterDegrees * 2.00f, 1.0f, 0.01f, SpotLight.Attenuation);
        ViewVolume.SetView(TransformComponent.WorldTransform.Location, -UpdatedForward, UpdatedUp);
        
        FLight Light;
        Light.Flags                 = LIGHT_TYPE_SPOT;
        Light.Position              = TransformComponent.WorldTransform.Location;
        Light.Direction             = glm::normalize(UpdatedForward);
        Light.Falloff               = SpotLight.Falloff;
        Light.Color                 = PackColor(glm::vec4(SpotLight.LightColor, 1.0));
        Light.Intensity             = SpotLight.Intensity;
        Light.Radius                = SpotLight.Attenuation;
        Light.Angles                = glm::vec2(InnerCos, OuterCos);
        Light.ViewProjection[0]     = ViewVolume.ToReverseDepthViewProjectionMatrix();
        
        if (SpotLight.bCastShadows)
        {
            int32 TileIndex = ShadowAtlas.AllocateTile();
            if (TileIndex != INDEX_NONE)
            {
                const FShadowTile& Tile             = ShadowAtlas.GetTile(TileIndex);
                Light.Shadow[0].ShadowMapIndex      = TileIndex;
                Light.Shadow[0].ShadowMapLayer      = 6;
                Light.Shadow[0].AtlasUVOffset       = Tile.UVOffset;
                Light.Shadow[0].AtlasUVScale        = Tile.UVScale;
                Light.Shadow[0].LightIndex          = (int32)Lights;
        
            }
            
            PackedShadows[(uint32)ELightType::Spot].push_back(Light.Shadow[0]);
        }
        else
        {
            Light.Shadow[0].ShadowMapIndex = INDEX_NONE;
        }
        
        LightData.Lights[Lights] = Light;
    }

    void FForwardRenderScene::ProcessDirectionalLight(const SDirectionalLightComponent& DirectionalLight, TAtomic<uint32>& LightCount)
    {
        LightData.bHasSun = true;
        const FViewVolume& ViewVolume = SceneViewport->GetViewVolume();
        
        const float NearClip = ViewVolume.GetNear();
        const float FarClip  = ViewVolume.GetFar();
        
        FLight Light            = {};
        Light.Flags             = LIGHT_TYPE_DIRECTIONAL;
        Light.Color             = PackColor(glm::vec4(DirectionalLight.Color, 1.0));
        Light.Intensity         = DirectionalLight.Intensity;
        Light.Direction         = glm::normalize(DirectionalLight.Direction);
        LightData.SunDirection  = Light.Direction;
        
        // (logarithmic + uniform blend). Lambda tuned so the last cascade
        // does not dwarf the preceding ones: at Lambda=0.92 + far=1000 the
        // final slice was ~10x the previous, producing the visible "hard
        // morph" from sharp to very blurry shadows. 0.75 gives a smoother
        // progression while still concentrating resolution near the camera.
        //
        // ShadowNear is clamped above the camera near so the log term does
        // not bunch the first couple of splits into a tiny sliver.
        constexpr float CascadeSplitLambda  = 0.75f;
        constexpr float ShadowMaxDistance   = 300.0f;
        constexpr float ShadowMinDistance   = 1.0f;

        const float ShadowFar  = glm::min(FarClip, ShadowMaxDistance);
        const float ShadowNear = glm::max(NearClip, ShadowMinDistance);
        const float ClipRange  = ShadowFar - ShadowNear;
        const float MinDepth   = ShadowNear;
        const float MaxDepth   = ShadowFar;
        const float DepthRatio = MaxDepth / glm::max(MinDepth, 0.0001f);
        
        float CascadeFarDistances[NumCascades];
        for (int i = 0; i < NumCascades; ++i)
        {
            const float P       = (float)(i + 1) / (float)NumCascades;
            const float LogD    = MinDepth * glm::pow(DepthRatio, P);
            const float UniD    = MinDepth + ClipRange * P;
            const float D       = CascadeSplitLambda * (LogD - UniD) + UniD;
            CascadeFarDistances[i]      = D;
            LightData.CascadeSplits[i]  = D; // World-distance, view-space Z.
        }
        
        const glm::mat4& CamView   = ViewVolume.GetViewMatrix();
        const float      CamFOV    = ViewVolume.GetFOV();
        const float      CamAspect = ViewVolume.GetAspectRatio();
        const glm::vec3  LightDir  = Light.Direction; // Toward the sun.
        
        float LastSplitDistance = ShadowNear;
        for (int i = 0; i < NumCascades; ++i)
        {
            const float SplitNear = LastSplitDistance;
            const float SplitFar  = CascadeFarDistances[i];
        
            // World-space corners of the camera sub-frustum [SplitNear, SplitFar].
            // Use a standard-Z perspective so ComputeFrustumCorners can un-project the
            // canonical NDC cube without caring about the engine's reverse-Z convention.
            const glm::mat4 SliceProj = glm::perspective(glm::radians(CamFOV), CamAspect, SplitNear, SplitFar);
            const glm::mat4 SliceVP   = SliceProj * CamView;
        
            glm::vec3 Corners[8];
            FFrustum::ComputeFrustumCorners(SliceVP, Corners);
        
            // Bound the slice with a sphere, rotation-invariant, so the cascade
            // size doesn't pulse as the camera turns.
            glm::vec3 SphereCenter(0.0f);
            for (int j = 0; j < 8; ++j)
            {
                SphereCenter += Corners[j];
            }
            SphereCenter /= 8.0f;
        
            float Radius = 0.0f;
            for (int j = 0; j < 8; ++j)
            {
                Radius = glm::max(Radius, glm::length(Corners[j] - SphereCenter));
            }
            // Round up to prevent radius jitter from sub-pixel camera movement.
            Radius = std::ceil(Radius * 16.0f) / 16.0f;
        
            // Build a light-space view centered on the sphere. BackDistance pushes
            // the light eye behind the cascade volume so off-screen occluders
            // (e.g. things directly above the cascade) still write into the depth
            // texture.
            constexpr float BackDistance = 100.0f;
            const float     OrthoRange   = Radius * 2.0f + BackDistance;
        
            glm::mat4 LightView = glm::lookAt(
                SphereCenter + LightDir * (Radius + BackDistance),
                SphereCenter,
                FViewVolume::UpAxis);
        
            // Project the cascade origin into light space, snap the
            // XY components to the shadow texel grid, and rebuild the view from the
            // snapped origin.
            {
                const glm::vec4 OriginLS = LightView * glm::vec4(SphereCenter, 1.0f);
                const float     TexelSize = (Radius * 2.0f) / (float)GCSMResolution;
                glm::vec4 SnappedOriginLS = OriginLS;
                SnappedOriginLS.x = std::floor(OriginLS.x / TexelSize) * TexelSize;
                SnappedOriginLS.y = std::floor(OriginLS.y / TexelSize) * TexelSize;
                const glm::vec4 SnappedOriginWS = glm::inverse(LightView) * SnappedOriginLS;
                const glm::vec3 SnappedCenter   = glm::vec3(SnappedOriginWS);
                LightView = glm::lookAt(
                    SnappedCenter + LightDir * (Radius + BackDistance),
                    SnappedCenter,
                    FViewVolume::UpAxis);
            }
        
            // Standard-Z [0,1] LH ortho. Near plane is at the eye (0), far at the
            // far face of the slab (OrthoRange). The full sphere fits inside.
            const glm::mat4 LightProjection = glm::ortho(
                -Radius, +Radius,
                -Radius, +Radius,
                0.0f, OrthoRange);
        
            Light.ViewProjection[i] = LightProjection * LightView;
            LastSplitDistance = SplitFar;
        }
        
        LightCount.fetch_add(1, std::memory_order_acquire);
        LightData.Lights[0] = Light;
    }

    void FForwardRenderScene::ProcessBatchedLines(FLineBatcherComponent& Batcher)
    {
        if (Batcher.Lines.empty())
        {
            return;
        }
        
        for (FLineBatcherComponent::FLineInstance& Line : Batcher.Lines)
        {
            if (!Line.bSingleFrame && Line.RemainingLifetime >= 0.0f)
            {
                Line.RemainingLifetime -= SceneGlobalData.DeltaTime;
            }
        }
        
        struct FLineWithVertices
        {
            FLineBatcherComponent::FLineInstance Line;
            FSimpleElementVertex Vertex0;
            FSimpleElementVertex Vertex1;
        };
        
        TVector<FLineWithVertices> AliveLinesWithVertices;
        AliveLinesWithVertices.reserve(Batcher.Lines.size());
        
        TVector<FLineBatcherComponent::FLineInstance> NewLines;
        TVector<FSimpleElementVertex> NewVertices;
        NewLines.reserve(Batcher.Lines.size());
        NewVertices.reserve(Batcher.Vertices.size());
        
        for (const FLineBatcherComponent::FLineInstance& Line : Batcher.Lines)
        {
            FLineWithVertices LineData;
            LineData.Line = Line;
            LineData.Vertex0 = Batcher.Vertices[Line.StartVertexIndex];
            LineData.Vertex1 = Batcher.Vertices[Line.StartVertexIndex + 1];
            AliveLinesWithVertices.emplace_back(LineData);
        }
        
        eastl::sort(AliveLinesWithVertices.begin(), AliveLinesWithVertices.end(), [](const FLineWithVertices& A, const FLineWithVertices& B)
        {
            if (A.Line.bDepthTest != B.Line.bDepthTest)
            {
                return A.Line.bDepthTest < B.Line.bDepthTest;
            }
            return A.Line.Thickness < B.Line.Thickness;
        });
        
        uint32 CurrentVertexIndex = 0;
        for (const FLineWithVertices& LineData : AliveLinesWithVertices)
        {
            FLineBatcherComponent::FLineInstance NewLine = LineData.Line;
            NewLine.StartVertexIndex = CurrentVertexIndex;
        
            if (!LineData.Line.bSingleFrame && LineData.Line.RemainingLifetime > 0.0f)
            {
                NewLines.emplace_back(NewLine);
                NewVertices.emplace_back(LineData.Vertex0);
                NewVertices.emplace_back(LineData.Vertex1);
                CurrentVertexIndex += 2;
            }
        }
        
        Batcher.Lines = std::move(NewLines);
        Batcher.Vertices = std::move(NewVertices);
        
        if (!AliveLinesWithVertices.empty())
        {
            SimpleVertices.clear();
            SimpleVertices.reserve(AliveLinesWithVertices.size() * 2);
            for (const FLineWithVertices& LineData : AliveLinesWithVertices)
            {
                SimpleVertices.emplace_back(LineData.Vertex0);
                SimpleVertices.emplace_back(LineData.Vertex1);
            }
        
            LineBatches.clear();
        
            FLineBatch CurrentBatch;
            CurrentBatch.StartVertex = 0;
            CurrentBatch.VertexCount = 2;
            CurrentBatch.Thickness = AliveLinesWithVertices[0].Line.Thickness;
            CurrentBatch.bDepthTest = AliveLinesWithVertices[0].Line.bDepthTest;
        
            for (size_t i = 1; i < AliveLinesWithVertices.size(); ++i)
            {
                const auto& LineData = AliveLinesWithVertices[i];
        
                if (glm::epsilonEqual(LineData.Line.Thickness, CurrentBatch.Thickness, LE_SMALL_NUMBER) &&
                    LineData.Line.bDepthTest == CurrentBatch.bDepthTest)
                {
                    CurrentBatch.VertexCount += 2;
                }
                else
                {
                    RenderStats.NumVertices += CurrentBatch.VertexCount;
                    LineBatches.emplace_back(CurrentBatch);
        
                    CurrentBatch.StartVertex = (uint32)SimpleVertices.size() - (uint32)AliveLinesWithVertices.size() * 2 + (uint32)(i * 2);
                    CurrentBatch.VertexCount = 2;
                    CurrentBatch.Thickness = LineData.Line.Thickness;
                    CurrentBatch.bDepthTest = LineData.Line.bDepthTest;
                }
            }
        
            RenderStats.NumVertices += CurrentBatch.VertexCount;
            LineBatches.emplace_back(CurrentBatch);
        }
    }

    void FForwardRenderScene::NotifyMaxLightsHit()
    {
        LOG_WARN("[Rendering] - Maximum Lights Hit! {}", MAX_LIGHTS);
    }

    void FForwardRenderScene::DrawBillboard(FRHIImage* Image, const glm::vec3& Location, float Scale)
    {
        if (Image->GetTextureCacheIndex() == -1)
        {
            return;
        }
        
        FBillboardInstance& Billboard   = BillboardInstances.emplace_back();
        Billboard.TextureIndex          = Image->GetTextureCacheIndex();
        Billboard.Position              = Location;
        Billboard.Size                  = Scale;
        Billboard.EntityID              = entt::null;
    }

    void FForwardRenderScene::ResetPass(FRenderGraph& RenderGraph)
    {
        SimpleVertices.clear();
        DrawCommands.clear();
        OpaqueDrawList.clear();
        TranslucentDrawList.clear();
        IndirectDrawArguments.clear();
        InstanceData.clear();
        LightData.NumLights = 0;
        ShadowAtlas.FreeTiles();
        BonesData.clear();
        BillboardInstances.clear();
        RenderStats = {};

        for (int i = 0; i < (int)ELightType::Num; ++i)
        {
            PackedShadows[i].clear();
        }
        
        if (DrawCommands.empty())
        {
            FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
            RenderGraph.AddPass(RG_Compute, "Clear Depth Pass", Descriptor, [&] (ICommandList& CmdList)
            {
               CmdList.ClearImageUInt(GetNamedImage(ENamedImage::DepthAttachment), AllSubresources, 0); 
            });
        }
    }

    void FForwardRenderScene::CullPass(FRenderGraph& RenderGraph)
    {
        if (DrawCommands.empty())
        {
            return;
        }

        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Compute, "Cull Pass", Descriptor, [&] (ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Cull Pass", tracy::Color::Pink2);

            const uint32 Num           = (uint32)InstanceData.size();
            const uint32 NumWorkGroups = (Num + 255) / 256;

            // Camera-view culling (frustum + occlusion). Fills the main indirect buffer
            // used by the depth pre-pass, base pass, billboards, etc.
            {
                FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("MeshCull.slang");

                FComputePipelineDesc PipelineDesc;
                PipelineDesc.SetComputeShader(ComputeShader);
                PipelineDesc.AddBindingLayout(SceneBindingLayout);
                PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

                FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

                FComputeState State;
                State.SetPipeline(Pipeline);
                State.AddBindingSet(SceneBindingSet);
                State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
                CmdList.SetComputeState(State);

                CmdList.Dispatch(NumWorkGroups, 1, 1);
            }

            // Shadow-caster culling. Writes a parallel indirect buffer consumed by
            // the CSM, spot, and point shadow passes so casters outside the camera
            // frustum still produce shadows that fall back into the camera view.
            {
                FRHIComputeShaderRef ShadowCullShader = FShaderLibrary::GetComputeShader("ShadowMeshCull.slang");

                FComputePipelineDesc PipelineDesc;
                PipelineDesc.SetComputeShader(ShadowCullShader);
                PipelineDesc.AddBindingLayout(SceneBindingLayout);
                PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

                FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

                FComputeState State;
                State.SetPipeline(Pipeline);
                State.AddBindingSet(SceneBindingSet);
                State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
                CmdList.SetComputeState(State);

                CmdList.Dispatch(NumWorkGroups, 1, 1);
            }
        });
    }

    void FForwardRenderScene::DepthPrePass(FRenderGraph& RenderGraph)
    {
        if (DrawCommands.empty())
        {
            return;
        }
        
        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Pre-Depth Pass", Descriptor, [&] (ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Pre-Depth Pass", tracy::Color::Orange);
        
            FRenderPassDesc::FAttachment Depth; Depth
                .SetImage(GetNamedImage(ENamedImage::DepthAttachment))
                .SetDepthClearValue(0.0f);
            
            FRenderPassDesc RenderPass; RenderPass
                .SetDepthAttachment(Depth)
                .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());
            
            FRenderState RenderState; RenderState
                .SetDepthStencilState(FDepthStencilState().SetDepthFunc(EComparisonFunc::Greater))
                .SetRasterState(FRasterState().EnableDepthClip());
            
            FRHIVertexShaderRef DepthOnlyVertexShader = FShaderLibrary::GetVertexShader("DepthPrePass.slang");

            for (uint32 Idx : OpaqueDrawList)
            {
                const FMeshDrawCommand& Batch = DrawCommands[Idx];

                FGraphicsPipelineDesc Desc; Desc
                    .SetRenderState(RenderState)
                    .AddBindingLayout(SceneBindingLayout)
                    .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

                if (Batch.bMasked)
                {
                    // Masked materials need the full material shader for alpha clip
                    Desc.SetVertexShader(Batch.VertexShader);
                    Desc.SetPixelShader(Batch.PixelShader);
                }
                else
                {
                    Desc.SetVertexShader(DepthOnlyVertexShader);
                }

                FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

                FGraphicsState GraphicsState;
                GraphicsState.SetRenderPass(RenderPass);
                GraphicsState.SetViewportState(SceneViewportState);
                GraphicsState.SetPipeline(Pipeline);
                GraphicsState.AddBindingSet(SceneBindingSet);
                GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
                GraphicsState.SetIndirectParams(GetNamedBuffer(ENamedBuffer::Indirect));

                CmdList.SetGraphicsState(GraphicsState);
                CmdList.DrawIndirect(Batch.DrawCount, Batch.IndirectDrawOffset * sizeof(FDrawIndirectArguments));
            }
        });
    }

    void FForwardRenderScene::DepthPyramidPass(FRenderGraph& RenderGraph)
    {
        if (DrawCommands.empty())
        {
            return;
        }

        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        Descriptor->SetFlag(ERGExecutionFlags::Async);
        RenderGraph.AddPass(RG_Compute, "Depth Pyramid Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Depth Pyramid Pass", tracy::Color::Orange);

            FRHIImage* DepthPyramid = GetNamedImage(ENamedImage::DepthPyramid);
            FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("DepthPyramidMips.slang");
            int MipCount = DepthPyramid->GetDescription().NumMips;

            // Create binding layout and pipeline once, reused for all mip levels
            FBindingLayoutDesc LayoutDesc;
            LayoutDesc.AddItem(FBindingLayoutItem::Texture_SRV(0));
            LayoutDesc.AddItem(FBindingLayoutItem::Texture_UAV(1));
            LayoutDesc.SetVisibility(ERHIShaderType::Compute);
            FRHIBindingLayout* Layout = BindingCache.GetOrCreateBindingLayout(LayoutDesc);

            FComputePipelineDesc PipelineDesc;
            PipelineDesc.AddBindingLayout(Layout);
            PipelineDesc.CS = ComputeShader;
            PipelineDesc.DebugName = "Depth Pyramid Mips";
            FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

            CmdList.SetEnableUavBarriersForImage(DepthPyramid, false);

            for (int i = 0; i < MipCount; ++i)
            {
                LUMINA_PROFILE_SECTION_COLORED("Process Mip", tracy::Color::Yellow4);

                // Both paths need a min-reduction clamp sampler so that a single
                // bilinear tap collapses the 2x2 footprint to the farthest depth
                // (min value in reverse-Z). For mip 0 the source is 1:1 with the
                // destination so the reduction is a no-op but the sampler type
                // must still be explicit.
                FRHISamplerRef Sampler = TStaticRHISampler<true, false, AM_Clamp, AM_Clamp, AM_Clamp, ESamplerReductionType::Minimum>::GetRHI();
                FBindingSetDesc SetDesc;
                if (i == 0)
                {
                    SetDesc.AddItem(FBindingSetItem::TextureSRV(0, GetNamedImage(ENamedImage::DepthAttachment), Sampler));
                }
                else
                {
                    SetDesc.AddItem(FBindingSetItem::TextureSRV(0, DepthPyramid, Sampler, DepthPyramid->GetFormat(), FTextureSubresourceSet(i - 1, 1, 0, 1)));
                }

                SetDesc.AddItem(FBindingSetItem::TextureUAV(1, DepthPyramid, DepthPyramid->GetFormat(), FTextureSubresourceSet(i, 1, 0, 1)));

                FRHIBindingSet* Set = BindingCache.GetOrCreateBindingSet(SetDesc, Layout);

                FComputeState State;
                State.AddBindingSet(Set);
                State.SetPipeline(Pipeline);

                CmdList.SetComputeState(State);

                uint32 LevelWidth = DepthPyramid->GetSizeX() >> i;
                uint32 LevelHeight = DepthPyramid->GetSizeY() >> i;

                LevelWidth = std::max(LevelWidth, 1u);
                LevelHeight = std::max(LevelHeight, 1u);

                glm::vec2 Data(LevelWidth,LevelHeight);
                CmdList.SetPushConstants(&Data, sizeof(glm::vec2));

                uint32 GroupsX = RenderUtils::GetGroupCount(LevelWidth, 32);
                uint32 GroupsY = RenderUtils::GetGroupCount(LevelHeight, 32);

                CmdList.Dispatch(GroupsX, GroupsY, 1);
            }

            CmdList.SetEnableUavBarriersForImage(DepthPyramid, true);

        });
    }

    void FForwardRenderScene::ClusterBuildPass(FRenderGraph& RenderGraph)
    {
		if (LightData.NumLights == 0 || DrawCommands.empty())
        {
            return;
        }

        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Compute, "Cluster Build Pass", Descriptor, [&] (ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Cluster Build Pass", tracy::Color::Pink2);
                
            FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("ClusterBuild.slang");

            FComputePipelineDesc PipelineDesc;
            PipelineDesc.SetComputeShader(ComputeShader);
            PipelineDesc.AddBindingLayout(SceneBindingLayout);
            PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
                    
            FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);
                
            FComputeState State;
            State.SetPipeline(Pipeline);
            State.AddBindingSet(SceneBindingSet);
            State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            CmdList.SetComputeState(State);

            FLightClusterPC ClusterPC;
            ClusterPC.InverseProjection = SceneViewport->GetViewVolume().GetInverseProjectionMatrix();
            ClusterPC.zNearFar = glm::vec2(SceneViewport->GetViewVolume().GetNear(), SceneViewport->GetViewVolume().GetFar());
            ClusterPC.GridSize = glm::vec4(ClusterGridSizeX, ClusterGridSizeY, ClusterGridSizeZ, 0.0f);
            ClusterPC.ScreenSize = glm::vec2(GetNamedImage(ENamedImage::HDR)->GetSizeX(), GetNamedImage(ENamedImage::HDR)->GetSizeY());
                
            CmdList.SetPushConstants(&ClusterPC, sizeof(FLightClusterPC));
                
            constexpr uint32 ClusterBuildGroupSize = 64;
            constexpr uint32 ClusterDispatchGroups = (NumClusters + ClusterBuildGroupSize - 1) / ClusterBuildGroupSize;
            CmdList.Dispatch(ClusterDispatchGroups, 1, 1);
                
        });
    }

    void FForwardRenderScene::LightCullPass(FRenderGraph& RenderGraph)
    {
        if (LightData.NumLights == 0)
        {
            return;
        }

        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Compute, "Light Cull Pass", Descriptor, [&] (ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Light Cull Pass", tracy::Color::Pink2);
                
            FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("LightCull.slang");

            FComputePipelineDesc PipelineDesc;
            PipelineDesc.SetComputeShader(ComputeShader);
            PipelineDesc.AddBindingLayout(SceneBindingLayout);
            PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
                    
            FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);
                
            FComputeState State;
            State.SetPipeline(Pipeline);
            State.AddBindingSet(SceneBindingSet);
            State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            CmdList.SetComputeState(State);
                
            glm::mat4 ViewProj = SceneViewport->GetViewVolume().GetViewMatrix();
                
            CmdList.SetPushConstants(&ViewProj, sizeof(glm::mat4));
                
            CmdList.Dispatch(27, 1, 1);
                
        });
    }

    void FForwardRenderScene::PointShadowPass(FRenderGraph& RenderGraph)
    {
        if (PackedShadows[(uint32)ELightType::Point].empty() || DrawCommands.empty())
        {
            return;
        }
        
        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Point Light Shadow Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Point Light Shadow Pass", tracy::Color::DeepPink2);
            
            FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("ShadowMappingPixel.slang");
            
            FRenderState RenderState; RenderState
                    .SetDepthStencilState(FDepthStencilState()
                    .SetDepthFunc(EComparisonFunc::Less))
                    .SetRasterState(FRasterState()
                        .SetSlopeScaleDepthBias(1.75f)
                        .SetDepthBias(100)
                        .SetCullFront());
            
            FRenderPassDesc::FAttachment Depth; Depth
                .SetLoadOp(ERenderLoadOp::Clear)
                .SetDepthClearValue(1.0)
                .SetImage(ShadowAtlas.GetImage())
                    .SetNumSlices(6);
            
            FRenderPassDesc RenderPass; RenderPass
                .SetDepthAttachment(Depth)
                .SetViewMask(RenderUtils::CreateViewMask<0u, 1u, 2u, 3u, 4u, 5u>())
                .SetRenderArea(glm::uvec2(GShadowAtlasResolution, GShadowAtlasResolution));
            
            FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("ShadowMappingVert.slang");

            FGraphicsPipelineDesc Desc; Desc
                .SetDebugName("Point Light Shadow Pass")
                .SetRenderState(RenderState)
                .AddBindingLayout(SceneBindingLayout)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                .SetVertexShader(VertexShader)
                .SetPixelShader(PixelShader);

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FGraphicsState GraphicsState; GraphicsState
                .SetRenderPass(Move(RenderPass))
                .SetPipeline(Pipeline)
                .AddBindingSet(SceneBindingSet)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                .SetIndirectParams(GetNamedBuffer(ENamedBuffer::IndirectShadow));


            const TVector<FLightShadow>& PointShadows = PackedShadows[(uint32)ELightType::Point];

            for (const FLightShadow& LightShadow : PointShadows)
            {
                LUMINA_PROFILE_SECTION_COLORED("Process Point Light", tracy::Color::DeepPink2);

                const FShadowTile& Tile = ShadowAtlas.GetTile(LightShadow.ShadowMapIndex);
                uint32 TilePixelX       = static_cast<uint32>(Tile.UVOffset.x * GShadowAtlasResolution);
                uint32 TilePixelY       = static_cast<uint32>(Tile.UVOffset.y * GShadowAtlasResolution);
                uint32 TileSize         = static_cast<uint32>(Tile.UVScale.x * GShadowAtlasResolution);

                FViewport Viewport
                (
                    (float)TilePixelX,
                    (float)TilePixelX + TileSize,
                    (float)TilePixelY,
                    (float)TilePixelY + TileSize,
                    0.0f,
                    1.0f
                );

                // FRect(minX, maxX, minY, maxY)
                FRect Scissor
                (
                    (int)TilePixelX,
                    (int)TilePixelX + TileSize,
                    (int)TilePixelY,
                    (int)TilePixelY + TileSize
                );

                GraphicsState.SetViewportState(FViewportState(Viewport, Scissor));

                for (uint32 OpaqueIdx : OpaqueDrawList)
                {
                    const FMeshDrawCommand& Batch = DrawCommands[OpaqueIdx];

                    CmdList.SetGraphicsState(GraphicsState);

                    CmdList.SetPushConstants(&LightShadow.LightIndex, sizeof(uint32));
                    CmdList.DrawIndirect(Batch.DrawCount, Batch.IndirectDrawOffset * sizeof(FDrawIndirectArguments));
                }
            }

            CmdList.EndRenderPass();
        });
    }

    void FForwardRenderScene::SpotShadowPass(FRenderGraph& RenderGraph)
    {
        if (PackedShadows[(uint32)ELightType::Spot].empty() || DrawCommands.empty())
        {
            return;
        }
        
        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Spot Shadow Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Spot Shadow Pass", tracy::Color::DeepPink4);
            
            FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("ShadowMappingPixel.slang");
            
            FRenderState RenderState; RenderState
                .SetDepthStencilState(FDepthStencilState()
                    .SetDepthFunc(EComparisonFunc::Less))
                    .SetRasterState(FRasterState()
                        .SetSlopeScaleDepthBias(1.75f)
                        .SetDepthBias(100)
                        .SetCullFront());
            
            
            const TVector<FLightShadow>& SpotShadows = PackedShadows[(uint32)ELightType::Spot];

            FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("ShadowMappingVert.slang");

            FGraphicsPipelineDesc PipelineDesc; PipelineDesc
                .SetDebugName("Spot Shadow Pass")
                .SetRenderState(RenderState)
                .AddBindingLayout(SceneBindingLayout)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                .SetVertexShader(VertexShader)
                .SetPixelShader(PixelShader);

            for (const FLightShadow& Shadow : SpotShadows)
            {
                LUMINA_PROFILE_SECTION_COLORED("Process Spot Light", tracy::Color::DeepPink);

                const FShadowTile& Tile = ShadowAtlas.GetTile(Shadow.ShadowMapIndex);
                uint32 TilePixelX = static_cast<uint32>(Tile.UVOffset.x * GShadowAtlasResolution);
                uint32 TilePixelY = static_cast<uint32>(Tile.UVOffset.y * GShadowAtlasResolution);
                uint32 TileSize = static_cast<uint32>(Tile.UVScale.x * GShadowAtlasResolution);

                FRenderPassDesc::FAttachment Depth; Depth
                    .SetLoadOp(ERenderLoadOp::Clear)
                    .SetDepthClearValue(1.0f)
                    .SetImage(ShadowAtlas.GetImage())
                        .SetArraySlice(6);

                FRenderPassDesc RenderPass; RenderPass
                    .SetDepthAttachment(Depth)
                    .SetRenderArea(glm::uvec2(GShadowAtlasResolution, GShadowAtlasResolution));

                FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(PipelineDesc, RenderPass);

                FViewportState ViewportState;
                ViewportState.SetViewport((FViewport
                (
                    (float)TilePixelX,
                    (float)TilePixelX + TileSize,
                    (float)TilePixelY,
                    (float)TilePixelY + TileSize,
                    0.0f,
                    1.0f
                )));

                ViewportState.SetScissorRect(FRect
                (
                    (int)TilePixelX,
                    (int)TilePixelX + TileSize,
                    (int)TilePixelY,
                    (int)TilePixelY + TileSize
                ));

                FGraphicsState GraphicsState; GraphicsState
                    .SetRenderPass(RenderPass)
                    .SetViewportState(ViewportState)
                    .SetPipeline(Pipeline)
                    .AddBindingSet(SceneBindingSet)
                    .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                    .SetIndirectParams(GetNamedBuffer(ENamedBuffer::IndirectShadow));

                for (uint32 OpaqueIdx : OpaqueDrawList)
                {
                    const FMeshDrawCommand& Batch = DrawCommands[OpaqueIdx];

                    CmdList.SetGraphicsState(GraphicsState);

                    CmdList.SetPushConstants(&Shadow.LightIndex, sizeof(uint32));
                    CmdList.DrawIndirect(Batch.DrawCount, Batch.IndirectDrawOffset * sizeof(FDrawIndirectArguments));
                }
            }

            CmdList.EndRenderPass();

        });
    }

    void FForwardRenderScene::CascadedShowPass(FRenderGraph& RenderGraph)
    {
        if (!LightData.bHasSun || DrawCommands.empty())
        {
            return;
        }
        
        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Cascaded Shadow Map Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Cascaded Shadow Map Pass", tracy::Color::DeepPink2);
            
            FRenderState RenderState; RenderState
                .SetDepthStencilState(FDepthStencilState()
                    .SetDepthFunc(EComparisonFunc::Less))
                    .SetRasterState(FRasterState().SetCullFront());
            
            
            FRenderPassDesc::FAttachment Depth; Depth
                .SetLoadOp(ERenderLoadOp::Clear)
                .SetDepthClearValue(1.0)
                .SetImage(GetNamedImage(ENamedImage::Cascade))
                    .SetNumSlices(NumCascades);
            
            FRenderPassDesc RenderPass; RenderPass
                .SetDepthAttachment(Depth)
                .SetViewMask(RenderUtils::CreateViewMask<0u, 1u, 2u>()) // Must match NUM_CASCADES
                .SetRenderArea(glm::uvec2(GCSMResolution, GCSMResolution));
            
            
            FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("ShadowMappingVert.slang");

            FGraphicsPipelineDesc Desc; Desc
                .SetDebugName("Cascaded Shadow Maps")
                .SetRenderState(RenderState)
                .AddBindingLayout(SceneBindingLayout)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                .SetVertexShader(VertexShader);

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FGraphicsState GraphicsState; GraphicsState
                .SetRenderPass(RenderPass)
                .SetViewportState(MakeViewportStateFromImage(GetNamedImage(ENamedImage::Cascade)))
                .SetPipeline(Pipeline)
                .AddBindingSet(SceneBindingSet)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                .SetIndirectParams(GetNamedBuffer(ENamedBuffer::IndirectShadow));

            CmdList.SetGraphicsState(GraphicsState);

            uint32 LightIndex = 0;
            CmdList.SetPushConstants(&LightIndex, sizeof(uint32));

            for (uint32 OpaqueIdx : OpaqueDrawList)
            {
                const FMeshDrawCommand& Batch = DrawCommands[OpaqueIdx];
                CmdList.DrawIndirect(Batch.DrawCount, Batch.IndirectDrawOffset * sizeof(FDrawIndirectArguments));
            }
        });
    }

    void FForwardRenderScene::BasePass(FRenderGraph& RenderGraph)
    {
        if (DrawCommands.empty())
        {
            return;
        }
        
        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Forward Base Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Forward Base Pass", tracy::Color::Red);
            
            FRenderPassDesc::FAttachment RenderTarget;
            RenderTarget.SetImage(GetNamedImage(ENamedImage::HDR));
            if (RenderSettings.bHasEnvironment)
            {
                RenderTarget.SetLoadOp(ERenderLoadOp::Load);
            }
            
            FRenderPassDesc::FAttachment PickerImageAttachment; PickerImageAttachment
                .SetImage(GetNamedImage(ENamedImage::Picker));
            
            FRenderPassDesc::FAttachment Depth; Depth
                .SetImage(GetNamedImage(ENamedImage::DepthAttachment))
                .SetLoadOp(ERenderLoadOp::Load);
            
            FRenderPassDesc RenderPass; RenderPass
                .AddColorAttachment(RenderTarget)
                .AddColorAttachment(PickerImageAttachment)
                .SetDepthAttachment(Depth)
                .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());
            
            FRasterState RasterState; RasterState
                .EnableDepthClip()
                .SetFillMode(RenderSettings.bWireframe ? ERasterFillMode::Wireframe : ERasterFillMode::Solid)
                .SetLineWidth(5.0f);
        
            FDepthStencilState DepthState; DepthState
                .SetDepthFunc(RenderSettings.bWireframe ? EComparisonFunc::GreaterOrEqual : EComparisonFunc::Equal)
                .DisableDepthWrite();
            
            FRenderState RenderState; RenderState
                .SetRasterState(RasterState)
                .SetDepthStencilState(DepthState);
            
            for (uint32 Idx : OpaqueDrawList)
            {
                const FMeshDrawCommand& Batch = DrawCommands[Idx];

                FGraphicsPipelineDesc Desc; Desc
                    .SetDebugName("Forward Base Pass")
                    .SetRenderState(RenderState)
                    .SetVertexShader(Batch.VertexShader)
                    .SetPixelShader(Batch.PixelShader)
                    .AddBindingLayout(SceneBindingLayout)
                    .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

                FGraphicsState GraphicsState; GraphicsState
                    .SetRenderPass(RenderPass)
                    .SetViewportState(SceneViewportState)
                    .SetPipeline(GRenderContext->CreateGraphicsPipeline(Desc, RenderPass))
                    .SetIndirectParams(GetNamedBuffer(ENamedBuffer::Indirect))
                    .AddBindingSet(SceneBindingSet)
                    .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());

                CmdList.SetGraphicsState(GraphicsState);
                CmdList.DrawIndirect(Batch.DrawCount, Batch.IndirectDrawOffset * sizeof(FDrawIndirectArguments));
            }
        });
    }

    void FForwardRenderScene::BillboardPass(FRenderGraph& RenderGraph)
    {
        if (BillboardInstances.empty() || !RenderSettings.bDrawBillboards)
        {
            return;
        }
        
        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Billboard Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Billboard Pass", tracy::Color::Red);
            
            FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("BillboardVert.slang");
            FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("BillboardPixel.slang");
            
            FRenderPassDesc::FAttachment RenderTarget;
            RenderTarget.SetImage(GetNamedImage(ENamedImage::HDR));
            if (RenderSettings.bHasEnvironment || !DrawCommands.empty())
            {
                RenderTarget.SetLoadOp(ERenderLoadOp::Load);
            }
            
            FRenderPassDesc::FAttachment PickerImageAttachment; PickerImageAttachment
                .SetImage(GetNamedImage(ENamedImage::Picker))
                .SetLoadOp(ERenderLoadOp::Load);
            
            FRenderPassDesc::FAttachment Depth; Depth
                .SetImage(GetNamedImage(ENamedImage::DepthAttachment))
                .SetLoadOp(ERenderLoadOp::Load);
            
            FRenderPassDesc RenderPass; RenderPass
                .AddColorAttachment(RenderTarget)
                .AddColorAttachment(PickerImageAttachment)
                .SetDepthAttachment(Depth)
                .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());
        
            FDepthStencilState DepthState; DepthState
                .DisableDepthWrite()
                .DisableDepthTest();
            
            FRenderState RenderState; RenderState
                .SetDepthStencilState(DepthState);
            
            FGraphicsPipelineDesc Desc; Desc
                .SetDebugName("Billboard Pass")
                .SetRenderState(RenderState)
                .SetVertexShader(VertexShader)
                .SetPixelShader(PixelShader)
                .AddBindingLayout(SceneBindingLayout)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
            
            FGraphicsState GraphicsState; GraphicsState
                .SetRenderPass(RenderPass)
                .SetViewportState(MakeViewportStateFromImage(GetNamedImage(ENamedImage::HDR)))
                .SetPipeline(GRenderContext->CreateGraphicsPipeline(Desc, RenderPass))
                .AddBindingSet(SceneBindingSet)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            
            CmdList.SetGraphicsState(GraphicsState);
            CmdList.Draw(6, BillboardInstances.size(), 0, 0);   
        });
    }

    void FForwardRenderScene::TransparentPass(FRenderGraph& RenderGraph)
    {
        if (TranslucentDrawList.empty())
        {
            return;
        }

        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Transparent Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Transparent Pass", tracy::Color::CadetBlue);

            FRenderPassDesc::FAttachment RenderTarget0;
            RenderTarget0.SetImage(GetNamedImage(ENamedImage::Accum))
                        .SetLoadOp(ERenderLoadOp::Clear)
                        .SetClearColor(glm::vec4(0.0));
            
            FRenderPassDesc::FAttachment RenderTarget1;
            RenderTarget1.SetImage(GetNamedImage(ENamedImage::Revealage))
                        .SetLoadOp(ERenderLoadOp::Clear)
                        .SetClearColor(glm::vec4(1.0));
            
            FRenderPassDesc::FAttachment PickerImageAttachment; 
            PickerImageAttachment.SetImage(GetNamedImage(ENamedImage::Picker))
                .SetLoadOp(ERenderLoadOp::Load);

            FRenderPassDesc::FAttachment Depth;
            Depth.SetImage(GetNamedImage(ENamedImage::DepthAttachment))
                 .SetLoadOp(ERenderLoadOp::Load);

            FRenderPassDesc RenderPass;
            RenderPass.AddColorAttachment(RenderTarget0)
                      .AddColorAttachment(RenderTarget1)
                      .AddColorAttachment(PickerImageAttachment)
                      .SetDepthAttachment(Depth)
                      .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());

            FRasterState RasterState;
            RasterState.EnableDepthClip()
                       .SetCullMode(ERasterCullMode::None);

            FBlendState::RenderTarget Blend0;
            Blend0.SetBlendEnable(true);
            Blend0.SetSrcBlend(EBlendFactor::One);
            Blend0.SetDestBlend(EBlendFactor::One);
            Blend0.SetBlendOp(EBlendOp::Add);

            FBlendState::RenderTarget Blend1;
            Blend1.SetBlendEnable(true);
            Blend1.SetSrcBlend(EBlendFactor::Zero);
            Blend1.SetDestBlend(EBlendFactor::OneMinusSrcColor);
            Blend1.SetBlendOp(EBlendOp::Add);

            FBlendState TranslucentBlendState;
            TranslucentBlendState.SetRenderTarget(0, Blend0);
            TranslucentBlendState.SetRenderTarget(1, Blend1);
            

            FBlendState AdditiveBlendState; AdditiveBlendState
                .Targets[0]
                    .SetBlendEnable(true)
                    .SetSrcBlend(EBlendFactor::SrcAlpha)
                    .SetDestBlend(EBlendFactor::One)
                    .SetBlendOp(EBlendOp::Add)
                    .SetSrcBlendAlpha(EBlendFactor::One)
                    .SetDestBlendAlpha(EBlendFactor::One)
                    .SetBlendOpAlpha(EBlendOp::Add);

            for (uint32 Idx : TranslucentDrawList)
            {
                const FMeshDrawCommand& Batch = DrawCommands[Idx];

                FDepthStencilState DepthState;
                DepthState.SetDepthFunc(EComparisonFunc::GreaterOrEqual)
                          .DisableDepthWrite();

                FRenderState RenderState;
                RenderState.SetDepthStencilState(DepthState)
                           .SetRasterState(RasterState)
                           .SetBlendState(Batch.bAdditive ? AdditiveBlendState : TranslucentBlendState);

                FGraphicsPipelineDesc Desc;
                Desc.SetDebugName("Transparent Pass")
                    .SetRenderState(RenderState)
                    .SetVertexShader(Batch.VertexShader)
                    .SetPixelShader(Batch.PixelShader)
                    .AddBindingLayout(SceneBindingLayout)
                    .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

                FGraphicsState GraphicsState;
                GraphicsState.SetRenderPass(RenderPass)
                             .SetViewportState(SceneViewportState)
                             .SetPipeline(GRenderContext->CreateGraphicsPipeline(Desc, RenderPass))
                             .SetIndirectParams(GetNamedBuffer(ENamedBuffer::Indirect))
                             .AddBindingSet(SceneBindingSet)
                             .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());

                CmdList.SetGraphicsState(GraphicsState);
                CmdList.DrawIndirect(Batch.DrawCount, Batch.IndirectDrawOffset * sizeof(FDrawIndirectArguments));
            }
        });
    }

    void FForwardRenderScene::OITResolvePass(FRenderGraph& RenderGraph)
    {
        if (TranslucentDrawList.empty())
        {
            return;
        }

        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "OIT Resolve Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("OIT Resolve Pass", tracy::Color::GreenYellow);
            
            FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
            FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("OITResolve.slang");
            if (!VertexShader || !PixelShader)
            {
                return;
            }
            
            
            FBlendState::RenderTarget Blend0;
            Blend0.SetBlendEnable(true);

            Blend0.SetSrcBlend(EBlendFactor::SrcAlpha);
            Blend0.SetDestBlend(EBlendFactor::OneMinusSrcAlpha);
            Blend0.SetBlendOp(EBlendOp::Add);

            Blend0.SetSrcBlendAlpha(EBlendFactor::One);
            Blend0.SetDestBlendAlpha(EBlendFactor::OneMinusSrcAlpha);
            Blend0.SetBlendOpAlpha(EBlendOp::Add);
            
            FBlendState BlendState;
            BlendState.SetRenderTarget(0, Blend0);
            
            FRenderPassDesc::FAttachment Attachment; Attachment
                .SetImage(GetNamedImage(ENamedImage::HDR))
                    .SetLoadOp(ERenderLoadOp::Load);
            
            FRenderPassDesc RenderPass; RenderPass
                .AddColorAttachment(Attachment)
                .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());
        
            FRasterState RasterState;
            RasterState.SetCullNone();
            
            FRenderState RenderState;
            RenderState.SetRasterState(RasterState);
            RenderState.SetBlendState(BlendState);

        
            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("OITResolve Pass");
            Desc.SetRenderState(RenderState);
            Desc.AddBindingLayout(SceneBindingLayout);
            Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
            Desc.SetVertexShader(VertexShader);
            Desc.SetPixelShader(PixelShader);
        
            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);
        
            FGraphicsState GraphicsState;
            GraphicsState.AddBindingSet(SceneBindingSet);
            GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            GraphicsState.SetPipeline(Pipeline);
            GraphicsState.SetRenderPass(RenderPass);
            GraphicsState.SetViewportState(SceneViewportState);
            
            CmdList.SetGraphicsState(GraphicsState);
        
            CmdList.Draw(3, 1, 0, 0);
        });
    }

    void FForwardRenderScene::EnvironmentPass(FRenderGraph& RenderGraph)
    {
        if (!RenderSettings.bHasEnvironment)
        {
            return;
        }

        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Environment Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Environment Pass", tracy::Color::Green3);
        
            FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
            FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("Environment.slang");
            if (!VertexShader || !PixelShader)
            {
                return;
            }
        
            FRenderPassDesc::FAttachment Attachment; Attachment
                .SetImage(GetNamedImage(ENamedImage::HDR));
            
            FRenderPassDesc RenderPass; RenderPass
                .AddColorAttachment(Attachment)
                .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());
        
            FRasterState RasterState;
            RasterState.SetCullNone();
            
            FRenderState RenderState;
            RenderState.SetRasterState(RasterState);
        
            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("Environment Pass");
            Desc.SetRenderState(RenderState);
            Desc.AddBindingLayout(SceneBindingLayout);
            Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
            Desc.SetVertexShader(VertexShader);
            Desc.SetPixelShader(PixelShader);
        
            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);
        
            FGraphicsState GraphicsState;
            GraphicsState.AddBindingSet(SceneBindingSet);
            GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            GraphicsState.SetPipeline(Pipeline);
            GraphicsState.SetRenderPass(RenderPass);
            GraphicsState.SetViewportState(SceneViewportState);
            
        
            CmdList.SetGraphicsState(GraphicsState);
        
            CmdList.Draw(3, 1, 0, 0); 
        });
    }

    void FForwardRenderScene::BatchedLineDraw(FRenderGraph& RenderGraph)
    {
        if (SimpleVertices.empty() || LineBatches.empty())
        {
            return;
        }
    
        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Batched Line Draw", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Batched Line Draw", tracy::Color::Red2);
    
            FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("SimpleElementVertex.slang");
            FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("SimpleElementPixel.slang");
            if (!VertexShader || !PixelShader)
            {
                return;
            }
    
            FRenderPassDesc::FAttachment RenderTarget;
            RenderTarget.SetImage(GetNamedImage(ENamedImage::HDR));
            if (!DrawCommands.empty() || RenderSettings.bHasEnvironment || !BillboardInstances.empty())
            {
                RenderTarget.SetLoadOp(ERenderLoadOp::Load);
            }
    
            FRenderPassDesc::FAttachment Depth; Depth
            .SetImage(GetNamedImage(ENamedImage::DepthAttachment))
            .SetLoadOp(ERenderLoadOp::Load);
    
            FRenderPassDesc RenderPass; RenderPass
                .AddColorAttachment(RenderTarget)
                .SetDepthAttachment(Depth)
                .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());
    
            FVertexBufferBinding VertexBinding{GetNamedBuffer(ENamedBuffer::SimpleVertex)};
    
            for (const FLineBatch& Batch : LineBatches)
            {
                FRasterState RasterState; RasterState
                    .SetLineWidth(Batch.Thickness)
                    .EnableDepthClip();
    
                FDepthStencilState DepthState; DepthState
                    .SetDepthFunc(EComparisonFunc::Greater)
                    .EnableDepthWrite();
                
                if (Batch.bDepthTest)
                {
                    DepthState.EnableDepthTest();
                }
                else
                {
                    DepthState.DisableDepthTest();
                }
    
                FRenderState RenderState; RenderState
                    .SetRasterState(RasterState)
                    .SetDepthStencilState(DepthState);
                
                FGraphicsPipelineDesc Desc; Desc
                    .SetDebugName("Batched Line Draw")
                    .SetPrimType(EPrimitiveType::LineList)
                    .SetRenderState(RenderState)
                    .SetInputLayout(SimpleVertexLayoutInput)
                    .AddBindingLayout(SceneBindingLayout)
                    .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                    .SetVertexShader(VertexShader)
                    .SetPixelShader(PixelShader);
    
                FGraphicsState GraphicsState; GraphicsState
                    .SetRenderPass(RenderPass)
                    .AddVertexBuffer(VertexBinding)
                    .SetViewportState(SceneViewportState)
                    .SetPipeline(GRenderContext->CreateGraphicsPipeline(Desc, RenderPass))
                    .AddBindingSet(SceneBindingSet)
                    .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
    
                CmdList.SetGraphicsState(GraphicsState);
                CmdList.Draw(Batch.VertexCount, 1, Batch.StartVertex, 0);
            }
        });
    }

    void FForwardRenderScene::SelectionPass(FRenderGraph& RenderGraph)
    {
        if (World->GetSelectedEntities().empty())
        {
            return;
        }
        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Selection Post Process Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Selection Post Process Pass", tracy::Color::Red2);
            
            FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
            FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("SelectionPostProcess.slang");
            if (!VertexShader || !PixelShader)
            {
                return;
            }
            
            FRenderPassDesc::FAttachment Attachment; Attachment
                .SetImage(GetNamedImage(ENamedImage::HDR))
                .SetLoadOp(ERenderLoadOp::Load);
        
            FRenderPassDesc RenderPass; RenderPass
                .AddColorAttachment(Attachment)
                .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());
            
            FRasterState RasterState;
            RasterState.SetCullNone();
            
            FDepthStencilState DepthState;
            DepthState.DisableDepthTest();
            DepthState.DisableDepthWrite();
            
            FRenderState RenderState;
            RenderState.SetRasterState(RasterState);
            RenderState.SetDepthStencilState(DepthState);
            
            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("Selection Post Process Pass");
            Desc.SetRenderState(RenderState);
            Desc.AddBindingLayout(SceneBindingLayout);
            Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
            Desc.SetVertexShader(VertexShader);
            Desc.SetPixelShader(PixelShader);
        
            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);
        
            FGraphicsState GraphicsState;
            GraphicsState.SetPipeline(Pipeline);
            GraphicsState.AddBindingSet(SceneBindingSet);
            GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            GraphicsState.SetRenderPass(RenderPass);               
            GraphicsState.SetViewportState(MakeViewportStateFromImage(GetNamedImage(ENamedImage::HDR)));
        
            CmdList.SetGraphicsState(GraphicsState);

            auto Selections = World->GetSelectedEntities();
            
            uint32 Push[32];
            Push[0] = PackColor(glm::vec4(255, 0, 0, 255));
            Push[1] = CVarSelectionThickness.GetValue();
            Push[2] = glm::min(29u, (uint32)Selections.size());
            uint32 Start = 3;
            
            for (int i = 0; std::cmp_less(i, Push[2]); ++i)
            {
                Push[Start++] = entt::to_integral(Selections[i]);
            }

            CmdList.SetPushConstants(Push, sizeof(Push));
            CmdList.Draw(3, 1, 0, 0); 
        });
    }

    void FForwardRenderScene::ToneMappingPass(FRenderGraph& RenderGraph)
    {
        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Tone Mapping Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Tone Mapping Pass", tracy::Color::Red2);
            
            FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
            FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("ToneMapping.slang");
            if (!VertexShader || !PixelShader)
            {
                return;
            }
            
            FRenderPassDesc::FAttachment Attachment; Attachment
                .SetImage(GetRenderTarget());
        
            FRenderPassDesc RenderPass; RenderPass
                .AddColorAttachment(Attachment)
                .SetRenderArea(GetRenderTarget()->GetExtent());
        
        
            FRasterState RasterState;
            RasterState.SetCullNone();
            
            FDepthStencilState DepthState;
            DepthState.DisableDepthTest();
            DepthState.DisableDepthWrite();
            
            FRenderState RenderState;
            RenderState.SetRasterState(RasterState);
            RenderState.SetDepthStencilState(DepthState);
            
            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("Tone Mapping Pass");
            Desc.SetRenderState(RenderState);
            Desc.AddBindingLayout(SceneBindingLayout);
            Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
            Desc.SetVertexShader(VertexShader);
            Desc.SetPixelShader(PixelShader);
        
            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);
        
            FGraphicsState GraphicsState;
            GraphicsState.SetPipeline(Pipeline);
            GraphicsState.AddBindingSet(SceneBindingSet);
            GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            GraphicsState.SetRenderPass(RenderPass);               
            GraphicsState.SetViewportState(MakeViewportStateFromImage(GetRenderTarget()));
        
            CmdList.SetGraphicsState(GraphicsState);

            glm::vec2 PC;
            PC.x = 1.0;
            PC.y = SceneGlobalData.Time;
            CmdList.SetPushConstants(&PC, sizeof(glm::vec2));
            CmdList.Draw(3, 1, 0, 0); 
        });
    }

    void FForwardRenderScene::DebugDrawPass(FRenderGraph& RenderGraph)
    {
#if 0
        if (RenderSettings.Flags == ERenderSceneDebugFlags::None)
        {
            return;
        }
        
        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Debug Draw Pass", Descriptor, [&](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Debug Draw Pass", tracy::Color::Red2);
        
            FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
            FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("Debug.slang");
            if (!VertexShader || !PixelShader)
            {
                return;
            }
        
            FRenderPassDesc::FAttachment Attachment; Attachment
                .SetLoadOp(ERenderLoadOp::Load)
                .SetImage(GetRenderTarget());
        
            FRenderPassDesc RenderPass; RenderPass
                .AddColorAttachment(Attachment)
                .SetRenderArea(GetRenderTarget()->GetExtent());
        
        
            FRasterState RasterState;
            RasterState.SetCullNone();
        
            FDepthStencilState DepthState;
            DepthState.DisableDepthTest();
            DepthState.DisableDepthWrite();
        
            FRenderState RenderState;
            RenderState.SetRasterState(RasterState);
            RenderState.SetDepthStencilState(DepthState);
        
            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("Debug Draw Pass");
            Desc.SetRenderState(RenderState);
            Desc.AddBindingLayout(SceneBindingLayout);
            Desc.AddBindingLayout(DebugPassLayout);
            Desc.SetVertexShader(VertexShader);
            Desc.SetPixelShader(PixelShader);
        
            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);
        
            FGraphicsState GraphicsState;
            GraphicsState.SetPipeline(Pipeline);
            GraphicsState.AddBindingSet(BindingSet);
            GraphicsState.AddBindingSet(DebugPassSet);
            GraphicsState.SetRenderPass(RenderPass);               
            GraphicsState.SetViewportState(MakeViewportStateFromImage(GetRenderTarget()));
        
            CmdList.SetGraphicsState(GraphicsState);
        
            uint32 Mode = static_cast<uint32>(RenderSettings.Flags);
            CmdList.SetPushConstants(&Mode, sizeof(uint32));
            CmdList.Draw(3, 1, 0, 0); 
        });
#endif
    }
    
    void FForwardRenderScene::InitBuffers()
    {
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FSceneGlobalData);
            BufferDesc.Usage.SetFlag(BUF_UniformBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::ShaderResource;
            BufferDesc.DebugName = "Scene Global Data";
            NamedBuffers[(int)ENamedBuffer::Scene] = GRenderContext->CreateBuffer(BufferDesc);
        }

        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FGPUInstance);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::ShaderResource;
            BufferDesc.DebugName = "Instance Buffer";
            NamedBuffers[(int)ENamedBuffer::Instance] = GRenderContext->CreateBuffer(BufferDesc);
        }
        
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(glm::mat4) * 255 * 1'000;
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::ShaderResource;
            BufferDesc.DebugName = "Bone Data Buffer";
            NamedBuffers[(int)ENamedBuffer::Bone] = GRenderContext->CreateBuffer(BufferDesc);
        }

        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(uint32);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = "Instance Mapping";
            NamedBuffers[(int)ENamedBuffer::InstanceMapping] = GRenderContext->CreateBuffer(BufferDesc);
        }

        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(uint32);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = "Instance Mapping (Shadow)";
            NamedBuffers[(int)ENamedBuffer::InstanceMappingShadow] = GRenderContext->CreateBuffer(BufferDesc);
        }

        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = offsetof(FSceneLightData, Lights);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::ShaderResource;
            BufferDesc.DebugName = "Light Data Buffer";
            NamedBuffers[(int)ENamedBuffer::Light] = GRenderContext->CreateBuffer(BufferDesc);
        }

        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FCluster) * NumClusters;
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = "Cluster SSBO";
            NamedBuffers[(int)ENamedBuffer::Cluster] = GRenderContext->CreateBuffer(BufferDesc);
        }

        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FSimpleElementVertex);
            BufferDesc.Usage.SetFlag(BUF_VertexBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::VertexBuffer;
            BufferDesc.DebugName = "Simple Element Vertex";
            NamedBuffers[(int)ENamedBuffer::SimpleVertex] = GRenderContext->CreateBuffer(BufferDesc);
        }

        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FDrawIndirectArguments);
            BufferDesc.Stride = sizeof(FDrawIndirectArguments);
            BufferDesc.Usage.SetMultipleFlags(BUF_Indirect, BUF_StorageBuffer);
            BufferDesc.InitialState = EResourceStates::IndirectArgument;
            BufferDesc.bKeepInitialState = true;
            BufferDesc.DebugName = "Indirect Draw Buffer";
            NamedBuffers[(int)ENamedBuffer::Indirect] = GRenderContext->CreateBuffer(BufferDesc);
        }

        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FDrawIndirectArguments);
            BufferDesc.Stride = sizeof(FDrawIndirectArguments);
            BufferDesc.Usage.SetMultipleFlags(BUF_Indirect, BUF_StorageBuffer);
            BufferDesc.InitialState = EResourceStates::IndirectArgument;
            BufferDesc.bKeepInitialState = true;
            BufferDesc.DebugName = "Indirect Draw Buffer (Shadow)";
            NamedBuffers[(int)ENamedBuffer::IndirectShadow] = GRenderContext->CreateBuffer(BufferDesc);
        }
        
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FBillboardInstance);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = "Billboard Data";
            NamedBuffers[(int)ENamedBuffer::Billboards] = GRenderContext->CreateBuffer(BufferDesc);
        }
    }

    static uint32 PreviousPow2(uint32 v)
    {
        uint32_t r = 1;

        while (r * 2 < v)
        {
            r *= 2;
        }

        return r;
    }

    void FForwardRenderScene::InitImages()
    {
        glm::uvec2 Extent = Windowing::GetPrimaryWindowHandle()->GetExtent();
        
        {
            FRHIImageDesc ImageDesc = GetRenderTarget()->GetDescription();
            ImageDesc.Format = EFormat::RGBA16_FLOAT;
            NamedImages[(int)ENamedImage::HDR] = GRenderContext->CreateImage(ImageDesc);
        }
        
        //==================================================================================================
        
        {
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent = Extent;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::DepthAttachment, EImageCreateFlags::ShaderResource);
            ImageDesc.Format = EFormat::D32;
            ImageDesc.InitialState = EResourceStates::DepthRead;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Dimension = EImageDimension::Texture2D;
            ImageDesc.DebugName = "Depth Attachment";
        
            NamedImages[(int)ENamedImage::DepthAttachment] = GRenderContext->CreateImage(ImageDesc);
        }

        //==================================================================================================

        {
            uint32 Width = PreviousPow2(Extent.x);
            uint32 Height = PreviousPow2(Extent.y);
            
            FRHIImageDesc ImageDesc;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage);
            ImageDesc.Extent            = glm::uvec2(Width, Height);
            ImageDesc.Format            = EFormat::R32_FLOAT;
            ImageDesc.NumMips           = (uint8)RenderUtils::CalculateMipCount(Width, Height);
            ImageDesc.InitialState      = EResourceStates::ShaderResource;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.DebugName         = "Depth Pyramid";
            
            NamedImages[(int)ENamedImage::DepthPyramid] = GRenderContext->CreateImage(ImageDesc);
        }

        //==================================================================================================
        
        {
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent = Extent;
            ImageDesc.Format = EFormat::R32_UINT;
            ImageDesc.Dimension = EImageDimension::Texture2D;
            ImageDesc.InitialState = EResourceStates::RenderTarget;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ColorAttachment, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName = "Picker";
            
            NamedImages[(int)ENamedImage::Picker] = GRenderContext->CreateImage(ImageDesc);
        }
        
        //==================================================================================================
        
        {
            FRHIImageDesc ImageDesc = {};
            ImageDesc.Extent = glm::uvec2(GCSMResolution, GCSMResolution);
            ImageDesc.Format = EFormat::D32;
            ImageDesc.Dimension = EImageDimension::Texture2DArray;
            ImageDesc.InitialState = EResourceStates::DepthWrite;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.ArraySize = NumCascades;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::DepthAttachment, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName = "ShadowCascadeMap";
            
            NamedImages[(int)ENamedImage::Cascade] = GRenderContext->CreateImage(ImageDesc);
        }
        
        {
            FRHIImageDesc ImageDesc = {};
            ImageDesc.Extent = Extent;
            ImageDesc.Format = EFormat::RGBA16_FLOAT;
            ImageDesc.Dimension = EImageDimension::Texture2D;
            ImageDesc.InitialState = EResourceStates::RenderTarget;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName = "Accum";
            
            NamedImages[(int)ENamedImage::Accum] = GRenderContext->CreateImage(ImageDesc);
        }
        
        {
            FRHIImageDesc ImageDesc = {};
            ImageDesc.Extent = Extent;
            ImageDesc.Format = EFormat::R32_FLOAT;
            ImageDesc.Dimension = EImageDimension::Texture2D;
            ImageDesc.InitialState = EResourceStates::RenderTarget;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName = "Revealage";
            
            NamedImages[(int)ENamedImage::Revealage] = GRenderContext->CreateImage(ImageDesc);
        }
        
        //==================================================================================================
        
    }

    void FForwardRenderScene::InitFrameResources()
    {
        InitImages();
        
        float SizeY = (float)GetNamedImage(ENamedImage::HDR)->GetSizeY();
        float SizeX = (float)GetNamedImage(ENamedImage::HDR)->GetSizeX();

        SceneViewportState = {};
        SceneViewportState.Viewports.emplace_back(FViewport(SizeX, SizeY));
        SceneViewportState.Scissors.emplace_back(FRect((int)SizeX, (int)SizeY));
        
        FVertexAttributeDesc VertexDesc[2];
        VertexDesc[0].SetElementStride(sizeof(FSimpleElementVertex));
        VertexDesc[0].SetOffset(offsetof(FSimpleElementVertex, Position));
        VertexDesc[0].Format = EFormat::RGBA32_FLOAT;
        
        VertexDesc[1].SetElementStride(sizeof(FSimpleElementVertex));
        VertexDesc[1].SetOffset(offsetof(FSimpleElementVertex, Color));
        VertexDesc[1].Format = EFormat::R32_UINT;
        
        SimpleVertexLayoutInput = GRenderContext->CreateInputLayout(VertexDesc, eastl::size(VertexDesc));
        
        CreateLayouts();
    }

    void FForwardRenderScene::CreateLayouts()
    {
                
        {
            FBindingSetDesc BindingSetDesc;
            BindingSetDesc.AddItem(FBindingSetItem::BufferCBV(0, GetNamedBuffer(ENamedBuffer::Scene)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(1, GetNamedBuffer(ENamedBuffer::Light)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(2, GetNamedBuffer(ENamedBuffer::Instance)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(3, GetNamedBuffer(ENamedBuffer::InstanceMapping)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(4, GetNamedBuffer(ENamedBuffer::Indirect)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(5, GetNamedBuffer(ENamedBuffer::Bone)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(6, GetNamedBuffer(ENamedBuffer::Cluster)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(7, GRenderManager->GetMaterialManager().GetMaterialBuffer()));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(8, GetNamedBuffer(ENamedBuffer::Billboards)));
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(9, GetNamedImage(ENamedImage::Cascade)));
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(10, ShadowAtlas.GetImage()));
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(11, GetNamedImage(ENamedImage::Picker), TStaticRHISampler<false, false>::GetRHI()));
            // Min-reduction clamp sampler: a single bilinear tap on the depth pyramid
            // returns the minimum of the 2x2 footprint (farthest depth in reverse-Z).
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(12, GetNamedImage(ENamedImage::DepthPyramid),
                TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp, ESamplerReductionType::Minimum>::GetRHI()));
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(13, GetNamedImage(ENamedImage::HDR)));
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(14, GetNamedImage(ENamedImage::Accum)));
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(15, GetNamedImage(ENamedImage::Revealage)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(16, GetNamedBuffer(ENamedBuffer::InstanceMappingShadow)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(17, GetNamedBuffer(ENamedBuffer::IndirectShadow)));

            TBitFlags<ERHIShaderType> Visibility;
            Visibility.SetMultipleFlags(ERHIShaderType::Vertex, ERHIShaderType::Fragment, ERHIShaderType::Compute);
            GRenderContext->CreateBindingSetAndLayout(Visibility, 0, BindingSetDesc, SceneBindingLayout, SceneBindingSet);
        }
    }

    FViewportState FForwardRenderScene::MakeViewportStateFromImage(const FRHIImage* Image)
    {
        float SizeY = (float)Image->GetSizeY();
        float SizeX = (float)Image->GetSizeX();

        FViewportState ViewportState;
        ViewportState.Viewports.emplace_back(FViewport(SizeX, SizeY));
        ViewportState.Scissors.emplace_back(FRect(SizeX, SizeY));

        return ViewportState;
    }

    FRHIImage* FForwardRenderScene::GetRenderTarget() const
    {
        return SceneViewport->GetRenderTarget();
    }

    const FSceneRenderStats& FForwardRenderScene::GetRenderStats() const
    {
        return RenderStats;
    }

    FSceneRenderSettings& FForwardRenderScene::GetSceneRenderSettings()
    {
        return RenderSettings;
    }

    entt::entity FForwardRenderScene::GetEntityAtPixel(uint32 X, uint32 Y) const
    {
        FRHIImage* PickerImage = GetNamedImage(ENamedImage::Picker);
        if (!PickerImage)
        {
            return entt::null;
        }

        FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
        CommandList->Open();

        FRHIStagingImageRef StagingImage = GRenderContext->CreateStagingImage(PickerImage->GetDescription(), ERHIAccess::HostRead);
        CommandList->CopyImage(PickerImage, FTextureSlice(), StagingImage, FTextureSlice());

        CommandList->Close();
        GRenderContext->ExecuteCommandList(CommandList);

        size_t RowPitch = 0;
        void* MappedMemory = GRenderContext->MapStagingTexture(StagingImage, FTextureSlice(), ERHIAccess::HostRead, &RowPitch);
        if (!MappedMemory)
        {
            return entt::null;
        }

        const uint32 Width  = PickerImage->GetDescription().Extent.x;
        const uint32 Height = PickerImage->GetDescription().Extent.y;

        if (X >= Width || Y >= Height)
        {
            GRenderContext->UnMapStagingTexture(StagingImage);
            return entt::null;
        }

        uint8* RowStart = static_cast<uint8*>(MappedMemory) + Y * RowPitch;
        uint32* PixelPtr = reinterpret_cast<uint32*>(RowStart) + X;
        uint32 PixelValue = *PixelPtr;

        GRenderContext->UnMapStagingTexture(StagingImage);

        if (PixelValue == 0)
        {
            return entt::null;
        }

        return static_cast<entt::entity>(PixelValue);
    }
}
