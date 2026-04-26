#include "pch.h"
#include "ForwardRenderScene.h"
#include <algorithm>
#include <execution>
#include "Assets/AssetTypes/Material/Material.h"
#include "Assets/AssetTypes/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "assets/assettypes/mesh/skeleton/skeleton.h"
#include "Assets/AssetTypes/Textures/Texture.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Windows/Window.h"
#include "Paths/Paths.h"
#include "Renderer/GPUProfiler/GPUProfiler.h"
#include "Renderer/RendererUtils.h"
#include "Renderer/RHIStaticStates.h"
#include "Renderer/ShaderCompiler.h"
#include "Renderer/RenderContext.h"
#include "Renderer/CommandList.h"
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
#include "World/Entity/Components/ParticleSystemComponent.h"
#include "World/Entity/Components/SkeletalMeshComponent.h"
#include "world/entity/components/staticmeshcomponent.h"
#include "World/Entity/Components/TerrainComponent.h"
#include "World/Scene/RenderScene/MeshDrawCommand.h"
#include "World/Scene/RenderScene/TerrainRenderTypes.h"
#include "World/Subsystems/WorldSettings.h"
#include "Renderer/SMAA/AreaTex.h"
#include "Renderer/SMAA/SearchTex.h"

namespace Lumina
{
    static FRHIImageRef CreateSMAALUTImage(const uint8* Bytes, uint32 Width, uint32 Height, EFormat Format, uint32 RowPitch, const char* DebugName)
    {
        FRHIImageDesc Desc;
        Desc.Extent            = glm::uvec2(Width, Height);
        Desc.Format            = Format;
        Desc.Dimension         = EImageDimension::Texture2D;
        Desc.NumMips           = 1;
        Desc.InitialState      = EResourceStates::ShaderResource;
        Desc.bKeepInitialState = true;
        Desc.Flags.SetFlag(EImageCreateFlags::ShaderResource);
        Desc.DebugName         = DebugName;

        FRHIImageRef Image = GRenderContext->CreateImage(Desc);

        FRHICommandListRef Transfer = GRenderContext->CreateCommandList(FCommandListInfo::Transfer());
        Transfer->Open();
        Transfer->WriteImage(Image, 0, 0, Bytes, RowPitch, 0);
        Transfer->Close();
        GRenderContext->ExecuteCommandList(Transfer, ECommandQueue::Transfer);

        return Image;
    }
    
    
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
        
        GRenderContext->WaitIdle();
        SceneViewport = GRenderContext->CreateViewport(Windowing::GetPrimaryWindowHandle()->GetExtent(), "Forward Renderer Viewport");

        InitBuffers();
        InitFrameResources();

        // Persistent SMAA LUTs, sized constants, not affected by swapchain size.
        NamedImages[(int)ENamedImage::SMAAArea] = CreateSMAALUTImage(areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, EFormat::RG8_UNORM, AREATEX_PITCH, "SMAA AreaTex");
        NamedImages[(int)ENamedImage::SMAASearch] = CreateSMAALUTImage(searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, EFormat::R8_UNORM, SEARCHTEX_PITCH, "SMAA SearchTex");

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
        GRenderContext->WaitIdle();
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

    void FForwardRenderScene::RenderView(ICommandList& CmdList, const FViewVolume& ViewVolume)
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
        SceneGlobalData.CullData.InstanceNum            = (uint32)Instances.size();
        SceneGlobalData.CullData.bFrustumCull           = RenderSettings.bFrustumCull;
        SceneGlobalData.CullData.bOcclusionCull         = RenderSettings.bOcclusionCull;
        SceneGlobalData.CullData.PyramidWidth           = (float)GetNamedImage(ENamedImage::DepthPyramid)->GetSizeX();
        SceneGlobalData.CullData.PyramidHeight          = (float)GetNamedImage(ENamedImage::DepthPyramid)->GetSizeY();
        SceneGlobalData.CullData.ShadowMaxDistance      = World->GetDefaultWorldSettings().ShadowMaxDistance;
        SceneGlobalData.CullData.bShadowOcclusionCull   = RenderSettings.bShadowOcclusionCull;
        SceneGlobalData.CullData.DebugMode              = (uint32)RenderSettings.Flags;
        
        
        // Wait for shader tasks.
        if(GRenderContext->GetShaderCompiler()->HasPendingRequests())
        {
            return;
        }

        GPU_PROFILE_SCOPE_COLOR(&CmdList, "RenderView", FColor(0.30f, 0.65f, 1.00f));

        ResetPass(CmdList);
        CompileDrawCommands(CmdList);
        
        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Cull Early", FColor(1.00f, 0.40f, 0.70f));
            CullPassEarly(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Depth PrePass Early", FColor(1.00f, 0.55f, 0.20f));
            DepthPrePassEarly(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Depth Pyramid (Mid)", FColor(1.00f, 0.75f, 0.30f));
            DepthPyramidPass(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Cull Late", FColor(1.00f, 0.30f, 0.55f));
            CullPassLate(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Depth PrePass Late", FColor(1.00f, 0.65f, 0.30f));
            DepthPrePassLate(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Cluster Build", FColor(0.95f, 0.30f, 0.55f));
            ClusterBuildPass(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Light Cull", FColor(0.95f, 0.30f, 0.55f));
            LightCullPass(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Point Shadows", FColor(0.85f, 0.10f, 0.55f));
            PointShadowPass(CmdList);

        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Spot Shadows", FColor(0.75f, 0.10f, 0.55f));
            SpotShadowPass(CmdList);

        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Cascaded Shadows", FColor(0.85f, 0.10f, 0.55f));
            CascadedShowPass(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Environment", FColor(0.20f, 0.80f, 0.30f));
            EnvironmentPass(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Terrain Update", FColor(0.20f, 0.70f, 0.50f));
            TerrainUpdatePass(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Base Pass", FColor(0.95f, 0.20f, 0.20f));
            BasePass(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Terrain Render", FColor(0.20f, 0.70f, 0.50f));
            TerrainRenderPass(CmdList);
        }

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Depth Pyramid (End)", FColor(1.00f, 0.55f, 0.20f));
            DepthPyramidPass(CmdList);
        }
        
        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Transparent", FColor(0.40f, 0.60f, 0.85f)); 
            TransparentPass(CmdList);
        }
        
        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "OIT Resolve", FColor(0.55f, 0.85f, 0.30f)); 
            OITResolvePass(CmdList);
        }
        
        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Batched Lines", FColor(0.95f, 0.20f, 0.20f)); 
            BatchedLineDraw(CmdList);
        }
        
        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Particles Simulate", FColor(1.00f, 0.55f, 0.20f)); 
            ParticleSimulatePass(CmdList);
        }
        
        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Particles Render", FColor(1.00f, 0.40f, 0.20f)); 
            ParticleRenderPass(CmdList);
        }
        
        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Billboards", FColor(0.95f, 0.20f, 0.20f)); 
            BillboardPass(CmdList);
        }
        
        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Tone Mapping", FColor(0.95f, 0.20f, 0.20f)); 
            ToneMappingPass(CmdList);
        }
        
        if (World->GetDefaultWorldSettings().SMAAQuality != ESMAAQuality::Off)
        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "SMAA", FColor(0.95f, 0.20f, 0.20f));
            {
                GPU_PROFILE_SCOPE(&CmdList, "Edge Detection"); 
                SMAAEdgeDetectionPass(CmdList);
            }
            
            {
                GPU_PROFILE_SCOPE(&CmdList, "Blend Weight"); 
                SMAABlendWeightPass(CmdList);
            }
            
