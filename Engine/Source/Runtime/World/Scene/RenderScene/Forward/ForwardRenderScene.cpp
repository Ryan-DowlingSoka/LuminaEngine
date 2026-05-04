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
#include "World/Entity/EntityUtils.h"
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
#include "World/Scene/RenderScene/EnvironmentRenderTypes.h"
#include "World/Scene/RenderScene/MeshDrawCommand.h"
#include "World/Scene/RenderScene/TerrainMeshletBuilder.h"
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
        
        SceneViewport = GRenderContext->CreateViewport(Windowing::GetPrimaryWindowHandle()->GetExtent(), "Forward Renderer Viewport");

        InitBuffers();

        // BRDF LUT must be baked before CreateLayouts() picks it up; survives swapchain rebuilds.
        InitBRDFLUT();

        // Sky cube allocated before CreateLayouts(); contents filled per-frame by SkyCubeCapturePass().
        InitSkyCube();
        InitIBLConvolutionTargets();

        InitFrameResources();

        // Persistent SMAA LUTs, sized constants, not affected by swapchain size.
        NamedImages[(int)ENamedImage::SMAAArea] = CreateSMAALUTImage(areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, EFormat::RG8_UNORM, AREATEX_PITCH, "SMAA AreaTex");
        NamedImages[(int)ENamedImage::SMAASearch] = CreateSMAALUTImage(searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, EFormat::R8_UNORM, SEARCHTEX_PITCH, "SMAA SearchTex");

        SwapchainResizedHandle = FRenderManager::OnSwapchainResized.AddMember(this, &FForwardRenderScene::SwapchainResized);
        
        #if USING(WITH_EDITOR)
        NamedImages[(int)ENamedImage::PointLightIcon]       = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/PointLight.png", false);
        NamedImages[(int)ENamedImage::DirectionalLightIcon] = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/DirectionalLight.png", false);
        NamedImages[(int)ENamedImage::SkyLightIcon]         = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/SkyLight.png", false);
        NamedImages[(int)ENamedImage::SpotLightIcon]        = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/SpotLight.png", false);
        NamedImages[(int)ENamedImage::CameraIcon]           = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/CameraIcon.png", false);
        NamedImages[(int)ENamedImage::CharacterIcon]        = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/PersonIcon.png", false);
        NamedImages[(int)ENamedImage::ParticleSystemIcon]   = Import::Textures::CreateTextureFromImport(Paths::GetEngineResourceDirectory() + "/Textures/Molecule.png", false);

        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::PointLightIcon]);
        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::DirectionalLightIcon]);
        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::SkyLightIcon]);
        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::SpotLightIcon]);
        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::CameraIcon]);
        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::CharacterIcon]);
        GRenderManager->GetTextureManager().AddTexture(NamedImages[(int)ENamedImage::ParticleSystemIcon]);
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
        GRenderManager->GetTextureManager().RemoveTexture(NamedImages[(int)ENamedImage::SkyLightIcon]);
        GRenderManager->GetTextureManager().RemoveTexture(NamedImages[(int)ENamedImage::SpotLightIcon]);
        GRenderManager->GetTextureManager().RemoveTexture(NamedImages[(int)ENamedImage::CameraIcon]);
        GRenderManager->GetTextureManager().RemoveTexture(NamedImages[(int)ENamedImage::CharacterIcon]);
        GRenderManager->GetTextureManager().RemoveTexture(NamedImages[(int)ENamedImage::ParticleSystemIcon]);
        #endif
        
        FRenderManager::OnSwapchainResized.Remove(SwapchainResizedHandle);
        
        LOG_TRACE("Shutting down Forward Render Scene");
    }

    void FForwardRenderScene::RenderView(ICommandList& CmdList, const FViewVolume& ViewVolume, const SPostProcessSettings* PostProcess)
    {
        LUMINA_PROFILE_SCOPE();

        // Reconciles MSAA toggle: alloc/free MS scratch so next frame uses new sample count without reload.
        SyncMSAAState();

        ActivePostProcess = PostProcess;

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
        
        
        if(GRenderContext->GetShaderCompiler()->HasPendingRequests())
        {
            return;
        }

        GPU_PROFILE_SCOPE_COLOR(&CmdList, "RenderView", FColor(0.30f, 0.65f, 1.00f));

        ResetPass(CmdList);
        CompileDrawCommands(CmdList);
        
        {
            LUMINA_PROFILE_SECTION("RenderPasses");
        
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
                // Sky cube capture here keeps the IBL cube in lockstep with the rendered background.
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Sky Cube Capture", FColor(0.55f, 0.85f, 0.95f));
                SkyCubeCapturePass(CmdList);
            }

            {
                // Convolves IBL diffuse + GGX specular cubemaps from the cube SkyCubeCapturePass wrote.
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Sky Irradiance", FColor(0.55f, 0.75f, 0.95f));
                IrradianceConvolutionPass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Sky Prefilter", FColor(0.55f, 0.75f, 0.85f));
                PrefilterEnvMapPass(CmdList);
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

            // Terrain cull uses the post-base-pass Hi-Z (freshest occlusion).
            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Terrain Cull", FColor(0.20f, 0.85f, 0.50f));
                TerrainCullPass(CmdList);
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
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Volumetric Lighting", FColor(0.85f, 0.65f, 0.30f));
                VolumetricLightingPass(CmdList);
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

            #if USING(WITH_EDITOR)
            {
                // After the last picker RT write; readback happens lazily in GetEntityAtPixel.
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Picker Readback", FColor(0.50f, 0.50f, 0.50f));
                IssuePickerReadback(CmdList);
            }
            #endif
        
            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Bloom", FColor(0.95f, 0.75f, 0.30f));
                BloomPass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Tone Mapping", FColor(0.95f, 0.20f, 0.20f));
                ToneMappingPass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Post Process Materials", FColor(0.85f, 0.30f, 0.85f));
                PostProcessMaterialPass(CmdList);
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
    }
    
    void FForwardRenderScene::SwapchainResized(glm::vec2 NewSize)
    {
        GRenderContext->ClearCommandListCache();
        GRenderContext->ClearBindingCaches();
        BindingCache.ReleaseResources();

        SceneViewport = GRenderContext->CreateViewport(NewSize, "Forward Renderer Viewport");

        InitFrameResources();

        #if USING(WITH_EDITOR)
        // Drop old staging slots; sized to previous extent so pixel grid no longer matches clicks.
        for (FPickerReadbackSlot& Slot : PickerReadbackRing)
        {
            Slot.Staging.SafeRelease();
            Slot.Width = 0;
            Slot.Height = 0;
            Slot.bPending = false;
        }
        #endif
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
            auto StaticView         = Registry.view<SStaticMeshComponent, STransformComponent>(entt::exclude<SDisabledTag>);
            auto SkeletalView       = Registry.view<SSkeletalMeshComponent, STransformComponent>(entt::exclude<SDisabledTag>);
            
            ECS::Utils::ResolveAllDirtyTransforms(Registry);

            // Per-frame CPU reject volumes built before parallel gather so workers query lock-free.
            BuildSceneCullContext();

            const size_t EntityCount       = StaticView.size_hint() + SkeletalView.size_hint();
            const size_t EstimatedProxies  = EntityCount * 2;

            Instances.reserve(EstimatedProxies);
            DrawMeshletStartOffsets.reserve(EstimatedProxies);
            DrawCommands.reserve(EstimatedProxies);

            const uint32 NumThreads = GTaskSystem->GetScheduler().GetNumTaskThreads();

            // 4 MB block: handles a single TFrameVector growth doubling up to ~1 MB without overflow.
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
            
            // Environment processing runs serially after Graph.Wait(): bAmbientFromSky needs
            // LightData.SunDirection populated by ProcessDirectionalLight first.
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
                // Editor visualizers: billboards for lights/cameras/sky/particles. Skipped in game/thumbnail worlds.
                if (!World->IsGameWorld())
                {
                    auto EmplaceVisualizer = [this](entt::entity Entity, const glm::vec3& Position, ENamedImage Icon, const glm::vec4& Color, float Size = 0.20f)
                    {
                        FBillboardInstance& Billboard = BillboardInstances.emplace_back();
                        Billboard.TextureIndex        = GetNamedImage(Icon)->GetTextureCacheIndex();
                        Billboard.ColorPack           = PackColor(Color);
                        Billboard.Position            = Position;
                        Billboard.Size                = Size;
                        Billboard.EntityID            = entt::to_integral(Entity);
                    };

                    // Skip editor viewport camera so the billboard doesn't sit on the user's view.
                    CameraView.each([&](entt::entity Entity, SCameraComponent&, const STransformComponent& Transform)
                    {
                        if (Registry.all_of<FEditorComponent>(Entity))
                        {
                            return;
                        }
                        EmplaceVisualizer(Entity, Transform.WorldTransform.Location, ENamedImage::CameraIcon, FColor::White);
                    });

                    CharacterView.each([&](entt::entity Entity, SCharacterControllerComponent&, const STransformComponent& Transform)
                    {
                        EmplaceVisualizer(Entity, Transform.WorldTransform.Location, ENamedImage::CharacterIcon, FColor::White);
                    });

                    PointLightView.each([&](entt::entity Entity, const SPointLightComponent& Light, const STransformComponent& Transform)
                    {
                        EmplaceVisualizer(Entity, Transform.WorldTransform.Location, ENamedImage::PointLightIcon, glm::vec4(Light.LightColor, 1.0f));
                    });

                    SpotLightView.each([&](entt::entity Entity, const SSpotLightComponent& Light, const STransformComponent& Transform)
                    {
                        EmplaceVisualizer(Entity, Transform.WorldTransform.Location, ENamedImage::SpotLightIcon, glm::vec4(Light.LightColor, 1.0f));
                    });

                    DirectionalView.each([&](entt::entity Entity, const SDirectionalLightComponent& Light)
                    {
                        const auto& Transform = Registry.get<STransformComponent>(Entity);
                        EmplaceVisualizer(Entity, Transform.WorldTransform.Location, ENamedImage::DirectionalLightIcon, glm::vec4(Light.Color, 1.0f));
                    });

                    EnvironmentView.each([&](entt::entity Entity, const SEnvironmentComponent&)
                    {
                        const auto& Transform = Registry.get<STransformComponent>(Entity);
                        EmplaceVisualizer(Entity, Transform.WorldTransform.Location, ENamedImage::SkyLightIcon, glm::vec4(1.0f));
                    });

                    auto ParticleView = Registry.view<SParticleSystemComponent, STransformComponent>(entt::exclude<SDisabledTag>);
                    ParticleView.each([&](entt::entity Entity, const SParticleSystemComponent&, const STransformComponent& Transform)
                    {
                        EmplaceVisualizer(Entity, Transform.WorldTransform.Location, ENamedImage::ParticleSystemIcon, glm::vec4(1.0f));
                    });
                }
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

            // Serial fit/allocate after parallel light pass; shrinks when sum(area) exceeds atlas budget.
            AllocateShadowTiles();

            // Serial: bAmbientFromSky needs LightData.SunDirection from ProcessDirectionalLight.
            // Last enabled SEnvironmentComponent wins.
            {
                LUMINA_PROFILE_SECTION("Environment Processing");

                RenderSettings.bHasEnvironment = false;
                RenderSettings.bSSAO           = false;
                LightData.AmbientLight         = glm::vec4(0.0f);
                EnvironmentParams              = FEnvironmentParams{};
                EnvironmentMapImage            = nullptr;
                // Set true below if any IBL input differs from the last bake snapshot.
                bIBLDirty                      = false;

                EnvironmentView.each([this] (const SEnvironmentComponent& Env)
                {
                    // bRenderSky gates the sky pass; ambient/skylight still flow when off (indoor scenes).
                    RenderSettings.bHasEnvironment = Env.bRenderSky;

                    // Only honor HDRI assignment when SkyMode == HDRI; otherwise IBL would convolve
                    // from HDRI even though visible sky is procedural. Non-Environment textures rejected.
                    if (Env.SkyMode == ESkyMode::HDRI)
                    {
                        if (CTexture* EnvMap = Env.EnvironmentMap.Get())
                        {
                            if (EnvMap->ColorSpace == ETextureColorSpace::Environment && EnvMap->GetRHIRef())
                            {
                                EnvironmentMapImage = EnvMap->GetRHIRef();
                            }
                        }
                    }

                    // Misc.x carries sky mode as float-cast uint; shader pulls it back via asuint().
                    const uint32 SkyModeBits = (Env.SkyMode == ESkyMode::SolidColor) ? GSkyMode_SolidColor
                                            : (Env.SkyMode == ESkyMode::Gradient)   ? GSkyMode_Gradient
                                            : (Env.SkyMode == ESkyMode::HDRI)       ? GSkyMode_HDRI
                                                                                    : GSkyMode_Dynamic;
                    float SkyModeAsFloat;
                    std::memcpy(&SkyModeAsFloat, &SkyModeBits, sizeof(float));

                    EnvironmentParams.SolidSkyColor = glm::vec4(Env.SolidSkyColor, 0.0f);
                    EnvironmentParams.ZenithColor   = glm::vec4(Env.ZenithColor, Env.HorizonExponent);
                    EnvironmentParams.HorizonColor  = glm::vec4(Env.HorizonColor, 0.0f);
                    EnvironmentParams.GroundColor   = glm::vec4(Env.GroundColor, 0.0f);
                    EnvironmentParams.SunTint       = glm::vec4(Env.SunColorTint, Env.SunIntensity);
                    EnvironmentParams.Misc          = glm::vec4(SkyModeAsFloat,
                                                                Env.SunDiscScale,
                                                                Env.SkyExposure,
                                                                Env.MieAnisotropy);

                    EnvironmentParams.NightSkyColor = glm::vec4(Env.NightSkyColor, Env.NightBrightness);
                    EnvironmentParams.StarParams    = glm::vec4(Env.StarDensity,
                                                                Env.StarBrightness,
                                                                Env.StarTwinkleSpeed,
                                                                Env.StarSize);
                    EnvironmentParams.MoonParams    = glm::vec4(Env.MoonSize,
                                                                Env.MoonGlowSize,
                                                                Env.MoonBrightness,
                                                                Env.bMoonOpposeSun ? 1.0f : 0.0f);
                    EnvironmentParams.MoonDirection = glm::vec4(Env.MoonDirection, 0.0f);
                    EnvironmentParams.GalaxyParams  = glm::vec4(Env.GalaxyIntensity, Env.GalaxyTilt, 0.0f, 0.0f);

                    // bAmbientFromSky derives ambient from active sky mode (avoids syncing two pickers).
                    glm::vec3 AmbientRGB = Env.AmbientColor;
                    if (Env.bAmbientFromSky)
                    {
                        if (Env.SkyMode == ESkyMode::SolidColor)
                        {
                            AmbientRGB = Env.SolidSkyColor;
                        }
                        else if (Env.SkyMode == ESkyMode::Gradient)
                        {
                            // 70/30 zenith/horizon matches what an upward-facing surface would integrate.
                            AmbientRGB = Env.ZenithColor * 0.7f + Env.HorizonColor * 0.3f;
                        }
                        else // Dynamic
                        {
                            // SunDirection points FROM surface TO sun, so .y is elevation.
                            const float SunHeight = LightData.bHasSun
                                ? glm::clamp(LightData.SunDirection.y, -1.0f, 1.0f)
                                : 0.5f;
                            const float Day = glm::clamp(SunHeight * 2.0f + 0.2f, 0.0f, 1.0f);
                            AmbientRGB = glm::mix(glm::vec3(0.05f, 0.06f, 0.10f),
                                                  glm::vec3(0.40f, 0.55f, 0.85f),
                                                  Day);
                        }
                    }
                    LightData.AmbientLight = glm::vec4(AmbientRGB, Env.AmbientIntensity);
                });
            }
        }
        
        
        SceneGlobalData.CullData.InstanceNum = (uint32)Instances.size();

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

        const uint32 NumCullViews                  = (uint32)CullViews.size();
        const uint32 NumDraws                      = NumDrawsPerView;
        SceneGlobalData.CullData.NumDraws          = NumDraws;
        SceneGlobalData.CullData.TotalMeshletBound = TotalMeshletBound;

        const SIZE_T SimpleVertexSize     = SimpleVertices.size() * sizeof(FSimpleElementVertex);
        const SIZE_T InstanceSize         = Instances.size() * sizeof(FGPUInstance);
        const SIZE_T BoneDataSize         = BonesData.size() * sizeof(glm::mat4);

        const SIZE_T ActiveLightsSize  = LightData.NumLights * sizeof(FLight);
        const SIZE_T LightsUploadSize  = offsetof(FSceneLightData, Lights) + ActiveLightsSize;
        const uint32 ActiveShadowCount = glm::min<uint32>(ShadowDataCount.load(std::memory_order_acquire), (uint32)MAX_SHADOWS);
        const SIZE_T ShadowsUploadSize = ActiveShadowCount * sizeof(FLightShadowData);
        // Sized to cover the shadow suffix even when only uploading active slice; WriteBuffer at
        // ShadowsOffset would otherwise overrun a buffer sized only to LightsUploadSize.
        const SIZE_T LightUploadSize   = offsetof(FSceneLightData, Shadows) + ShadowsUploadSize;
        const SIZE_T BillboardSize     = BillboardInstances.size() * sizeof(FBillboardInstance);

        // Per-instance meshlet prefix sum (one uint per instance + sentinel); cull pass binary-searches.
        const SIZE_T InstanceMeshletPrefixSize = glm::max<SIZE_T>(
            sizeof(uint32),
            InstanceMeshletPrefix.size() * sizeof(uint32));

        // Shared draw list: NumViews * TotalMeshletBound FMeshletDraw; views own disjoint slices via FCullView.DrawListOffset.
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

        // Worst case: every meshlet HZB-occluded in phase 0 (first frame). Stride matches FMeshletDeferred.
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
        bAnyBufferResized |= RenderUtils::ResizeBufferIfNeeded(NamedBuffers[(int)ENamedBuffer::InstanceMeshletPrefix], (uint32)InstanceMeshletPrefixSize, 1.2f);

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
            CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::Environment), EResourceStates::CopyDest);
            CmdList.SetBufferState(GetNamedBuffer(ENamedBuffer::InstanceMeshletPrefix), EResourceStates::CopyDest);
            CmdList.CommitBarriers();

            CmdList.DisableAutomaticBarriers();
            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Scene), &SceneGlobalData, sizeof(FSceneGlobalData));
            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Instance), Instances.data(), InstanceSize);
            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Bone), BonesData.data(),  BoneDataSize);

            if (!CullViews.empty())
            {
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::CullView), CullViews.data(), CullViews.size() * sizeof(FCullView));
            }

            // FirstInstance pre-seeded so each atomic append in CullMeshlets lands in its own slice.
            if (!IndirectArgs.empty())
            {
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::IndirectArgs), IndirectArgs.data(), IndirectArgs.size() * sizeof(FDrawIndirectArguments));
            }

            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::SimpleVertex), SimpleVertices.data(), SimpleVertexSize);
            // Upload only populated prefix: header + active FLight, and active FLightShadowData. Middle is unread.
            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Light), &LightData, LightsUploadSize, 0);
            if (ShadowsUploadSize > 0)
            {
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Light), &LightData.Shadows[0], ShadowsUploadSize, offsetof(FSceneLightData, Shadows));
            }
            CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Billboards), BillboardInstances.data(), BillboardSize);

            // Skip env upload + IBL convolution when nothing they depend on changed.
            const bool bEnvParamsChanged =
                !bEnvironmentParamsUploaded ||
                std::memcmp(&EnvironmentParams, &LastUploadedEnvironmentParams, sizeof(FEnvironmentParams)) != 0;
            if (bEnvParamsChanged)
            {
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::Environment), &EnvironmentParams, sizeof(FEnvironmentParams));
                LastUploadedEnvironmentParams = EnvironmentParams;
                bEnvironmentParamsUploaded   = true;
            }

            const bool bSunChanged =
                LastIBLSunDirection != LightData.SunDirection ||
                bLastIBLHasSun       != (LightData.bHasSun != 0);
            const bool bMapChanged =
                LastIBLEnvironmentMap != EnvironmentMapImage;

            if (RenderSettings.bHasEnvironment &&
                (!bIBLValid || bEnvParamsChanged || bSunChanged || bMapChanged))
            {
                bIBLDirty                  = true;
                LastIBLEnvironmentParams   = EnvironmentParams;
                LastIBLEnvironmentMap      = EnvironmentMapImage;
                LastIBLSunDirection        = LightData.SunDirection;
                bLastIBLHasSun             = (LightData.bHasSun != 0);
                bIBLValid                  = true;
            }
            if (!InstanceMeshletPrefix.empty())
            {
                CmdList.WriteBuffer(GetNamedBuffer(ENamedBuffer::InstanceMeshletPrefix),
                                    InstanceMeshletPrefix.data(),
                                    InstanceMeshletPrefix.size() * sizeof(uint32));
            }
            CmdList.EnableAutomaticBarriers();
        }
    }

    struct FResolvedSlot
    {
        FRHIVertexShader*   VertexShader;
        FRHIPixelShader*    PixelShader;
        // Populated only when material drives WorldPositionOffset; null = global library shader.
        FRHIVertexShader*   DepthVertexShader  = nullptr;
        FRHIVertexShader*   ShadowVertexShader = nullptr;
        uint64              MaterialID;
        EInstanceFlags      ExtraFlags;
        uint16              MaterialIdx;
        uint8               bTranslucent     : 1;
        uint8               bMasked          : 1;
        uint8               bAdditive        : 1;
        uint8               bDrawInDepthPass : 1;
    };

    template <typename TComponent>
    static FResolvedSlot ResolveSlot(const TComponent& MeshComponent, int16 SlotIdx, bool bSignificantOccluder)
    {
        CMaterialInterface* Material = MeshComponent.GetMaterialForSlot(SlotIdx);
        // Terrain/PostProcess have different pipeline layouts; misassignment falls back to default.
        if (IsValid(Material) &&
            (Material->GetMaterialType() == EMaterialType::Terrain ||
             Material->GetMaterialType() == EMaterialType::PostProcess))
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

        FResolvedSlot R;
        R.VertexShader     = Material->GetVertexShader();
        R.PixelShader      = Material->GetPixelShader();
        if (CMaterial* ConcreteMaterial = Material->GetMaterial(); ConcreteMaterial && ConcreteMaterial->UsesWorldPositionOffset())
        {
            R.DepthVertexShader  = ConcreteMaterial->GetDepthPrepassVertexShader();
            R.ShadowVertexShader = ConcreteMaterial->GetShadowVertexShader();
        }
        R.MaterialID       = (uint64)Material->GetMaterial();
        R.ExtraFlags       = Extra;
        R.MaterialIdx      = (uint16)Material->GetMaterialIndex();
        R.bTranslucent     = bTranslucent;
        R.bMasked          = bMasked;
        R.bAdditive        = bAdditive;
        R.bDrawInDepthPass = MeshComponent.bUseAsOccluder && !bTranslucent && bSignificantOccluder;
        return R;
    }

    static uint16 FindOrAddLocalBatch(FForwardRenderScene::FThreadLocalDrawData& Local, const FDrawBatchKey& Key, const FResolvedSlot& Slot)
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

        // Pass arena explicitly so inner TFrameVectors bind to the same per-thread arena.
        FForwardRenderScene::FLocalBatchEntry& Entry = Local.LocalBatches.emplace_back(Local.Arena);
        Entry.Key                = Key;
        Entry.VertexShader       = Slot.VertexShader;
        Entry.PixelShader        = Slot.PixelShader;
        Entry.DepthVertexShader  = Slot.DepthVertexShader;
        Entry.ShadowVertexShader = Slot.ShadowVertexShader;

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

    // Thresholds stored as (distance/radius) ratios; monotonic, so stop at first that fails.
    static uint32 SelectLODIndex(const FGeometrySurface& Surface, float DistanceOverRadius)
    {
        uint32 Picked = 0;
        const uint32 LastLOD = Surface.NumLODs > 0 ? Surface.NumLODs - 1u : 0u;
        for (uint32 i = 1; i <= LastLOD; ++i)
        {
            if (DistanceOverRadius >= Surface.LODScreenThreshold[i])
            {
                Picked = i;
            }
            else
            {
                break;
            }
        }
        return Picked;
    }

    // Component override beats global setting; both clamped to surface NumLODs.
    static uint32 ResolveSurfaceLOD(const FGeometrySurface& Surface, int32 ForcedLODIndex, bool bUseLODs, float DistanceOverRadius)
    {
        if (Surface.NumLODs <= 1)
        {
            return 0u;
        }
        if (ForcedLODIndex >= 0)
        {
            return (uint32)glm::min((int32)Surface.NumLODs - 1, ForcedLODIndex);
        }
        if (bUseLODs)
        {
            return SelectLODIndex(Surface, DistanceOverRadius);
        }
        return 0u;
    }

    // Pick the surface's shadow-only LOD. Camera LOD plus ShadowLODBias,
    // capped at MAX_SHADOW_LOD so sloppy LODs are never selected for
    // shadows -- their topology breaks would surface as light leaks
    // through what should be solid casters.
    static uint32 ResolveShadowLOD(const FGeometrySurface& Surface, uint32 CameraLOD, int32 ShadowLODBias)
    {
        if (Surface.NumLODs == 0)
        {
            return 0u;
        }
        const int32 Biased = (int32)CameraLOD + ShadowLODBias;
        const int32 MaxLOD = (int32)glm::min<uint32>(Surface.NumLODs - 1u, MAX_SHADOW_LOD);
        return (uint32)glm::clamp(Biased, 0, MaxLOD);
    }

    void FForwardRenderScene::ProcessStaticMeshEntityInternal(entt::entity Entity, const SStaticMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local)
    {
        CMesh* Mesh = MeshComponent.StaticMesh;
        if (!IsValid(Mesh))
        {
            return;
        }

        const glm::mat4& TransformMatrix = TransformComponent.CachedMatrix;

        // World bounds first so we can reject before paying for mesh/surface lookups.
        // BoundsScale inflates cull sphere when animation/displacement push past asset AABB.
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

        // Angular size proxy (2r/d, squared to skip sqrt). Tiny props skip depth pre-pass.
        const glm::vec3 CameraPos  = glm::vec3(SceneGlobalData.CameraData.Location);
        const glm::vec3 ToCamera   = Center - CameraPos;
        const float     DistSq     = glm::dot(ToCamera, ToCamera);
        constexpr float kMinAngularSize = 0.05f;
        const bool bSignificantOccluder = (Radius * Radius) > DistSq * (kMinAngularSize * kMinAngularSize);

        // Distance in radii; one sqrt+divide hoisted out of the per-surface loop.
        const float DistanceOverRadius = (Radius > 0.0f)
            ? (glm::sqrt(DistSq) / Radius)
            : 0.0f;

        EInstanceFlags BaseFlags = EInstanceFlags::None;
        if (MeshComponent.bReceiveShadow)
        {
            BaseFlags |= EInstanceFlags::ReceiveShadow;
        }
        if (MeshComponent.bIgnoreOcclusionCulling)
        {
            BaseFlags |= EInstanceFlags::IgnoreOcclusionCulling;
        }

        const uint32 EntityRecordIdx = (uint32)Local.EntityRecords.size();
        FEntityRecord& EntityRecord = Local.EntityRecords.emplace_back();
        EntityRecord.Transform            = TransformMatrix;
        EntityRecord.SphereBounds         = SphereBounds;
        EntityRecord.MeshletHeaderAddress = MeshletHeaderAddress;
        EntityRecord.CustomData           = MeshComponent.CustomPrimitiveData.Data.Packed;
        EntityRecord.EntityID             = entt::to_integral(Entity);
        EntityRecord.LocalBoneOffset      = ~0u;
        EntityRecord._Pad                 = 0u;

        for (const FGeometrySurface& Surface : Resource.GeometrySurfaces)
        {
            const FResolvedSlot Slot = ResolveSlot(MeshComponent, Surface.MaterialIndex, bSignificantOccluder);

            const EInstanceFlags Flags = BaseFlags | Slot.ExtraFlags;

            FDrawBatchKey BatchKey
            {
                .MaterialID       = Slot.MaterialID,
                .bDrawInDepthPass = (Slot.bDrawInDepthPass ? 1u : 0u),
                .bTranslucent     = (Slot.bTranslucent     ? 1u : 0u),
                .bMasked          = (Slot.bMasked          ? 1u : 0u),
                .bAdditive        = (Slot.bAdditive        ? 1u : 0u),
            };
            // CPU LOD pick replaces LOD 0; smaller ranges directly cut cull-pass cost.
            const uint32 LODIndex       = ResolveSurfaceLOD(Surface, MeshComponent.ForcedLODIndex, RenderSettings.bUseLODs, DistanceOverRadius);
            const uint32 ShadowLODIndex = ResolveShadowLOD(Surface, LODIndex, RenderSettings.ShadowLODBias);

            // Zero meshlet count gates the cull shader's MeshletHeader deref.
            const uint32 SurfaceMeshletCount  = MeshletHeaderAddress ? Surface.LODMeshletCount[LODIndex]       : 0u;
            const uint32 SurfaceMeshletOffset = Surface.LODMeshletOffset[LODIndex];
            const uint32 ShadowMeshletCount   = MeshletHeaderAddress ? Surface.LODMeshletCount[ShadowLODIndex] : 0u;
            const uint32 ShadowMeshletOffset  = Surface.LODMeshletOffset[ShadowLODIndex];

            const uint16 LocalBatchIdx = FindOrAddLocalBatch(Local, BatchKey, Slot);
            const uint16 LocalDrawIdx  = FindOrAddLocalDraw(Local.LocalBatches[LocalBatchIdx], FDrawKey{ Surface.StartIndex, Surface.IndexCount }, SurfaceMeshletCount);

            FProcessedDrawItem& Item = Local.Items.emplace_back();
            Item.EntityRecordIndex    = EntityRecordIdx;
            Item.SurfaceMeshletOffset = SurfaceMeshletOffset;
            Item.SurfaceMeshletCount  = SurfaceMeshletCount;
            Item.ShadowMeshletOffset  = ShadowMeshletOffset;
            Item.ShadowMeshletCount   = ShadowMeshletCount;
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

        // Reject before uploading bones (biggest per-entity skeletal cost).
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

        // Every skeletal draw needs a valid bone slice (LoadSkinnedVertex path); fall back to bind pose
        // when no animation is playing -- otherwise stale matrices from the previous frame leak through.
        const CSkeletalMesh*     SkelMesh = MeshComponent.SkeletalMesh.Get();
        const FSkeletonResource* SkelRes  = (SkelMesh && SkelMesh->Skeleton.IsValid())
            ? SkelMesh->Skeleton->GetSkeletonResource()
            : nullptr;
        const uint32 SkeletonBoneCount = SkelRes ? (uint32)SkelRes->GetNumBones() : 0u;

        uint32 LocalBoneOffset = ~0u;
        if (SkeletonBoneCount > 0)
        {
            LocalBoneOffset = (uint32)Local.BonesData.size();
            if ((uint32)MeshComponent.BoneTransforms.size() == SkeletonBoneCount)
            {
                Local.BonesData.insert(Local.BonesData.end(),
                                       MeshComponent.BoneTransforms.begin(),
                                       MeshComponent.BoneTransforms.end());
            }
            else
            {
                // Append bind pose directly into the per-thread bone buffer; avoids mutating const component.
                const size_t BindBase = Local.BonesData.size();
                Local.BonesData.resize(BindBase + SkeletonBoneCount);
                for (uint32 i = 0; i < SkeletonBoneCount; ++i)
                {
                    const auto& Bone = SkelRes->GetBone((int32)i);
                    const glm::mat4 BoneWorld = (Bone.ParentIndex == INDEX_NONE)
                        ? Bone.LocalTransform
                        : Local.BonesData[BindBase + Bone.ParentIndex] * Bone.LocalTransform;
                    Local.BonesData[BindBase + i] = BoneWorld;
                }
                for (uint32 i = 0; i < SkeletonBoneCount; ++i)
                {
                    const auto& Bone = SkelRes->GetBone((int32)i);
                    Local.BonesData[BindBase + i] = Local.BonesData[BindBase + i] * Bone.InvBindMatrix;
                }
            }
        }

        const uint32 NumBones = SkeletonBoneCount;

        // Angular size proxy; bind-pose bounds are conservative but fine for this coarse test.
        const glm::vec3 CameraPos  = glm::vec3(SceneGlobalData.CameraData.Location);
        const glm::vec3 ToCamera   = Center - CameraPos;
        const float     DistSq     = glm::dot(ToCamera, ToCamera);
        constexpr float kMinAngularSize = 0.05f;
        const bool bSignificantOccluder = (Radius * Radius) > DistSq * (kMinAngularSize * kMinAngularSize);

        const float DistanceOverRadius = (Radius > 0.0f)
            ? (glm::sqrt(DistSq) / Radius)
            : 0.0f;

        // Skeletal assets always carry FSkinnedVertex; flag is unconditional.
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

        for (const FGeometrySurface& Surface : Resource.GeometrySurfaces)
        {
            const FResolvedSlot Slot = ResolveSlot(MeshComponent, Surface.MaterialIndex, bSignificantOccluder);

            const EInstanceFlags Flags = BaseFlags | Slot.ExtraFlags;

            FDrawBatchKey BatchKey
            {
                .MaterialID       = Slot.MaterialID,
                .bDrawInDepthPass = (uint32)(Slot.bDrawInDepthPass ? 1u : 0u),
                .bTranslucent     = (uint32)(Slot.bTranslucent     ? 1u : 0u),
                .bMasked          = (uint32)(Slot.bMasked          ? 1u : 0u),
                .bAdditive        = (uint32)(Slot.bAdditive        ? 1u : 0u),
            };
            // Skinned bounds are bind-pose; BoundsScale handles outliers.
            const uint32 LODIndex       = ResolveSurfaceLOD(Surface, MeshComponent.ForcedLODIndex, RenderSettings.bUseLODs, DistanceOverRadius);
            const uint32 ShadowLODIndex = ResolveShadowLOD(Surface, LODIndex, RenderSettings.ShadowLODBias);

            const uint32 SurfaceMeshletCount  = MeshletHeaderAddress ? Surface.LODMeshletCount[LODIndex]       : 0u;
            const uint32 SurfaceMeshletOffset = Surface.LODMeshletOffset[LODIndex];
            const uint32 ShadowMeshletCount   = MeshletHeaderAddress ? Surface.LODMeshletCount[ShadowLODIndex] : 0u;
            const uint32 ShadowMeshletOffset  = Surface.LODMeshletOffset[ShadowLODIndex];

            const uint16 LocalBatchIdx = FindOrAddLocalBatch(Local, BatchKey, Slot);
            const uint16 LocalDrawIdx  = FindOrAddLocalDraw(Local.LocalBatches[LocalBatchIdx], FDrawKey{ Surface.StartIndex, Surface.IndexCount }, SurfaceMeshletCount);

            FProcessedDrawItem& Item = Local.Items.emplace_back();
            Item.EntityRecordIndex    = EntityRecordIdx;
            Item.SurfaceMeshletOffset = SurfaceMeshletOffset;
            Item.SurfaceMeshletCount  = SurfaceMeshletCount;
            Item.ShadowMeshletOffset  = ShadowMeshletOffset;
            Item.ShadowMeshletCount   = ShadowMeshletCount;
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

        // Bones merged serially: skinned meshes reference by absolute index.
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

        // Linear search: per-thread batch tables are tiny (tens of entries).
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
                    NewCmd.VertexShader        = LocalBatch.VertexShader;
                    NewCmd.PixelShader         = LocalBatch.PixelShader;
                    NewCmd.DepthVertexShader   = LocalBatch.DepthVertexShader;
                    NewCmd.ShadowVertexShader  = LocalBatch.ShadowVertexShader;
                    NewCmd.IndirectDrawOffset  = 0;
                    NewCmd.DrawCount           = 0;
                    NewCmd.bDrawInDepthPass    = LocalBatch.Key.bDrawInDepthPass;
                    NewCmd.bTranslucent        = LocalBatch.Key.bTranslucent;
                    NewCmd.bMasked             = LocalBatch.Key.bMasked;
                    NewCmd.bAdditive           = LocalBatch.Key.bAdditive;
                }
                LocalBatch.GlobalBatchIndex = GlobalIdx;
            }
        }

        const uint32 NumBatches = (uint32)GlobalBatchKeys.size();

        // Group LocalBatches by global batch so heavy per-batch passes fan out in parallel.
        // Pre-resize output vectors here -- parallel tasks would race on each thread's frame arena.
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

        // Per-batch GlobalDraw ranges are disjoint, so writes don't race.
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

        // Per-batch DrawCursor slices are disjoint; race-free.
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

        // Per-draw meshlet prefix sum; CullMeshlets atomically appends into per-view disjoint slices.
        uint32 MeshletRunning = 0u;
        for (uint32 d = 0; d < TotalDrawArgs; ++d)
        {
            DrawMeshletStartOffsets[d] = MeshletRunning;
            MeshletRunning            += MeshletCountsPerDraw[d];
        }
        TotalMeshletBound = MeshletRunning;

        // Each worker only touches its own Local data; in-place cursor advance needs no sync.
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

                        const uint32 GlobalBoneOffset = Entity.LocalBoneOffset != ~0u
                            ? (BoneBase + Entity.LocalBoneOffset)
                            : 0u;
                        // Bone offset packed into 16 bits w/ MaterialIndex; ~65k bone hard cap per frame.
                        DEBUG_ASSERT(GlobalBoneOffset <= 0xFFFFu);
                        const uint16 BoneOffset = (uint16)GlobalBoneOffset;

                        FGPUInstance& Out = Instances[WriteIdx];
                        Out.Transform                  = Entity.Transform;
                        Out.SphereBounds               = Entity.SphereBounds;
                        Out.VBAddress                  = 0ull;
                        Out.ShadowMeshletOffset        = Item.ShadowMeshletOffset;
                        Out.ShadowMeshletCount         = Item.ShadowMeshletCount;
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

        // Inclusive prefix sum; cull pass binary-searches to recover (instance, meshletLocal).
        {
            const size_t NumInstancesLocal = Instances.size();
            InstanceMeshletPrefix.resize(NumInstancesLocal + 1);
            uint32 RunningInstanceMeshlets = 0u;
            for (size_t i = 0; i < NumInstancesLocal; ++i)
            {
                InstanceMeshletPrefix[i] = RunningInstanceMeshlets;
                RunningInstanceMeshlets += Instances[i].SurfaceMeshletCount;
            }
            InstanceMeshletPrefix[NumInstancesLocal] = RunningInstanceMeshlets;
            // Both counts must agree (same meshlets counted differently).
            DEBUG_ASSERT(RunningInstanceMeshlets == TotalMeshletBound);
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

        // First enabled directional light wins (matches ProcessDirectionalLight).
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
            // Sweep camera frustum along sun so off-screen casters between sun and view stay.
            // Distance MUST match ShadowSweepDistance in CompileDrawCommands.
            constexpr float ShadowSweepDistance = 2000.0f;
            SceneCullContext.SunShadowFrustum = SceneCullContext.Frustum.Extruded(
                SceneCullContext.SunDirection, ShadowSweepDistance);
        }

        // Shadow-casting locals: only keep lights whose attenuation sphere intersects camera frustum.
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
        Light.Flags                 = ELightFlags::Point;
        Light.Falloff               = PointLight.Falloff;
        Light.Color                 = PackColor(glm::vec4(PointLight.LightColor, 1.0));
        Light.Intensity             = PointLight.Intensity;
        Light.Radius                = PointLight.Attenuation;
        Light.Position              = TransformComponent.WorldTransform.Location;
        Light.ShadowDataIndex       = INDEX_NONE;
        if (PointLight.bVolumetric)
        {
            Light.Flags             |= ELightFlags::Volumetric;
            Light.VolumetricIntensity = PointLight.VolumetricIntensity;
        }

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

        const glm::quat WorldRotation = TransformComponent.GetWorldRotation();
        glm::vec3 UpdatedForward    = WorldRotation * FViewVolume::ForwardAxis;
        glm::vec3 UpdatedUp         = WorldRotation * FViewVolume::UpAxis;

        float InnerDegrees = SpotLight.InnerConeAngle;
        float OuterDegrees = SpotLight.OuterConeAngle;

        float InnerCos = glm::cos(glm::radians(InnerDegrees));
        float OuterCos = glm::cos(glm::radians(OuterDegrees));

        FLight Light                = {};
        Light.Flags                 = ELightFlags::Spot;
        Light.Position              = TransformComponent.WorldTransform.Location;
        Light.Direction             = glm::normalize(UpdatedForward);
        Light.Falloff               = SpotLight.Falloff;
        Light.Color                 = PackColor(glm::vec4(SpotLight.LightColor, 1.0));
        Light.Intensity             = SpotLight.Intensity;
        Light.Radius                = SpotLight.Attenuation;
        Light.Angles                = glm::vec2(InnerCos, OuterCos);
        Light.ShadowDataIndex       = INDEX_NONE;
        if (SpotLight.bVolumetric)
        {
            Light.Flags             |= ELightFlags::Volumetric;
            Light.VolumetricIntensity = SpotLight.VolumetricIntensity;
        }

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
            Req.Direction       = -UpdatedForward;
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

        // Drop farthest shadow views first to fit GMaxCullViews
        // (camera + cascades + 6/point + 1/spot). Overflow crashes the GPU.
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

        // Point lights now consume one tile per cube face (six tiles total);
        // spot lights stay at one. Reflect that in the area accounting so the
        // budget shrink loop doesn't underestimate point-light cost.
        auto AreaCost = [&](uint32 i) -> uint64
        {
            const uint64 PerTile = (uint64)Sizes[i] * (uint64)Sizes[i];
            return ShadowRequests[i].Type == ELightType::Point ? PerTile * 6ull : PerTile;
        };

        auto AreaSum = [&]() -> uint64
        {
            uint64 S = 0;
            for (uint32 i = 0; i < NumRequests; ++i)
            {
                S += AreaCost(i);
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

            if (Req.Type == ELightType::Point)
            {
                // Each cube face gets its own 2D tile. Allocate all six up front
                // so a partial allocation doesn't leave the light half-shadowed.
                int32 FaceTileIndices[6];
                bool  bAllAllocated = true;
                for (uint32 Face = 0; Face < 6; ++Face)
                {
                    FaceTileIndices[Face] = ShadowAtlas.AllocateTile(TileSize);
                    if (FaceTileIndices[Face] == INDEX_NONE)
                    {
                        bAllAllocated = false;
                        break;
                    }
                }
                if (!bAllAllocated)
                {
                    continue;
                }

                const uint32 ShadowSlot = ShadowDataCount.fetch_add(1, std::memory_order_acquire);
                if (ShadowSlot >= (uint32)MAX_SHADOWS)
                {
                    continue;
                }

                LightData.Lights[Req.LightIndex].ShadowDataIndex = (int32)ShadowSlot;
                FLightShadowData& ShadowData = LightData.Shadows[ShadowSlot];

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

                    const FShadowTile& FaceTile = ShadowAtlas.GetTile(FaceTileIndices[Face]);

                    FLightShadow& Shadow   = ShadowData.Shadow[Face];
                    Shadow.AtlasUVOffset   = FaceTile.UVOffset;
                    Shadow.AtlasUVScale    = FaceTile.UVScale;
                    Shadow.ShadowMapIndex  = FaceTileIndices[Face];
                    Shadow.LightIndex      = (int32)Req.LightIndex;
                    Shadow.ShadowDataIndex = (int32)ShadowSlot;
                    Shadow._Padding        = 0;
                }

                PackedShadows[(uint32)ELightType::Point].push_back(ShadowData.Shadow[0]);
            }
            else // Spot
            {
                const int32 TileIndex = ShadowAtlas.AllocateTile(TileSize);
                if (TileIndex == INDEX_NONE)
                {
                    continue;
                }

                const uint32 ShadowSlot = ShadowDataCount.fetch_add(1, std::memory_order_acquire);
                if (ShadowSlot >= (uint32)MAX_SHADOWS)
                {
                    continue;
                }

                LightData.Lights[Req.LightIndex].ShadowDataIndex = (int32)ShadowSlot;
                FLightShadowData& ShadowData = LightData.Shadows[ShadowSlot];
                const FShadowTile& Tile      = ShadowAtlas.GetTile(TileIndex);

                const float ShadowNear = glm::max(Req.Attenuation * 0.01f, 0.1f);
                FViewVolume ViewVolume(Req.OuterFOVDegrees * 2.0f, 1.0f, ShadowNear, Req.Attenuation);
                ViewVolume.SetView(Req.Position, Req.Direction, Req.Up);
                ShadowData.ViewProjection[0] = ViewVolume.ToReverseDepthViewProjectionMatrix();

                FLightShadow& Shadow   = ShadowData.Shadow[0];
                Shadow.AtlasUVOffset   = Tile.UVOffset;
                Shadow.AtlasUVScale    = Tile.UVScale;
                Shadow.ShadowMapIndex  = TileIndex;
                Shadow.LightIndex      = (int32)Req.LightIndex;
                Shadow.ShadowDataIndex = (int32)ShadowSlot;
                Shadow._Padding        = 0;

                PackedShadows[(uint32)ELightType::Spot].push_back(Shadow);
            }
        }
    }

    void FForwardRenderScene::BuildCullViews(const FViewVolume& ViewVolume)
    {
        // Per-view: DrawList slice v = [v*TotalMeshletBound, (v+1)*TotalMeshletBound),
        // IndirectArgs slot (v,d) = v*NumDraws + d. CullMeshlets owns all atomic appends.
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

        // Camera-late view: phase 1 re-tests the defer list against the rebuilt HZB.
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
        Light.Flags             = ELightFlags::Directional;
        Light.Color             = PackColor(glm::vec4(DirectionalLight.Color, 1.0));
        Light.Intensity         = DirectionalLight.Intensity;
        Light.Direction         = glm::normalize(DirectionalLight.Direction);
        Light.ShadowDataIndex   = INDEX_NONE;
        LightData.SunDirection  = Light.Direction;
        if (DirectionalLight.bVolumetric)
        {
            Light.Flags             |= ELightFlags::Volumetric;
            Light.VolumetricIntensity = DirectionalLight.VolumetricIntensity;
        }

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

            // Per-cascade resolution. Outer cascades drop to a smaller
            // texture; texel-size math below reads this so the snap step
            // and world-space texel pitch shift accordingly.
            const int   CascadeRes        = GCSMCascadeSizes[i];
            const float CascadeResFloat   = (float)CascadeRes;
            LightData.CascadeResolutions[i] = CascadeResFloat;

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
            const float QuantStep = 1.0f / CascadeResFloat;
            Radius = std::ceil(Radius / QuantStep) * QuantStep;
            const float TexelSize = (Radius * 2.0f) / CascadeResFloat;

            // BackDistance pushes the light eye behind the cascade volume so
            // off-screen occluders (e.g. things directly above the cascade)
            // still write into the depth texture.
            constexpr float BackDistance = 200.0f;
            const float     OrthoRange   = Radius * 2.0f + BackDistance;

            // lookAt target = origin (not SphereCenter) so the rotation
            // depends only on LightDir; otherwise the texel snap below collapses.
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
            // Y-flip bakes Vulkan +Y-down NDC into the matrix so the cascade
            // shader samples with the same NDC->UV math as everything else.
            glm::mat4 LightProjection = glm::ortho(
                -Radius, +Radius,
                -Radius, +Radius,
                0.0f, OrthoRange);
            LightProjection[1][1] *= -1.0f;

            const glm::mat4 CascadeVP = LightProjection * LightView;
            if (CascadeShadowData)
            {
                CascadeShadowData->ViewProjection[i] = CascadeVP;

                // Atlas UV transform for this cascade. The pixel shader
                // takes the per-cascade UV in [0,1] and remaps it into the
                // packed atlas region. Origin/size come from the constant
                // packing table so the shader doesn't need to know the
                // atlas layout, just the per-cascade transform.
                FLightShadow& CascadeTile = CascadeShadowData->Shadow[i];
                CascadeTile.AtlasUVOffset = glm::vec2(
                    (float)GCSMCascadeOriginX[i] / (float)GCSMAtlasWidth,
                    (float)GCSMCascadeOriginY[i] / (float)GCSMAtlasHeight);
                CascadeTile.AtlasUVScale = glm::vec2(
                    (float)GCSMCascadeSizes[i]  / (float)GCSMAtlasWidth,
                    (float)GCSMCascadeSizes[i]  / (float)GCSMAtlasHeight);
                CascadeTile.ShadowMapIndex  = INDEX_NONE;
                CascadeTile.LightIndex      = 0;
                CascadeTile.ShadowDataIndex = (int32)ShadowSlot;
                CascadeTile._Padding        = 0;
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
        // Pull anything pushed by workers this frame into the canonical
        // arrays. This is the only place the queue is drained, and it runs
        // single-threaded on the render-extraction tick, so DrawLine /
        // RemoveLine / lifetime fix-ups below stay race-free.
        Batcher.DrainQueue();

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
        TotalMeshletBound = 0;
        NumDrawsPerView = 0;
        CameraLateViewIndex = ~0u;
        Instances.clear();
        InstanceMeshletPrefix.clear();
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
        if (DrawCommands.empty() || CullViews.empty() || TotalMeshletBound == 0u)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Cull Pass (Early)", tracy::Color::Pink2);

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

        // Flat thread-per-meshlet; workgroups beyond the 65535 X cap fold into Y.
        const uint32 NumWorkgroups = (TotalMeshletBound + 63u) / 64u;
        constexpr uint32 MaxDispatchAxis = 65535u;
        const uint32 DispatchX = NumWorkgroups < MaxDispatchAxis ? NumWorkgroups : MaxDispatchAxis;
        const uint32 DispatchY = (NumWorkgroups + MaxDispatchAxis - 1u) / MaxDispatchAxis;
        CmdList.Dispatch(DispatchX, DispatchY, 1u);
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

        // Worst-case dispatch: every camera meshlet was deferred. Same X/Y
        // fold as CullPassEarly so Vulkan's 65535 per-axis workgroup limit
        // doesn't cap us on very large scenes.
        const uint32 NumWorkgroups = (TotalMeshletBound + 63u) / 64u;
        constexpr uint32 MaxDispatchAxis = 65535u;
        const uint32 DispatchX = NumWorkgroups < MaxDispatchAxis ? NumWorkgroups : MaxDispatchAxis;
        const uint32 DispatchY = (NumWorkgroups + MaxDispatchAxis - 1u) / MaxDispatchAxis;
        CmdList.Dispatch(DispatchX, DispatchY, 1u);
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
        bool bClearDepth,
        FRHIImage* DepthResolveImage = nullptr)
    {
        FRenderPassDesc::FAttachment Depth; Depth
            .SetImage(DepthImage)
            .SetResolveImage(DepthResolveImage)
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
                // Masked materials need their own pixel shader to discard.
                // If they also use WPO, prefer the per-material depth VS so
                // the displaced + masked geometry produces matching depth.
                Desc.SetVertexShader(Batch.DepthVertexShader ? Batch.DepthVertexShader : Batch.VertexShader);
                Desc.SetPixelShader(Batch.PixelShader);
            }
            else
            {
                // WPO materials get their own depth VS (writes displaced
                // depth so [earlydepthstencil] in the base pass matches).
                FRHIVertexShader* DepthVS = Batch.DepthVertexShader ? Batch.DepthVertexShader : DepthOnlyVertexShader.GetReference();
                Desc.SetVertexShader(DepthVS);
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
            GetSceneDepthRT(),
            GetNamedImage(ENamedImage::HDR),
            SceneBindingLayout,
            SceneBindingSet,
            SceneViewportState,
            GetNamedBuffer(ENamedBuffer::IndirectArgs),
            0u,
            NumDrawsPerView,
            true,
            GetSceneDepthResolve());
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
            GetSceneDepthRT(),
            GetNamedImage(ENamedImage::HDR),
            SceneBindingLayout,
            SceneBindingSet,
            SceneViewportState,
            GetNamedBuffer(ENamedBuffer::IndirectArgs),
            CameraLateViewIndex,
            NumDrawsPerView,
            false,
            GetSceneDepthResolve());
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
        LUMINA_PROFILE_SECTION_COLORED("Point Light Shadow Pass", tracy::Color::DeepPink2);

        // The point pass owns the once-per-frame Clear of the shared 2D
        // shadow atlas. Even with no point shadows we still issue an empty
        // pass so SpotShadowPass can safely use LoadOp::Load and so stale
        // depth from prior frames doesn't leak through sampling.
        if (DrawCommands.empty() || PackedShadows[(uint32)ELightType::Point].empty())
        {
            FRenderPassDesc::FAttachment ClearDepth; ClearDepth
                .SetLoadOp(ERenderLoadOp::Clear)
                .SetDepthClearValue(1.0)
                .SetImage(ShadowAtlas.GetImage());

            FRenderPassDesc ClearPass; ClearPass
                .SetDepthAttachment(ClearDepth)
                .SetRenderArea(glm::uvec2(GShadowAtlasResolution, GShadowAtlasResolution));

            CmdList.BeginRenderPass(ClearPass);
            CmdList.EndRenderPass();
            return;
        }

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

        // Single 2D atlas: clear once, then every (light, face) pair draws
        // into its own tile via per-tile viewport/scissor. SpotShadowPass
        // re-enters the same atlas with LoadOp::Load to preserve our writes.
        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("ShadowMappingVert.slang");

        const TVector<FLightShadow>& PointShadows = PackedShadows[(uint32)ELightType::Point];

        FRenderPassDesc::FAttachment Depth; Depth
            .SetLoadOp(ERenderLoadOp::Clear)
            .SetDepthClearValue(1.0)
            .SetImage(ShadowAtlas.GetImage());

        FRenderPassDesc RenderPass; RenderPass
            .SetDepthAttachment(Depth)
            .SetRenderArea(glm::uvec2(GShadowAtlasResolution, GShadowAtlasResolution));

        // Pipeline desc minus the vertex shader; the per-batch loop picks
        // either the global shadow VS or a per-material WPO variant. The
        // pipeline cache short-circuits identical lookups.
        FGraphicsPipelineDesc DescTemplate; DescTemplate
            .SetDebugName("Point Light Shadow Pass")
            .SetRenderState(RenderState)
            .AddBindingLayout(SceneBindingLayout)
            .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
            .SetVertexShader(VertexShader)
            .SetPixelShader(PixelShader);

        for (uint32 LightIdx = 0; LightIdx < PointShadows.size(); ++LightIdx)
        {
            const FLightShadow& LightShadow = PointShadows[LightIdx];
            const uint32 ViewBase = PointShadowCullViewBases[LightIdx];
            if (ViewBase == ~0u)
            {
                continue;
            }

            const FLightShadowData& ShadowData = LightData.Shadows[LightShadow.ShadowDataIndex];

            for (int32 Face = 0; Face < 6; ++Face)
            {
                LUMINA_PROFILE_SECTION_COLORED("Point Shadow Face", tracy::Color::DeepPink2);

                const FLightShadow& FaceShadow = ShadowData.Shadow[Face];
                const FShadowTile& Tile = ShadowAtlas.GetTile(FaceShadow.ShadowMapIndex);
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

                    // Pick per-material shadow VS for WPO materials so the
                    // shadow tracks the displaced geometry instead of the
                    // un-displaced rest pose.
                    FGraphicsPipelineDesc Desc = DescTemplate;
                    if (Batch.ShadowVertexShader)
                    {
                        Desc.SetVertexShader(Batch.ShadowVertexShader);
                    }
                    FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

                    FGraphicsState GraphicsState; GraphicsState
                        .SetRenderPass(RenderPass)
                        .SetViewportState(FViewportState(Viewport, Scissor))
                        .SetPipeline(Pipeline)
                        .AddBindingSet(SceneBindingSet)
                        .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                        .SetIndirectParams(GetNamedBuffer(ENamedBuffer::IndirectArgs));

                    CmdList.SetGraphicsState(GraphicsState);
                    CmdList.SetPushConstants(&PointPush, sizeof(PointPush));
                    CmdList.DrawIndirect(Batch.DrawCount, (FaceBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
                }
            }
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
        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("ShadowMappingVert.slang");

        // See PointShadowPass for why these bias values are lower than the CSM pass.
        FRenderState RenderState; RenderState
            .SetDepthStencilState(FDepthStencilState()
                .SetDepthFunc(EComparisonFunc::Less))
                .SetRasterState(FRasterState()
                    .SetSlopeScaleDepthBias(1.5f)
                    .SetDepthBias(1)
                    .SetCullBack());


        // Render pass + pipeline are built ONCE outside the per-light loop.
        // Load (not Clear) to preserve the point-shadow tiles that
        // PointShadowPass already wrote into this same 2D atlas.
        FRenderPassDesc::FAttachment Depth; Depth
            .SetLoadOp(ERenderLoadOp::Load)
            .SetImage(ShadowAtlas.GetImage());

        FRenderPassDesc RenderPass; RenderPass
            .SetDepthAttachment(Depth)
            .SetRenderArea(glm::uvec2(GShadowAtlasResolution, GShadowAtlasResolution));
        
        // Pipeline desc reused across batches; per-batch loop swaps in a WPO
        // VS variant when the material has WorldPositionOffset connected.
        FGraphicsPipelineDesc PipelineDescTemplate; PipelineDescTemplate
            .SetDebugName("Spot Shadow Pass")
            .SetRenderState(RenderState)
            .AddBindingLayout(SceneBindingLayout)
            .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
            .SetVertexShader(VertexShader)
            .SetPixelShader(PixelShader);

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

            // ShadowMappingVert push = { int ShadowDataIndex; int ViewIndex; }.
            // Spotlights only use ViewProjection[0], so ViewIndex is 0.
            struct { int32 ShadowDataIndex; int32 ViewIndex; } SpotPush;
            SpotPush.ShadowDataIndex = Shadow.ShadowDataIndex;
            SpotPush.ViewIndex       = 0;

            const uint32 ViewBase = ViewIndex * NumDrawsPerView;
            for (uint32 OpaqueIdx : OpaqueDrawList)
            {
                const FMeshDrawCommand& Batch = DrawCommands[OpaqueIdx];

                FGraphicsPipelineDesc Desc = PipelineDescTemplate;
                if (Batch.ShadowVertexShader)
                {
                    Desc.SetVertexShader(Batch.ShadowVertexShader);
                }
                FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

                FGraphicsState GraphicsState; GraphicsState
                    .SetRenderPass(RenderPass)
                    .SetViewportState(FViewportState(Viewport, Scissor))
                    .SetPipeline(Pipeline)
                    .AddBindingSet(SceneBindingSet)
                    .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                    .SetIndirectParams(GetNamedBuffer(ENamedBuffer::IndirectArgs));

                CmdList.SetGraphicsState(GraphicsState);
                CmdList.SetPushConstants(&SpotPush, sizeof(SpotPush));
                CmdList.DrawIndirect(Batch.DrawCount, (ViewBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
            }
        }
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
            // Cascade 0's pass clears the entire atlas (depth = 1.0). The
            // outer cascades load the existing depth so their viewport
            // region overwrites only itself, leaving the inner cascade's
            // pixels and the unrendered atlas margin untouched.
            FRenderPassDesc::FAttachment Depth; Depth
                .SetLoadOp(c == 0u ? ERenderLoadOp::Clear : ERenderLoadOp::Load)
                .SetDepthClearValue(1.0f)
                .SetImage(GetNamedImage(ENamedImage::Cascade));

            FRenderPassDesc RenderPass; RenderPass
                .SetDepthAttachment(Depth)
                .SetRenderArea(glm::uvec2(GCSMAtlasWidth, GCSMAtlasHeight));

            // Per-cascade viewport: only this cascade's atlas tile rasterizes.
            // Pixel coordinates come straight from the GCSMCascadeOrigin /
            // GCSMCascadeSizes packing table. FViewport's Y range is
            // (MinY, MaxY) which matches Vulkan after the engine's flip.
            const float TileX = (float)GCSMCascadeOriginX[c];
            const float TileY = (float)GCSMCascadeOriginY[c];
            const float TileW = (float)GCSMCascadeSizes[c];

            FViewport Viewport(TileX, TileX + TileW, TileY, TileY + TileW, 0.0f, 1.0f);
            FRect     Scissor((int)TileX, (int)(TileX + TileW), (int)TileY, (int)(TileY + TileW));

            struct { int32 ShadowDataIndex; int32 CascadeIndex; } CascadePush;
            CascadePush.ShadowDataIndex = LightData.Lights[0].ShadowDataIndex;
            CascadePush.CascadeIndex    = (int32)c;

            const uint32 ViewIndex = CascadeViewBase + c;
            const uint32 ViewBase  = ViewIndex * NumDrawsPerView;

            for (uint32 OpaqueIdx : OpaqueDrawList)
            {
                const FMeshDrawCommand& Batch = DrawCommands[OpaqueIdx];

                FGraphicsPipelineDesc PerBatchDesc = Desc;
                if (Batch.ShadowVertexShader)
                {
                    PerBatchDesc.SetVertexShader(Batch.ShadowVertexShader);
                }
                FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(PerBatchDesc, RenderPass);

                FGraphicsState GraphicsState; GraphicsState
                    .SetRenderPass(RenderPass)
                    .SetViewportState(FViewportState(Viewport, Scissor))
                    .SetPipeline(Pipeline)
                    .AddBindingSet(SceneBindingSet)
                    .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                    .SetIndirectParams(GetNamedBuffer(ENamedBuffer::IndirectArgs));

                CmdList.SetGraphicsState(GraphicsState);
                CmdList.SetPushConstants(&CascadePush, sizeof(CascadePush));
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
        RenderTarget.SetImage(GetSceneColorRT()).SetResolveImage(GetSceneColorResolve());
        if (RenderSettings.bHasEnvironment)
        {
            RenderTarget.SetLoadOp(ERenderLoadOp::Load);
        }

        FRenderPassDesc::FAttachment PickerImageAttachment; PickerImageAttachment
            .SetImage(GetPickerRT())
            .SetResolveImage(GetPickerResolve());

        FRenderPassDesc::FAttachment Depth; Depth
            .SetImage(GetSceneDepthRT())
            .SetResolveImage(GetSceneDepthResolve())
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
    
        // GREATER_EQUAL: occluders re-write their pre-pass depth; non-occluders fill the rest.
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

            // GREATER_EQUAL allows non-occluder fragments to write depth atop the pre-pass.
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

            // Late phase: re-tested deferred meshlets (slice may be empty).
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

            const FResolvedParticleParams Resolved = ResolveParticleParams(*PS, Component);

            const uint32 MaxParticles = (uint32)Resolved.MaxParticles;
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

            const bool bDurationExpired = (Resolved.Duration > 0.0f) && (State.SystemAge >= Resolved.Duration);
            if (bDurationExpired)
            {
                if (Resolved.bLooping)
                {
                    State.SystemAge = fmodf(State.SystemAge, Resolved.Duration);
                    State.bBurstPending = true;
                }
            }

            const bool bEmitActive = Component.bEmit && !(bDurationExpired && !Resolved.bLooping);

            uint32 SpawnCount = 0;
            if (bEmitActive && Resolved.SpawnRate > 0.0f && Component.SpawnRateMultiplier > 0.0f)
            {
                State.SpawnAccumulator += DeltaTime * Resolved.SpawnRate * Component.SpawnRateMultiplier;
                SpawnCount = (uint32)State.SpawnAccumulator;
                State.SpawnAccumulator -= (float)SpawnCount;
            }
            else
            {
                State.SpawnAccumulator = 0.0f;
            }

            const bool bDoBurst = bEmitActive && Component.bBurstOnSpawn && State.bBurstPending && Resolved.BurstCount > 0;
            if (bDoBurst)
            {
                SpawnCount += (uint32)Resolved.BurstCount;
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

            const float InheritFactor = glm::clamp(Resolved.InheritEmitterVelocity, 0.0f, 1.0f);

            State.FrameSeed = (State.FrameSeed + 2654435761u) ^ (uint32)Entity;

            uint32 SimFlags = 0u;
            if (Resolved.bLooping)
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
            SimParams.Modes             = glm::uvec4((uint32)Resolved.Shape, (uint32)Resolved.VelocityMode, 0u, 0u);
            SimParams.ShapeSize         = glm::vec4(Resolved.ShapeSize, glm::radians(Resolved.ShapeAngle));
            SimParams.VelocityMin       = glm::vec4(Resolved.VelocityMin, 0.0f);
            SimParams.VelocityMax       = glm::vec4(Resolved.VelocityMax, 0.0f);
            SimParams.SpeedAndLifetime  = glm::vec4(Resolved.SpeedRange.x, Resolved.SpeedRange.y, Resolved.LifetimeRange.x, Resolved.LifetimeRange.y);
            SimParams.Gravity           = glm::vec4(Resolved.Gravity, Resolved.Drag);
            SimParams.StartColor        = Resolved.StartColor;
            SimParams.EndColor          = Resolved.EndColor;
            SimParams.SizeRange         = glm::vec4(Resolved.StartSizeRange.x, Resolved.StartSizeRange.y, Resolved.EndSizeRange.x, Resolved.EndSizeRange.y);
            SimParams.RotationRange     = glm::vec4(Resolved.RotationRange.x, Resolved.RotationRange.y, Resolved.RotationSpeedRange.x, Resolved.RotationSpeedRange.y);
            SimParams.NoiseStrength     = glm::vec4(Resolved.NoiseStrength, Resolved.NoiseScale);
            SimParams.NoiseParams       = glm::vec4(Resolved.NoiseSpeed, InheritFactor, 0.0f, 0.0f);
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

            const FResolvedParticleParams Resolved = ResolveParticleParams(*PS, Component);

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
            RenderParams.Flags      = glm::uvec4(TextureIndex, Resolved.bBillboardToCamera ? 1u : 0u, 0u, 0u);
            RenderParams.Tint       = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            RenderParams.UVParams   = glm::vec4(0.0f);
            CmdList.WriteBuffer(State.RenderParamsBuffer, &RenderParams, sizeof(RenderParams));

            FBlendState BlendState;
            BlendState.SetRenderTarget(0, MakeParticleBlendTarget(Resolved.BlendMode));

            FDepthStencilState DepthState;
            DepthState.SetDepthFunc(EComparisonFunc::GreaterOrEqual);
            if (Resolved.bWriteDepth)
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

        TerrainView.each([&](entt::entity Entity, STerrainComponent& Terrain, const STransformComponent& Transform)
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
                // New texture geometry implies fresh chunk bounds; force a rebuild
                // so the chunk/meshlet metadata uses the right Resolution/ChunkRes.
                State.bChunksDirty        = true;

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

            // Rebuild chunk + meshlet metadata whenever the heightmap geometry
            // shifted. The cull pass tests against tight world-space AABBs
            // computed here, so this has to fire before the next cull dispatch.
            if (State.bChunksDirty)
            {
                const glm::vec3 WorldOrigin = glm::vec3(Transform.GetWorldMatrix()[3]);
                TerrainMeshletBuilder::Build(Terrain, WorldOrigin);

                const uint32 ChunkCount   = (uint32)State.Chunks.size();
                const uint32 MeshletCount = (uint32)State.Meshlets.size();

                if (ChunkCount > 0 && MeshletCount > 0)
                {
                    const FString DebugBase = FString("Terrain_") + std::to_string((uint32)Entity).c_str();

                    auto AllocSSBO = [&](FRHIBufferRef& Buffer, uint64 SizeBytes, const FString& Name, bool bIndirect)
                    {
                        if (!Buffer || Buffer->GetDescription().Size < SizeBytes)
                        {
                            FRHIBufferDesc Desc;
                            Desc.Size      = std::max<uint64>(SizeBytes, 16ull);
                            Desc.DebugName = Name;
                            Desc.Stride    = 0u;
                            Desc.Usage.SetFlag(BUF_StorageBuffer);
                            if (bIndirect)
                            {
                                Desc.Usage.SetFlag(BUF_Indirect);
                            }
                            // bKeepInitialState pins the Common state so the
                            // automatic state tracker can move the buffer
                            // freely between SRV/UAV/Indirect at use sites.
                            Desc.bKeepInitialState = true;
                            Desc.InitialState      = EResourceStates::Common;
                            Buffer = GRenderContext->CreateBuffer(Desc);
                        }
                    };

                    AllocSSBO(State.ChunkInfoBuffer,      uint64(ChunkCount)   * sizeof(FTerrainChunkInfo),      DebugBase + "_Chunks",   false);
                    AllocSSBO(State.MeshletInfoBuffer,    uint64(MeshletCount) * sizeof(FTerrainMeshletInfo),    DebugBase + "_Meshlets", false);
                    AllocSSBO(State.VisibleMeshletBuffer, uint64(MeshletCount) * sizeof(FTerrainVisibleMeshlet), DebugBase + "_Visible",  false);
                    AllocSSBO(State.IndirectDrawBuffer,   sizeof(FDrawIndirectArguments),                        DebugBase + "_Indirect", true);

                    CmdList.WriteBuffer(State.ChunkInfoBuffer,   State.Chunks.data(),   ChunkCount * sizeof(FTerrainChunkInfo));
                    CmdList.WriteBuffer(State.MeshletInfoBuffer, State.Meshlets.data(), MeshletCount * sizeof(FTerrainMeshletInfo));

                    State.AllocatedChunkCount   = ChunkCount;
                    State.AllocatedMeshletCount = MeshletCount;
                }

                State.bChunksDirty = false;
            }
        });
    }

    void FForwardRenderScene::TerrainCullPass(ICommandList& CmdList)
    {
        FEntityRegistry& Registry = World->GetEntityRegistry();
        auto TerrainView = Registry.view<STerrainComponent, STransformComponent>(entt::exclude<SDisabledTag>);
        if (TerrainView.begin() == TerrainView.end())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Terrain Cull", tracy::Color::SeaGreen);

        FRHIComputeShaderRef CullShader = FShaderLibrary::GetComputeShader("TerrainCull.slang");
        if (!CullShader)
        {
            return;
        }

        FBindingLayoutDesc CullLayoutDesc;
        CullLayoutDesc.SetBindingIndex(2)
            .SetVisibility(ERHIShaderType::Compute)
            .AddItem(FBindingLayoutItem::Buffer_SRV(0))
            .AddItem(FBindingLayoutItem::Buffer_SRV(1))
            .AddItem(FBindingLayoutItem::Buffer_UAV(2))
            .AddItem(FBindingLayoutItem::Buffer_UAV(3));
        FRHIBindingLayout* CullLayout = BindingCache.GetOrCreateBindingLayout(CullLayoutDesc);

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.SetComputeShader(CullShader)
                    .AddBindingLayout(SceneBindingLayout)
                    .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                    .AddBindingLayout(CullLayout);
        FRHIComputePipelineRef CullPipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        TerrainView.each([&](STerrainComponent& Terrain, const STransformComponent&)
        {
            FTerrainGPUState& State = Terrain.GPUState;
            if (!State.ChunkInfoBuffer || !State.MeshletInfoBuffer || !State.VisibleMeshletBuffer || !State.IndirectDrawBuffer)
            {
                return;
            }
            if (State.AllocatedChunkCount == 0u || State.AllocatedMeshletCount == 0u)
            {
                return;
            }

            // Seed the indirect args slot. Six verts per quad * meshletQuads^2
            // is the per-meshlet vertex count the terrain VS expects.
            FDrawIndirectArguments InitialArgs{};
            InitialArgs.VertexCount           = (uint32)(GTerrainMeshletMaxQuads * 6);
            InitialArgs.InstanceCount         = 0u;
            InitialArgs.StartVertexLocation   = 0u;
            InitialArgs.StartInstanceLocation = 0u;
            CmdList.WriteBuffer(State.IndirectDrawBuffer, &InitialArgs, sizeof(InitialArgs));

            FBindingSetDesc CullSetDesc;
            CullSetDesc
                .AddItem(FBindingSetItem::BufferSRV(0, State.ChunkInfoBuffer))
                .AddItem(FBindingSetItem::BufferSRV(1, State.MeshletInfoBuffer))
                .AddItem(FBindingSetItem::BufferUAV(2, State.VisibleMeshletBuffer))
                .AddItem(FBindingSetItem::BufferUAV(3, State.IndirectDrawBuffer));
            FRHIBindingSetRef CullSet = GRenderContext->CreateBindingSet(CullSetDesc, CullLayout);

            FComputeState ComputeState;
            ComputeState.SetPipeline(CullPipeline)
                        .AddBindingSet(SceneBindingSet)
                        .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                        .AddBindingSet(CullSet);
            CmdList.SetComputeState(ComputeState);

            FTerrainCullPushConstants Push{};
            Push.ChunkCount   = State.AllocatedChunkCount;
            Push.MeshletCount = State.AllocatedMeshletCount;
            CmdList.SetPushConstants(&Push, sizeof(Push));

            // One workgroup per chunk, one thread per meshlet inside that
            // chunk. The shader's chunk-level test fires on thread 0 and
            // gates every other thread via groupshared, so dispatch sizes
            // up to MeshletsPerChunk (default 81) without wasting threads.
            CmdList.Dispatch(State.AllocatedChunkCount, 1u, 1u);
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
            // Cull pass output is what makes the indirect draw legal; bail
            // when it hasn't been built yet (first frame, or heightmap dirty
            // but TerrainUpdatePass has not run since).
            if (!State.ChunkInfoBuffer || !State.MeshletInfoBuffer || !State.VisibleMeshletBuffer || !State.IndirectDrawBuffer)
            {
                return;
            }
            if (State.AllocatedMeshletCount == 0u)
            {
                return;
            }


            // Strict material gate. The terrain pipeline binds set 2 to the
            // heightmap / layer-weight / chunk-meshlet resources, which is
            // only valid against a TERRAIN-compiled shader. A user-assigned
            // material that isn't a Terrain material would point at a shader
            // built for the mesh binding layout, so the descriptor set would
            // bind to the wrong slots -- skip rather than render garbage.
            CMaterialInterface* MaterialInterface = Terrain.Material.Get();
            if (MaterialInterface && MaterialInterface->GetMaterialType() != EMaterialType::Terrain)
            {
                return;
            }
            if (!MaterialInterface || !MaterialInterface->IsReadyForRender())
            {
                MaterialInterface = CMaterial::GetDefaultTerrainMaterial();
            }
            if (!MaterialInterface || !MaterialInterface->IsReadyForRender())
            {
                return;
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

            const int32 QuadsPerChunk        = std::max(1, Terrain.ChunkResolution - 1);
            const int32 ChunksPerSide        = std::max(1, ((int32)Res - 1) / QuadsPerChunk);
            const int32 MeshletsPerChunkSide = (QuadsPerChunk + GTerrainMeshletQuads - 1) / GTerrainMeshletQuads;

            FTerrainRenderParams RenderParams{};
            RenderParams.OriginXZ             = glm::vec2(WorldOrigin.x - HalfSize, WorldOrigin.z - HalfSize);
            RenderParams.TileWorldSize        = Terrain.TileWorldSize;
            RenderParams.MaxHeight            = Terrain.MaxHeight;
            RenderParams.Resolution           = (int32)Res;
            RenderParams.ChunkResolution      = Terrain.ChunkResolution;
            RenderParams.ChunksPerSide        = ChunksPerSide;
            RenderParams.LayerCount           = (int32)Terrain.Layers.size();
            RenderParams.WorldOriginY         = glm::vec3(WorldOrigin.y, 0.0f, 0.0f);
            RenderParams.EntityID             = (uint32)Entity;
            RenderParams.MaterialIndex        = (uint32)std::max(MaterialInterface->GetMaterialIndex(), 0);
            RenderParams.MeshletsPerChunkSide = MeshletsPerChunkSide;
            RenderParams.MeshletQuadSide      = GTerrainMeshletQuads;

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
            RenderTarget.SetImage(GetSceneColorRT()).SetResolveImage(GetSceneColorResolve());
            if (bHDRWasWritten)
            {
                RenderTarget.SetLoadOp(ERenderLoadOp::Load);
            }

            FRenderPassDesc::FAttachment PickerAttachment;
            PickerAttachment.SetImage(GetPickerRT()).SetResolveImage(GetPickerResolve());
            if (!DrawCommands.empty())
            {
                PickerAttachment.SetLoadOp(ERenderLoadOp::Load);
            }

            FRenderPassDesc::FAttachment Depth;
            Depth.SetImage(GetSceneDepthRT())
                 .SetResolveImage(GetSceneDepthResolve())
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

            // Bind set 2 = terrain-local resources. Slots 0..3 are the same
            // texture/CB layout the legacy direct draw used; 4..6 are the
            // chunk + meshlet metadata + cull-output draw list the new
            // indirect path consumes via SV_InstanceID.
            FBindingLayoutDesc TerrainLayoutDesc;
            TerrainLayoutDesc.SetBindingIndex(2)
                .SetVisibility(ERHIShaderType::Vertex)
                .SetVisibility(ERHIShaderType::Fragment)
                .AddItem(FBindingLayoutItem::Texture_SRV(0))
                .AddItem(FBindingLayoutItem::Texture_SRV(1))
                .AddItem(FBindingLayoutItem::Texture_SRV(2))
                .AddItem(FBindingLayoutItem::Buffer_CBV(3))
                .AddItem(FBindingLayoutItem::Buffer_SRV(4))
                .AddItem(FBindingLayoutItem::Buffer_SRV(5))
                .AddItem(FBindingLayoutItem::Buffer_SRV(6));
            FRHIBindingLayout* TerrainLayout = BindingCache.GetOrCreateBindingLayout(TerrainLayoutDesc);

            FBindingSetDesc TerrainSetDesc;
            TerrainSetDesc.AddItem(FBindingSetItem::TextureSRV(0, State.HeightmapTexture, HeightSampler))
                          .AddItem(FBindingSetItem::TextureSRV(1, State.NormalTexture,    HeightSampler))
                          .AddItem(FBindingSetItem::TextureSRV(2, State.LayerWeightTexture, HeightSampler))
                          .AddItem(FBindingSetItem::BufferCBV(3, RenderCB))
                          .AddItem(FBindingSetItem::BufferSRV(4, State.ChunkInfoBuffer))
                          .AddItem(FBindingSetItem::BufferSRV(5, State.MeshletInfoBuffer))
                          .AddItem(FBindingSetItem::BufferSRV(6, State.VisibleMeshletBuffer));
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
                .AddBindingSet(TerrainSet)
                .SetIndirectParams(State.IndirectDrawBuffer);

            CmdList.SetGraphicsState(GraphicsState);

            // The cull pass populated the single FDrawIndirectArguments slot
            // with VertexCount = MeshletQuads^2 * 6 and InstanceCount =
            // (number of surviving meshlets). One draw, GPU-driven.
            CmdList.DrawIndirect(1u, 0u);
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

        FBlendState BlendState; BlendState
            .Targets[0]
                .SetBlendEnable(true)
                .SetSrcBlend(EBlendFactor::SrcAlpha)
                .SetDestBlend(EBlendFactor::OneMinusSrcAlpha)
                .SetBlendOp(EBlendOp::Add)
                .SetSrcBlendAlpha(EBlendFactor::One)
                .SetDestBlendAlpha(EBlendFactor::OneMinusSrcAlpha)
                .SetBlendOpAlpha(EBlendOp::Add);

        FRenderState RenderState; RenderState
            .SetDepthStencilState(DepthState)
            .SetBlendState(BlendState);

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

    namespace
    {
        // Mirror of VolumetricLighting.slang::FPushConstants. Kept under
        // the 128-byte Vulkan minimum guarantee.
        struct FVolumetricLightingPushConstants
        {
            uint32   NumSteps;          // Ray-march sample count along the camera ray.
            float    MaxDistance;       // Hard cap on march length (meters); shorter = cheaper, less far fog.
            float    Density;           // Participating-media density. Scales overall scattering strength.
            float    Anisotropy;        // Henyey-Greenstein g in [-1, 1]. Positive = forward scatter (sunbeam look).
            uint32   ScreenSize[2];
            float    InvScreenSize[2];
            float    Time;              // For temporal jitter rotation.
            float    _Pad0;
            float    _Pad1;
            float    _Pad2;
        };
        static_assert(sizeof(FVolumetricLightingPushConstants) <= 128,
            "Volumetric push constants must stay within the 128B Vulkan minimum.");
    }

    void FForwardRenderScene::VolumetricLightingPass(ICommandList& CmdList)
    {
        // Cheap CPU-side early-out: skip the whole pass when no light has the
        // volumetric flag. The walk over LightData.Lights is O(NumLights) but
        // NumLights is typically a handful and saves a fullscreen ray-march.
        bool bAnyVolumetric = false;
        for (uint32 i = 0; i < LightData.NumLights; ++i)
        {
            if (EnumHasAnyFlags(LightData.Lights[i].Flags, ELightFlags::Volumetric))
            {
                bAnyVolumetric = true;
                break;
            }
        }
        if (!bAnyVolumetric)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Volumetric Lighting Pass", tracy::Color::Orange3);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef  PixelShader  = FShaderLibrary::GetPixelShader("VolumetricLighting.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        // Additive blend into HDR. The opaque + transparent scene already lives
        // there; volumetric scattering is a separable additive term so we just
        // accumulate on top without disturbing the existing color.
        FBlendState::RenderTarget Blend0;
        Blend0.SetBlendEnable(true);
        Blend0.SetSrcBlend(EBlendFactor::One);
        Blend0.SetDestBlend(EBlendFactor::One);
        Blend0.SetBlendOp(EBlendOp::Add);
        Blend0.SetSrcBlendAlpha(EBlendFactor::One);
        Blend0.SetDestBlendAlpha(EBlendFactor::One);
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

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest();
        DepthState.DisableDepthWrite();

        FRenderState RenderState;
        RenderState.SetRasterState(RasterState);
        RenderState.SetBlendState(BlendState);
        RenderState.SetDepthStencilState(DepthState);

        // Set 2: scene depth as SRV. The base pass + terrain finished writing
        // depth before this point, so reading it here picks up every opaque
        // surface. Transparent fragments don't write depth (WBOIT) so they
        // don't occlude fog -- desirable for the typical "glass + light shaft"
        // case.
        FBindingLayoutDesc DepthLayoutDesc;
        DepthLayoutDesc.SetBindingIndex(2)
            .SetVisibility(ERHIShaderType::Fragment)
            .AddItem(FBindingLayoutItem::Texture_SRV(0));
        FRHIBindingLayout* DepthLayout = BindingCache.GetOrCreateBindingLayout(DepthLayoutDesc);

        FRHISamplerRef PointClamp = TStaticRHISampler<false, false, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

        FBindingSetDesc DepthSetDesc;
        DepthSetDesc.AddItem(FBindingSetItem::TextureSRV(0, GetNamedImage(ENamedImage::DepthAttachment), PointClamp));
        FRHIBindingSetRef DepthSet = GRenderContext->CreateBindingSet(DepthSetDesc, DepthLayout);

        FGraphicsPipelineDesc Desc;
        Desc.SetDebugName("Volumetric Lighting Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(SceneBindingLayout);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.AddBindingLayout(DepthLayout);
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.AddBindingSet(SceneBindingSet);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        GraphicsState.AddBindingSet(DepthSet);
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(SceneViewportState);

        CmdList.SetGraphicsState(GraphicsState);

        FRHIImage* HDR = GetNamedImage(ENamedImage::HDR);
        FVolumetricLightingPushConstants PC = {};
        PC.NumSteps         = 32;
        PC.MaxDistance      = 200.0f;
        PC.Density          = 0.4f;
        PC.Anisotropy       = 0.6f;
        PC.ScreenSize[0]    = HDR->GetSizeX();
        PC.ScreenSize[1]    = HDR->GetSizeY();
        PC.InvScreenSize[0] = 1.0f / (float)HDR->GetSizeX();
        PC.InvScreenSize[1] = 1.0f / (float)HDR->GetSizeY();
        PC.Time             = SceneGlobalData.Time;

        CmdList.SetPushConstants(&PC, sizeof(PC));
        CmdList.Draw(3, 1, 0, 0);
    }

    // 5 mips: roughness M/(NumMips-1) = {0, 0.25, 0.5, 0.75, 1.0}. Standard PBR choice.
    static constexpr uint32 GSkyPrefilterMipCount = 5;
    // GGX samples per prefilter texel. 256 is the readable/cost compromise vs Karis 1024.
    static constexpr uint32 GPrefilterSampleCount = 256;

    void FForwardRenderScene::SkyCubeCapturePass(ICommandList& CmdList)
    {
        if (!RenderSettings.bHasEnvironment)
        {
            return;
        }

        if (!bIBLDirty)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Sky Cube Capture", tracy::Color::SkyBlue);

        FRHIImage* SkyCube = GetNamedImage(ENamedImage::SkyCube);
        if (!SkyCube)
        {
            return;
        }

        // HDRI path: equirect->cube replaces the procedural fill.
        if (EnvironmentMapImage != nullptr)
        {
            FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("EquirectToCubemap.slang");
            if (!ComputeShader)
            {
                return;
            }

            FBindingLayoutDesc LayoutDesc;
            LayoutDesc.AddItem(FBindingLayoutItem::Texture_SRV(0));
            LayoutDesc.AddItem(FBindingLayoutItem::Texture_UAV(1));
            LayoutDesc.SetVisibility(ERHIShaderType::Compute);
            FRHIBindingLayout* Layout = BindingCache.GetOrCreateBindingLayout(LayoutDesc);

            FComputePipelineDesc PipelineDesc;
            PipelineDesc.AddBindingLayout(Layout);
            PipelineDesc.CS = ComputeShader;
            PipelineDesc.DebugName = "Equirect To Cubemap";
            FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

            // Linear-clamp sampler. atan2/asin in the shader can produce UVs
            // a hair past 1.0 from numerical noise at the wrap seam; clamp
            // keeps the sample on the texture instead of wrapping into the
            // wrong hemisphere.
            FRHISamplerRef LinearClamp = TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

            FBindingSetDesc SetDesc;
            SetDesc.AddItem(FBindingSetItem::TextureSRV(
                0, EnvironmentMapImage, LinearClamp, EnvironmentMapImage->GetFormat(),
                FTextureSubresourceSet(0, 1, 0, 1),
                EImageDimension::Texture2D));
            SetDesc.AddItem(FBindingSetItem::TextureUAV(
                1, SkyCube, SkyCube->GetFormat(),
                FTextureSubresourceSet(0, 1, 0, FTextureSubresourceSet::AllArraySlices),
                EImageDimension::Texture2DArray));
            FRHIBindingSet* Set = BindingCache.GetOrCreateBindingSet(SetDesc, Layout);

            FComputeState State;
            State.AddBindingSet(Set);
            State.SetPipeline(Pipeline);
            CmdList.SetComputeState(State);

            constexpr uint32 EquirectTile = 8u;
            const uint32 FaceSize = SkyCube->GetSizeX();
            const uint32 GroupsXY = RenderUtils::GetGroupCount(FaceSize, EquirectTile);
            CmdList.Dispatch(GroupsXY, GroupsXY, 6u);
            return;
        }

        FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("SkyCubeCapture.slang");
        if (!ComputeShader)
        {
            return;
        }

        // Cube bound as Texture2DArray UAV (6 layers) for write; IBL passes sample as TextureCube.
        FBindingLayoutDesc LayoutDesc;
        LayoutDesc.AddItem(FBindingLayoutItem::Texture_UAV(0));
        LayoutDesc.AddItem(FBindingLayoutItem::Buffer_CBV(1));
        LayoutDesc.SetVisibility(ERHIShaderType::Compute);
        FRHIBindingLayout* Layout = BindingCache.GetOrCreateBindingLayout(LayoutDesc);

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(Layout);
        PipelineDesc.CS = ComputeShader;
        PipelineDesc.DebugName = "Sky Cube Capture";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FBindingSetDesc SetDesc;
        SetDesc.AddItem(FBindingSetItem::TextureUAV(
            0, SkyCube, SkyCube->GetFormat(),
            FTextureSubresourceSet(0, 1, 0, FTextureSubresourceSet::AllArraySlices),
            EImageDimension::Texture2DArray));
        SetDesc.AddItem(FBindingSetItem::BufferCBV(1, GetNamedBuffer(ENamedBuffer::Environment)));
        FRHIBindingSet* Set = BindingCache.GetOrCreateBindingSet(SetDesc, Layout);

        FComputeState State;
        State.AddBindingSet(Set);
        State.SetPipeline(Pipeline);
        CmdList.SetComputeState(State);

        struct FSkyCapturePC
        {
            glm::vec3 SunDirection;
            float     Time;
        } PC = {};

        // Same source the EnvironmentPass uses (LightData.SunDirection points
        // FROM surface TO sun, set up wherever the directional light is
        // processed). Falls back to a sensible day-time direction if no sun
        // is present so the cubemap still has structure for IBL.
        if (LightData.bHasSun)
        {
            PC.SunDirection = glm::normalize(LightData.SunDirection);
        }
        else
        {
            PC.SunDirection = glm::normalize(glm::vec3(0.3f, 0.8f, 0.4f));
        }
        // Star twinkle samples Time -- the cube only re-bakes when env params
        // or sun direction change, so this freezes for IBL purposes (stars
        // get blurred to nothing in the convolution anyway).
        PC.Time = SceneGlobalData.Time;
        CmdList.SetPushConstants(&PC, sizeof(PC));

        constexpr uint32 SkyCaptureTile = 8u;
        const uint32 FaceSize = SkyCube->GetSizeX();
        const uint32 GroupsXY = RenderUtils::GetGroupCount(FaceSize, SkyCaptureTile);
        // Z = 6 layers, one per cube face -- each thread owns one (face, x, y).
        CmdList.Dispatch(GroupsXY, GroupsXY, 6u);
    }

    void FForwardRenderScene::IrradianceConvolutionPass(ICommandList& CmdList)
    {
        if (!RenderSettings.bHasEnvironment)
        {
            return;
        }

        // Skip the 1024-sample-per-texel convolution when the source cube
        // is unchanged from last frame -- the irradiance cube is persistent.
        if (!bIBLDirty)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Sky Irradiance Convolution", tracy::Color::SkyBlue1);

        FRHIImage* SkyCube      = GetNamedImage(ENamedImage::SkyCube);
        FRHIImage* IrradianceCube = GetNamedImage(ENamedImage::SkyIrradiance);
        if (!SkyCube || !IrradianceCube)
        {
            return;
        }

        FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("IrradianceConvolution.slang");
        if (!ComputeShader)
        {
            return;
        }

        FBindingLayoutDesc LayoutDesc;
        LayoutDesc.AddItem(FBindingLayoutItem::Texture_SRV(0));
        LayoutDesc.AddItem(FBindingLayoutItem::Texture_UAV(1));
        LayoutDesc.SetVisibility(ERHIShaderType::Compute);
        FRHIBindingLayout* Layout = BindingCache.GetOrCreateBindingLayout(LayoutDesc);

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(Layout);
        PipelineDesc.CS = ComputeShader;
        PipelineDesc.DebugName = "Sky Irradiance Convolution";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        // Linear-clamp cube sampler. Clamp matches Vulkan cubemap edge
        // sampling rules and avoids any face-edge bleed on the input cube.
        FRHISamplerRef LinearClamp = TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

        FBindingSetDesc SetDesc;
        // SkyCube is bound as a TextureCube SRV: same image, different view
        // type from the SkyCubeCapturePass UAV write earlier this frame.
        // Auto-barriers transition between the two states.
        SetDesc.AddItem(FBindingSetItem::TextureSRV(
            0, SkyCube, LinearClamp, SkyCube->GetFormat(),
            FTextureSubresourceSet(0, 1, 0, FTextureSubresourceSet::AllArraySlices),
            EImageDimension::TextureCube));
        SetDesc.AddItem(FBindingSetItem::TextureUAV(
            1, IrradianceCube, IrradianceCube->GetFormat(),
            FTextureSubresourceSet(0, 1, 0, FTextureSubresourceSet::AllArraySlices),
            EImageDimension::Texture2DArray));
        FRHIBindingSet* Set = BindingCache.GetOrCreateBindingSet(SetDesc, Layout);

        FComputeState State;
        State.AddBindingSet(Set);
        State.SetPipeline(Pipeline);
        CmdList.SetComputeState(State);

        constexpr uint32 IrradianceTile = 8u;
        const uint32 FaceSize = IrradianceCube->GetSizeX();
        const uint32 GroupsXY = RenderUtils::GetGroupCount(FaceSize, IrradianceTile);
        CmdList.Dispatch(GroupsXY, GroupsXY, 6u);
    }

    void FForwardRenderScene::PrefilterEnvMapPass(ICommandList& CmdList)
    {
        if (!RenderSettings.bHasEnvironment)
        {
            return;
        }

        // Same dirty gate as the irradiance pass -- the prefilter cube
        // outlives a single frame, so re-running 256 GGX samples per
        // texel per mip is wasted when the source cube hasn't moved.
        if (!bIBLDirty)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Sky Prefilter Convolution", tracy::Color::SkyBlue2);

        FRHIImage* SkyCube       = GetNamedImage(ENamedImage::SkyCube);
        FRHIImage* PrefilterCube = GetNamedImage(ENamedImage::SkyPrefilter);
        if (!SkyCube || !PrefilterCube)
        {
            return;
        }

        FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("PrefilterEnvMap.slang");
        if (!ComputeShader)
        {
            return;
        }

        FBindingLayoutDesc LayoutDesc;
        LayoutDesc.AddItem(FBindingLayoutItem::Texture_SRV(0));
        LayoutDesc.AddItem(FBindingLayoutItem::Texture_UAV(1));
        LayoutDesc.SetVisibility(ERHIShaderType::Compute);
        FRHIBindingLayout* Layout = BindingCache.GetOrCreateBindingLayout(LayoutDesc);

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(Layout);
        PipelineDesc.CS = ComputeShader;
        PipelineDesc.DebugName = "Sky Prefilter Convolution";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FRHISamplerRef LinearClamp = TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

        struct FPrefilterPC
        {
            float  Roughness;
            uint32 NumSamples;
            uint32 _Pad0;
            uint32 _Pad1;
        };

        const uint32 NumMips     = (uint32)PrefilterCube->GetDescription().NumMips;
        const uint32 BaseFaceSize = PrefilterCube->GetSizeX();

        constexpr uint32 PrefilterTile = 8u;

        // One dispatch per mip, each writing through a UAV view of just that
        // mip. Roughness is uniform across the dispatch (same for every
        // texel of every face at this mip), threaded in via push constant.
        for (uint32 Mip = 0; Mip < NumMips; ++Mip)
        {
            FBindingSetDesc SetDesc;
            SetDesc.AddItem(FBindingSetItem::TextureSRV(
                0, SkyCube, LinearClamp, SkyCube->GetFormat(),
                FTextureSubresourceSet(0, 1, 0, FTextureSubresourceSet::AllArraySlices),
                EImageDimension::TextureCube));
            SetDesc.AddItem(FBindingSetItem::TextureUAV(
                1, PrefilterCube, PrefilterCube->GetFormat(),
                FTextureSubresourceSet(Mip, 1, 0, FTextureSubresourceSet::AllArraySlices),
                EImageDimension::Texture2DArray));
            FRHIBindingSet* Set = BindingCache.GetOrCreateBindingSet(SetDesc, Layout);

            FComputeState State;
            State.AddBindingSet(Set);
            State.SetPipeline(Pipeline);
            CmdList.SetComputeState(State);

            FPrefilterPC PC = {};
            // Even spacing of roughness across mips: mip 0 is mirror, last
            // mip is fully rough. Mapping matches the runtime mip select
            // SamplePrefilter() will use.
            PC.Roughness  = (NumMips <= 1u) ? 0.0f
                                            : (float)Mip / (float)(NumMips - 1u);
            PC.NumSamples = GPrefilterSampleCount;
            CmdList.SetPushConstants(&PC, sizeof(PC));

            const uint32 MipFaceSize = eastl::max<uint32>(BaseFaceSize >> Mip, 1u);
            const uint32 GroupsXY    = RenderUtils::GetGroupCount(MipFaceSize, PrefilterTile);
            CmdList.Dispatch(GroupsXY, GroupsXY, 6u);
        }
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
            .SetImage(GetSceneColorRT())
            .SetResolveImage(GetSceneColorResolve());

        FRenderPassDesc RenderPass; RenderPass
            .AddColorAttachment(Attachment)
            .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());

        FRasterState RasterState;
        RasterState.SetCullNone();

        FRenderState RenderState;
        RenderState.SetRasterState(RasterState);

        // Set 2: the env params CB at slot 0, plus the SkyCube at slot 1
        // for SKY_MODE_HDRI to sample by view direction. SkyCube is filled
        // earlier this frame by SkyCubeCapturePass -- auto-barriers handle
        // the UAV->SRV transition so the env shader sees coherent texels.
        FBindingLayoutDesc EnvLayoutDesc;
        EnvLayoutDesc.SetBindingIndex(2)
            .SetVisibility(ERHIShaderType::Fragment)
            .AddItem(FBindingLayoutItem::Buffer_CBV(0))
            .AddItem(FBindingLayoutItem::Texture_SRV(1));
        FRHIBindingLayout* EnvLayout = BindingCache.GetOrCreateBindingLayout(EnvLayoutDesc);

        FRHIImage* SkyCube = GetNamedImage(ENamedImage::SkyCube);
        FRHISamplerRef LinearClamp = TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

        FBindingSetDesc EnvSetDesc;
        EnvSetDesc.AddItem(FBindingSetItem::BufferCBV(0, GetNamedBuffer(ENamedBuffer::Environment)));
        EnvSetDesc.AddItem(FBindingSetItem::TextureSRV(
            1, SkyCube, LinearClamp, SkyCube->GetFormat(),
            FTextureSubresourceSet(0, 1, 0, FTextureSubresourceSet::AllArraySlices),
            EImageDimension::TextureCube));
        FRHIBindingSet* EnvSet = BindingCache.GetOrCreateBindingSet(EnvSetDesc, EnvLayout);

        FGraphicsPipelineDesc Desc;
        Desc.SetDebugName("Environment Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(SceneBindingLayout);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.AddBindingLayout(EnvLayout);
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.AddBindingSet(SceneBindingSet);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        GraphicsState.AddBindingSet(EnvSet);
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

    namespace
    {
        // Mirror of ColorGrading.slang::FPushConstants. 128 bytes, std430-style
        // packing: scalar groups are aligned in 16-byte chunks then a run of
        // float4s. Keep the field order in lockstep with the shader.
        struct FColorGradingPushConstants
        {
            float    Exposure;
            float    Contrast;
            float    Saturation;
            float    Gamma;

            float    WhiteTemp;
            float    WhiteTint;
            float    VignetteIntensity;
            float    VignetteSmoothness;

            float    VignetteRoundness;
            uint32   TonemapMode;
            float    Time;
            float    BloomIntensity;     // 0 disables the bloom composite path in the shader.

            glm::vec4 ColorFilter;

            // .a slots carry film-grain knobs (Shadows.a=Intensity, Midtones.a=Size,
            // Highlights.a=Response) -- avoids growing past Vulkan's 128B push guarantee.
            glm::vec4 Shadows;
            glm::vec4 Midtones;
            glm::vec4 Highlights;
            glm::vec4 VignetteColor;

            // .rgb = bloom tint, .a = chromatic aberration intensity.
            glm::vec4 BloomTint;
        };
        static_assert(sizeof(FColorGradingPushConstants) == 144,
            "ColorGrading push constants stay within the 256B push-constant tier.");

        // Build a fully-defaulted constants block (identity grade, AGX tone
        // mapping). Used when the active camera has no post-process or
        // grading is globally disabled.
        FColorGradingPushConstants MakeDefaultColorGrading(float Time)
        {
            FColorGradingPushConstants PC{};
            PC.Exposure           = 1.0f;
            PC.Contrast           = 1.0f;
            PC.Saturation         = 1.0f;
            PC.Gamma              = 1.0f;
            PC.WhiteTemp          = 0.0f;
            PC.WhiteTint          = 0.0f;
            PC.VignetteIntensity  = 0.0f;
            PC.VignetteSmoothness = 0.5f;
            PC.VignetteRoundness  = 1.0f;
            PC.TonemapMode        = (uint32)EToneMapper::AGX;
            PC.Time               = Time;
            PC.BloomIntensity     = 0.0f;
            PC.ColorFilter        = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            PC.Shadows            = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
            PC.Midtones           = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
            PC.Highlights         = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
            PC.VignetteColor      = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            PC.BloomTint          = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
            return PC;
        }

        FColorGradingPushConstants BuildColorGradingConstants(const SPostProcessSettings* Settings, float Time)
        {
            if (Settings == nullptr || !Settings->bEnabled)
            {
                return MakeDefaultColorGrading(Time);
            }

            FColorGradingPushConstants PC{};
            // ExposureCompensation is in stops; 2^EV gives the linear
            // multiplier the shader expects.
            PC.Exposure           = std::exp2(Settings->ExposureCompensation);
            PC.Contrast           = Settings->Contrast;
            PC.Saturation         = Settings->Saturation;
            PC.Gamma              = Settings->Gamma;
            PC.WhiteTemp          = Settings->Temperature;
            PC.WhiteTint          = Settings->Tint;
            PC.VignetteIntensity  = Settings->VignetteIntensity;
            PC.VignetteSmoothness = Settings->VignetteSmoothness;
            PC.VignetteRoundness  = Settings->VignetteRoundness;
            PC.TonemapMode        = (uint32)Settings->ToneMapper;
            PC.Time               = Time;
            PC.BloomIntensity     = Settings->BloomIntensity;
            PC.ColorFilter        = glm::vec4(Settings->ColorFilter, Settings->ColorFilterIntensity);
            PC.Shadows            = glm::vec4(Settings->Shadows,    Settings->FilmGrainIntensity);
            PC.Midtones           = glm::vec4(Settings->Midtones,   std::max(Settings->FilmGrainSize, 0.0001f));
            PC.Highlights         = glm::vec4(Settings->Highlights, Settings->FilmGrainResponse);
            PC.VignetteColor      = glm::vec4(Settings->VignetteColor, 0.0f);
            PC.BloomTint          = glm::vec4(Settings->BloomTint, Settings->ChromaticAberration);
            return PC;
        }
    }

    namespace
    {
        // Push constants for BloomDownsample.slang. 32 B (well under the
        // 128B Vulkan minimum) -- a single 16-byte scalar block plus a
        // 16-byte vec3+pad block.
        struct FBloomDownPushConstants
        {
            glm::vec2 SrcTexelSize;
            float     Threshold;
            uint32    bIsPrefilter;

            glm::vec3 KneeCurve;
            float     _Pad0;
        };
        static_assert(sizeof(FBloomDownPushConstants) == 32,
            "FBloomDownPushConstants must match BloomDownsample.slang::FPushConstants.");

        // Push constants for BloomUpsample.slang. 16 B.
        struct FBloomUpPushConstants
        {
            glm::vec2 SrcTexelSize;
            float     Radius;
            float     _Pad0;
        };
        static_assert(sizeof(FBloomUpPushConstants) == 16,
            "FBloomUpPushConstants must match BloomUpsample.slang::FPushConstants.");
    }

    void FForwardRenderScene::BloomPass(ICommandList& CmdList)
    {
        // Cheap early-out. Skipping when bloom is disabled keeps the whole
        // mip chain off the GPU; the tone map shader's BloomIntensity > 0
        // branch additionally prevents it from sampling stale uBloom
        // contents on the very first frame after disable.
        if (ActivePostProcess == nullptr || !ActivePostProcess->bEnabled || ActivePostProcess->BloomIntensity <= 0.0f)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Bloom Pass", tracy::Color::Yellow3);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef  DownPS       = FShaderLibrary::GetPixelShader("BloomDownsample.slang");
        FRHIPixelShaderRef  UpPS         = FShaderLibrary::GetPixelShader("BloomUpsample.slang");
        if (!VertexShader || !DownPS || !UpPS)
        {
            return;
        }

        FRHISamplerRef LinearClamp = TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

        // Set 0, binding 0: the source mip / HDR scene color, sampled with
        // a linear-clamp combined sampler so the 13-tap downsample picks
        // up bilinear taps without manual cross-fetching.
        FBindingLayoutDesc SrcLayoutDesc;
        SrcLayoutDesc.SetBindingIndex(0)
            .SetVisibility(ERHIShaderType::Fragment)
            .AddItem(FBindingLayoutItem::Texture_SRV(0));
        FRHIBindingLayout* SrcLayout = BindingCache.GetOrCreateBindingLayout(SrcLayoutDesc);

        // Knee maps the SoftKnee 0..1 dial onto an actual knee width in HDR
        // units. The published Jimenez form uses (Threshold - Knee, 2*Knee,
        // 0.25 / Knee); precomputing the three terms turns the per-pixel
        // soft threshold into a single max + saturate + multiply.
        const float Threshold = ActivePostProcess->BloomThreshold;
        const float Knee      = ActivePostProcess->BloomSoftKnee * Threshold + 1e-5f;
        const glm::vec3 KneeCurve(Threshold - Knee, 2.0f * Knee, 0.25f / Knee);

        FRasterState RasterState;
        RasterState.SetCullNone();

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest();
        DepthState.DisableDepthWrite();

        // Additive blend used by the upsample chain. Each upsample writes
        // its 3x3 tent-filtered contribution on top of the larger mip the
        // downsample chain already populated, producing the smooth halo.
        FBlendState::RenderTarget AdditiveTarget;
        AdditiveTarget.SetBlendEnable(true);
        AdditiveTarget.SetSrcBlend(EBlendFactor::One);
        AdditiveTarget.SetDestBlend(EBlendFactor::One);
        AdditiveTarget.SetBlendOp(EBlendOp::Add);
        AdditiveTarget.SetSrcBlendAlpha(EBlendFactor::One);
        AdditiveTarget.SetDestBlendAlpha(EBlendFactor::One);
        AdditiveTarget.SetBlendOpAlpha(EBlendOp::Add);
        FBlendState AdditiveBlend;
        AdditiveBlend.SetRenderTarget(0, AdditiveTarget);

        auto RecordPass = [&](FRHIImage* Src, FRHIImage* Dst, FRHIPixelShader* PixelShader,
                              const FBlendState* OptionalBlend, const void* PCData, uint32 PCSize,
                              const char* DebugName)
        {
            FRenderPassDesc::FAttachment Attachment;
            Attachment.SetImage(Dst);
            if (OptionalBlend != nullptr)
            {
                // Additive: preserve the destination so the upsample
                // accumulates instead of overwriting.
                Attachment.SetLoadOp(ERenderLoadOp::Load);
            }

            FRenderPassDesc RenderPass;
            RenderPass.AddColorAttachment(Attachment).SetRenderArea(Dst->GetExtent());

            FRenderState RenderState;
            RenderState.SetRasterState(RasterState);
            RenderState.SetDepthStencilState(DepthState);
            if (OptionalBlend != nullptr)
            {
                RenderState.SetBlendState(*OptionalBlend);
            }

            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName(DebugName);
            Desc.SetRenderState(RenderState);
            Desc.AddBindingLayout(SrcLayout);
            Desc.SetVertexShader(VertexShader);
            Desc.SetPixelShader(PixelShader);

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FBindingSetDesc SetDesc;
            SetDesc.AddItem(FBindingSetItem::TextureSRV(0, Src, LinearClamp));
            FRHIBindingSetRef Set = GRenderContext->CreateBindingSet(SetDesc, SrcLayout);

            FGraphicsState GraphicsState;
            GraphicsState.AddBindingSet(Set);
            GraphicsState.SetPipeline(Pipeline);
            GraphicsState.SetRenderPass(RenderPass);
            GraphicsState.SetViewportState(MakeViewportStateFromImage(Dst));

            CmdList.SetGraphicsState(GraphicsState);
            CmdList.SetPushConstants(PCData, PCSize);
            CmdList.Draw(3, 1, 0, 0);
        };

        // ---- Downsample chain. HDR -> mip0 (with prefilter), then halve. ----
        for (uint32 i = 0; i < BLOOM_MIP_COUNT; ++i)
        {
            FRHIImage* Src = (i == 0) ? GetNamedImage(ENamedImage::HDR) : (FRHIImage*)BloomMips[i - 1];
            FRHIImage* Dst = BloomMips[i];

            FBloomDownPushConstants PC = {};
            PC.SrcTexelSize = glm::vec2(1.0f / (float)Src->GetSizeX(), 1.0f / (float)Src->GetSizeY());
            PC.Threshold    = Threshold;
            PC.bIsPrefilter = (i == 0) ? 1u : 0u;
            PC.KneeCurve    = KneeCurve;
            PC._Pad0        = 0.0f;

            RecordPass(Src, Dst, DownPS, nullptr, &PC, sizeof(PC), "Bloom Downsample");
        }

        // ---- Upsample chain. Walk back to mip0, additively blending. ----
        for (uint32 i = BLOOM_MIP_COUNT - 1; i > 0; --i)
        {
            FRHIImage* Src = BloomMips[i];
            FRHIImage* Dst = BloomMips[i - 1];

            FBloomUpPushConstants PC = {};
            PC.SrcTexelSize = glm::vec2(1.0f / (float)Src->GetSizeX(), 1.0f / (float)Src->GetSizeY());
            PC.Radius       = 1.0f;
            PC._Pad0        = 0.0f;

            RecordPass(Src, Dst, UpPS, &AdditiveBlend, &PC, sizeof(PC), "Bloom Upsample");
        }
    }

    void FForwardRenderScene::ToneMappingPass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("Color Grading + Tone Map Pass", tracy::Color::Red2);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("ColorGrading.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        // When SMAA is enabled, tonemap renders into an LDR intermediate that the
        // SMAA passes then resolve into the final render target. Otherwise we
        // write straight to the render target -- unless the post-process
        // material chain is non-empty, in which case we still need an
        // intermediate so the chain can ping-pong against it before the
        // final blit to RT in PostProcessMaterialPass.
        const bool bSMAAEnabled = World->GetDefaultWorldSettings().SMAAQuality != ESMAAQuality::Off;
        const bool bPPMaterials = !ActivePostProcessMaterials.empty();
        FRHIImage* OutputImage = (bSMAAEnabled || bPPMaterials) ? GetNamedImage(ENamedImage::LDR) : GetRenderTarget();

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

        FColorGradingPushConstants PC = BuildColorGradingConstants(ActivePostProcess, SceneGlobalData.Time);
        CmdList.SetPushConstants(&PC, sizeof(PC));
        CmdList.Draw(3, 1, 0, 0);
    }

    namespace
    {
        // 16 B push block for the PostProcess material template.
        // Mirrors PostProcessPixelPass.slang::FPostProcessPushConstants.
        struct FPostProcessMaterialPushConstants
        {
            uint32 MaterialIndex;
            uint32 _Pad0;
            uint32 _Pad1;
            uint32 _Pad2;
        };
        static_assert(sizeof(FPostProcessMaterialPushConstants) == 16,
            "FPostProcessMaterialPushConstants must match the slang push block.");
    }

    void FForwardRenderScene::PostProcessMaterialPass(ICommandList& CmdList)
    {
        if (ActivePostProcessMaterials.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Post Process Material Pass", tracy::Color::Magenta);

        // Set 2 layout for the post-process inputs. Materials see three
        // bindings: the chain's current input (uSceneColor, the ping-pong
        // source), the opaque depth attachment (uSceneDepth), and the
        // pre-tone-mapping HDR scene color (uHDRSceneColor) for materials
        // that want the unclipped data. Each material in the chain gets a
        // fresh binding set with the current source bound; the layout
        // itself is shared (cached via BindingCache).
        FBindingLayoutDesc PPLayoutDesc;
        PPLayoutDesc.SetBindingIndex(2)
            .SetVisibility(ERHIShaderType::Fragment)
            .AddItem(FBindingLayoutItem::Texture_SRV(0))
            .AddItem(FBindingLayoutItem::Texture_SRV(1))
            .AddItem(FBindingLayoutItem::Texture_SRV(2));
        FRHIBindingLayout* PPLayout = BindingCache.GetOrCreateBindingLayout(PPLayoutDesc);

        FRHISamplerRef LinearClamp = TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        FRHISamplerRef PointClamp  = TStaticRHISampler<false, false, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

        FRasterState RasterState;
        RasterState.SetCullNone();

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest();
        DepthState.DisableDepthWrite();

        FRenderState RenderState;
        RenderState.SetRasterState(RasterState);
        RenderState.SetDepthStencilState(DepthState);

        const bool bSMAAEnabled = World->GetDefaultWorldSettings().SMAAQuality != ESMAAQuality::Off;

        // The chain reads from "Source" and writes to "Dest", swapping each
        // pass. Tone mapping wrote into LDR (we forced that path in
        // ToneMappingPass when ActivePostProcessMaterials is non-empty), so
        // the first read is always LDR.
        FRHIImage* Source = GetNamedImage(ENamedImage::LDR);
        FRHIImage* Dest   = GetNamedImage(ENamedImage::PostProcessScratch);

        for (CMaterialInterface* MaterialInterface : ActivePostProcessMaterials)
        {
            if (MaterialInterface == nullptr || !MaterialInterface->IsReadyForRender())
            {
                continue;
            }
            CMaterial* Material = MaterialInterface->GetMaterial();
            if (Material == nullptr || Material->GetMaterialType() != EMaterialType::PostProcess)
            {
                continue;
            }
            FRHIVertexShader* VS = Material->GetVertexShader();
            FRHIPixelShader*  PS = Material->GetPixelShader();
            if (VS == nullptr || PS == nullptr)
            {
                continue;
            }

            FRenderPassDesc::FAttachment Attachment;
            Attachment.SetImage(Dest);

            FRenderPassDesc RenderPass;
            RenderPass.AddColorAttachment(Attachment).SetRenderArea(Dest->GetExtent());

            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("Post Process Material");
            Desc.SetRenderState(RenderState);
            Desc.AddBindingLayout(SceneBindingLayout);
            Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
            Desc.AddBindingLayout(PPLayout);
            Desc.SetVertexShader(VS);
            Desc.SetPixelShader(PS);

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FBindingSetDesc SetDesc;
            SetDesc.AddItem(FBindingSetItem::TextureSRV(0, Source, LinearClamp));
            // Depth uses a point sampler -- linear filtering across a depth
            // discontinuity returns garbage in-between depths, which any
            // depth-based effect (DoF, distance fog, edge detection)
            // would propagate as halos.
            SetDesc.AddItem(FBindingSetItem::TextureSRV(1, GetNamedImage(ENamedImage::DepthAttachment), PointClamp));
            SetDesc.AddItem(FBindingSetItem::TextureSRV(2, GetNamedImage(ENamedImage::HDR), LinearClamp));
            FRHIBindingSetRef PPSet = GRenderContext->CreateBindingSet(SetDesc, PPLayout);

            FGraphicsState GraphicsState;
            GraphicsState.SetPipeline(Pipeline);
            GraphicsState.AddBindingSet(SceneBindingSet);
            GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            GraphicsState.AddBindingSet(PPSet);
            GraphicsState.SetRenderPass(RenderPass);
            GraphicsState.SetViewportState(MakeViewportStateFromImage(Dest));

            CmdList.SetGraphicsState(GraphicsState);

            FPostProcessMaterialPushConstants PC = {};
            // Use the interface's index, not the parent material's — instances
            // own their own slot in the material buffer (parameter overrides
            // live there), so reading the parent's slot would ignore overrides.
            PC.MaterialIndex = (uint32)MaterialInterface->GetMaterialIndex();
            CmdList.SetPushConstants(&PC, sizeof(PC));
            CmdList.Draw(3, 1, 0, 0);

            eastl::swap(Source, Dest);
        }

        // After the loop, Source holds the latest result. SMAA reads LDR,
        // and the no-SMAA path expects the final image in the swapchain RT
        // (which the bypass tone-map path used to write to directly). Make
        // sure both consumers see Source where they expect.
        FRHIImage* LDR = GetNamedImage(ENamedImage::LDR);
        if (bSMAAEnabled)
        {
            if (Source != LDR)
            {
                CmdList.CopyImage(Source, FTextureSlice(), LDR, FTextureSlice());
            }
        }
        else
        {
            CmdList.CopyImage(Source, FTextureSlice(), GetRenderTarget(), FTextureSlice());
        }
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

        // Per-view cull descriptors: camera, cascades, 6/point, 1/spot.
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

        // Per-frame environment params consumed by Environment.slang. Sized
        // for one FEnvironmentParams; the EnvironmentPass writes a fresh
        // copy each frame from the active SEnvironmentComponent.
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FEnvironmentParams);
            BufferDesc.Usage.SetFlag(BUF_UniformBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::ConstantBuffer;
            BufferDesc.DebugName = "Environment Params";
            NamedBuffers[(int)ENamedBuffer::Environment] = GRenderContext->CreateBuffer(BufferDesc);
        }

        // Per-instance meshlet prefix sum. Initial size is one entry; the
        // upload site grows it as Instances grows.
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(uint32);
            BufferDesc.Stride = sizeof(uint32);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::ShaderResource;
            BufferDesc.DebugName = "Instance Meshlet Prefix";
            NamedBuffers[(int)ENamedBuffer::InstanceMeshletPrefix] = GRenderContext->CreateBuffer(BufferDesc);
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

    void FForwardRenderScene::AllocateMSAAImages(const glm::uvec2& Extent)
    {
        if (MSAASampleCount <= 1)
        {
            return;
        }

        {
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = Extent;
            ImageDesc.Format            = EFormat::RGBA16_FLOAT;
            ImageDesc.NumSamples        = MSAASampleCount;
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.InitialState      = EResourceStates::RenderTarget;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetFlag(EImageCreateFlags::ColorAttachment);
            ImageDesc.DebugName         = "HDR_MS";
            NamedImages[(int)ENamedImage::HDR_MS] = GRenderContext->CreateImage(ImageDesc);
        }

        {
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = Extent;
            ImageDesc.Flags.SetFlag(EImageCreateFlags::DepthAttachment);
            ImageDesc.Format            = EFormat::D32;
            ImageDesc.NumSamples        = MSAASampleCount;
            ImageDesc.InitialState      = EResourceStates::DepthRead;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.DebugName         = "Depth_MS";
            NamedImages[(int)ENamedImage::Depth_MS] = GRenderContext->CreateImage(ImageDesc);
        }

        {
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = Extent;
            ImageDesc.Format            = EFormat::R32_UINT;
            ImageDesc.NumSamples        = MSAASampleCount;
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.InitialState      = EResourceStates::RenderTarget;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetFlag(EImageCreateFlags::ColorAttachment);
            ImageDesc.DebugName         = "Picker_MS";
            NamedImages[(int)ENamedImage::Picker_MS] = GRenderContext->CreateImage(ImageDesc);
        }
    }

    void FForwardRenderScene::SyncMSAAState()
    {
        const uint8 Desired = ::Lumina::GetMSAASampleCount(World->GetDefaultWorldSettings().MSAASampleCount);

        if (Desired == MSAASampleCount)
        {
            return;
        }

        NamedImages[(int)ENamedImage::HDR_MS]    = nullptr;
        NamedImages[(int)ENamedImage::Depth_MS]  = nullptr;
        NamedImages[(int)ENamedImage::Picker_MS] = nullptr;

        MSAASampleCount = Desired;

        if (MSAASampleCount > 1)
        {
            AllocateMSAAImages(Windowing::GetPrimaryWindowHandle()->GetExtent());
        }
    }

    void FForwardRenderScene::InitImages()
    {
        glm::uvec2 Extent = Windowing::GetPrimaryWindowHandle()->GetExtent();

        // Snapshot the MSAA setting once per init. Live changes are handled by SyncMSAAState().
        MSAASampleCount = ::Lumina::GetMSAASampleCount(World->GetDefaultWorldSettings().MSAASampleCount);

        {
            FRHIImageDesc ImageDesc = GetRenderTarget()->GetDescription();
            ImageDesc.Format = EFormat::RGBA16_FLOAT;
            ImageDesc.DebugName = "HDR";
            NamedImages[(int)ENamedImage::HDR] = GRenderContext->CreateImage(ImageDesc);
        }

        {
            FRHIImageDesc ImageDesc = GetRenderTarget()->GetDescription();
            ImageDesc.DebugName = "LDR";
            NamedImages[(int)ENamedImage::LDR] = GRenderContext->CreateImage(ImageDesc);
        }

        // Ping-pong scratch for the post-process material chain.
        {
            FRHIImageDesc ImageDesc = GetRenderTarget()->GetDescription();
            ImageDesc.DebugName = "PostProcessScratch";
            NamedImages[(int)ENamedImage::PostProcessScratch] = GRenderContext->CreateImage(ImageDesc);
        }

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

        {
            uint32 Width = PreviousPow2(Extent.x);
            uint32 Height = PreviousPow2(Extent.y);

            // R16_FLOAT HZB: reverse-Z [0,1], min-reduced; quantization error is conservative.
            FRHIImageDesc ImageDesc;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage);
            ImageDesc.Extent            = glm::uvec2(Width, Height);
            ImageDesc.Format            = EFormat::R16_FLOAT;
            ImageDesc.NumMips           = (uint8)RenderUtils::CalculateMipCount(Width, Height);
            ImageDesc.InitialState      = EResourceStates::ShaderResource;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.DebugName         = "Depth Pyramid";

            NamedImages[(int)ENamedImage::DepthPyramid] = GRenderContext->CreateImage(ImageDesc);
        }

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

        AllocateMSAAImages(Extent);

        {
            // Progressive CSM atlas: cascade 0 = 2048², 1/2 = 1024² each (see GCSMAtlasW/H).
            FRHIImageDesc ImageDesc = {};
            ImageDesc.Extent = glm::uvec2(GCSMAtlasWidth, GCSMAtlasHeight);
            ImageDesc.Format = EFormat::D32;
            ImageDesc.Dimension = EImageDimension::Texture2D;
            ImageDesc.InitialState = EResourceStates::DepthWrite;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::DepthAttachment, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName = "ShadowCascadeAtlas";

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
            // WBOIT revealage = multiplicative product of (1-a_i); R16F is the reference format.
            FRHIImageDesc ImageDesc = {};
            ImageDesc.Extent = Extent;
            ImageDesc.Format = EFormat::R16_FLOAT;
            ImageDesc.Dimension = EImageDimension::Texture2D;
            ImageDesc.InitialState = EResourceStates::RenderTarget;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName = "Revealage";

            NamedImages[(int)ENamedImage::Revealage] = GRenderContext->CreateImage(ImageDesc);
        }

        {
            // Bloom mip chain. Half-res start, R11G11B10_FLOAT (additive over scene; quantization invisible).
            uint32 W = eastl::max<uint32>(Extent.x / 2u, 1u);
            uint32 H = eastl::max<uint32>(Extent.y / 2u, 1u);
            for (uint32 i = 0; i < BLOOM_MIP_COUNT; ++i)
            {
                FRHIImageDesc ImageDesc;
                ImageDesc.Extent            = glm::uvec2(W, H);
                ImageDesc.Format            = EFormat::R11G11B10_FLOAT;
                ImageDesc.Dimension         = EImageDimension::Texture2D;
                ImageDesc.NumMips           = 1;
                ImageDesc.InitialState      = EResourceStates::ShaderResource;
                ImageDesc.bKeepInitialState = true;
                ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);

                FString Name = "Bloom Mip ";
                Name.append_sprintf("%u", i);
                ImageDesc.DebugName = Name.c_str();

                BloomMips[i] = GRenderContext->CreateImage(ImageDesc);

                W = eastl::max<uint32>(W / 2u, 1u);
                H = eastl::max<uint32>(H / 2u, 1u);
            }
        }
    }

    void FForwardRenderScene::InitBRDFLUT()
    {
        // 256x256 RG16_FLOAT is the standard size for Karis 2013 split-sum.
        // R holds the F0 scale, G holds the F0 bias. RG16 is plenty -- the
        // integrand is smooth and saturates inside [0, 1] over the whole
        // (NdotV, Roughness) plane, so 16-bit half precision is invisible
        // against per-frame noise from the rest of the pipeline.
        constexpr uint32 BRDFLutSize = 256u;

        FRHIImageDesc ImageDesc;
        ImageDesc.Extent            = glm::uvec2(BRDFLutSize, BRDFLutSize);
        ImageDesc.Format            = EFormat::RG16_FLOAT;
        ImageDesc.Dimension         = EImageDimension::Texture2D;
        ImageDesc.NumMips           = 1;
        // Bake transitions us to ShaderResource at the end of the pass; from
        // then on the LUT is read-only for the life of the scene.
        ImageDesc.InitialState      = EResourceStates::ShaderResource;
        ImageDesc.bKeepInitialState = true;
        ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage);
        ImageDesc.DebugName         = "BRDF LUT";

        FRHIImageRef BRDFLut = GRenderContext->CreateImage(ImageDesc);
        NamedImages[(int)ENamedImage::BRDFLut] = BRDFLut;

        // Private layout/set for the bake -- the integrand uses no scene data,
        // so binding it through SceneBindingSet would force CreateLayouts() to
        // run (and reference an empty NamedImages slot) before this function
        // runs. Standalone is the cleanest order.
        FBindingLayoutDesc LayoutDesc;
        LayoutDesc.AddItem(FBindingLayoutItem::Texture_UAV(0));
        LayoutDesc.SetVisibility(ERHIShaderType::Compute);
        FRHIBindingLayout* Layout = BindingCache.GetOrCreateBindingLayout(LayoutDesc);

        FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("BRDFIntegration.slang");

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(Layout);
        PipelineDesc.CS = ComputeShader;
        PipelineDesc.DebugName = "BRDF Integration";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FBindingSetDesc SetDesc;
        SetDesc.AddItem(FBindingSetItem::TextureUAV(0, BRDFLut, BRDFLut->GetFormat()));
        FRHIBindingSet* Set = BindingCache.GetOrCreateBindingSet(SetDesc, Layout);

        FRHICommandListRef CmdList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
        CmdList->Open();

        FComputeState State;
        State.AddBindingSet(Set);
        State.SetPipeline(Pipeline);
        CmdList->SetComputeState(State);

        // Tile size matches numthreads in BRDFIntegration.slang.
        constexpr uint32 BRDFLutTile = 8u;
        const uint32 GroupsX = RenderUtils::GetGroupCount(BRDFLutSize, BRDFLutTile);
        const uint32 GroupsY = RenderUtils::GetGroupCount(BRDFLutSize, BRDFLutTile);
        CmdList->Dispatch(GroupsX, GroupsY, 1);

        CmdList->Close();
        GRenderContext->ExecuteCommandList(CmdList);
    }

    void FForwardRenderScene::InitSkyCube()
    {
        // 1024 per face: keeps HDRI detail when read 1:1 by Environment.slang.
        constexpr uint32 SkyCubeFaceSize = 1024u;

        FRHIImageDesc ImageDesc;
        ImageDesc.Extent            = glm::uvec2(SkyCubeFaceSize, SkyCubeFaceSize);
        ImageDesc.Format            = EFormat::R11G11B10_FLOAT;
        ImageDesc.Dimension         = EImageDimension::TextureCube;
        ImageDesc.ArraySize         = 6;
        ImageDesc.NumMips           = 1;
        // The capture pass alternates the cube between UAV (write) and SRV
        // (sample by IBL passes), so let the auto-barrier system track it.
        ImageDesc.InitialState      = EResourceStates::ShaderResource;
        ImageDesc.bKeepInitialState = true;
        // CubeCompatible is required for the SamplerCube SRV view downstream.
        // Vulkan does not derive VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT from
        // Dimension::TextureCube alone -- the flag must be set explicitly or
        // the cube view is undefined behavior (driver-dependent black/garbage).
        ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage, EImageCreateFlags::CubeCompatible);
        ImageDesc.DebugName         = "Sky Cube";

        NamedImages[(int)ENamedImage::SkyCube] = GRenderContext->CreateImage(ImageDesc);
    }

    void FForwardRenderScene::InitIBLConvolutionTargets()
    {
        // Diffuse irradiance: very low frequency, 32 per face is the
        // textbook size and indistinguishable from larger captures because
        // the cos-weighted hemispherical integration smears everything.
        {
            constexpr uint32 IrradianceFaceSize = 32u;

            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = glm::uvec2(IrradianceFaceSize, IrradianceFaceSize);
            ImageDesc.Format            = EFormat::R11G11B10_FLOAT;
            ImageDesc.Dimension         = EImageDimension::TextureCube;
            ImageDesc.ArraySize         = 6;
            ImageDesc.NumMips           = 1;
            ImageDesc.InitialState      = EResourceStates::ShaderResource;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage, EImageCreateFlags::CubeCompatible);
            ImageDesc.DebugName         = "Sky Irradiance";

            NamedImages[(int)ENamedImage::SkyIrradiance] = GRenderContext->CreateImage(ImageDesc);
        }

        // Pre-filtered specular: 128 base size with 5 mips ({128, 64, 32,
        // 16, 8}). The smallest mip corresponds to fully rough surfaces;
        // the GGX lobe at roughness=1 is so wide that 8 per face is more
        // than enough resolution.
        {
            constexpr uint32 PrefilterFaceSize = 128u;

            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = glm::uvec2(PrefilterFaceSize, PrefilterFaceSize);
            ImageDesc.Format            = EFormat::R11G11B10_FLOAT;
            ImageDesc.Dimension         = EImageDimension::TextureCube;
            ImageDesc.ArraySize         = 6;
            ImageDesc.NumMips           = (uint8)GSkyPrefilterMipCount;
            ImageDesc.InitialState      = EResourceStates::ShaderResource;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage, EImageCreateFlags::CubeCompatible);
            ImageDesc.DebugName         = "Sky Prefilter";

            NamedImages[(int)ENamedImage::SkyPrefilter] = GRenderContext->CreateImage(ImageDesc);
        }
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
            // Per-instance meshlet prefix sum used by the cull pass to map a
            // flat thread index to (InstanceID, MeshletLocalIdx) without the
            // per-instance over-dispatch the old (X = MaxMeshletsPerInstance,
            // Y = NumInstances) layout produced.
            BindingSetDesc.AddItem(FBindingSetItem::BufferSRV(16, GetNamedBuffer(ENamedBuffer::InstanceMeshletPrefix)));

            // Pre-integrated BRDF LUT (Karis 2013 split-sum). Linear-clamp:
            // the texel grid is sampled by (NdotV, Roughness) -- both in [0,1]
            // -- and the table is smooth, so bilinear filtering is the right
            // reconstruction filter and clamp matches the half-texel offsets
            // baked into the LUT (so edge fetches read the boundary samples
            // rather than wrapping or extrapolating).
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(17, GetNamedImage(ENamedImage::BRDFLut),
                TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()));

            // IBL cubemaps. Linear-clamp: the convolutions store smooth HDR
            // values and the prefilter has a real mip chain we want filtered
            // across (the consumer picks a mip from Roughness, but bilinear
            // between adjacent mips smooths the roughness-step transitions).
            FRHISamplerRef IBLCubeSampler = TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(18, GetNamedImage(ENamedImage::SkyIrradiance), IBLCubeSampler,
                EFormat::UNKNOWN, AllSubresources, EImageDimension::TextureCube));
            BindingSetDesc.AddItem(FBindingSetItem::TextureSRV(19, GetNamedImage(ENamedImage::SkyPrefilter),  IBLCubeSampler,
                EFormat::UNKNOWN, AllSubresources, EImageDimension::TextureCube));

            TBitFlags<ERHIShaderType> Visibility;
            Visibility.SetMultipleFlags(ERHIShaderType::Vertex, ERHIShaderType::Fragment, ERHIShaderType::Compute);
            GRenderContext->CreateBindingSetAndLayout(Visibility, 0, BindingSetDesc, SceneBindingLayout, SceneBindingSet);
        }

        // Standalone set-2 layout for ToneMapping. Binds:
        //   0 -- HDR scene color (input to grading + chromatic aberration)
        //   1 -- Bloom mip 0 (output of the bloom upsample chain)
        // Linear-clamp samplers on both: chromatic aberration offsets the
        // HDR sample by a fraction of a UV so it must filter, and the
        // bloom composite reads the largest mip at a different resolution.
        {
            FRHISamplerRef LinearClamp = TStaticRHISampler<true, true, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

            FBindingSetDesc ComposeSetDesc;
            ComposeSetDesc.AddItem(FBindingSetItem::TextureSRV(0, GetNamedImage(ENamedImage::HDR), LinearClamp));
            ComposeSetDesc.AddItem(FBindingSetItem::TextureSRV(1, BloomMips[0], LinearClamp));

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
    #if USING(WITH_EDITOR)
        // Find the newest slot whose GPU work is guaranteed complete. The render
        // thread submits at most FRAMES_IN_FLIGHT frames before the CPU is gated
        // on the previous frame's timeline value, so a slot whose SubmittedFrame
        // is at least FRAMES_IN_FLIGHT older than the most recent issue is safe
        // to map without waiting on a semaphore.
        int32 BestSlotIdx = -1;
        uint64 BestFrame = 0;
        for (uint32 i = 0; i < PickerReadbackRingSize; ++i)
        {
            const FPickerReadbackSlot& Slot = PickerReadbackRing[i];
            if (!Slot.bPending || !Slot.Staging)
            {
                continue;
            }
            if (PickerReadbackFrame - Slot.SubmittedFrame <= FRAMES_IN_FLIGHT)
            {
                continue;
            }
            if (BestSlotIdx == -1 || Slot.SubmittedFrame > BestFrame)
            {
                BestSlotIdx = static_cast<int32>(i);
                BestFrame = Slot.SubmittedFrame;
            }
        }

        if (BestSlotIdx == -1)
        {
            // Not enough frames have rendered since the last invalidation
            // (startup, swapchain resize, or first click on a fresh world).
            // Caller treats this the same as "no entity under cursor".
            return entt::null;
        }

        const FPickerReadbackSlot& Slot = PickerReadbackRing[BestSlotIdx];
        if (X >= Slot.Width || Y >= Slot.Height)
        {
            return entt::null;
        }

        size_t RowPitch = 0;
        void* MappedMemory = GRenderContext->MapStagingTexture(Slot.Staging, FTextureSlice(), ERHIAccess::HostRead, &RowPitch);
        if (!MappedMemory)
        {
            return entt::null;
        }

        uint8* RowStart = static_cast<uint8*>(MappedMemory) + Y * RowPitch;
        uint32* PixelPtr = reinterpret_cast<uint32*>(RowStart) + X;
        uint32 PixelValue = *PixelPtr;

        GRenderContext->UnMapStagingTexture(Slot.Staging);

        if (PixelValue == 0)
        {
            return entt::null;
        }

        return static_cast<entt::entity>(PixelValue);
    #else
        (void)X;
        (void)Y;
        return entt::null;
    #endif
    }

    #if USING(WITH_EDITOR)
    void FForwardRenderScene::IssuePickerReadback(ICommandList& CmdList)
    {
        FRHIImage* PickerImage = GetNamedImage(ENamedImage::Picker);
        if (!PickerImage)
        {
            return;
        }

        const uint32 W = PickerImage->GetDescription().Extent.x;
        const uint32 H = PickerImage->GetDescription().Extent.y;

        FPickerReadbackSlot& Slot = PickerReadbackRing[PickerReadbackWriteIndex];

        if (!Slot.Staging || Slot.Width != W || Slot.Height != H)
        {
            // First use of this slot, or extent changed (post-resize). Reallocate
            // to match; bPending stays false until the new copy is recorded below.
            Slot.Staging = GRenderContext->CreateStagingImage(PickerImage->GetDescription(), ERHIAccess::HostRead);
            Slot.Width = W;
            Slot.Height = H;
        }

        CmdList.CopyImage(PickerImage, FTextureSlice(), Slot.Staging, FTextureSlice());

        Slot.SubmittedFrame = PickerReadbackFrame;
        Slot.bPending = true;

        ++PickerReadbackFrame;
        PickerReadbackWriteIndex = (PickerReadbackWriteIndex + 1) % PickerReadbackRingSize;
    }
    #endif
}