            {
                GPU_PROFILE_SCOPE(&CmdList, "Neighborhood Blend");   
                SMAANeighborhoodBlendPass(CmdList);
            }
        }
    }
    
    void FForwardRenderScene::SwapchainResized(glm::vec2 NewSize)
    {
        GRenderContext->ClearCommandListCache();
        GRenderContext->ClearBindingCaches();
        BindingCache.ReleaseResources();
        
        SceneViewport = GRenderContext->CreateViewport(NewSize, "Forward Renderer Viewport");
        
        InitFrameResources();
    }

    void FForwardRenderScene::CompileDrawCommands(ICommandList& CmdList)
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

            // Build the per-frame CPU reject volumes (camera frustum, sun-
            // swept shadow frustum, shadow-casting light spheres) before any
            // parallel gather fires so each worker can query it lock-free.
            BuildSceneCullContext();

            const size_t EntityCount       = StaticView.size_hint() + SkeletalView.size_hint();
            const size_t EstimatedProxies  = EntityCount * 2;

            Instances.reserve(EstimatedProxies);
            DrawMeshletStartOffsets.reserve(EstimatedProxies);
            DrawCommands.reserve(EstimatedProxies);

            // One thread-local accumulator per scheduler thread.
            const uint32 NumThreads = GTaskSystem->GetScheduler().GetNumTaskThreads();

            // Frame arenas back every per-thread TFrameVector/TFrameHashMap.
            // 4 MB block size handles a single TFrameVector growth doubling
            // up to ~1 MB without overflow.
            constexpr SIZE_T kArenaBlockSize = 4 * 1024 * 1024;
            if (FrameArenas.size() < NumThreads)
            {
                FrameArenas.reserve(NumThreads);
                while (FrameArenas.size() < NumThreads)
                {
                    FrameArenas.push_back(MakeUnique<FBlockLinearAllocator>(kArenaBlockSize));
                }
            }
            for (uint32 t = 0; t < NumThreads; ++t)
            {
                FrameArenas[t]->Reset();
            }

            TVector<FThreadLocalDrawData> ThreadLocal;
            ThreadLocal.reserve(NumThreads);
            for (uint32 t = 0; t < NumThreads; ++t)
            {
                ThreadLocal.emplace_back(FFrameArenaAllocator(FrameArenas[t].get()));
            }
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

            // Serial fit + allocate. Runs after the parallel light pass so the
            // whole shadow request set is visible and we can shrink
            // proportionally when sum(area) exceeds the atlas budget.
            AllocateShadowTiles();
        }
        
        
        //========================================================================================================================

        // All CPU-side state mutation (buffer resize, binding-set recreation, scene-global
        // finalization) must happen BEFORE any pass lambdas run. The render graph records
        // batches in parallel, so a pass in a later batch (e.g. CSM) may capture its buffer
        // handles concurrently with an earlier batch's "Write Scene Buffers" lambda. If the
        // resize is done inside that lambda, later batches can latch the old undersized
        // buffer handle and fire a validation error (or crash) on draw recording.

        SceneGlobalData.CullData.InstanceNum = (uint32)Instances.size();

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

        // BuildCullViews populates CullViews[] and IndirectArgs[] (indexed
        // v * NumDraws + d with FirstInstance = v * TotalMeshletBound + prefix[d])
        // and must run after AllocateShadowTiles so every shadow view's VP
        // matrix is settled. Everything downstream (size calcs, upload) reads
        // the array lengths it produced.
        BuildCullViews(SceneViewport->GetViewVolume());

        const uint32 NumCullViews         = (uint32)CullViews.size();
        const uint32 NumDraws             = NumDrawsPerView;
        SceneGlobalData.CullData.NumDraws = NumDraws;

        const SIZE_T SimpleVertexSize     = SimpleVertices.size() * sizeof(FSimpleElementVertex);
        const SIZE_T InstanceSize         = Instances.size() * sizeof(FGPUInstance);
        const SIZE_T BoneDataSize         = BonesData.size() * sizeof(glm::mat4);

        const SIZE_T ActiveLightsSize  = LightData.NumLights * sizeof(FLight);
        const SIZE_T LightsUploadSize  = offsetof(FSceneLightData, Lights) + ActiveLightsSize;
        const uint32 ActiveShadowCount = glm::min<uint32>(ShadowDataCount.load(std::memory_order_acquire), (uint32)MAX_SHADOWS);
        const SIZE_T ShadowsUploadSize = ActiveShadowCount * sizeof(FLightShadowData);
        // Buffer must be sized to cover the shadow suffix even when uploading
        // only the active slice; otherwise WriteBuffer at ShadowsOffset would
        // overrun a buffer sized only to LightsUploadSize.
        const SIZE_T LightUploadSize   = offsetof(FSceneLightData, Shadows) + ShadowsUploadSize;
        const SIZE_T BillboardSize     = BillboardInstances.size() * sizeof(FBillboardInstance);

        // Shared meshlet draw list: NumViews * TotalMeshletBound FMeshletDraw
        // entries (sizeof(FMeshletDraw) == 2 * sizeof(uint32)). Each view owns
        // a disjoint slice addressed by FCullView.DrawListOffset. NumCullViews
        // here already includes the appended camera-late view, so its slice is
        // pre-allocated contiguously after the last shadow view.
        const SIZE_T MeshletDrawListSize = glm::max<SIZE_T>(
            sizeof(uint32) * 2,
            (SIZE_T)NumCullViews * (SIZE_T)TotalMeshletBound * sizeof(uint32) * 2);

        // Shared indirect args: NumViews * NumDraws FDrawIndirectArguments.
        const SIZE_T IndirectArgsSize = glm::max<SIZE_T>(
            sizeof(FDrawIndirectArguments),
            IndirectArgs.size() * sizeof(FDrawIndirectArguments));

        const SIZE_T CullViewSize = glm::max<SIZE_T>(
            sizeof(FCullView),
            (SIZE_T)NumCullViews * sizeof(FCullView));

        // Worst-case defer list: every camera meshlet is HZB-occluded in
        // phase 0 (e.g. first frame where previous HZB is cleared). Sizing to
        // TotalMeshletBound entries (sizeof(FMeshletDeferred) == 4 * uint32)
        // matches CullMeshlets.slang's FMeshletDeferred stride.
        const SIZE_T DeferListSize = glm::max<SIZE_T>(
            sizeof(uint32) * 4,
            (SIZE_T)TotalMeshletBound * sizeof(uint32) * 4);

        bool bAnyBufferResized = false;
        bAnyBufferResized |= RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::Instance], (uint32)InstanceSize, 1.2f);
        bAnyBufferResized |= RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::SimpleVertex], (uint32)SimpleVertexSize, 1.2f);
        bAnyBufferResized |= RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::Bone], (uint32)BoneDataSize, 1.2f);
        bAnyBufferResized |= RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::Light], (uint32)LightUploadSize, 1.2f);
        bAnyBufferResized |= RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::Billboards], (uint32)BillboardSize, 1.2f);
        bAnyBufferResized |= RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::CullView], (uint32)CullViewSize, 1.2f);
        bAnyBufferResized |= RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::MeshletDrawList], (uint32)MeshletDrawListSize, 1.2f);
        bAnyBufferResized |= RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::IndirectArgs], (uint32)IndirectArgsSize, 1.2f);
        bAnyBufferResized |= RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::MeshletDeferList], (uint32)DeferListSize, 1.2f);

        if (bAnyBufferResized)
        {
            CreateLayouts();
        }

        {
            LUMINA_PROFILE_SECTION_COLORED("Write Scene Buffers", tracy::Color::OrangeRed3);

            CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Scene), EResourceStates::CopyDest);
            CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Instance), EResourceStates::CopyDest);
            CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Bone), EResourceStates::CopyDest);
            CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::CullView), EResourceStates::CopyDest);
            CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::IndirectArgs), EResourceStates::CopyDest);
            CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::SimpleVertex), EResourceStates::CopyDest);
            CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Light), EResourceStates::CopyDest);
            CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Billboards), EResourceStates::CopyDest);
            CmdList.CommitBarriers();

            CmdList.DisableAutomaticBarriers();
            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Scene), &SceneGlobalData, sizeof(FSceneGlobalData));
            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Instance), Instances.data(), InstanceSize);
            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Bone), BonesData.data(),  BoneDataSize);

            // Per-view descriptors (camera + every shadow view) the unified
            // CullMeshlets pass iterates.
            if (!CullViews.empty())
            {
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::CullView), CullViews.data(), CullViews.size() * sizeof(FCullView));
            }

            // Per-view indirect args with FirstInstance pre-seeded so each
            // atomic append in CullMeshlets lands inside its own slice.
            // InstanceCount starts at 0 and is atomically incremented.
            if (!IndirectArgs.empty())
            {
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::IndirectArgs), IndirectArgs.data(), IndirectArgs.size() * sizeof(FDrawIndirectArguments));
            }

            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::SimpleVertex), SimpleVertices.data(), SimpleVertexSize);
            // Upload only the populated prefix of the light buffer: the
            // header + active hot FLight entries, and the active FLightShadowData
            // entries. The unused middle region is never read by shaders.
            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Light), &LightData, LightsUploadSize, 0);
            if (ShadowsUploadSize > 0)
            {
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Light), &LightData.Shadows[0], ShadowsUploadSize, offsetof(FSceneLightData, Shadows));
            }
            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Billboards), BillboardInstances.data(), BillboardSize);
            CmdList.EnableAutomaticBarriers();
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

    // Resolved view of a material slot. The per-surface flag derivation
    // depends only on (MaterialIndex, MeshComponent flags, bSignificantOccluder),
    // all entity-constant; cache once per entity and skip ~9 virtual calls
    // per repeat lookup.
    struct FResolvedSlot
    {
        FRHIVertexShader*   VertexShader;
        FRHIPixelShader*    PixelShader;
        uint64              MaterialID;
        EInstanceFlags      ExtraFlags;
        uint16              MaterialIdx;
        int16               SlotIdx;
        uint8               bTranslucent     : 1;
        uint8               bMasked          : 1;
        uint8               bAdditive        : 1;
        uint8               bDrawInDepthPass : 1;
    };

    template <typename TComponent>
    static const FResolvedSlot& LookupOrResolveSlot(
        TFixedVector<FResolvedSlot, 16>& Cache,
        const TComponent&                MeshComponent,
        int16                            SlotIdx,
        bool                             bSignificantOccluder)
    {
        // Fast path: the most-recently-resolved slot is at the back. Mesh
        // surfaces are typically grouped by material slot, so this hits often.
        if (!Cache.empty() && Cache.back().SlotIdx == SlotIdx)
        {
            return Cache.back();
        }
        for (const FResolvedSlot& C : Cache)
        {
            if (C.SlotIdx == SlotIdx)
            {
                return C;
            }
        }

        CMaterialInterface* Material = MeshComponent.GetMaterialForSlot(SlotIdx);
        // Terrain materials route through the wrong pipeline layout; fall back.
        if (IsValid(Material) && Material->GetMaterialType() == EMaterialType::Terrain)
        {
            Material = nullptr;
        }
        if (!IsValid(Material) || !IsValid(Material->GetMaterial()) || !Material->IsReadyForRender())
        {
            Material = CMaterial::GetDefaultMaterial();
        }

        const EBlendMode BlendMode    = Material->GetBlendMode();
        const bool       bTranslucent = BlendMode == EBlendMode::Translucent || BlendMode == EBlendMode::Additive;
        const bool       bMasked      = BlendMode == EBlendMode::Masked;
        const bool       bAdditive    = BlendMode == EBlendMode::Additive;
        const bool       bTwoSided    = bTranslucent || Material->IsTwoSided();

        EInstanceFlags Extra = EInstanceFlags::None;
        if (MeshComponent.bCastShadow && Material->DoesCastShadows()) Extra |= EInstanceFlags::CastShadow;
        if (bTranslucent)                                              Extra |= EInstanceFlags::Translucent;
        if (bMasked)                                                   Extra |= EInstanceFlags::Masked;
        if (bTwoSided)                                                 Extra |= EInstanceFlags::TwoSided;

        FResolvedSlot& C = Cache.emplace_back();
        C.VertexShader     = Material->GetVertexShader();
        C.PixelShader      = Material->GetPixelShader();
        C.MaterialID       = (uint64)Material->GetMaterial();
        C.ExtraFlags       = Extra;
        C.MaterialIdx      = (uint16)Material->GetMaterialIndex();
        C.SlotIdx          = SlotIdx;
        C.bTranslucent     = bTranslucent;
        C.bMasked          = bMasked;
        C.bAdditive        = bAdditive;
        C.bDrawInDepthPass = MeshComponent.bUseAsOccluder && !bTranslucent && bSignificantOccluder;
        return C;
    }

    static uint16 FindOrAddLocalBatch(FForwardRenderScene::FThreadLocalDrawData& Local, const FDrawBatchKey& Key, FRHIVertexShader* VS, FRHIPixelShader* PS)
    {
        // Linear scan: per-thread batch counts are tiny (typically <30).
        const uint32 Count = (uint32)Local.LocalBatches.size();
        for (uint32 i = 0; i < Count; ++i)
        {
            if (Local.LocalBatches[i].Key == Key)
            {
                return (uint16)i;
            }
        }

        // Pass the arena explicitly so the new entry's inner TFrameVectors
        // bind to the same per-thread arena.
        FForwardRenderScene::FLocalBatchEntry& Entry = Local.LocalBatches.emplace_back(Local.Arena);
        Entry.Key          = Key;
        Entry.VertexShader = VS;
        Entry.PixelShader  = PS;

        return (uint16)Count;
    }

    static uint16 FindOrAddLocalDraw(FForwardRenderScene::FLocalBatchEntry& Batch, const FDrawKey& Key, uint32 MeshletCount)
    {
        auto It = Batch.DrawIndexByKey.find(Key);
        if (It != Batch.DrawIndexByKey.end())
        {
            const uint16 Idx = It->second;
            Batch.LocalDrawCounts[Idx]++;
            Batch.LocalMeshletCounts[Idx] += MeshletCount;
            return Idx;
        }

        const uint16 NewIdx = (uint16)Batch.LocalDraws.size();
        Batch.LocalDraws.push_back(Key);
        Batch.LocalDrawCounts.push_back(1);
        Batch.LocalMeshletCounts.push_back(MeshletCount);
        Batch.DrawIndexByKey.emplace(Key, NewIdx);
        return NewIdx;
    }

    void FForwardRenderScene::ProcessStaticMeshEntityInternal(entt::entity Entity, const SStaticMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local)
    {
        CMesh* Mesh = MeshComponent.StaticMesh;
        if (!IsValid(Mesh))
        {
            return;
        }

        const glm::mat4& TransformMatrix = TransformComponent.CachedMatrix;

        // Compute world bounds first so we can reject before paying the cost
        // of resolving mesh addresses, iterating surfaces, looking up batches
        // and emplacing FProcessedDrawItem entries. BoundsScale lets assets
        // inflate the cull sphere when animation or displacement pushes
        // geometry beyond the asset-baked AABB.
        const float     CullScale   = glm::max(MeshComponent.BoundsScale, 1.0f);
        const FAABB     BoundingBox = Mesh->GetAABB().ToWorld(TransformMatrix);
        const glm::vec3 Center      = (BoundingBox.Min + BoundingBox.Max) * 0.5f;
        const glm::vec3 Extents     = BoundingBox.Max - Center;
        const float     Radius      = glm::length(Extents) * CullScale;
        const glm::vec4 SphereBounds = glm::vec4(Center, Radius);

        if (!SceneCullContext.ShouldKeep(
                Center,
                Radius,
                MeshComponent.bCastShadow,
                MeshComponent.MaxDrawDistance,
                glm::vec3(SceneGlobalData.CameraData.Location)))
        {
            ++Local.Stats.NumInstancesCulled;
            return;
        }

        const FMeshResource& Resource = Mesh->GetMeshResource();
        const uint64 MeshletHeaderAddress = Mesh->GetMeshBuffers().MeshletHeaderBuffer
            ? Mesh->GetMeshBuffers().MeshletHeaderBuffer->GetAddress()
            : 0ull;

        // Screen-space coverage proxy: an object with radius r at distance d
        // subtends angular diameter ~ 2r/d. We square both sides to skip the
        // sqrt for distance. Threshold tuned so only meshes covering a
        // meaningful chunk of the view end up in the depth pre-pass -- tiny
        // props are faster to overdraw than to rasterize twice.
        const glm::vec3 CameraPos  = glm::vec3(SceneGlobalData.CameraData.Location);
        const glm::vec3 ToCamera   = Center - CameraPos;
        const float     DistSq     = glm::dot(ToCamera, ToCamera);
        constexpr float kMinAngularSize = 0.05f;
        const bool bSignificantOccluder = (Radius * Radius) > DistSq * (kMinAngularSize * kMinAngularSize);

        EInstanceFlags BaseFlags = EInstanceFlags::None;
        if (MeshComponent.bReceiveShadow)
        {
            BaseFlags |= EInstanceFlags::ReceiveShadow;
        }
        if (MeshComponent.bIgnoreOcclusionCulling)
        {
            BaseFlags |= EInstanceFlags::IgnoreOcclusionCulling;
        }

        // One shared FEntityRecord; every surface item references it by index.
        const uint32 EntityRecordIdx = (uint32)Local.EntityRecords.size();
        FEntityRecord& EntityRecord = Local.EntityRecords.emplace_back();
        EntityRecord.Transform            = TransformMatrix;
        EntityRecord.SphereBounds         = SphereBounds;
        EntityRecord.MeshletHeaderAddress = MeshletHeaderAddress;
        EntityRecord.CustomData           = MeshComponent.CustomPrimitiveData.Data.Packed;
        EntityRecord.EntityID             = entt::to_integral(Entity);
        EntityRecord.LocalBoneOffset      = ~0u;
        EntityRecord._Pad                 = 0u;

        // Per-entity cache; the slot resolution chain (~9 virtual calls) only
        // fires the first time we see each unique MaterialIndex.
        TFixedVector<FResolvedSlot, 16> SlotCache;

        for (const FGeometrySurface& Surface : Resource.GeometrySurfaces)
        {
            const FResolvedSlot& Slot = LookupOrResolveSlot(SlotCache, MeshComponent, Surface.MaterialIndex, bSignificantOccluder);

            const EInstanceFlags Flags = BaseFlags | Slot.ExtraFlags;

            FDrawBatchKey BatchKey
            {
                .MaterialID       = Slot.MaterialID,
                .bDrawInDepthPass = (uint32)(Slot.bDrawInDepthPass ? 1u : 0u),
                .bTranslucent     = (uint32)(Slot.bTranslucent     ? 1u : 0u),
                .bMasked          = (uint32)(Slot.bMasked          ? 1u : 0u),
                .bAdditive        = (uint32)(Slot.bAdditive        ? 1u : 0u),
            };
            // Zero SurfaceMeshletCount when no header was uploaded so the cull
            // shader's MeshletHeader deref is gated.
            const uint32 SurfaceMeshletCount = MeshletHeaderAddress ? Surface.MeshletCount : 0u;
            const uint16 LocalBatchIdx = FindOrAddLocalBatch(Local, BatchKey, Slot.VertexShader, Slot.PixelShader);
            const uint16 LocalDrawIdx  = FindOrAddLocalDraw(Local.LocalBatches[LocalBatchIdx], FDrawKey{ Surface.StartIndex, Surface.IndexCount }, SurfaceMeshletCount);
            Local.MaxMeshletsPerInstance = std::max(Local.MaxMeshletsPerInstance, SurfaceMeshletCount);

            FProcessedDrawItem& Item = Local.Items.emplace_back();
            Item.EntityRecordIndex    = EntityRecordIdx;
            Item.SurfaceMeshletOffset = Surface.MeshletOffset;
            Item.SurfaceMeshletCount  = SurfaceMeshletCount;
            Item.Flags                = Flags;
            Item.MaterialIndex        = Slot.MaterialIdx;
            Item.LocalBatchIndex      = LocalBatchIdx;
            Item.LocalDrawIndex       = LocalDrawIdx;
            Item._Pad                 = 0;
        }
    }

    void FForwardRenderScene::ProcessSkeletalMeshEntityInternal(entt::entity Entity, const SSkeletalMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local)
    {
        CMesh* Mesh = MeshComponent.SkeletalMesh;
        if (!IsValid(Mesh))
        {
            return;
        }

        const glm::mat4 TransformMatrix = TransformComponent.GetWorldMatrix();

        // Reject before uploading per-bone data; the bone copy below is the
        // biggest per-entity cost for skeletal paths. Bind-pose AABB is a
        // conservative envelope; BoundsScale can inflate it when animations
        // push geometry outside the asset bounds.
        const float     CullScale   = glm::max(MeshComponent.BoundsScale, 1.0f);
        const FAABB     BoundingBox = Mesh->GetAABB().ToWorld(TransformMatrix);
        const glm::vec3 Center      = (BoundingBox.Min + BoundingBox.Max) * 0.5f;
        const glm::vec3 Extents     = BoundingBox.Max - Center;
        const float     Radius      = glm::length(Extents) * CullScale;
        const glm::vec4 SphereBounds = glm::vec4(Center, Radius);

        if (!SceneCullContext.ShouldKeep(
                Center,
                Radius,
                MeshComponent.bCastShadow,
                MeshComponent.MaxDrawDistance,
                glm::vec3(SceneGlobalData.CameraData.Location)))
        {
            ++Local.Stats.NumInstancesCulled;
            return;
        }

        const FMeshResource& Resource = Mesh->GetMeshResource();

        const uint32 LocalBoneOffset = (uint32)Local.BonesData.size();
        Local.BonesData.insert(Local.BonesData.end(), MeshComponent.BoneTransforms.begin(), MeshComponent.BoneTransforms.end());

        // See static-mesh path for the angular-size reasoning; skinned bind-
        // pose bounds are conservative but fine for this coarse test.
        const glm::vec3 CameraPos  = glm::vec3(SceneGlobalData.CameraData.Location);
        const glm::vec3 ToCamera   = Center - CameraPos;
        const float     DistSq     = glm::dot(ToCamera, ToCamera);
        constexpr float kMinAngularSize = 0.05f;
        const bool bSignificantOccluder = (Radius * Radius) > DistSq * (kMinAngularSize * kMinAngularSize);

        EInstanceFlags BaseFlags = EInstanceFlags::Skinned;
        if (MeshComponent.bReceiveShadow)
        {
            BaseFlags |= EInstanceFlags::ReceiveShadow;
        }

        const uint64 MeshletHeaderAddress = Mesh->GetMeshBuffers().MeshletHeaderBuffer
            ? Mesh->GetMeshBuffers().MeshletHeaderBuffer->GetAddress()
            : 0ull;

        const uint32 EntityRecordIdx = (uint32)Local.EntityRecords.size();
        FEntityRecord& EntityRecord = Local.EntityRecords.emplace_back();
        EntityRecord.Transform            = TransformMatrix;
        EntityRecord.SphereBounds         = SphereBounds;
        EntityRecord.MeshletHeaderAddress = MeshletHeaderAddress;
        EntityRecord.CustomData           = MeshComponent.CustomPrimitiveData.Data.Packed;
        EntityRecord.EntityID             = entt::to_integral(Entity);
        EntityRecord.LocalBoneOffset      = LocalBoneOffset;
        EntityRecord._Pad                 = 0u;

        TFixedVector<FResolvedSlot, 16> SlotCache;

        for (const FGeometrySurface& Surface : Resource.GeometrySurfaces)
        {
            const FResolvedSlot& Slot = LookupOrResolveSlot(SlotCache, MeshComponent, Surface.MaterialIndex, bSignificantOccluder);

            const EInstanceFlags Flags = BaseFlags | Slot.ExtraFlags;

            FDrawBatchKey BatchKey
            {
                .MaterialID       = Slot.MaterialID,
                .bDrawInDepthPass = (uint32)(Slot.bDrawInDepthPass ? 1u : 0u),
                .bTranslucent     = (uint32)(Slot.bTranslucent     ? 1u : 0u),
                .bMasked          = (uint32)(Slot.bMasked          ? 1u : 0u),
                .bAdditive        = (uint32)(Slot.bAdditive        ? 1u : 0u),
            };
            const uint32 SurfaceMeshletCount = MeshletHeaderAddress ? Surface.MeshletCount : 0u;
            const uint16 LocalBatchIdx = FindOrAddLocalBatch(Local, BatchKey, Slot.VertexShader, Slot.PixelShader);
            const uint16 LocalDrawIdx  = FindOrAddLocalDraw(Local.LocalBatches[LocalBatchIdx], FDrawKey{ Surface.StartIndex, Surface.IndexCount }, SurfaceMeshletCount);
            Local.MaxMeshletsPerInstance = std::max(Local.MaxMeshletsPerInstance, SurfaceMeshletCount);

            FProcessedDrawItem& Item = Local.Items.emplace_back();
            Item.EntityRecordIndex    = EntityRecordIdx;
            Item.SurfaceMeshletOffset = Surface.MeshletOffset;
            Item.SurfaceMeshletCount  = SurfaceMeshletCount;
            Item.Flags                = Flags;
            Item.MaterialIndex        = Slot.MaterialIdx;
            Item.LocalBatchIndex      = LocalBatchIdx;
            Item.LocalDrawIndex       = LocalDrawIdx;
            Item._Pad                 = 0;
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
        uint64 TotalInstancesCulled = 0;
        for (uint32 t = 0; t < NumThreads; ++t)
        {
            FThreadLocalDrawData& Local = ThreadLocal[t];
            ThreadBoneBase[t] = (uint32)BonesData.size();
            BonesData.insert(BonesData.end(), Local.BonesData.begin(), Local.BonesData.end());
            TotalInstances += (uint32)Local.Items.size();
            TotalInstancesCulled += Local.Stats.NumInstancesCulled;
        }
        RenderStats.NumInstancesCulled += TotalInstancesCulled;

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

        // Group LocalBatches by their resolved global batch so the heavy
        // per-batch passes below can fan out over batches in parallel.
        // Pre-resize the per-LocalBatch output vectors here too: the parallel
        // tasks below would otherwise race on each thread's frame arena.
        TVector<TVector<FLocalBatchEntry*>> BatchToLocals(NumBatches);
        for (FThreadLocalDrawData& Local : ThreadLocal)
        {
            for (FLocalBatchEntry& LocalBatch : Local.LocalBatches)
            {
                BatchToLocals[LocalBatch.GlobalBatchIndex].push_back(&LocalBatch);

                const uint32 NumLocal = (uint32)LocalBatch.LocalDraws.size();
                LocalBatch.LocalToGlobalDraw.resize(NumLocal);
                LocalBatch.LocalDrawWriteBase.resize(NumLocal);
            }
        }

        // Phase C: per-batch draw dedupe. Each task owns its batch's
        // GlobalDrawsPerBatch[b] slot and its own scratch hash map; batches
        // are independent so no synchronization is needed.
        TVector<TVector<FDrawKey>> GlobalDrawsPerBatch(NumBatches);
        {
            FTaskGraph DedupGraph;
            DedupGraph.AddParallelFor(NumBatches, 1, [&](const Task::FParallelRange& Range)
            {
                for (uint32 b = Range.Start; b < Range.End; ++b)
                {
                    THashMap<uint64, uint32> Dedupe;
                    TVector<FDrawKey>&       Globals = GlobalDrawsPerBatch[b];
                    for (FLocalBatchEntry* LB : BatchToLocals[b])
                    {
                        const uint32 NumLocal = (uint32)LB->LocalDraws.size();
                        for (uint32 ld = 0; ld < NumLocal; ++ld)
                        {
                            const FDrawKey& K = LB->LocalDraws[ld];
                            const uint64 PackedKey = ((uint64)K.StartIndex << 32) | (uint64)K.IndexCount;
                            uint32 GlobalDraw;
                            auto It = Dedupe.find(PackedKey);
                            if (It != Dedupe.end())
                            {
                                GlobalDraw = It->second;
                            }
                            else
                            {
                                GlobalDraw = (uint32)Globals.size();
                                Globals.push_back(K);
                                Dedupe.emplace(PackedKey, GlobalDraw);
                            }
                            LB->LocalToGlobalDraw[ld] = GlobalDraw;
                        }
                    }
                }
            });
            DedupGraph.Dispatch();
            DedupGraph.Wait();
        }

        // Each batch gets a contiguous block of draw args.
        TVector<uint32> BatchDrawArgBase(NumBatches);
        uint32 TotalDrawArgs = 0;
        for (uint32 b = 0; b < NumBatches; ++b)
        {
            BatchDrawArgBase[b]                 = TotalDrawArgs;
            DrawCommands[b].IndirectDrawOffset  = TotalDrawArgs;
            DrawCommands[b].DrawCount           = (uint32)GlobalDrawsPerBatch[b].size();
            TotalDrawArgs                       += (uint32)GlobalDrawsPerBatch[b].size();
        }

        DrawMeshletStartOffsets.resize(TotalDrawArgs);
        Instances.resize(TotalInstances);
        NumDrawsPerView = TotalDrawArgs;

        TVector<uint32> DrawInstanceCounts(TotalDrawArgs, 0u);
        TVector<uint32> MeshletCountsPerDraw(TotalDrawArgs, 0u);

        // Phase E: per-batch sparse count. Each batch's GlobalDraw range is
        // disjoint (BatchDrawArgBase prefix sum), so per-batch tasks write
        // into non-overlapping slices of DrawInstanceCounts/MeshletCountsPerDraw.
        {
            FTaskGraph CountGraph;
            CountGraph.AddParallelFor(NumBatches, 1, [&](const Task::FParallelRange& Range)
            {
                for (uint32 b = Range.Start; b < Range.End; ++b)
                {
                    const uint32 BatchBase = BatchDrawArgBase[b];
                    for (FLocalBatchEntry* LB : BatchToLocals[b])
                    {
                        const uint32 NumLocal = (uint32)LB->LocalDrawCounts.size();
                        for (uint32 ld = 0; ld < NumLocal; ++ld)
                        {
                            const uint32 GlobalDraw = BatchBase + LB->LocalToGlobalDraw[ld];
                            DrawInstanceCounts[GlobalDraw]   += LB->LocalDrawCounts[ld];
                            MeshletCountsPerDraw[GlobalDraw] += LB->LocalMeshletCounts[ld];
                        }
                    }
                }
            });
            CountGraph.Dispatch();
            CountGraph.Wait();
        }

        // Prefix sum for instance offsets (serial; data dependency).
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

        // Phase G: per-batch cursor assignment. Like phase E, batches own
        // disjoint DrawCursor slices so per-batch parallelism is race-free.
        TVector<uint32> DrawCursor = DrawInstanceOffsets;
        {
            FTaskGraph CursorGraph;
            CursorGraph.AddParallelFor(NumBatches, 1, [&](const Task::FParallelRange& Range)
            {
                for (uint32 b = Range.Start; b < Range.End; ++b)
                {
                    const uint32 BatchBase = BatchDrawArgBase[b];
                    for (FLocalBatchEntry* LB : BatchToLocals[b])
                    {
                        const uint32 NumLocal = (uint32)LB->LocalDrawCounts.size();
                        for (uint32 ld = 0; ld < NumLocal; ++ld)
                        {
                            const uint32 GlobalDraw = BatchBase + LB->LocalToGlobalDraw[ld];
                            LB->LocalDrawWriteBase[ld] = DrawCursor[GlobalDraw];
                            DrawCursor[GlobalDraw] += LB->LocalDrawCounts[ld];
                        }
                    }
                }
            });
            CursorGraph.Dispatch();
            CursorGraph.Wait();
        }

        // Per-draw meshlet prefix sum. CullMeshlets atomically appends into the
        // slice [v * TotalMeshletBound + DrawMeshletStartOffsets[d], ...), and
        // BuildCullViews seeds IndirectArgs[v * NumDraws + d].FirstInstance with
        // exactly that base so every view writes into its own disjoint slice.
        MaxMeshletsPerInstance = 0u;
        for (FThreadLocalDrawData& Local : ThreadLocal)
        {
            MaxMeshletsPerInstance = std::max(MaxMeshletsPerInstance, Local.MaxMeshletsPerInstance);
        }

        uint32 MeshletRunning = 0u;
        for (uint32 d = 0; d < TotalDrawArgs; ++d)
        {
            DrawMeshletStartOffsets[d] = MeshletRunning;
            MeshletRunning            += MeshletCountsPerDraw[d];
        }
        TotalMeshletBound = MeshletRunning;

        // Parallel instance write. Each worker only touches its own Local data,
        // so the in-place cursor advance needs no synchronization. FGPUInstance
        // is composed here from the per-entity FEntityRecord + per-surface item.
        {
            LUMINA_PROFILE_SECTION("Parallel Instance Write");

            FTaskGraph WriteGraph;
            WriteGraph.AddParallelFor(NumThreads, 1, [&](const Task::FParallelRange& Range)
            {
                for (uint32 t = Range.Start; t < Range.End; ++t)
                {
                    FThreadLocalDrawData& Local = ThreadLocal[t];
                    const uint32 BoneBase = ThreadBoneBase[t];

                    for (FProcessedDrawItem& Item : Local.Items)
                    {
                        FLocalBatchEntry& LocalBatch = Local.LocalBatches[Item.LocalBatchIndex];
                        const uint32 GlobalDraw = BatchDrawArgBase[LocalBatch.GlobalBatchIndex]
                                                + LocalBatch.LocalToGlobalDraw[Item.LocalDrawIndex];

                        const uint32 WriteIdx = LocalBatch.LocalDrawWriteBase[Item.LocalDrawIndex]++;
                        const FEntityRecord& Entity = Local.EntityRecords[Item.EntityRecordIndex];

                        const uint16 BoneOffset = Entity.LocalBoneOffset != ~0u
                            ? (uint16)(BoneBase + Entity.LocalBoneOffset)
                            : (uint16)0;

                        FGPUInstance& Out = Instances[WriteIdx];
                        Out.Transform                  = Entity.Transform;
                        Out.SphereBounds               = Entity.SphereBounds;
                        Out.VBAddress                  = 0ull;
                        Out._ReservedAddress           = 0ull;
                        Out.MeshletHeaderAddress       = Entity.MeshletHeaderAddress;
                        Out.DrawIDAndFlags             = PackDrawIDAndFlags(GlobalDraw, Item.Flags);
                        Out.SurfaceMeshletOffset       = Item.SurfaceMeshletOffset;
                        Out.SurfaceMeshletCount        = Item.SurfaceMeshletCount;
                        Out.CustomData                 = Entity.CustomData;
                        Out.BoneOffsetAndMaterialIndex = PackBoneOffsetAndMaterial(BoneOffset, Item.MaterialIndex);
                        Out.EntityID                   = Entity.EntityID;
                    }
                }
            });
            WriteGraph.Dispatch();
            WriteGraph.Wait();
        }

        RenderStats.NumBatches = NumBatches;
        OpaqueDrawList.reserve(NumBatches);
        OpaqueOccluderDrawList.reserve(NumBatches);
        TranslucentDrawList.reserve(NumBatches);
        for (uint32 i = 0; i < NumBatches; ++i)
        {
            const FMeshDrawCommand& Cmd = DrawCommands[i];
            if (Cmd.bTranslucent)
            {
                TranslucentDrawList.push_back(i);
            }
            else
            {
                OpaqueDrawList.push_back(i);
                if (Cmd.bDrawInDepthPass)
                {
                    OpaqueOccluderDrawList.push_back(i);
                }
            }
        }
    }

    bool FForwardRenderScene::ShouldRequestShadow(const glm::vec3& LightPosition, float LightRadius) const
    {
        return SceneGlobalData.CullData.Frustum.IntersectsSphere(LightPosition, LightRadius);
    }

    void FForwardRenderScene::BuildSceneCullContext()
    {
        LUMINA_PROFILE_SCOPE();

        SceneCullContext.Reset();
        SceneCullContext.bEnabled = RenderSettings.bCPUInstanceCull;
        SceneCullContext.Frustum  = SceneGlobalData.CullData.Frustum;

        if (!SceneCullContext.bEnabled)
        {
            return;
        }

        FEntityRegistry& Registry = World->GetEntityRegistry();

        // Sun direction: serial lookup of the first enabled directional
        // light. Matches ProcessDirectionalLight (last-writer wins, fine for
        // a single scene sun). Guard against non-normalized values.
        auto DirectionalView = Registry.view<SDirectionalLightComponent>(entt::exclude<SDisabledTag>);
        for (entt::entity Entity : DirectionalView)
        {
            const SDirectionalLightComponent& Light = DirectionalView.get<SDirectionalLightComponent>(Entity);
            const float DirLenSq = glm::dot(Light.Direction, Light.Direction);
            if (DirLenSq > 0.0001f)
            {
                SceneCullContext.SunDirection = glm::normalize(Light.Direction);
                SceneCullContext.bHasSun      = true;
                break;
            }
        }

        if (SceneCullContext.bHasSun)
        {
            // Sweep the camera frustum along the sun direction so casters that
            // live outside the camera view but between sun and view still get
            // kept. Distance must match the ShadowSweepDistance used when
            // CullData.ShadowFrustum is built (see CompileDrawCommands), or
            // instances near the sweep boundary get dropped here while the
            // GPU cull pass still wants them.
            constexpr float ShadowSweepDistance = 2000.0f;
            SceneCullContext.SunShadowFrustum = SceneCullContext.Frustum.Extruded(
                SceneCullContext.SunDirection, ShadowSweepDistance);
        }

        // Shadow-casting local lights. Influence region is a sphere around
        // the light with attenuation radius. Only collect lights whose shadow
        // sphere intersects the camera frustum; casters outside for lights
        // also outside can't affect any visible pixel.
        auto PointView = Registry.view<SPointLightComponent, STransformComponent>(entt::exclude<SDisabledTag>);
        for (entt::entity Entity : PointView)
        {
            const SPointLightComponent& Light    = PointView.get<SPointLightComponent>(Entity);
            if (!Light.bCastShadows)
            {
                continue;
            }
            const STransformComponent&  Transform = PointView.get<STransformComponent>(Entity);
            const glm::vec3 Position = Transform.WorldTransform.Location;
            const float     Radius   = Light.Attenuation;
            if (!SceneCullContext.Frustum.IntersectsSphere(Position, Radius))
            {
                continue;
            }
            SceneCullContext.ShadowLights.push_back({ Position, Radius });
        }

        auto SpotView = Registry.view<SSpotLightComponent, STransformComponent>(entt::exclude<SDisabledTag>);
        for (entt::entity Entity : SpotView)
        {
            const SSpotLightComponent& Light    = SpotView.get<SSpotLightComponent>(Entity);
            if (!Light.bCastShadows)
            {
                continue;
            }
            const STransformComponent& Transform = SpotView.get<STransformComponent>(Entity);
            const glm::vec3 Position = Transform.WorldTransform.Location;
            const float     Radius   = Light.Attenuation;
            if (!SceneCullContext.Frustum.IntersectsSphere(Position, Radius))
            {
                continue;
            }
            SceneCullContext.ShadowLights.push_back({ Position, Radius });
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

        FLight Light                = {};
        Light.Flags                 = LIGHT_TYPE_POINT;
        Light.Falloff               = PointLight.Falloff;
        Light.Color                 = PackColor(glm::vec4(PointLight.LightColor, 1.0));
        Light.Intensity             = PointLight.Intensity;
        Light.Radius                = PointLight.Attenuation;
        Light.Position              = TransformComponent.WorldTransform.Location;
        Light.ShadowDataIndex       = INDEX_NONE;

        if (PointLight.bCastShadows && ShouldRequestShadow(Light.Position, Light.Radius))
        {
            const glm::vec3 CamPos = SceneViewport->GetViewVolume().GetViewPosition();
            const float Dist = glm::distance(CamPos, Light.Position);
            constexpr float ResolutionScale = 2048.0f;
            const uint32 DesiredPixels = (uint32)((Light.Radius / glm::max(Dist, 0.01f)) * ResolutionScale);

            FShadowRequest Req;
            Req.LightIndex      = Lights;
            Req.Type            = ELightType::Point;
            Req.DesiredPixels   = DesiredPixels;
            Req.DistanceToCamera = Dist;
            Req.Position        = Light.Position;
            Req.Direction       = glm::vec3(0.0f);
            Req.Up              = glm::vec3(0.0f);
            Req.Attenuation     = Light.Radius;
            Req.OuterFOVDegrees = 0.0f;
            {
                FScopeLock Lock(ShadowRequestMutex);
                ShadowRequests.push_back(Req);
            }
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

        FLight Light                = {};
        Light.Flags                 = LIGHT_TYPE_SPOT;
        Light.Position              = TransformComponent.WorldTransform.Location;
        Light.Direction             = glm::normalize(UpdatedForward);
        Light.Falloff               = SpotLight.Falloff;
        Light.Color                 = PackColor(glm::vec4(SpotLight.LightColor, 1.0));
        Light.Intensity             = SpotLight.Intensity;
        Light.Radius                = SpotLight.Attenuation;
        Light.Angles                = glm::vec2(InnerCos, OuterCos);
        Light.ShadowDataIndex       = INDEX_NONE;

        if (SpotLight.bCastShadows && ShouldRequestShadow(Light.Position, Light.Radius))
        {
            const glm::vec3 CamPos = SceneViewport->GetViewVolume().GetViewPosition();
            const float Dist = glm::distance(CamPos, Light.Position);
            constexpr float ResolutionScale = 2048.0f;
            const uint32 DesiredPixels = (uint32)((Light.Radius / glm::max(Dist, 0.01f)) * ResolutionScale);

            FShadowRequest Req;
            Req.LightIndex      = Lights;
            Req.Type            = ELightType::Spot;
            Req.DesiredPixels   = DesiredPixels;
            Req.DistanceToCamera = Dist;
            Req.Position        = Light.Position;
            Req.Direction       = UpdatedForward;
            Req.Up              = UpdatedUp;
            Req.Attenuation     = SpotLight.Attenuation;
            Req.OuterFOVDegrees = OuterDegrees;
            {
                FScopeLock Lock(ShadowRequestMutex);
                ShadowRequests.push_back(Req);
            }
        }

        LightData.Lights[Lights] = Light;
    }

    void FForwardRenderScene::AllocateShadowTiles()
    {
        if (ShadowRequests.empty())
            return;

        // ----- View-budget fit -------------------------------------------------
        // CullMeshlets.slang reads a fixed-size FCullView array (GMaxCullViews).
        // View 0 is the camera; 1..NumCascades are cascades (if a directional
        // light exists); remaining slots are shadow views (6 per point, 1 per
        // spot). Overflow would record out-of-range base indices and crash
        // the GPU when a draw pass read past IndirectArgs.
        //
        // Farthest-first drop order: nearby shadows dominate the image;
        // distant ones are already shrunk to near-min tiles and lose the
        // least perceived quality when dropped.
        {
            const uint32 SunViews        = LightData.bHasSun ? (uint32)NumCascades : 0u;
            const uint32 ReservedViews   = 1u + SunViews;                     // Camera + CSM cascades.
            const uint32 AvailableViews  = (ReservedViews >= (uint32)GMaxCullViews)
                                         ? 0u
                                         : ((uint32)GMaxCullViews - ReservedViews);

            auto ViewCost = [](const FShadowRequest& Req)
            {
                return Req.Type == ELightType::Point ? 6u : 1u;
            };

            uint32 UsedViews = 0u;
            for (const FShadowRequest& Req : ShadowRequests)
            {
                UsedViews += ViewCost(Req);
            }

            if (UsedViews > AvailableViews)
            {
                // Sort descending by distance; stable eastl::sort so equal-distance
                // requests keep their input order (deterministic across frames).
                TVector<uint32> Order;
                Order.resize(ShadowRequests.size());
                for (uint32 i = 0; i < (uint32)ShadowRequests.size(); ++i)
                {
                    Order[i] = i;
                }
                eastl::stable_sort(Order.begin(), Order.end(),
                    [&](uint32 A, uint32 B)
                    {
                        return ShadowRequests[A].DistanceToCamera > ShadowRequests[B].DistanceToCamera;
                    });

                TVector<bool> Drop;
                Drop.assign(ShadowRequests.size(), false);
                for (uint32 i = 0; i < (uint32)Order.size() && UsedViews > AvailableViews; ++i)
                {
                    const uint32 Idx = Order[i];
                    Drop[Idx] = true;
                    UsedViews -= ViewCost(ShadowRequests[Idx]);
                }

                TVector<FShadowRequest> Kept;
                Kept.reserve(ShadowRequests.size());
                for (uint32 i = 0; i < (uint32)ShadowRequests.size(); ++i)
                {
                    if (!Drop[i])
                    {
                        Kept.push_back(ShadowRequests[i]);
                    }
                }
                ShadowRequests = std::move(Kept);
            }
        }

        if (ShadowRequests.empty())
        {
            return;
        }

        const FShadowAtlasConfig& AtlasConfig = ShadowAtlas.GetConfig();
        const uint32 MinTile   = AtlasConfig.MinTileResolution;
        const uint32 MaxTile   = AtlasConfig.MaxTileResolution;
        const uint32 AtlasSize = AtlasConfig.AtlasResolution;

        // pow2 area budget: all tiles are pow2 so sum(area) <= AtlasSize^2
        // is a sufficient packing guarantee for the quad-tree allocator.
        const uint64 Budget = (uint64)AtlasSize * (uint64)AtlasSize;

        const uint32 NumRequests = (uint32)ShadowRequests.size();

        // Round-up-pow2 clamped to [Min, Max]. Matches FShadowAtlas::AllocateTile's
        // internal quantization so the area sum we reason about is the same area
        // the allocator will actually consume.
        TVector<uint32> Sizes;
        Sizes.resize(NumRequests);
        for (uint32 i = 0; i < NumRequests; ++i)
        {
            uint32 V = ShadowRequests[i].DesiredPixels;
            // Round up to pow2.
            if (V <= 1)
            {
                V = 1;
            }
            else
            {
                --V;
                V |= V >> 1;  V |= V >> 2;  V |= V >> 4;
                V |= V >> 8;  V |= V >> 16;
                ++V;
            }
            Sizes[i] = glm::clamp(V, MinTile, MaxTile);
        }

        auto AreaSum = [&]() -> uint64
        {
            uint64 S = 0;
            for (uint32 i = 0; i < NumRequests; ++i)
            {
                S += (uint64)Sizes[i] * (uint64)Sizes[i];
            }
            return S;
        };

        // Iteratively halve the current largest tile until the set fits
        // budget. Halving the largest drops summed area by 3/4 of its own
        // area, the biggest single-step reduction available.
        while (AreaSum() > Budget)
        {
            uint32 LargestIdx = 0;
            uint32 LargestVal = Sizes[0];
            for (uint32 i = 1; i < NumRequests; ++i)
            {
                if (Sizes[i] > LargestVal)
                {
                    LargestVal = Sizes[i];
                    LargestIdx = i;
                }
            }
            if (LargestVal <= MinTile)
            {
                // Everyone is already at the floor and still over budget.
                // AllocateTile returns INDEX_NONE and the overflow request
                // drops, which is preferable to spinning.
                break;
            }
            Sizes[LargestIdx] = LargestVal >> 1;
        }

        // Largest-first allocation keeps the quad-tree from fragmenting: big
        // tiles get a root split, small tiles fall into whatever quadrants
        // remain. Allocating small-first wastes root splits on leaves.
        TVector<uint32> SortedIndices;
        SortedIndices.resize(NumRequests);
        for (uint32 i = 0; i < NumRequests; ++i)
        {
            SortedIndices[i] = i;
        }
        eastl::sort(SortedIndices.begin(), SortedIndices.end(),
            [&](uint32 A, uint32 B) { return Sizes[A] > Sizes[B]; });

        for (uint32 SortedI = 0; SortedI < NumRequests; ++SortedI)
        {
            const uint32 ReqIdx       = SortedIndices[SortedI];
            const FShadowRequest& Req = ShadowRequests[ReqIdx];
            const uint32 TileSize     = Sizes[ReqIdx];

            const int32 TileIndex = ShadowAtlas.AllocateTile(TileSize);
            if (TileIndex == INDEX_NONE)
                continue;

            const uint32 ShadowSlot = ShadowDataCount.fetch_add(1, std::memory_order_acquire);
            if (ShadowSlot >= (uint32)MAX_SHADOWS)
                continue;

            LightData.Lights[Req.LightIndex].ShadowDataIndex = (int32)ShadowSlot;
            FLightShadowData& ShadowData = LightData.Shadows[ShadowSlot];
            const FShadowTile& Tile      = ShadowAtlas.GetTile(TileIndex);

            if (Req.Type == ELightType::Point)
            {
                // Cube map: 6 faces share the tile's UV rect across layers 0-5.
                // Near plane scales with radius; a fixed 0.01 collapses NDC z
                // into the last ~0.001 of the depth buffer for any realistic
                // light, leaving no precision for the PCF compare.
                const float ShadowNear = glm::max(Req.Attenuation * 0.01f, 0.1f);
                FViewVolume LightView(90.0f, 1.0f, ShadowNear, Req.Attenuation);

                auto SetFace = [&](uint32 Face)
                {
                    switch (Face)
                    {
                        case 0: LightView.SetView(Req.Position, FViewVolume::RightAxis,    FViewVolume::DownAxis);     break;
                        case 1: LightView.SetView(Req.Position, FViewVolume::LeftAxis,     FViewVolume::DownAxis);     break;
                        case 2: LightView.SetView(Req.Position, FViewVolume::UpAxis,       FViewVolume::ForwardAxis);  break;
                        case 3: LightView.SetView(Req.Position, FViewVolume::DownAxis,     FViewVolume::BackwardAxis); break;
                        case 4: LightView.SetView(Req.Position, FViewVolume::ForwardAxis,  FViewVolume::DownAxis);     break;
                        case 5: LightView.SetView(Req.Position, FViewVolume::BackwardAxis, FViewVolume::DownAxis);     break;
                    }
                };

                for (uint32 Face = 0; Face < 6; ++Face)
                {
                    SetFace(Face);
                    ShadowData.ViewProjection[Face] = LightView.ToReverseDepthViewProjectionMatrix();

                    FLightShadow& Shadow   = ShadowData.Shadow[Face];
                    Shadow.AtlasUVOffset   = Tile.UVOffset;
                    Shadow.AtlasUVScale    = Tile.UVScale;
                    Shadow.ShadowMapIndex  = TileIndex;
                    Shadow.ShadowMapLayer  = (int32)Face;
                    Shadow.LightIndex      = (int32)Req.LightIndex;
                    Shadow.ShadowDataIndex = (int32)ShadowSlot;
                }

                PackedShadows[(uint32)ELightType::Point].push_back(ShadowData.Shadow[0]);
            }
            else // Spot
            {
                const float ShadowNear = glm::max(Req.Attenuation * 0.01f, 0.1f);
                FViewVolume ViewVolume(Req.OuterFOVDegrees * 2.0f, 1.0f, ShadowNear, Req.Attenuation);
                ViewVolume.SetView(Req.Position, Req.Direction, Req.Up);
                ShadowData.ViewProjection[0] = ViewVolume.ToReverseDepthViewProjectionMatrix();

                FLightShadow& Shadow   = ShadowData.Shadow[0];
                Shadow.AtlasUVOffset   = Tile.UVOffset;
                Shadow.AtlasUVScale    = Tile.UVScale;
                Shadow.ShadowMapIndex  = TileIndex;
                Shadow.ShadowMapLayer  = 6; // Spot lights live on the dedicated 2D layer.
                Shadow.LightIndex      = (int32)Req.LightIndex;
                Shadow.ShadowDataIndex = (int32)ShadowSlot;

                PackedShadows[(uint32)ELightType::Spot].push_back(Shadow);
            }
        }
    }

    void FForwardRenderScene::BuildCullViews(const FViewVolume& ViewVolume)
    {
        // Shared per-view layout:
        //   MeshletDrawList slice v  = [v * TotalMeshletBound, (v+1) * TotalMeshletBound)
        //   IndirectArgs    slot (v, d) = v * NumDraws + d
        //   IndirectArgs[(v,d)].FirstInstance = v * TotalMeshletBound + DrawMeshletStartOffsets[d]
        //
        // CullMeshlets owns all atomic appends into the draw list and all
        // InstanceCount increments, so InstanceCount starts at 0 every frame
        // and each draw pass indirect-draws out of its own (v, d) slot.

        const uint32 NumDraws = NumDrawsPerView;

        auto PushView = [&](const glm::mat4& ViewProjection, const glm::vec3& Origin, uint32 Flags)
        {
            // AllocateShadowTiles guarantees the total view count fits in
            // GMaxCullViews before we get here, so no runtime clamp is needed.
            const uint32 ViewIndex = (uint32)CullViews.size();
            FFrustum Frustum = FFrustum::FromViewProjection(ViewProjection);

            FCullView View = {};
            for (int p = 0; p < 6; ++p)
            {
                View.FrustumPlanes[p] = Frustum.Planes[p];
            }
            // Reinterpret flag bits through the w channel; matches the
            // shader's asuint(ViewOriginAndFlags.w) unpack.
            float FlagsAsFloat;
            std::memcpy(&FlagsAsFloat, &Flags, sizeof(float));
            View.ViewOriginAndFlags = glm::vec4(Origin, FlagsAsFloat);
            View.DrawListOffset     = ViewIndex * TotalMeshletBound;
            View.DrawListCapacity   = TotalMeshletBound;
            View.IndirectArgsOffset = ViewIndex * NumDraws;
            View.NumDraws           = NumDraws;
            CullViews.push_back(View);

            // Seed this view's indirect slice. FirstInstance lands inside the
            // view's draw-list slice so the atomic-append in CullMeshlets can
            // never overflow into another view.
            const uint32 ViewDrawListBase = ViewIndex * TotalMeshletBound;
            for (uint32 d = 0; d < NumDraws; ++d)
            {
                FDrawIndirectArguments& Arg = IndirectArgs[ViewIndex * NumDraws + d];
                Arg.VertexCount           = MESHLET_VERTICES_PER_DRAW;
                Arg.InstanceCount         = 0u;
                Arg.StartVertexLocation   = 0u;
                Arg.StartInstanceLocation = ViewDrawListBase + DrawMeshletStartOffsets[d];
            }
        };

        // Pre-size IndirectArgs to exact view count. AllocateShadowTiles has
        // already guaranteed NumViews <= GMaxCullViews by dropping far shadow
        // requests, so no clamp is needed here. We also budget +1 view for
        // the camera-late phase (two-pass occlusion re-test); it's appended
        // last so existing CascadeViewBase / PointShadowCullViewBases /
        // SpotShadowCullViewBases indices stay valid.
        const uint32 NumViews =
            1u +                                                        // Camera (early)
            (LightData.bHasSun ? (uint32)NumCascades : 0u) +            // CSM cascades
            (uint32)PackedShadows[(uint32)ELightType::Point].size() * 6u +
            (uint32)PackedShadows[(uint32)ELightType::Spot].size() +
            1u;                                                         // Camera (late, phase 1)

        ASSERT(NumViews <= (uint32)GMaxCullViews);

        CullViews.reserve(NumViews);
        IndirectArgs.assign((size_t)NumViews * (size_t)NumDraws, FDrawIndirectArguments{});

        CascadeViewBase = ~0u;
        CameraLateViewIndex = ~0u;
        PointShadowCullViewBases.clear();
        PointShadowCullViewBases.reserve(PackedShadows[(uint32)ELightType::Point].size());
        SpotShadowCullViewBases.clear();
        SpotShadowCullViewBases.reserve(PackedShadows[(uint32)ELightType::Spot].size());

        // View 0: main camera. Frustum + cone + occlusion (Hi-Z + micro-poly).
        {
            const glm::mat4 CameraVP = ViewVolume.GetProjectionMatrix() * ViewVolume.GetViewMatrix();
            const uint32 CameraFlags =
                ECullViewFlags::Frustum |
                ECullViewFlags::Cone |
                ECullViewFlags::Occlusion;
            PushView(CameraVP, ViewVolume.GetViewPosition(), CameraFlags);
        }

        // CSM cascades. Sun-aligned cone + cast-shadow-only + distance (match
        // ShadowMaxDistance). Frustum still cheap and catches casters completely
        // outside the cascade volume.
        if (LightData.bHasSun)
        {
            const int32 SunShadowIndex = LightData.Lights[0].ShadowDataIndex;
            if (SunShadowIndex != INDEX_NONE)
            {
                const FLightShadowData& SunShadow = LightData.Shadows[SunShadowIndex];
                const uint32 CascadeFlags =
                    ECullViewFlags::Frustum |
                    ECullViewFlags::Cone |
                    ECullViewFlags::SunAligned |
                    ECullViewFlags::CastShadowOnly |
                    ECullViewFlags::Distance;

                CascadeViewBase = (uint32)CullViews.size();
                for (int32 c = 0; c < NumCascades; ++c)
                {
                    PushView(SunShadow.ViewProjection[c], ViewVolume.GetViewPosition(), CascadeFlags);
                }
            }
        }

        // Point lights: 6 views each (one per cube face). Cone cull uses
        // the light's world-space position as the apex. Parallel array
        // records each point shadow's face-0 view index for draw-pass lookup.
        for (const FLightShadow& PointShadow : PackedShadows[(uint32)ELightType::Point])
        {
            if (PointShadow.ShadowDataIndex < 0)
            {
                PointShadowCullViewBases.push_back(~0u);
                continue;
            }

            const FLightShadowData& ShadowData = LightData.Shadows[PointShadow.ShadowDataIndex];
            const FLight& Light = LightData.Lights[PointShadow.LightIndex];
            const uint32 FaceFlags =
                ECullViewFlags::Frustum |
                ECullViewFlags::Cone |
                ECullViewFlags::CastShadowOnly;

            PointShadowCullViewBases.push_back((uint32)CullViews.size());
            for (int32 Face = 0; Face < 6; ++Face)
            {
                PushView(ShadowData.ViewProjection[Face], Light.Position, FaceFlags);
            }
        }

        // Spotlights: one view each.
        for (const FLightShadow& SpotShadow : PackedShadows[(uint32)ELightType::Spot])
        {
            if (SpotShadow.ShadowDataIndex < 0)
            {
                SpotShadowCullViewBases.push_back(~0u);
                continue;
            }

            const FLightShadowData& ShadowData = LightData.Shadows[SpotShadow.ShadowDataIndex];
            const FLight& Light = LightData.Lights[SpotShadow.LightIndex];
            const uint32 SpotFlags =
                ECullViewFlags::Frustum |
                ECullViewFlags::Cone |
                ECullViewFlags::CastShadowOnly;

            SpotShadowCullViewBases.push_back((uint32)CullViews.size());
            PushView(ShadowData.ViewProjection[0], Light.Position, SpotFlags);
        }

        // Camera-late view. Phase 1 (CullPassLate) re-tests the defer list
        // against the rebuilt Hi-Z pyramid and emits survivors into this
        // view's slice. PhaseLate flag tells CullMeshlets phase 0 to skip
        // it (phase 1 owns it) and the flag set excludes Frustum/Cone
        // because those already ran on the defer-list entries in phase 0.
        // Only the Occlusion flag's accompanying HZB test matters here.
        {
            const glm::mat4 CameraVP = ViewVolume.GetProjectionMatrix() * ViewVolume.GetViewMatrix();
            const uint32 CameraLateFlags =
                ECullViewFlags::Occlusion |
                ECullViewFlags::PhaseLate;

            CameraLateViewIndex = (uint32)CullViews.size();
            PushView(CameraVP, ViewVolume.GetViewPosition(), CameraLateFlags);
        }
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
        Light.ShadowDataIndex   = INDEX_NONE;
        LightData.SunDirection  = Light.Direction;

        // Directional CSM always allocates a shadow slot; the cascade VPs live
        // in FLightShadowData and are looked up by the PS/VS via ShadowDataIndex.
        uint32 ShadowSlot = ShadowDataCount.fetch_add(1, std::memory_order_acquire);
        FLightShadowData* CascadeShadowData = nullptr;
        if (ShadowSlot < (uint32)MAX_SHADOWS)
        {
            Light.ShadowDataIndex = (int32)ShadowSlot;
            CascadeShadowData     = &LightData.Shadows[ShadowSlot];
        }
        
        float CascadeSplitLambda  = World->GetDefaultWorldSettings().CascadeSplitLambda;
        
        constexpr float ShadowMinDistance   = 1.0f;

        const float ShadowFar  = glm::min(FarClip, World->GetDefaultWorldSettings().ShadowMaxDistance);
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
            // Quantize the radius to whole texels. Sub-texel jitter in the
            // radius would change TexelSize between frames, defeating the
            // snap below. Rounding up at texel granularity keeps the world-
            // space texel size constant across small camera motions.
            const float QuantStep = 1.0f / (float)GCSMResolution;
            Radius = std::ceil(Radius / QuantStep) * QuantStep;
            const float TexelSize = (Radius * 2.0f) / (float)GCSMResolution;

            // BackDistance pushes the light eye behind the cascade volume so
            // off-screen occluders (e.g. things directly above the cascade)
            // still write into the depth texture.
            constexpr float BackDistance = 200.0f;
            const float     OrthoRange   = Radius * 2.0f + BackDistance;

            // Build a *fixed-orientation* reference view. Critically, the
            // lookAt target is the world origin, not SphereCenter — that
            // makes the rotation depend only on LightDir, so projecting any
            // world point through this view gives a stable XY that we can
            // round to the texel grid. Building lookAt with target =
            // SphereCenter (as the previous code did) trivially projects
            // SphereCenter to (0, 0), making the snap a no-op and producing
            // the per-frame shadow crawl.
            const glm::mat4 LightRotation = glm::lookAt(
                LightDir * (Radius + BackDistance),
                glm::vec3(0.0f),
                FViewVolume::UpAxis);

            // Snap the sphere center to the nearest whole texel in light
            // space. As the camera moves continuously, SnappedCenter moves
            // in discrete one-texel jumps perpendicular to LightDir, so
            // every world point projects to the same shadow texel until the
            // grid actually advances by a step.
            glm::vec4 CenterLS = LightRotation * glm::vec4(SphereCenter, 1.0f);
            CenterLS.x = std::round(CenterLS.x / TexelSize) * TexelSize;
            CenterLS.y = std::round(CenterLS.y / TexelSize) * TexelSize;
            const glm::vec3 SnappedCenter = glm::vec3(glm::inverse(LightRotation) * CenterLS);

            const glm::mat4 LightView = glm::lookAt(
                SnappedCenter + LightDir * (Radius + BackDistance),
                SnappedCenter,
                FViewVolume::UpAxis);

            // Standard-Z [0,1] LH ortho. Near plane is at the eye (0), far at the
            // far face of the slab (OrthoRange). The full sphere fits inside.
            const glm::mat4 LightProjection = glm::ortho(
                -Radius, +Radius,
                -Radius, +Radius,
                0.0f, OrthoRange);
        
            const glm::mat4 CascadeVP = LightProjection * LightView;
            if (CascadeShadowData)
            {
                CascadeShadowData->ViewProjection[i] = CascadeVP;
            }

            // Expose this cascade's world-space half-extent so the lit pixel
            // shader can convert a shadow texel into a world-space length for
            // normal-offset bias.
            LightData.CascadeRadii[i] = Radius;

            // Feed this cascade's frustum to the shadow cull pass so small casters
            // that only touch cascade 0 don't pay VPC cost on cascades 1/2.
            SceneGlobalData.CullData.CascadeFrustum[i] = FFrustum::FromViewProjection(CascadeVP);

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
                    LineBatches.emplace_back(CurrentBatch);

                    CurrentBatch.StartVertex = (uint32)SimpleVertices.size() - (uint32)AliveLinesWithVertices.size() * 2 + (uint32)(i * 2);
                    CurrentBatch.VertexCount = 2;
                    CurrentBatch.Thickness = LineData.Line.Thickness;
                    CurrentBatch.bDepthTest = LineData.Line.bDepthTest;
                }
            }

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

    void FForwardRenderScene::ResetPass(ICommandList& CmdList)
    {
        SimpleVertices.clear();
        DrawCommands.clear();
        OpaqueDrawList.clear();
        OpaqueOccluderDrawList.clear();
        TranslucentDrawList.clear();
        DrawMeshletStartOffsets.clear();
        IndirectArgs.clear();
        CullViews.clear();
        MaxMeshletsPerInstance = 0;
        TotalMeshletBound = 0;
        NumDrawsPerView = 0;
        CameraLateViewIndex = ~0u;
        Instances.clear();
        LightData = {};
        ShadowDataCount.store(0, std::memory_order_release);
        ShadowAtlas.FreeTiles();
        ShadowRequests.clear();
        BonesData.clear();
        BillboardInstances.clear();
        RenderStats = {};

        for (int i = 0; i < (int)ELightType::Num; ++i)
        {
            PackedShadows[i].clear();
        }
        
        if (DrawCommands.empty())
        {
           CmdList.ClearImageUInt(GetNamedImage(ENamedImage::DepthAttachment), AllSubresources, 0); 
        }
    }


    struct FCullMeshletPushConstants
    {
        uint32 NumViews;
        uint32 Phase;
        uint32 CameraLateViewIndex;
        uint32 _Pad;
    };

    void FForwardRenderScene::CullPassEarly(ICommandList& CmdList)
    {
        if (DrawCommands.empty() || CullViews.empty() || MaxMeshletsPerInstance == 0)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Cull Pass (Early)", tracy::Color::Pink2);

        const uint32 Num = (uint32)Instances.size();
        if (Num == 0)
        {
            return;
        }


        CmdList.FillBuffer(GetNamedBuffer(ENamedBuffer::DeferCount), 0u);
        
        FRHIComputeShaderRef CullShader = FShaderLibrary::GetComputeShader("CullMeshlets.slang");

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.SetComputeShader(CullShader);
        PipelineDesc.AddBindingLayout(SceneBindingLayout);
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(SceneBindingSet);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        CmdList.SetComputeState(State);

        FCullMeshletPushConstants PC = {};
        PC.NumViews            = (uint32)CullViews.size();
        PC.Phase               = (uint32)ECullPhase::Early;
        PC.CameraLateViewIndex = CameraLateViewIndex;
        CmdList.SetPushConstants(&PC, sizeof(PC));

        const uint32 MeshletGroupsX = (MaxMeshletsPerInstance + 63) / 64;
        // Vulkan caps groupCountY at 65535, so fold instance overflow into Z.
        // Shader reconstructs InstanceID = GroupID.y + GroupID.z * 65535.
        constexpr uint32 MaxDispatchY = 65535;
        const uint32 DispatchY = Num < MaxDispatchY ? Num : MaxDispatchY;
        const uint32 DispatchZ = (Num + MaxDispatchY - 1) / MaxDispatchY;
        CmdList.Dispatch(MeshletGroupsX, DispatchY, DispatchZ);
    }

    void FForwardRenderScene::CullPassLate(ICommandList& CmdList)
    {
        if (DrawCommands.empty() || CullViews.empty() || TotalMeshletBound == 0)
        {
            return;
        }

        if (CameraLateViewIndex == ~0u)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Cull Pass (Late)", tracy::Color::Pink3);

        // Dispatch covers the worst case: every camera meshlet was deferred.
        // Threads past uDeferCount (a GPU value) early-out; we don't have an
        // indirect-dispatch readback here because it would cost a round-trip
        // and the dispatch is tiny anyway at typical occlusion rates.
        FRHIComputeShaderRef CullShader = FShaderLibrary::GetComputeShader("CullMeshlets.slang");

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.SetComputeShader(CullShader);
        PipelineDesc.AddBindingLayout(SceneBindingLayout);
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(SceneBindingSet);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        CmdList.SetComputeState(State);

        FCullMeshletPushConstants PC = {};
        PC.NumViews            = (uint32)CullViews.size();
        PC.Phase               = (uint32)ECullPhase::Late;
        PC.CameraLateViewIndex = CameraLateViewIndex;
        CmdList.SetPushConstants(&PC, sizeof(PC));

        const uint32 MeshletGroupsX = (TotalMeshletBound + 63) / 64;
        CmdList.Dispatch(MeshletGroupsX, 1, 1);
    }


    static void RecordDepthPrePassSlice(
        ICommandList& CmdList,
        const TVector<FMeshDrawCommand>& DrawCommands,
        const TVector<uint32>& OpaqueOccluderDrawList,
        FRHIImage* DepthImage,
        FRHIImage* SizedToImage,
        FRHIBindingLayout* SceneBindingLayout,
        FRHIBindingSet* SceneBindingSet,
        const FViewportState& SceneViewportState,
        FRHIBuffer* IndirectArgsBuffer,
        uint32 ViewIndex,
        uint32 NumDrawsPerView,
        bool bClearDepth)
    {
        FRenderPassDesc::FAttachment Depth; Depth
            .SetImage(DepthImage)
            .SetLoadOp(bClearDepth ? ERenderLoadOp::Clear : ERenderLoadOp::Load)
            .SetDepthClearValue(0.0f);

        FRenderPassDesc RenderPass; RenderPass
            .SetDepthAttachment(Depth)
            .SetRenderArea(SizedToImage->GetExtent());

        FRenderState RenderState; RenderState
            .SetDepthStencilState(FDepthStencilState().SetDepthFunc(EComparisonFunc::Greater))
            .SetRasterState(FRasterState().EnableDepthClip());

        FRHIVertexShaderRef DepthOnlyVertexShader = FShaderLibrary::GetVertexShader("DepthPrePass.slang");

        const uint32 ViewBase = ViewIndex * NumDrawsPerView;

        for (uint32 Idx : OpaqueOccluderDrawList)
        {
            const FMeshDrawCommand& Batch = DrawCommands[Idx];

            FGraphicsPipelineDesc Desc; Desc
                .SetRenderState(RenderState)
                .AddBindingLayout(SceneBindingLayout)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

            if (Batch.bMasked)
            {
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
            GraphicsState.SetIndirectParams(IndirectArgsBuffer);

            CmdList.SetGraphicsState(GraphicsState);
            CmdList.DrawIndirect(Batch.DrawCount, (ViewBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
        }
    }

    void FForwardRenderScene::DepthPrePassEarly(ICommandList& CmdList)
    {
        if (OpaqueOccluderDrawList.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Pre-Depth (Early)", tracy::Color::Orange);
        
        RecordDepthPrePassSlice(
            CmdList,
            DrawCommands,
            OpaqueOccluderDrawList,
            GetNamedImage(ENamedImage::DepthAttachment),
            GetNamedImage(ENamedImage::HDR),
            SceneBindingLayout,
            SceneBindingSet,
            SceneViewportState,
            GetNamedBuffer(ENamedBuffer::IndirectArgs),
            0u,
            NumDrawsPerView,
            true);
    }

    void FForwardRenderScene::DepthPrePassLate(ICommandList& CmdList)
    {
        if (OpaqueOccluderDrawList.empty() || CameraLateViewIndex == ~0u)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Pre-Depth (Late)", tracy::Color::Orange2);
        
        RecordDepthPrePassSlice(
            CmdList,
            DrawCommands,
            OpaqueOccluderDrawList,
            GetNamedImage(ENamedImage::DepthAttachment),
            GetNamedImage(ENamedImage::HDR),
            SceneBindingLayout,
            SceneBindingSet,
            SceneViewportState,
            GetNamedBuffer(ENamedBuffer::IndirectArgs),
            CameraLateViewIndex,
            NumDrawsPerView,
            false);
    }

    void FForwardRenderScene::DepthPyramidPass(ICommandList& CmdList)
    {
        if (DrawCommands.empty())
        {
            return;
        }
        
        LUMINA_PROFILE_SECTION_COLORED("Depth Pyramid Pass (SPD)", tracy::Color::Orange);

        FRHIImage* DepthPyramid = GetNamedImage(ENamedImage::DepthPyramid);
        FRHIImage* DepthSource  = GetNamedImage(ENamedImage::DepthAttachment);
        FRHIBuffer* SpdCounter  = GetNamedBuffer(ENamedBuffer::SpdCounter);

        const uint32 PyramidW = DepthPyramid->GetSizeX();
        const uint32 PyramidH = DepthPyramid->GetSizeY();
        const uint32 MipCount = (uint32)DepthPyramid->GetDescription().NumMips;

        constexpr uint32 SpdMaxMips = 12;
        const uint32 NumMips = std::min(MipCount, SpdMaxMips);

        CmdList.FillBuffer(SpdCounter, 0u);

        FBindingLayoutDesc LayoutDesc;
        LayoutDesc.AddItem(FBindingLayoutItem::Texture_SRV(0));
        for (uint32 i = 0; i < SpdMaxMips; ++i)
        {
            LayoutDesc.AddItem(FBindingLayoutItem::Texture_UAV(1 + i));
        }
        LayoutDesc.AddItem(FBindingLayoutItem::Buffer_UAV(13));
        LayoutDesc.SetVisibility(ERHIShaderType::Compute);
        FRHIBindingLayout* Layout = BindingCache.GetOrCreateBindingLayout(LayoutDesc);

        FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("DepthPyramidSPD.slang");
        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(Layout);
        PipelineDesc.CS = ComputeShader;
        PipelineDesc.DebugName = "Depth Pyramid SPD";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);


        FRHISamplerRef MinSampler = TStaticRHISampler<true, false, AM_Clamp, AM_Clamp, AM_Clamp, ESamplerReductionType::Minimum>::GetRHI();

        FBindingSetDesc SetDesc;
        SetDesc.AddItem(FBindingSetItem::TextureSRV(0, DepthSource, MinSampler));

        for (uint32 i = 0; i < SpdMaxMips; ++i)
        {
            const uint32 SrcMip = (i < MipCount) ? i : 0u;
            SetDesc.AddItem(FBindingSetItem::TextureUAV(1 + i, DepthPyramid, DepthPyramid->GetFormat(),
                FTextureSubresourceSet(SrcMip, 1, 0, 1)));
        }

        SetDesc.AddItem(FBindingSetItem::BufferUAV(13, SpdCounter));

        FRHIBindingSet* Set = BindingCache.GetOrCreateBindingSet(SetDesc, Layout);

        FComputeState State;
        State.AddBindingSet(Set);
        State.SetPipeline(Pipeline);
        CmdList.SetComputeState(State);

        struct FSpdPushConstants
        {
            uint32 PyramidSize[2];
            uint32 NumMips;
            uint32 NumWorkGroups;
            float  InvPyramidSize[2];
        } PC = {};

        constexpr uint32 SpdTileSize = 32;
        const uint32 DispatchX = RenderUtils::GetGroupCount(PyramidW, SpdTileSize);
        const uint32 DispatchY = RenderUtils::GetGroupCount(PyramidH, SpdTileSize);
        const uint32 TotalGroups = DispatchX * DispatchY;

        PC.PyramidSize[0]     = PyramidW;
        PC.PyramidSize[1]     = PyramidH;
        PC.NumMips            = NumMips;
        PC.NumWorkGroups      = TotalGroups;
        PC.InvPyramidSize[0]  = 1.0f / (float)PyramidW;
        PC.InvPyramidSize[1]  = 1.0f / (float)PyramidH;
        CmdList.SetPushConstants(&PC, sizeof(PC));

        CmdList.Dispatch(DispatchX, DispatchY, 1);
    }

    void FForwardRenderScene::ClusterBuildPass(ICommandList& CmdList)
    {
		if (LightData.NumLights == 0 || DrawCommands.empty())
        {
            return;
        }

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
            
    }

    void FForwardRenderScene::LightCullPass(ICommandList& CmdList)
    {
        if (LightData.NumLights == 0)
        {
            return;
        }

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

        // LightCull.slang uses LOCAL_SIZE=128 and each invocation processes
        // one cluster, so group count = ceil(NumClusters / 128).
        constexpr uint32 LightCullGroupSize = 128;
        constexpr uint32 LightCullGroups    = (NumClusters + LightCullGroupSize - 1) / LightCullGroupSize;
        CmdList.Dispatch(LightCullGroups, 1, 1);
            
    }

    void FForwardRenderScene::PointShadowPass(ICommandList& CmdList)
    {
        if (PackedShadows[(uint32)ELightType::Point].empty() || DrawCommands.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Point Light Shadow Pass", tracy::Color::DeepPink2);

        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("ShadowMappingPixel.slang");

        // Bias tuned for the NDC-z atlas (near/far ~ radius*0.01 / radius).
        // CSM uses larger values since its depth spreads uniformly; here the
        // z range is compressed near 1.0, so slope-scale must be gentle or
        // it pushes occluder depth past the receiver.
        FRenderState RenderState; RenderState
                .SetDepthStencilState(FDepthStencilState()
                .SetDepthFunc(EComparisonFunc::Less))
                .SetRasterState(FRasterState()
                    .SetSlopeScaleDepthBias(1.5f)
                    .SetDepthBias(1)
                    .SetCullBack());

        // Per-face render pass: one clear per atlas layer, all point lights
        // draw into that layer through their own per-tile viewport/scissor.
        // The old per-light-per-face structure had clear loadop wiping the
        // full layer every light, leaving only the last light's tile intact.
        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("ShadowMappingVert.slang");

        const TVector<FLightShadow>& PointShadows = PackedShadows[(uint32)ELightType::Point];

        for (int32 Face = 0; Face < 6; ++Face)
        {
            LUMINA_PROFILE_SECTION_COLORED("Point Shadow Face", tracy::Color::DeepPink2);

            FRenderPassDesc::FAttachment Depth; Depth
                .SetLoadOp(ERenderLoadOp::Clear)
                .SetDepthClearValue(1.0)
                .SetImage(ShadowAtlas.GetImage())
                    .SetArraySlice((uint16)Face);

            FRenderPassDesc RenderPass; RenderPass
                .SetDepthAttachment(Depth)
                .SetRenderArea(glm::uvec2(GShadowAtlasResolution, GShadowAtlasResolution));

            FGraphicsPipelineDesc Desc; Desc
                .SetDebugName("Point Light Shadow Pass")
                .SetRenderState(RenderState)
                .AddBindingLayout(SceneBindingLayout)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                .SetVertexShader(VertexShader)
                .SetPixelShader(PixelShader);

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            bool bPassBegun = false;

            for (uint32 LightIdx = 0; LightIdx < PointShadows.size(); ++LightIdx)
            {
                const FLightShadow& LightShadow = PointShadows[LightIdx];
                const uint32 ViewBase = PointShadowCullViewBases[LightIdx];
                if (ViewBase == ~0u)
                {
                    continue;
                }

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

                FRect Scissor
                (
                    (int)TilePixelX,
                    (int)TilePixelX + TileSize,
                    (int)TilePixelY,
                    (int)TilePixelY + TileSize
                );

                FGraphicsState GraphicsState; GraphicsState
                    .SetRenderPass(RenderPass)
                    .SetViewportState(FViewportState(Viewport, Scissor))
                    .SetPipeline(Pipeline)
                    .AddBindingSet(SceneBindingSet)
                    .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                    .SetIndirectParams(GetNamedBuffer(ENamedBuffer::IndirectArgs));

                // ShadowMappingVert push = { int ShadowDataIndex; int ViewIndex; }.
                // ViewIndex indexes ShadowData.ViewProjection[]; here the cube face.
                struct { int32 ShadowDataIndex; int32 ViewIndex; } PointPush;
                PointPush.ShadowDataIndex = LightShadow.ShadowDataIndex;
                PointPush.ViewIndex       = Face;

                const uint32 FaceViewIndex = ViewBase + (uint32)Face;
                const uint32 FaceBase      = FaceViewIndex * NumDrawsPerView;

                for (uint32 OpaqueIdx : OpaqueDrawList)
                {
                    const FMeshDrawCommand& Batch = DrawCommands[OpaqueIdx];
                    CmdList.SetGraphicsState(GraphicsState);
                    CmdList.SetPushConstants(&PointPush, sizeof(PointPush));
                    CmdList.DrawIndirect(Batch.DrawCount, (FaceBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
                    bPassBegun = true;
                }
            }

            // The face may have no lights (all dropped or pre-fit empty);
            // still clear the layer so stale depth doesn't leak during sampling.
            if (!bPassBegun)
            {
                CmdList.BeginRenderPass(RenderPass);
            }

            CmdList.EndRenderPass();
        }
    }

    void FForwardRenderScene::SpotShadowPass(ICommandList& CmdList)
    {
        if (PackedShadows[(uint32)ELightType::Spot].empty() || DrawCommands.empty())
        {
            return;
        }
        
        LUMINA_PROFILE_SECTION_COLORED("Spot Shadow Pass", tracy::Color::DeepPink4);
        
        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("ShadowMappingPixel.slang");
        
        // See PointShadowPass for why these bias values are lower than the CSM pass.
        FRenderState RenderState; RenderState
            .SetDepthStencilState(FDepthStencilState()
                .SetDepthFunc(EComparisonFunc::Less))
                .SetRasterState(FRasterState()
                    .SetSlopeScaleDepthBias(1.5f)
                    .SetDepthBias(1)
                    .SetCullBack());


        // Render pass + pipeline are built ONCE outside the per-light loop.
        // Building them inside would re-clear the atlas layer between lights,
        // wiping every spot's shadow except the last.
        FRenderPassDesc::FAttachment Depth; Depth
            .SetLoadOp(ERenderLoadOp::Clear)
            .SetDepthClearValue(1.0f)
            .SetImage(ShadowAtlas.GetImage())
                .SetArraySlice(6);

        FRenderPassDesc RenderPass; RenderPass
            .SetDepthAttachment(Depth)
            .SetRenderArea(glm::uvec2(GShadowAtlasResolution, GShadowAtlasResolution));

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("ShadowMappingVert.slang");

        FGraphicsPipelineDesc PipelineDesc; PipelineDesc
            .SetDebugName("Spot Shadow Pass")
            .SetRenderState(RenderState)
            .AddBindingLayout(SceneBindingLayout)
            .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
            .SetVertexShader(VertexShader)
            .SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(PipelineDesc, RenderPass);

        FGraphicsState GraphicsState; GraphicsState
            .SetRenderPass(Move(RenderPass))
            .SetPipeline(Pipeline)
            .AddBindingSet(SceneBindingSet)
            .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
            .SetIndirectParams(GetNamedBuffer(ENamedBuffer::IndirectArgs));

        const TVector<FLightShadow>& SpotShadows = PackedShadows[(uint32)ELightType::Spot];

        for (uint32 SpotIdx = 0; SpotIdx < SpotShadows.size(); ++SpotIdx)
        {
            const FLightShadow& Shadow  = SpotShadows[SpotIdx];
            const uint32 ViewIndex      = SpotShadowCullViewBases[SpotIdx];
            if (ViewIndex == ~0u)
            {
                continue;
            }

            LUMINA_PROFILE_SECTION_COLORED("Process Spot Light", tracy::Color::DeepPink);

            const FShadowTile& Tile = ShadowAtlas.GetTile(Shadow.ShadowMapIndex);
            uint32 TilePixelX = static_cast<uint32>(Tile.UVOffset.x * GShadowAtlasResolution);
            uint32 TilePixelY = static_cast<uint32>(Tile.UVOffset.y * GShadowAtlasResolution);
            uint32 TileSize   = static_cast<uint32>(Tile.UVScale.x * GShadowAtlasResolution);

            FViewport Viewport
            (
                (float)TilePixelX,
                (float)TilePixelX + TileSize,
                (float)TilePixelY,
                (float)TilePixelY + TileSize,
                0.0f,
                1.0f
            );

            FRect Scissor
            (
                (int)TilePixelX,
                (int)TilePixelX + TileSize,
                (int)TilePixelY,
                (int)TilePixelY + TileSize
            );

            GraphicsState.SetViewportState(FViewportState(Viewport, Scissor));

            // ShadowMappingVert push = { int ShadowDataIndex; int ViewIndex; }.
            // Spot lights only use ViewProjection[0], so ViewIndex is 0.
            struct { int32 ShadowDataIndex; int32 ViewIndex; } SpotPush;
            SpotPush.ShadowDataIndex = Shadow.ShadowDataIndex;
            SpotPush.ViewIndex       = 0;

            const uint32 ViewBase = ViewIndex * NumDrawsPerView;
            for (uint32 OpaqueIdx : OpaqueDrawList)
            {
                const FMeshDrawCommand& Batch = DrawCommands[OpaqueIdx];

                CmdList.SetGraphicsState(GraphicsState);
                CmdList.SetPushConstants(&SpotPush, sizeof(SpotPush));
                CmdList.DrawIndirect(Batch.DrawCount, (ViewBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
            }
        }

        CmdList.EndRenderPass();
    }

    void FForwardRenderScene::CascadedShowPass(ICommandList& CmdList)
    {
        if (!LightData.bHasSun || DrawCommands.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Cascaded Shadow Map Pass", tracy::Color::DeepPink2);

        FRenderState RenderState; RenderState
            .SetDepthStencilState(FDepthStencilState()
                .SetDepthFunc(EComparisonFunc::Less))
                .SetRasterState(FRasterState()
                    .SetCullBack()
                    .SetDepthBias(25)
                    .SetSlopeScaleDepthBias(0.75f));

        FString CSMDefine = "SHADOW_CSM";
        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("ShadowMappingVert.slang", TSpan<FString>(&CSMDefine, 1));

        FGraphicsPipelineDesc Desc; Desc
            .SetDebugName("Cascaded Shadow Maps")
            .SetRenderState(RenderState)
            .AddBindingLayout(SceneBindingLayout)
            .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
            .SetVertexShader(VertexShader);

        // Each cascade maps to its own cull view; BuildCullViews recorded the
        // base index. Bail early if the sun didn't get a shadow data slot (rare:
        // happens when MaxShadows was exceeded).
        if (CascadeViewBase == ~0u)
        {
            return;
        }

        for (uint32 c = 0; c < (uint32)NumCascades; ++c)
        {
            FRenderPassDesc::FAttachment Depth; Depth
                .SetLoadOp(ERenderLoadOp::Clear)
                .SetDepthClearValue(1.0f)
                .SetImage(GetNamedImage(ENamedImage::Cascade))
                    .SetArraySlice((uint16)c);

            FRenderPassDesc RenderPass; RenderPass
                .SetDepthAttachment(Depth)
                .SetRenderArea(glm::uvec2(GCSMResolution, GCSMResolution));

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            // Meshlet-driven: one indirect "instance" per surviving meshlet,
            // indirects sourced from this cascade's slice of the unified
            // IndirectArgs buffer.
            FGraphicsState GraphicsState; GraphicsState
                .SetRenderPass(RenderPass)
                .SetViewportState(MakeViewportStateFromImage(GetNamedImage(ENamedImage::Cascade)))
                .SetPipeline(Pipeline)
                .AddBindingSet(SceneBindingSet)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                .SetIndirectParams(GetNamedBuffer(ENamedBuffer::IndirectArgs));

            CmdList.SetGraphicsState(GraphicsState);

            struct { int32 ShadowDataIndex; int32 CascadeIndex; } CascadePush;
            CascadePush.ShadowDataIndex = LightData.Lights[0].ShadowDataIndex;
            CascadePush.CascadeIndex    = (int32)c;
            CmdList.SetPushConstants(&CascadePush, sizeof(CascadePush));

            const uint32 ViewIndex = CascadeViewBase + c;
            const uint32 ViewBase  = ViewIndex * NumDrawsPerView;
            for (uint32 OpaqueIdx : OpaqueDrawList)
            {
                const FMeshDrawCommand& Batch = DrawCommands[OpaqueIdx];
                CmdList.DrawIndirect(Batch.DrawCount, (ViewBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
            }
        }
    }

    void FForwardRenderScene::BasePass(ICommandList& CmdList)
    {
        if (DrawCommands.empty())
        {
            return;
        }
        
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
    
        // Pre-pass only laid down depth for the best occluders, so the
        // base pass runs GREATER_EQUAL and writes depth for everything
        // else. Occluder pixels hit the equality branch and re-write
        // their already-correct depth (no-op); non-occluder fragments
        // that survive against the occluder silhouette write their real
        // depth so the post-pass pyramid sees the full opaque scene.
        FDepthStencilState DepthState; DepthState
            .SetDepthFunc(EComparisonFunc::GreaterOrEqual)
            .EnableDepthWrite();
        
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

            // The camera's visible meshlets are split across two slices:
            //   * view 0              : passed HZB against last-frame pyramid (early phase)
            //   * CameraLateViewIndex : failed early HZB but passed rebuilt HZB (late phase)
            // Both slices feed the same pipeline / same shaders; only the
            // IndirectArgs offset differs. GREATER_EQUAL still lets non-
            // occluder fragments write real depth against the pre-pass
            // occluder-populated depth buffer.
            FGraphicsState GraphicsState; GraphicsState
                .SetRenderPass(RenderPass)
                .SetViewportState(SceneViewportState)
                .SetPipeline(GRenderContext->CreateGraphicsPipeline(Desc, RenderPass))
                .SetIndirectParams(GetNamedBuffer(ENamedBuffer::IndirectArgs))
                .AddBindingSet(SceneBindingSet)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());

            CmdList.SetGraphicsState(GraphicsState);

            // View 0 = camera-early.
            CmdList.DrawIndirect(Batch.DrawCount, Batch.IndirectDrawOffset * sizeof(FDrawIndirectArguments));

            // Camera-late. Empty slice when no meshlets were deferred or all
            // deferred meshlets were genuinely occluded; the per-draw
            // InstanceCount reads 0 and GPU short-circuits with no perf hit.
            if (CameraLateViewIndex != ~0u)
            {
                const uint32 LateBase = CameraLateViewIndex * NumDrawsPerView;
                CmdList.DrawIndirect(Batch.DrawCount, (LateBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
            }
        }
    }

    void FForwardRenderScene::ParticleSimulatePass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("Particle Simulate", tracy::Color::Orange);

        const float DeltaTime = (float)World->GetWorldDeltaTime();

        FEntityRegistry& Registry = World->GetEntityRegistry();

        auto ParticleView = Registry.view<SParticleSystemComponent, STransformComponent>(entt::exclude<SDisabledTag>);
        ParticleView.each([&](entt::entity Entity, SParticleSystemComponent& Component, const STransformComponent& Transform)
        {
            CParticleSystem* PS = Component.ParticleSystem.Get();
            if (!PS || !PS->IsReadyForSimulation())
            {
                return;
            }

            const uint32 MaxParticles = (uint32)PS->MaxParticles;
            if (MaxParticles == 0)
            {
                return;
            }

            FParticleGPUState& State = Component.GPUState;

            const bool bNeedsAlloc = (!State.ParticleBuffer) || (State.AllocatedMax != MaxParticles);
            if (bNeedsAlloc)
            {
                const FString AssetName = PS->GetName().ToString();

                FRHIBufferDesc ParticleDesc;
                ParticleDesc.Size       = (uint64)MaxParticles * 64ull;
                ParticleDesc.Stride     = 64u;
                ParticleDesc.DebugName  = AssetName + "_Particles";
                ParticleDesc.bKeepInitialState = true;
                ParticleDesc.InitialState = EResourceStates::UnorderedAccess;
                ParticleDesc.Usage.SetFlag(BUF_StorageBuffer);
                State.ParticleBuffer = GRenderContext->CreateBuffer(ParticleDesc);

                FRHIBufferDesc SimParamsDesc;
                SimParamsDesc.Size      = sizeof(FParticleSimParamsGPU);
                SimParamsDesc.DebugName = AssetName + "_SimParams";
                SimParamsDesc.bKeepInitialState = true;
                SimParamsDesc.InitialState = EResourceStates::UnorderedAccess;
                SimParamsDesc.Usage.SetFlag(BUF_UniformBuffer);
                State.SimParamsBuffer = GRenderContext->CreateBuffer(SimParamsDesc);

                FRHIBufferDesc RenderParamsDesc;
                RenderParamsDesc.Size       = sizeof(FParticleRenderParamsGPU);
                RenderParamsDesc.DebugName  = AssetName + "_RenderParams";
                RenderParamsDesc.Usage.SetFlag(BUF_UniformBuffer);
                RenderParamsDesc.bKeepInitialState = true;
                RenderParamsDesc.InitialState = EResourceStates::UnorderedAccess;
                State.RenderParamsBuffer = GRenderContext->CreateBuffer(RenderParamsDesc);

                FRHIBufferDesc SpawnCounterDesc;
                SpawnCounterDesc.Size       = sizeof(uint32);
                SpawnCounterDesc.Stride     = sizeof(uint32);
                SpawnCounterDesc.DebugName  = AssetName + "_SpawnCounter";
                SpawnCounterDesc.Usage.SetFlag(BUF_StorageBuffer);
                SpawnCounterDesc.bKeepInitialState = true;
                SpawnCounterDesc.InitialState = EResourceStates::UnorderedAccess;
                State.SpawnCounterBuffer = GRenderContext->CreateBuffer(SpawnCounterDesc);

                // Zero-fill the particle buffer so all entries start dead.
                CmdList.FillBuffer(State.ParticleBuffer, 0u);

                State.AllocatedMax      = MaxParticles;
                State.SpawnAccumulator  = 0.0f;
                State.SystemAge         = 0.0f;
                State.bBurstPending     = true;
            }

            // Zero the spawn counter every frame before dispatch. @TODO see if needed.
            CmdList.FillBuffer(State.SpawnCounterBuffer, 0u);
            
            if (State.bPendingReset)
            {
                CmdList.FillBuffer(State.ParticleBuffer, 0u);
                State.bPendingReset = false;
            }

            const float ScaledDelta = DeltaTime * Component.TimeScale;
            State.TotalTime += DeltaTime;
            State.SystemAge += ScaledDelta;

            const bool bDurationExpired = (PS->Duration > 0.0f) && (State.SystemAge >= PS->Duration);
            if (bDurationExpired)
            {
                if (PS->bLooping)
                {
                    State.SystemAge = fmodf(State.SystemAge, PS->Duration);
                    State.bBurstPending = true;
                }
            }

            const bool bEmitActive = Component.bEmit && !(bDurationExpired && !PS->bLooping);

            uint32 SpawnCount = 0;
            if (bEmitActive && PS->SpawnRate > 0.0f && Component.SpawnRateMultiplier > 0.0f)
            {
                State.SpawnAccumulator += DeltaTime * PS->SpawnRate * Component.SpawnRateMultiplier;
                SpawnCount = (uint32)State.SpawnAccumulator;
                State.SpawnAccumulator -= (float)SpawnCount;
            }
            else
            {
                State.SpawnAccumulator = 0.0f;
            }

            const bool bDoBurst = bEmitActive && Component.bBurstOnSpawn && State.bBurstPending && PS->BurstCount > 0;
            if (bDoBurst)
            {
                SpawnCount += (uint32)PS->BurstCount;
                State.bBurstPending = false;
            }
            else if (!Component.bBurstOnSpawn)
            {
                State.bBurstPending = false;
            }

            SpawnCount = eastl::min(SpawnCount, MaxParticles);

            const glm::mat4 WorldMat = Transform.GetWorldMatrix();
            const glm::vec3 EmitterWorld = glm::vec3(WorldMat * glm::vec4(Component.EmitterOffset, 1.0f));
            const glm::vec3 EmitterRight   = glm::normalize(glm::vec3(WorldMat[0]));
            const glm::vec3 EmitterUp      = glm::normalize(glm::vec3(WorldMat[1]));
            const glm::vec3 EmitterForward = glm::normalize(glm::vec3(WorldMat[2]));
            
            glm::vec3 EmitterVelocity(0.0f);
            if (State.bHasPrevPosition && DeltaTime > 0.0f)
            {
                EmitterVelocity = (EmitterWorld - State.PrevEmitterPosition) / DeltaTime;
            }
            State.PrevEmitterPosition = EmitterWorld;
            State.bHasPrevPosition    = true;

            const float InheritFactor = glm::clamp(PS->InheritEmitterVelocity, 0.0f, 1.0f);

            State.FrameSeed = (State.FrameSeed + 2654435761u) ^ (uint32)Entity;

            uint32 SimFlags = 0u;
            if (PS->bLooping)
            {
                SimFlags |= PARTICLE_SIM_FLAG_LOOP;
            }
            if (State.bBurstPending)
            {
                SimFlags |= PARTICLE_SIM_FLAG_BURST_PENDING;
            }

            FParticleSimParamsGPU SimParams{};
            // Pack EmitterVelocity.xyz into the w components of the basis vectors to
            // avoid growing the CBV layout. The shader reconstructs it from these slots.
            SimParams.EmitterPosition   = glm::vec4(EmitterWorld, 1.0f);
            SimParams.EmitterForward    = glm::vec4(EmitterForward, EmitterVelocity.x);
            SimParams.EmitterRight      = glm::vec4(EmitterRight,   EmitterVelocity.y);
            SimParams.EmitterUp         = glm::vec4(EmitterUp,      EmitterVelocity.z);
            SimParams.Counts            = glm::uvec4(MaxParticles, SpawnCount, State.FrameSeed, SimFlags);
            SimParams.Modes             = glm::uvec4((uint32)PS->Shape, (uint32)PS->VelocityMode, 0u, 0u);
            SimParams.ShapeSize         = glm::vec4(PS->ShapeSize, glm::radians(PS->ShapeAngle));
            SimParams.VelocityMin       = glm::vec4(PS->VelocityMin, 0.0f);
            SimParams.VelocityMax       = glm::vec4(PS->VelocityMax, 0.0f);
            SimParams.SpeedAndLifetime  = glm::vec4(PS->SpeedRange.x, PS->SpeedRange.y, PS->LifetimeRange.x, PS->LifetimeRange.y);
            SimParams.Gravity           = glm::vec4(PS->Gravity, PS->Drag);
            SimParams.StartColor        = PS->StartColor;
            SimParams.EndColor          = PS->EndColor;
            SimParams.SizeRange         = glm::vec4(PS->StartSizeRange.x, PS->StartSizeRange.y, PS->EndSizeRange.x, PS->EndSizeRange.y);
            SimParams.RotationRange     = glm::vec4(PS->RotationRange.x, PS->RotationRange.y, PS->RotationSpeedRange.x, PS->RotationSpeedRange.y);
            SimParams.NoiseStrength     = glm::vec4(PS->NoiseStrength, PS->NoiseScale);
            SimParams.NoiseParams       = glm::vec4(PS->NoiseSpeed, InheritFactor, 0.0f, 0.0f);
            SimParams.Timing            = glm::vec4(ScaledDelta, State.TotalTime, State.SystemAge, 0.0f);

            CmdList.WriteBuffer(State.SimParamsBuffer, &SimParams, sizeof(SimParams));

            FRHIComputeShaderRef ComputeShader;
            if (PS->UsesCustomShader())
            {
                ComputeShader = PS->GetCustomComputeShader();
            }
            else
            {
                ComputeShader = FShaderLibrary::GetComputeShader("ParticleSimulate.slang");
            }

            if (!ComputeShader)
            {
                return;
            }

            FBindingLayoutDesc LayoutDesc;
            LayoutDesc.SetBindingIndex(0)
                .SetVisibility(ERHIShaderType::Compute)
                .AddItem(FBindingLayoutItem::Buffer_UAV(0))
                .AddItem(FBindingLayoutItem::Buffer_CBV(1))
                .AddItem(FBindingLayoutItem::Buffer_UAV(2));
            FRHIBindingLayout* Layout = BindingCache.GetOrCreateBindingLayout(LayoutDesc);

            FBindingSetDesc SetDesc;
            SetDesc.AddItem(FBindingSetItem::BufferUAV(0, State.ParticleBuffer))
                   .AddItem(FBindingSetItem::BufferCBV(1, State.SimParamsBuffer))
                   .AddItem(FBindingSetItem::BufferUAV(2, State.SpawnCounterBuffer));
            FRHIBindingSetRef BindingSet = GRenderContext->CreateBindingSet(SetDesc, Layout);

            FComputePipelineDesc PipelineDesc;
            PipelineDesc.SetComputeShader(ComputeShader)
                        .AddBindingLayout(Layout);
            FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

            FComputeState ComputeState;
            ComputeState.SetPipeline(Pipeline)
                        .AddBindingSet(BindingSet);

            CmdList.SetComputeState(ComputeState);
            CmdList.Dispatch((MaxParticles + 63u) / 64u, 1, 1);
        });
    }

    static FBlendState::RenderTarget MakeParticleBlendTarget(EParticleBlendMode Mode)
    {
        FBlendState::RenderTarget RT;
        RT.SetBlendEnable(true);

        switch (Mode)
        {
        case EParticleBlendMode::Additive:
            RT.SetSrcBlend(EBlendFactor::SrcAlpha)
              .SetDestBlend(EBlendFactor::One)
              .SetBlendOp(EBlendOp::Add)
              .SetSrcBlendAlpha(EBlendFactor::One)
              .SetDestBlendAlpha(EBlendFactor::One)
              .SetBlendOpAlpha(EBlendOp::Add);
            break;

        case EParticleBlendMode::PreMultiplied:
            RT.SetSrcBlend(EBlendFactor::One)
              .SetDestBlend(EBlendFactor::InvSrcAlpha)
              .SetBlendOp(EBlendOp::Add)
              .SetSrcBlendAlpha(EBlendFactor::One)
              .SetDestBlendAlpha(EBlendFactor::InvSrcAlpha)
              .SetBlendOpAlpha(EBlendOp::Add);
            break;

        case EParticleBlendMode::Multiply:
            RT.SetSrcBlend(EBlendFactor::DstColor)
              .SetDestBlend(EBlendFactor::Zero)
              .SetBlendOp(EBlendOp::Add)
              .SetSrcBlendAlpha(EBlendFactor::One)
              .SetDestBlendAlpha(EBlendFactor::Zero)
              .SetBlendOpAlpha(EBlendOp::Add);
            break;

        case EParticleBlendMode::Alpha:
        default:
            RT.SetSrcBlend(EBlendFactor::SrcAlpha)
              .SetDestBlend(EBlendFactor::InvSrcAlpha)
              .SetBlendOp(EBlendOp::Add)
              .SetSrcBlendAlpha(EBlendFactor::One)
              .SetDestBlendAlpha(EBlendFactor::InvSrcAlpha)
              .SetBlendOpAlpha(EBlendOp::Add);
            break;
        }
        return RT;
    }

    void FForwardRenderScene::ParticleRenderPass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("Particle Render", tracy::Color::OrangeRed);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("ParticleVertex.slang");
        FRHIPixelShaderRef  PixelShader  = FShaderLibrary::GetPixelShader("ParticlePixel.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        // Only Load when an earlier pass actually wrote to these targets. In the
        // particle editor's preview world (no meshes, no environment) BasePass and
        // DepthPrePass both early-return, so the first pass to touch HDR/Depth has
        // to clear them itself, or it'll sample uninitialized memory.
        const bool bHDRWasWritten = !DrawCommands.empty() || RenderSettings.bHasEnvironment;

        FRenderPassDesc::FAttachment RenderTarget;
        RenderTarget.SetImage(GetNamedImage(ENamedImage::HDR));
        if (bHDRWasWritten)
        {
            RenderTarget.SetLoadOp(ERenderLoadOp::Load);
        }

        FRenderPassDesc::FAttachment Depth;
        Depth.SetImage(GetNamedImage(ENamedImage::DepthAttachment))
            .SetDepthClearValue(0.0f);
        if (!DrawCommands.empty())
        {
            Depth.SetLoadOp(ERenderLoadOp::Load);
        }

        FRenderPassDesc RenderPass;
        RenderPass.AddColorAttachment(RenderTarget)
            .SetDepthAttachment(Depth)
            .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());

        FRasterState RasterState;
        RasterState.EnableDepthClip()
                   .SetCullMode(ERasterCullMode::None);

        FBindingLayoutDesc ParticleLayoutDesc;
        ParticleLayoutDesc.SetBindingIndex(2)
            .SetVisibility(ERHIShaderType::Vertex)
            .AddItem(FBindingLayoutItem::Buffer_SRV(0))
            .AddItem(FBindingLayoutItem::Buffer_CBV(1));
        FRHIBindingLayout* ParticleLayout = BindingCache.GetOrCreateBindingLayout(ParticleLayoutDesc);

        FEntityRegistry& Registry = World->GetEntityRegistry();
        auto ParticleView = Registry.view<SParticleSystemComponent, STransformComponent>(entt::exclude<SDisabledTag>);

        ParticleView.each([&](SParticleSystemComponent& Component, const STransformComponent&)
        {
            CParticleSystem* PS = Component.ParticleSystem.Get();
            if (!PS || !PS->IsReadyForSimulation())
            {
                return;
            }

            FParticleGPUState& State = Component.GPUState;
            if (!State.ParticleBuffer || !State.RenderParamsBuffer)
            {
                return;
            }

            uint32 TextureIndex = 0u;
            if (CTexture* Tex = PS->Texture.Get())
            {
                if (FRHIImage* Image = Tex->GetRHIRef())
                {
                    const int32 CacheIdx = Image->GetTextureCacheIndex();
                    if (CacheIdx > 0)
                    {
                        TextureIndex = (uint32)CacheIdx;
                    }
                }
            }

            FParticleRenderParamsGPU RenderParams{};
            RenderParams.Flags      = glm::uvec4(TextureIndex, PS->bBillboardToCamera ? 1u : 0u, 0u, 0u);
            RenderParams.Tint       = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            RenderParams.UVParams   = glm::vec4(0.0f);
            CmdList.WriteBuffer(State.RenderParamsBuffer, &RenderParams, sizeof(RenderParams));

            FBlendState BlendState;
            BlendState.SetRenderTarget(0, MakeParticleBlendTarget(PS->BlendMode));

            FDepthStencilState DepthState;
            DepthState.SetDepthFunc(EComparisonFunc::GreaterOrEqual);
            if (PS->bWriteDepth)
            {
                DepthState.EnableDepthWrite();
            }
            else
            {
                DepthState.DisableDepthWrite();
            }

            FRenderState RenderState;
            RenderState.SetRasterState(RasterState)
                       .SetDepthStencilState(DepthState)
                       .SetBlendState(BlendState);

            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("Particle Render")
                .SetRenderState(RenderState)
                .SetVertexShader(VertexShader)
                .SetPixelShader(PixelShader)
                .AddBindingLayout(SceneBindingLayout)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                .AddBindingLayout(ParticleLayout);

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FBindingSetDesc ParticleSetDesc;
            ParticleSetDesc.AddItem(FBindingSetItem::BufferSRV(0, State.ParticleBuffer))
                           .AddItem(FBindingSetItem::BufferCBV(1, State.RenderParamsBuffer));
            FRHIBindingSetRef ParticleSet = GRenderContext->CreateBindingSet(ParticleSetDesc, ParticleLayout);

            FGraphicsState GraphicsState;
            GraphicsState.SetRenderPass(RenderPass)
                .SetViewportState(MakeViewportStateFromImage(GetNamedImage(ENamedImage::HDR)))
                .SetPipeline(Pipeline)
                .AddBindingSet(SceneBindingSet)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                .AddBindingSet(ParticleSet);

            CmdList.SetGraphicsState(GraphicsState);
            CmdList.Draw(6u * State.AllocatedMax, 1u, 0u, 0u);
        });
    }

    namespace
    {
        // Ensure the component's CPU backing stores match the declared dimensions.
        // Called lazily on the render thread so designers can tweak Resolution and
        // LayerCount in the inspector without rebooting.
        static void EnsureTerrainCpuBuffers(STerrainComponent& Terrain)
        {
            const size_t NeededHeights = size_t(Terrain.Resolution) * size_t(Terrain.Resolution);
            if (Terrain.Heightmap.size() != NeededHeights)
            {
                Terrain.Heightmap.assign(NeededHeights, 0.0f);
                Terrain.GPUState.bFullHeightmapDirty = true;
            }
            const size_t NeededWeights = size_t(Terrain.Layers.size()) * NeededHeights;
            if (Terrain.LayerWeights.size() != NeededWeights)
            {
                Terrain.LayerWeights.resize(NeededWeights, 0u);
                Terrain.GPUState.bFullWeightsDirty = true;
            }
        }

        static FRHIImageRef CreateTerrainImage(const FString& DebugName, uint32 Size, uint16 ArraySize, EFormat Format, bool bUav, bool bArrayView = false)
        {
            FRHIImageDesc Desc;
            Desc.Extent       = glm::uvec2(Size, Size);
            Desc.ArraySize    = ArraySize;
            // Shader samples weight maps as Sampler2DArray; Texture2D collapses array slices
            // in FTextureSubresourceSet::Resolve, breaking per-slice state tracking.
            Desc.Dimension    = (bArrayView || ArraySize > 1) ? EImageDimension::Texture2DArray : EImageDimension::Texture2D;
            Desc.Format       = Format;
            Desc.DebugName    = DebugName;
            Desc.bKeepInitialState = true;
            Desc.InitialState = EResourceStates::ShaderResource;
            Desc.Flags.SetFlag(EImageCreateFlags::ShaderResource);
            if (bUav)
            {
                Desc.Flags.SetFlag(EImageCreateFlags::Storage);
            }
            return GRenderContext->CreateImage(Desc);
        }
    }

    void FForwardRenderScene::TerrainUpdatePass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("Terrain Update", tracy::Color::SeaGreen);

        FRHIComputeShaderRef NormalShader = FShaderLibrary::GetComputeShader("TerrainNormalCompute.slang");

        FEntityRegistry& Registry = World->GetEntityRegistry();
        auto TerrainView = Registry.view<STerrainComponent, STransformComponent>(entt::exclude<SDisabledTag>);

        TerrainView.each([&](entt::entity Entity, STerrainComponent& Terrain, const STransformComponent&)
        {
            if (Terrain.Resolution < 2 || Terrain.ChunkResolution < 2)
            {
                return;
            }

            EnsureTerrainCpuBuffers(Terrain);

            FTerrainGPUState& State = Terrain.GPUState;
            const uint32 Res        = (uint32)Terrain.Resolution;
            const uint32 LayerCount = (uint32)std::max<size_t>(Terrain.Layers.size(), 1u);

            // (Re)allocate GPU textures if the CPU dimensions drifted.
            if (Terrain.NeedsReallocation())
            {
                const FString Name = FString("Terrain_") + std::to_string((uint32)Entity).c_str();
                State.HeightmapTexture   = CreateTerrainImage(Name + "_Height",  Res, 1u,          EFormat::R32_FLOAT, false);
                State.NormalTexture      = CreateTerrainImage(Name + "_Normal",  Res, 1u,          EFormat::RGBA8_UNORM, true);
                State.LayerWeightTexture = CreateTerrainImage(Name + "_Weights", Res, (uint16)std::max(LayerCount, 1u), EFormat::R8_UNORM, false, true);
                State.AllocatedResolution = Res;
                State.AllocatedLayerCount = LayerCount;
                State.bFullHeightmapDirty = true;
                State.bFullWeightsDirty   = true;

                // Vulkan does not zero new image memory. Unwritten slices
                // return undefined data (~0.5 historically, producing uniform
                // layer blends). Upload every slice from the CPU buffer so
                // no slice is ever undefined; also clears dirty flags to
                // skip redundant re-upload below.
                if (!Terrain.LayerWeights.empty())
                {
                    const size_t SlicePixels = size_t(Res) * size_t(Res);
                    for (uint32 L = 0; L < LayerCount && (size_t(L + 1) * SlicePixels) <= Terrain.LayerWeights.size(); ++L)
                    {
                        const uint8* Slice = Terrain.LayerWeights.data() + L * SlicePixels;
                        CmdList.WriteImage(State.LayerWeightTexture, L, 0u, Slice, Res, 0u);
                    }
                    State.bFullWeightsDirty    = false;
                    State.WeightDirtyLayerMask = 0u;
                }
                else
                {
                    // No CPU data yet: zero every slice so a sampler can't
                    // read garbage; next paint/sculpt tick populates.
                    CmdList.ClearImageFloat(
                        State.LayerWeightTexture,
                        FTextureSubresourceSet(0u, 1u, 0u, (uint16)std::max(LayerCount, 1u)),
                        FColor(0.0f, 0.0f, 0.0f, 0.0f));
                }
            }

            bool bHeightDirty = false;
            if (State.bFullHeightmapDirty)
            {
                CmdList.WriteImage(State.HeightmapTexture, 0u, 0u, Terrain.Heightmap.data(), Res * (uint32)sizeof(float), 0u);
                State.bFullHeightmapDirty = false;
                bHeightDirty = true;
            }
            else if (State.HeightDirtyMax.x >= State.HeightDirtyMin.x)
            {
                CmdList.WriteImage(State.HeightmapTexture, 0u, 0u, Terrain.Heightmap.data(), Res * (uint32)sizeof(float), 0u);
                bHeightDirty = true;
            }

            if (State.bFullWeightsDirty && !Terrain.LayerWeights.empty())
            {
                const size_t SlicePixels = size_t(Res) * size_t(Res);
                for (uint32 L = 0; L < LayerCount && (size_t(L + 1) * SlicePixels) <= Terrain.LayerWeights.size(); ++L)
                {
                    const uint8* Slice = Terrain.LayerWeights.data() + L * SlicePixels;
                    CmdList.WriteImage(State.LayerWeightTexture, L, 0u, Slice, Res, 0u);
                }
                State.bFullWeightsDirty   = false;
                State.WeightDirtyLayerMask = 0u;
            }
            else if (State.WeightDirtyLayerMask != 0u && !Terrain.LayerWeights.empty())
            {
                const size_t SlicePixels = size_t(Res) * size_t(Res);
                for (uint32 L = 0; L < LayerCount; ++L)
                {
                    if ((State.WeightDirtyLayerMask & (1u << L)) == 0u)
                    {
                        continue;
                    }
                    if ((size_t(L + 1) * SlicePixels) > Terrain.LayerWeights.size())
                    {
                        continue;
                    }
                    const uint8* Slice = Terrain.LayerWeights.data() + L * SlicePixels;
                    CmdList.WriteImage(State.LayerWeightTexture, L, 0u, Slice, Res, 0u);
                }
                State.WeightDirtyLayerMask = 0u;
            }

            if (bHeightDirty && NormalShader)
            {
                FTerrainNormalParams NormalParams{};
                NormalParams.Resolution    = (int32)Res;
                NormalParams.RegionMinX    = 0;
                NormalParams.RegionMinY    = 0;
                NormalParams.RegionSizeX   = (int32)Res;
                NormalParams.RegionSizeY   = (int32)Res;
                NormalParams.TileWorldSize = Terrain.TileWorldSize;
                NormalParams.MaxHeight     = Terrain.MaxHeight;

                FRHIBufferDesc NormalCBDesc;
                NormalCBDesc.Size      = sizeof(FTerrainNormalParams);
                NormalCBDesc.DebugName = "TerrainNormalParams";
                NormalCBDesc.Usage.SetFlag(BUF_UniformBuffer);
                NormalCBDesc.bKeepInitialState = true;
                NormalCBDesc.InitialState = EResourceStates::ConstantBuffer;
                FRHIBufferRef NormalCB = GRenderContext->CreateBuffer(NormalCBDesc);
                CmdList.WriteBuffer(NormalCB, &NormalParams, sizeof(NormalParams));

                FRHISamplerRef ClampSampler = TStaticRHISampler<true, false, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
                
                FBindingLayoutDesc NormalLayoutDesc;
                NormalLayoutDesc.SetBindingIndex(0)
                    .SetVisibility(ERHIShaderType::Compute)
                    .AddItem(FBindingLayoutItem::Texture_SRV(0))
                    .AddItem(FBindingLayoutItem::Texture_UAV(1))
                    .AddItem(FBindingLayoutItem::Buffer_CBV(2));
                FRHIBindingLayout* NormalLayout = BindingCache.GetOrCreateBindingLayout(NormalLayoutDesc);

                FBindingSetDesc NormalSetDesc;
                NormalSetDesc.AddItem(FBindingSetItem::TextureSRV(0, State.HeightmapTexture, ClampSampler))
                             .AddItem(FBindingSetItem::TextureUAV(1, State.NormalTexture))
                             .AddItem(FBindingSetItem::BufferCBV(2, NormalCB));
                FRHIBindingSetRef NormalSet = GRenderContext->CreateBindingSet(NormalSetDesc, NormalLayout);

                FComputePipelineDesc NormalPipelineDesc;
                NormalPipelineDesc.SetComputeShader(NormalShader)
                                  .AddBindingLayout(NormalLayout);
                FRHIComputePipelineRef NormalPipeline = GRenderContext->CreateComputePipeline(NormalPipelineDesc);

                FComputeState NormalComputeState;
                NormalComputeState.SetPipeline(NormalPipeline)
                                  .AddBindingSet(NormalSet);

                CmdList.SetComputeState(NormalComputeState);
                CmdList.Dispatch((Res + 7u) / 8u, (Res + 7u) / 8u, 1u);

                State.HeightDirtyMin = glm::ivec2(INT32_MAX);
                State.HeightDirtyMax = glm::ivec2(INT32_MIN);
            }
        });
    }

    void FForwardRenderScene::TerrainRenderPass(ICommandList& CmdList)
    {
        FEntityRegistry& PreCheckRegistry = World->GetEntityRegistry();
        auto PreCheckView = PreCheckRegistry.view<STerrainComponent, STransformComponent>(entt::exclude<SDisabledTag>);
        if (PreCheckView.begin() == PreCheckView.end())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Terrain Render", tracy::Color::SeaGreen);

        FEntityRegistry& Registry = World->GetEntityRegistry();
        auto TerrainView = Registry.view<STerrainComponent, STransformComponent>(entt::exclude<SDisabledTag>);

        TerrainView.each([&](entt::entity Entity, STerrainComponent& Terrain, const STransformComponent& Transform)
        {
            if (Terrain.Resolution < 2 || Terrain.ChunkResolution < 2)
            {
                return;
            }

            FTerrainGPUState& State = Terrain.GPUState;
            if (!State.HeightmapTexture || !State.NormalTexture || !State.LayerWeightTexture)
            {
                return;
            }


            CMaterialInterface* MaterialInterface = Terrain.Material.Get();
            if (MaterialInterface && MaterialInterface->GetMaterialType() != EMaterialType::Terrain)
            {
                MaterialInterface = nullptr;
            }
            if (!MaterialInterface || !MaterialInterface->IsReadyForRender())
            {
                MaterialInterface = CMaterial::GetDefaultTerrainMaterial();
            }

            FRHIVertexShader* VertexShader = MaterialInterface->GetVertexShader();
            FRHIPixelShader*  PixelShader  = MaterialInterface->GetPixelShader();
            if (!VertexShader || !PixelShader)
            {
                return;
            }

            const uint32 Res = (uint32)Terrain.Resolution;

            const glm::mat4 WorldMat = Transform.GetWorldMatrix();
            const glm::vec3 WorldOrigin = glm::vec3(WorldMat[3]);
            const float HalfSize = Terrain.TileWorldSize * 0.5f;

            FTerrainRenderParams RenderParams{};
            RenderParams.OriginXZ        = glm::vec2(WorldOrigin.x - HalfSize, WorldOrigin.z - HalfSize);
            RenderParams.TileWorldSize   = Terrain.TileWorldSize;
            RenderParams.MaxHeight       = Terrain.MaxHeight;
            RenderParams.Resolution      = (int32)Res;
            RenderParams.ChunkResolution = Terrain.ChunkResolution;
            const int32 QuadsPerChunk    = std::max(1, Terrain.ChunkResolution - 1);
            RenderParams.ChunksPerSide   = std::max(1, ((int32)Res - 1) / QuadsPerChunk);
            RenderParams.LayerCount      = (int32)Terrain.Layers.size();
            RenderParams.WorldOriginY    = glm::vec3(WorldOrigin.y, 0.0f, 0.0f);
            RenderParams.EntityID        = (uint32)Entity;
            RenderParams.MaterialIndex   = (uint32)std::max(MaterialInterface->GetMaterialIndex(), 0);

            FRHIBufferDesc RenderCBDesc;
            RenderCBDesc.Size      = sizeof(FTerrainRenderParams);
            RenderCBDesc.DebugName = "TerrainRenderParams";
            RenderCBDesc.Usage.SetFlag(BUF_UniformBuffer);
            RenderCBDesc.bKeepInitialState = true;
            RenderCBDesc.InitialState = EResourceStates::ConstantBuffer;
            FRHIBufferRef RenderCB = GRenderContext->CreateBuffer(RenderCBDesc);
            CmdList.WriteBuffer(RenderCB, &RenderParams, sizeof(RenderParams));
            
            const bool bHDRWasWritten = !DrawCommands.empty() || RenderSettings.bHasEnvironment;

            FRenderPassDesc::FAttachment RenderTarget;
            RenderTarget.SetImage(GetNamedImage(ENamedImage::HDR));
            if (bHDRWasWritten)
            {
                RenderTarget.SetLoadOp(ERenderLoadOp::Load);
            }
            
            FRenderPassDesc::FAttachment PickerAttachment;
            PickerAttachment.SetImage(GetNamedImage(ENamedImage::Picker));
            if (!DrawCommands.empty())
            {
                PickerAttachment.SetLoadOp(ERenderLoadOp::Load);
            }

            FRenderPassDesc::FAttachment Depth;
            Depth.SetImage(GetNamedImage(ENamedImage::DepthAttachment))
                 .SetDepthClearValue(0.0f);
            if (!DrawCommands.empty())
            {
                Depth.SetLoadOp(ERenderLoadOp::Load);
            }

            FRenderPassDesc RenderPass;
            RenderPass.AddColorAttachment(RenderTarget)
                .AddColorAttachment(PickerAttachment)
                .SetDepthAttachment(Depth)
                .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());

            FRasterState RasterState;
            RasterState.EnableDepthClip()
                       .SetCullMode(ERasterCullMode::None);

            FDepthStencilState DepthState;
            DepthState.SetDepthFunc(EComparisonFunc::GreaterOrEqual)
                      .EnableDepthWrite();

            FRenderState RenderState;
            RenderState.SetRasterState(RasterState)
                       .SetDepthStencilState(DepthState);

            FRHISamplerRef HeightSampler = TStaticRHISampler<true, false, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
            
            FBindingLayoutDesc TerrainLayoutDesc;
            TerrainLayoutDesc.SetBindingIndex(2)
                .SetVisibility(ERHIShaderType::Vertex)
                .SetVisibility(ERHIShaderType::Fragment)
                .AddItem(FBindingLayoutItem::Texture_SRV(0))
                .AddItem(FBindingLayoutItem::Texture_SRV(1))
                .AddItem(FBindingLayoutItem::Texture_SRV(2))
                .AddItem(FBindingLayoutItem::Buffer_CBV(3));
            FRHIBindingLayout* TerrainLayout = BindingCache.GetOrCreateBindingLayout(TerrainLayoutDesc);

            FBindingSetDesc TerrainSetDesc;
            TerrainSetDesc.AddItem(FBindingSetItem::TextureSRV(0, State.HeightmapTexture, HeightSampler))
                          .AddItem(FBindingSetItem::TextureSRV(1, State.NormalTexture,    HeightSampler))
                          .AddItem(FBindingSetItem::TextureSRV(2, State.LayerWeightTexture, HeightSampler))
                          .AddItem(FBindingSetItem::BufferCBV(3, RenderCB));
            FRHIBindingSetRef TerrainSet = GRenderContext->CreateBindingSet(TerrainSetDesc, TerrainLayout);

            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("Terrain")
                .SetRenderState(RenderState)
                .SetVertexShader(VertexShader)
                .SetPixelShader(PixelShader)
                .AddBindingLayout(SceneBindingLayout)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                .AddBindingLayout(TerrainLayout);

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FGraphicsState GraphicsState;
            GraphicsState.SetRenderPass(RenderPass)
                .SetViewportState(MakeViewportStateFromImage(GetNamedImage(ENamedImage::HDR)))
                .SetPipeline(Pipeline)
                .AddBindingSet(SceneBindingSet)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                .AddBindingSet(TerrainSet);

            CmdList.SetGraphicsState(GraphicsState);

            const uint32 VertsPerChunk = uint32(QuadsPerChunk) * uint32(QuadsPerChunk) * 6u;
            const uint32 NumChunks     = uint32(RenderParams.ChunksPerSide) * uint32(RenderParams.ChunksPerSide);
            CmdList.Draw(VertsPerChunk, NumChunks, 0u, 0u);
        });
    }

    void FForwardRenderScene::BillboardPass(ICommandList& CmdList)
    {
        if (BillboardInstances.empty() || !RenderSettings.bDrawBillboards)
        {
            return;
        }
        
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
    }

    void FForwardRenderScene::TransparentPass(ICommandList& CmdList)
    {
        if (TranslucentDrawList.empty())
        {
            return;
        }

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
                         .SetIndirectParams(GetNamedBuffer(ENamedBuffer::IndirectArgs))
                         .AddBindingSet(SceneBindingSet)
                         .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());

            CmdList.SetGraphicsState(GraphicsState);
            // View 0 = camera.
            CmdList.DrawIndirect(Batch.DrawCount, Batch.IndirectDrawOffset * sizeof(FDrawIndirectArguments));
        }
    }

    void FForwardRenderScene::OITResolvePass(ICommandList& CmdList)
    {
        if (TranslucentDrawList.empty())
        {
            return;
        }

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
        Desc.AddBindingLayout(OITBindingLayout);
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.AddBindingSet(OITBindingSet);
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(SceneViewportState);
        
        CmdList.SetGraphicsState(GraphicsState);
    
        CmdList.Draw(3, 1, 0, 0);
    }

    void FForwardRenderScene::EnvironmentPass(ICommandList& CmdList)
    {
        if (!RenderSettings.bHasEnvironment)
        {
            return;
        }

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
    }

    void FForwardRenderScene::BatchedLineDraw(ICommandList& CmdList)
    {
        if (SimpleVertices.empty() || LineBatches.empty())
        {
            return;
        }
    
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
    }

    void FForwardRenderScene::ToneMappingPass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("Tone Mapping Pass", tracy::Color::Red2);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("ToneMapping.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        // When SMAA is enabled, tonemap renders into an LDR intermediate that the
        // SMAA passes then resolve into the final render target. Otherwise we
        // write straight to the render target.
        FRHIImage* OutputImage = World->GetDefaultWorldSettings().SMAAQuality != ESMAAQuality::Off ? GetNamedImage(ENamedImage::LDR) : GetRenderTarget();

        FRenderPassDesc::FAttachment Attachment; Attachment
            .SetImage(OutputImage);

        FRenderPassDesc RenderPass; RenderPass
            .AddColorAttachment(Attachment)
            .SetRenderArea(OutputImage->GetExtent());


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
        Desc.AddBindingLayout(ComposeBindingLayout);
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.AddBindingSet(SceneBindingSet);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        GraphicsState.AddBindingSet(ComposeBindingSet);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(MakeViewportStateFromImage(OutputImage));

        CmdList.SetGraphicsState(GraphicsState);

        glm::vec2 PC;
        PC.x = 1.0;
        PC.y = SceneGlobalData.Time;
        CmdList.SetPushConstants(&PC, sizeof(glm::vec2));
        CmdList.Draw(3, 1, 0, 0);
    }

    struct FSMAAPushConstants
    {
        glm::vec4 RTMetrics;  // x = 1/w, y = 1/h, z = w, w = h
        float     EdgeThreshold;
        float     DebugMode;
        float     _Pad0;
        float     _Pad1;
    };

    static float GetSMAAEdgeThreshold(ESMAAQuality Quality)
    {
        switch (Quality)
        {
        case ESMAAQuality::Low:    return 0.15f;
        case ESMAAQuality::Medium: return 0.12f;
        case ESMAAQuality::High:   return 0.10f;
        case ESMAAQuality::Ultra:  return 0.05f;
        default:                   return 0.10f;
        }
    }

    static FSMAAPushConstants BuildSMAAPushConstants(const FRHIImage* Image, const SDefaultWorldSettings& Settings)
    {
        FSMAAPushConstants PC;
        const float W = (float)Image->GetSizeX();
        const float H = (float)Image->GetSizeY();
        PC.RTMetrics      = glm::vec4(1.0f / W, 1.0f / H, W, H);
        PC.EdgeThreshold  = GetSMAAEdgeThreshold(Settings.SMAAQuality);
        PC.DebugMode      = 0.0f;
        PC._Pad0 = 0.0f;
        PC._Pad1 = 0.0f;
        return PC;
    }

    void FForwardRenderScene::SMAAEdgeDetectionPass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("SMAA Edge Detection", tracy::Color::Red2);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("SMAAEdgeDetection.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        FRHIImage* OutputImage = GetNamedImage(ENamedImage::SMAAEdges);

        FRenderPassDesc::FAttachment Attachment; Attachment
            .SetImage(OutputImage)
            .SetClearColor(glm::vec4(0.0f));

        FRenderPassDesc RenderPass; RenderPass
            .AddColorAttachment(Attachment)
            .SetRenderArea(OutputImage->GetExtent());

        FRasterState RasterState;
        RasterState.SetCullNone();

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest();
        DepthState.DisableDepthWrite();

        FRenderState RenderState;
        RenderState.SetRasterState(RasterState);
        RenderState.SetDepthStencilState(DepthState);

        FGraphicsPipelineDesc Desc;
        Desc.SetDebugName("SMAA Edge Detection Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(SceneBindingLayout);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.AddBindingLayout(SMAAEdgeBindingLayout);
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.AddBindingSet(SceneBindingSet);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        GraphicsState.AddBindingSet(SMAAEdgeBindingSet);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(MakeViewportStateFromImage(OutputImage));

        CmdList.SetGraphicsState(GraphicsState);

        FSMAAPushConstants PC = BuildSMAAPushConstants(OutputImage, World->GetDefaultWorldSettings());
        CmdList.SetPushConstants(&PC, sizeof(PC));
        CmdList.Draw(3, 1, 0, 0);
    }

    void FForwardRenderScene::SMAABlendWeightPass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("SMAA Blend Weight", tracy::Color::Red2);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("SMAABlendWeight.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        FRHIImage* OutputImage = GetNamedImage(ENamedImage::SMAABlend);

        FRenderPassDesc::FAttachment Attachment; Attachment
            .SetImage(OutputImage)
            .SetClearColor(glm::vec4(0.0f));

        FRenderPassDesc RenderPass; RenderPass
            .AddColorAttachment(Attachment)
            .SetRenderArea(OutputImage->GetExtent());

        FRasterState RasterState;
        RasterState.SetCullNone();

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest();
        DepthState.DisableDepthWrite();

        FRenderState RenderState;
        RenderState.SetRasterState(RasterState);
        RenderState.SetDepthStencilState(DepthState);

        FGraphicsPipelineDesc Desc;
        Desc.SetDebugName("SMAA Blend Weight Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(SceneBindingLayout);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.AddBindingLayout(SMAABlendWeightBindingLayout);
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.AddBindingSet(SceneBindingSet);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        GraphicsState.AddBindingSet(SMAABlendWeightBindingSet);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(MakeViewportStateFromImage(OutputImage));

        CmdList.SetGraphicsState(GraphicsState);

        FSMAAPushConstants PC = BuildSMAAPushConstants(OutputImage, World->GetDefaultWorldSettings());
        CmdList.SetPushConstants(&PC, sizeof(PC));
        CmdList.Draw(3, 1, 0, 0);
    }

    void FForwardRenderScene::SMAANeighborhoodBlendPass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("SMAA Neighborhood Blend", tracy::Color::Red2);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("SMAANeighborhoodBlend.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        FRHIImage* OutputImage = GetRenderTarget();

        FRenderPassDesc::FAttachment Attachment; Attachment
            .SetImage(OutputImage);

        FRenderPassDesc RenderPass; RenderPass
            .AddColorAttachment(Attachment)
            .SetRenderArea(OutputImage->GetExtent());

        FRasterState RasterState;
        RasterState.SetCullNone();

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest();
        DepthState.DisableDepthWrite();

        FRenderState RenderState;
        RenderState.SetRasterState(RasterState);
        RenderState.SetDepthStencilState(DepthState);

        FGraphicsPipelineDesc Desc;
        Desc.SetDebugName("SMAA Neighborhood Blend Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(SceneBindingLayout);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.AddBindingLayout(SMAANeighborhoodBindingLayout);
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.AddBindingSet(SceneBindingSet);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        GraphicsState.AddBindingSet(SMAANeighborhoodBindingSet);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(MakeViewportStateFromImage(OutputImage));

        CmdList.SetGraphicsState(GraphicsState);

        FSMAAPushConstants PC = BuildSMAAPushConstants(OutputImage, World->GetDefaultWorldSettings());
        CmdList.SetPushConstants(&PC, sizeof(PC));
        CmdList.Draw(3, 1, 0, 0);
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
            BufferDesc.Size = sizeof(FBillboardInstance);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::ShaderResource;
            BufferDesc.DebugName = "Billboard Data";
            NamedBuffers[(int)ENamedBuffer::Billboards] = GRenderContext->CreateBuffer(BufferDesc);
        }

        // Per-view cull descriptors. CullMeshlets.slang reads this SSBO to
        // test each meshlet against every active view's frustum / cone /
        // occlusion / distance policy. One FCullView per logical render
        // view: main camera at index 0, followed by CSM cascades, then 6
        // views per shadow-casting point light, then 1 per shadow-casting
        // spot light.
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FCullView);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::ShaderResource;
            BufferDesc.DebugName = "Cull View Buffer";
            NamedBuffers[(int)ENamedBuffer::CullView] = GRenderContext->CreateBuffer(BufferDesc);
        }

        // Unified meshlet draw list (NumViews * TotalMeshletBound).
        // CullMeshlets.slang appends surviving meshlets into each view's
        // slice via FCullView.DrawListOffset; draw passes read through
        // FCullView.IndirectArgsOffset for the per-view InstanceCount pair.
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(uint32) * 2;
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = "Meshlet Draw List";
            NamedBuffers[(int)ENamedBuffer::MeshletDrawList] = GRenderContext->CreateBuffer(BufferDesc);
        }

        // Unified indirect draw args. Sized NumViews * NumDraws.
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FDrawIndirectArguments);
            BufferDesc.Stride = sizeof(FDrawIndirectArguments);
            BufferDesc.Usage.SetMultipleFlags(BUF_Indirect, BUF_StorageBuffer);
            BufferDesc.InitialState = EResourceStates::IndirectArgument;
            BufferDesc.bKeepInitialState = true;
            BufferDesc.DebugName = "Indirect Args";
            NamedBuffers[(int)ENamedBuffer::IndirectArgs] = GRenderContext->CreateBuffer(BufferDesc);
        }

        // Two-pass cull defer list. Phase 0 appends camera meshlets that
        // failed the previous-frame HZB test; phase 1 pops them and
        // re-tests against the rebuilt HZB. Stride matches FMeshletDeferred
        // (4x uint32).
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(uint32) * 4;
            BufferDesc.Stride = sizeof(uint32) * 4;
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = "Meshlet Defer List";
            NamedBuffers[(int)ENamedBuffer::MeshletDeferList] = GRenderContext->CreateBuffer(BufferDesc);
        }

        // Atomic counter paired with MeshletDeferList. ResetPass zeroes it
        // every frame via FillBuffer before phase 0 runs.
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(uint32);
            BufferDesc.Stride = sizeof(uint32);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = "Meshlet Defer Count";
            NamedBuffers[(int)ENamedBuffer::DeferCount] = GRenderContext->CreateBuffer(BufferDesc);
        }

        // Atomic counter used by the Single-Pass Downsampler to hand off from
        // phase 1 (per-tile mips 0..5) to phase 2 (last workgroup generates
        // mips 6..11). DepthPyramidPass zeroes it before each dispatch; SPD
        // phase 2 also resets it so either writer leaves the buffer at zero.
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(uint32);
            BufferDesc.Stride = sizeof(uint32);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = "SPD Counter";
            NamedBuffers[(int)ENamedBuffer::SpdCounter] = GRenderContext->CreateBuffer(BufferDesc);
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
            ImageDesc.DebugName = "HDR";
            NamedImages[(int)ENamedImage::HDR] = GRenderContext->CreateImage(ImageDesc);
        }

        //==================================================================================================

        {
            FRHIImageDesc ImageDesc = GetRenderTarget()->GetDescription();
            ImageDesc.DebugName = "LDR";
            NamedImages[(int)ENamedImage::LDR] = GRenderContext->CreateImage(ImageDesc);
        }

        //==================================================================================================

        {
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = Extent;
            ImageDesc.Format            = EFormat::RG8_UNORM;
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.InitialState      = EResourceStates::RenderTarget;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName         = "SMAA Edges";
            NamedImages[(int)ENamedImage::SMAAEdges] = GRenderContext->CreateImage(ImageDesc);
        }

        {
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = Extent;
            ImageDesc.Format            = EFormat::RGBA8_UNORM;
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.InitialState      = EResourceStates::RenderTarget;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName         = "SMAA Blend";
            NamedImages[(int)ENamedImage::SMAABlend] = GRenderContext->CreateImage(ImageDesc);
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
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(3, GetNamedBuffer(ENamedBuffer::Bone)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(4, GetNamedBuffer(ENamedBuffer::Cluster)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(5, GRenderManager->GetMaterialManager().GetMaterialBuffer()));
            BindingSetDesc.AddItem(FBindingSetItem::BufferSRV(6, GetNamedBuffer(ENamedBuffer::Billboards)));
            // Comparison sampler enables hardware PCF: SampleCmp returns a 4-tap
            // bilinear-filtered shadow term in one texture fetch, replacing the
            // old manual Sample + step path.
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(7, GetNamedImage(ENamedImage::Cascade),
                TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp, ESamplerReductionType::Comparison>::GetRHI()));
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(8, ShadowAtlas.GetImage(),
                TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp, ESamplerReductionType::Comparison>::GetRHI()));
            // Min-reduction clamp sampler: a single bilinear tap on the depth pyramid
            // returns the minimum of the 2x2 footprint (farthest depth in reverse-Z).
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(9, GetNamedImage(ENamedImage::DepthPyramid),
                TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp, ESamplerReductionType::Minimum>::GetRHI()));
            // Unified per-view culling: CullMeshlets.slang reads FCullView entries
            // from uCullViews and atomically appends surviving meshlets into the
            // owning view's slice of uMeshletDrawList / uIndirectArgs. Every shadow
            // / camera draw pass uses the same two buffers, addressed through each
            // view's DrawListOffset / IndirectArgsOffset.
            BindingSetDesc.AddItem(FBindingSetItem::BufferSRV(10, GetNamedBuffer(ENamedBuffer::CullView)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(11, GetNamedBuffer(ENamedBuffer::MeshletDrawList)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(12, GetNamedBuffer(ENamedBuffer::IndirectArgs)));
            // PCSS needs raw depth (not PCF results) during blocker search so
            // individual texels can be classified against the receiver depth.
            // Binding 7 returns filtered compares; bind the same cascade with
            // a point/standard sampler at binding 13.
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(13, GetNamedImage(ENamedImage::Cascade),
                TStaticRHISampler<false, false, AM_Clamp, AM_Clamp, AM_Clamp, ESamplerReductionType::Standard>::GetRHI()));
            // Two-pass cull defer list + atomic counter. CullMeshlets phase 0
            // writes meshlets occluded by the stale HZB here; phase 1 pops
            // them and re-tests against the rebuilt HZB.
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(14, GetNamedBuffer(ENamedBuffer::MeshletDeferList)));
            BindingSetDesc.AddItem(FBindingSetItem::BufferUAV(15, GetNamedBuffer(ENamedBuffer::DeferCount)));

            TBitFlags<ERHIShaderType> Visibility;
            Visibility.SetMultipleFlags(ERHIShaderType::Vertex, ERHIShaderType::Fragment, ERHIShaderType::Compute);
            GRenderContext->CreateBindingSetAndLayout(Visibility, 0, BindingSetDesc, SceneBindingLayout, SceneBindingSet);
        }

        // Standalone set-0 layout for ToneMapping (uHDRSceneColor at binding 0).
        {
            FBindingSetDesc ComposeSetDesc;
            ComposeSetDesc.AddItem(FBindingSetItem::TextureSRV(0, GetNamedImage(ENamedImage::HDR)));

            TBitFlags<ERHIShaderType> Visibility;
            Visibility.SetMultipleFlags(ERHIShaderType::Fragment);
            GRenderContext->CreateBindingSetAndLayout(Visibility, 2, ComposeSetDesc, ComposeBindingLayout, ComposeBindingSet);
        }

        // SMAA Pass 1 (Edge Detection): reads LDR tonemapped color with point-clamp,
        // writes 2-channel edges texture.
        {
            FRHISamplerRef PointClamp  = TStaticRHISampler<false, false, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

            FBindingSetDesc SetDesc;
            SetDesc.AddItem(FBindingSetItem::TextureSRV(0, GetNamedImage(ENamedImage::LDR), PointClamp));

            TBitFlags<ERHIShaderType> Visibility;
            Visibility.SetMultipleFlags(ERHIShaderType::Fragment);
            GRenderContext->CreateBindingSetAndLayout(Visibility, 2, SetDesc, SMAAEdgeBindingLayout, SMAAEdgeBindingSet);
        }

        // SMAA Pass 2 (Blend Weight): reads edges, AreaTex, SearchTex. SearchTex
        // uses linear-clamp even though it's an integer-coded packed texture - the
        // technique relies on bilinear filtering for the packed-pair encoding.
        {
            FRHISamplerRef LinearClamp = TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

            FBindingSetDesc SetDesc;
            SetDesc.AddItem(FBindingSetItem::TextureSRV(0, GetNamedImage(ENamedImage::SMAAEdges), LinearClamp));
            SetDesc.AddItem(FBindingSetItem::TextureSRV(1, GetNamedImage(ENamedImage::SMAAArea), LinearClamp));
            SetDesc.AddItem(FBindingSetItem::TextureSRV(2, GetNamedImage(ENamedImage::SMAASearch), LinearClamp));

            TBitFlags<ERHIShaderType> Visibility;
            Visibility.SetMultipleFlags(ERHIShaderType::Fragment);
            GRenderContext->CreateBindingSetAndLayout(Visibility, 2, SetDesc, SMAABlendWeightBindingLayout, SMAABlendWeightBindingSet);
        }

        // SMAA Pass 3 (Neighborhood Blend): reads LDR color and blend weights, both linear-clamp.
        {
            FRHISamplerRef LinearClamp = TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

            FBindingSetDesc SetDesc;
            SetDesc.AddItem(FBindingSetItem::TextureSRV(0, GetNamedImage(ENamedImage::LDR), LinearClamp));
            SetDesc.AddItem(FBindingSetItem::TextureSRV(1, GetNamedImage(ENamedImage::SMAABlend), LinearClamp));

            TBitFlags<ERHIShaderType> Visibility;
            Visibility.SetMultipleFlags(ERHIShaderType::Fragment);
            GRenderContext->CreateBindingSetAndLayout(Visibility, 2, SetDesc, SMAANeighborhoodBindingLayout, SMAANeighborhoodBindingSet);
        }

        // Standalone set-0 layout for OITResolve (uAccum at 0, uRevealage at 1).
        {
            FBindingSetDesc OITSetDesc;
            OITSetDesc.AddItem(FBindingSetItem::TextureSRV(0, GetNamedImage(ENamedImage::Accum)));
            OITSetDesc.AddItem(FBindingSetItem::TextureSRV(1, GetNamedImage(ENamedImage::Revealage)));

            TBitFlags<ERHIShaderType> Visibility;
            Visibility.SetMultipleFlags(ERHIShaderType::Fragment);
            GRenderContext->CreateBindingSetAndLayout(Visibility, 0, OITSetDesc, OITBindingLayout, OITBindingSet);
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
