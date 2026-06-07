#include "pch.h"
#include "ForwardRenderScene.h"
#include <algorithm>
#include <execution>
#include <random>
#include "Assets/AssetTypes/Material/Material.h"
#include "Assets/AssetTypes/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "assets/assettypes/mesh/skeleton/skeleton.h"
#include "Assets/AssetTypes/Textures/Texture.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Windows/Window.h"
#include "Memory/MemoryTracking.h"
#include "Paths/Paths.h"
#include "Renderer/GPUProfiler/GPUProfiler.h"
#include "Renderer/RendererUtils.h"
#include "Renderer/RHIStaticStates.h"
#include "Renderer/ShaderCompiler.h"
#include "Renderer/RenderContext.h"
#include "Renderer/CommandList.h"
#include "Renderer/RHI.h"
#include "TaskSystem/TaskGraph.h"
#include "TaskSystem/TaskSystem.h"
#include "Tools/Import/ImportHelpers.h"
#include "World/World.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/BillboardComponent.h"
#include "World/Entity/Components/WidgetComponent.h"
#include "World/Entity/Components/TextComponent.h"
#include "Tools/FontManager/FontManager.h"
#include "world/entity/components/charactercontrollercomponent.h"
#include "World/Entity/Components/EditorComponent.h"
#include "world/entity/components/entitytags.h"
#include "world/entity/components/environmentcomponent.h"
#include "World/Entity/Components/ExponentialHeightFogComponent.h"
#include "world/entity/components/lightcomponent.h"
#include "World/Entity/Components/LineBatcherComponent.h"
#include "World/Entity/Components/TriangleBatcherComponent.h"
#include "World/Entity/Components/ParticleSystemComponent.h"
#include "World/Entity/Components/DecalComponent.h"
#include "World/Entity/Components/WaterComponent.h"
#include "World/Entity/Components/SkeletalMeshComponent.h"
#include "World/Entity/Components/EnvironmentComponent.h"
#include "World/Entity/Components/SkyLightComponent.h"
#include "world/entity/components/staticmeshcomponent.h"
#include "World/Entity/Components/TerrainComponent.h"
#include "World/Scene/RenderScene/EnvironmentRenderTypes.h"
#include "World/Scene/RenderScene/MeshDrawCommand.h"
#include "World/Scene/RenderScene/TerrainMeshletBuilder.h"
#include "World/Scene/RenderScene/TerrainRenderTypes.h"
#include "World/Subsystems/WorldSettings.h"
#include "Renderer/SMAA/AreaTex.h"
#include "Renderer/SMAA/SearchTex.h"
#include "TaskSystem/FiberSync.h"

namespace Lumina
{
    namespace
    {
        constexpr uint32 GFroxelGridX = 160;
        constexpr uint32 GFroxelGridY = 90;
        constexpr uint32 GFroxelGridZ = 128;

        // Hard cap on non-sun volumetric lights injected per frame; packed into the
        // inject push constants (matches the prior half-res march's cap).
        constexpr uint32 GFroxelMaxLocalLights = 16;

        static TConsoleVar<bool> CVarVolFogEnabled(
            "r.VolFog.Enabled",
            true,
            "Enable froxel volumetric fog. Still requires an enabled ExponentialHeightFog component.");
    }

    static FVariableRateShadingState MakeWorldShadingRate(const SDefaultWorldSettings& Settings)
    {
        FVariableRateShadingState ShadingRate;
        switch (Settings.VariableRateShading)
        {
            case EVariableRateShading::Rate2x2: ShadingRate.SetEnabled(true).SetShadingRate(EVariableShadingRate::e2x2); break;
            case EVariableRateShading::Rate4x4: ShadingRate.SetEnabled(true).SetShadingRate(EVariableShadingRate::e4x4); break;
            default: break;
        }
        return ShadingRate;
    }

    static FRHIImageRef CreateSMAALUTImage(const uint8* Bytes, uint32 Width, uint32 Height, EFormat Format, uint32 RowPitch, const char* DebugName)
    {
        FRHIImageDesc Desc;
        Desc.Extent            = FUIntVector2(Width, Height);
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

    // Hemisphere SSAO kernel: SSAO_KERNEL_SIZE directions in the +Z (tangent-space) hemisphere, scaled so
    // samples bunch near the origin (more local detail) via an accelerating lerp. Deterministic seed so the
    // pattern is stable frame-to-frame (no shimmer). The shader rotates this per-pixel by the noise texture.
    static void GenerateSSAOKernel(FSSAOSettings& Settings)
    {
        std::mt19937 Rng(1337u);
        std::uniform_real_distribution<float> Rand01(0.0f, 1.0f);
        std::uniform_real_distribution<float> RandM11(-1.0f, 1.0f);

        for (uint32 i = 0; i < SSAO_KERNEL_SIZE; ++i)
        {
            FVector3 Sample(RandM11(Rng), RandM11(Rng), Rand01(Rng));
            float Len = Math::Sqrt(Sample.x * Sample.x + Sample.y * Sample.y + Sample.z * Sample.z);
            if (Len > 1e-6f)
            {
                Sample /= Len;
            }
            Sample *= Rand01(Rng);

            // Accelerating distribution: push samples toward the surface for tighter contact AO.
            float T     = (float)i / (float)SSAO_KERNEL_SIZE;
            float Scale = 0.1f + 0.9f * T * T;
            Sample *= Scale;

            Settings.Samples[i] = FVector4(Sample.x, Sample.y, Sample.z, 0.0f);
        }
    }

    // 4x4 RGBA32F rotation noise: random vectors in the XY tangent plane (z=0) used to rotate the kernel
    // per-pixel, trading banding for high-frequency noise the (here absent) blur would normally smooth out.
    static FRHIImageRef CreateSSAONoiseTexture()
    {
        std::mt19937 Rng(7331u);
        std::uniform_real_distribution<float> RandM11(-1.0f, 1.0f);

        FVector4 Noise[16];
        for (int i = 0; i < 16; ++i)
        {
            Noise[i] = FVector4(RandM11(Rng), RandM11(Rng), 0.0f, 0.0f);
        }

        return CreateSMAALUTImage(reinterpret_cast<const uint8*>(Noise), 4, 4,
                                  EFormat::RGBA32_FLOAT, 4 * sizeof(FVector4), "SSAO Noise");
    }


    FForwardRenderScene::FForwardRenderScene(CWorld* InWorld)
        : World(InWorld)
        , ShadowAtlas(FShadowAtlasConfig())
    {
    }

    void FForwardRenderScene::Init()
    {
        LUMINA_MEMORY_SCOPE("Render Scene");

        GRenderContext->WaitIdle();

        // MSAA is a scene-global world setting; cached here and used to size every view's
        // MS scratch images. Init runs before any Extract -- read straight from the world.
        const SDefaultWorldSettings& InitSettings = World ? World->GetDefaultWorldSettings() : SDefaultWorldSettings{};
        MSAASampleCount = ::Lumina::GetMSAASampleCount(InitSettings.MSAASampleCount);

        // Shared (view-independent) buffers + images first; the per-view binding sets
        // built in AddSceneView reference these.
        InitBuffers();

        // Must precede the per-view binding sets so the BRDF + SMAA sets see valid images.
        InitSharedResources();
        
        GenerateSSAOKernel(CachedSSAOSettings);
        SSAONoiseTexture = CreateSSAONoiseTexture();

        AppliedIBLResolution = FIBLBakeResolution{};
        InitSkyCube(AppliedIBLResolution.SkyCube);
        InitIBLConvolutionTargets(AppliedIBLResolution);

        // Cascade shadow atlas: shared across all views. Capture (preview) cameras reuse the
        // primary camera's CSM cascades rather than fitting their own, so it's created once here.
        {
            FRHIImageDesc ImageDesc = {};
            ImageDesc.Extent = FUIntVector2(GCSMAtlasWidth, GCSMAtlasHeight);
            ImageDesc.Format = EFormat::D32;
            ImageDesc.Dimension = EImageDimension::Texture2D;
            ImageDesc.InitialState = EResourceStates::DepthWrite;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::DepthAttachment, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName = "ShadowCascadeAtlas";
            NamedImages[(int)ENamedImage::Cascade] = GRenderContext->CreateImage(ImageDesc);
        }

        // Reserve so capture-view registration never reallocates SceneViews -- the render
        // thread holds raw FSceneView* (CurrentView) and indexes SceneViews by snapshot.
        SceneViews.reserve(MaxSceneViews);

        // Primary view (index 0) tracks the swapchain; AddSceneView builds its per-view
        // images/buffers/binding sets and lazily creates the shared scene/compose/SMAA/OIT layouts.
        FRHIViewportRef PrimaryViewport = GRenderContext->CreateViewport(Windowing::GetPrimaryWindowHandle()->GetExtent(), "Forward Renderer Viewport");
        AddSceneView(PrimaryViewport, /*bPrimary*/ true);

        SwapchainResizedHandle = FRenderManager::OnSwapchainResized.AddMember(this, &FForwardRenderScene::SwapchainResized);
    }

    FForwardRenderScene::FSceneView& FForwardRenderScene::AddSceneView(const FRHIViewportRef& Viewport, bool bPrimary)
    {
        // SceneViews backing storage is stable during a frame; views are only added/removed
        // at controlled points (init / capture register), never mid-RenderView.
        SceneViews.emplace_back();
        FSceneView& View = SceneViews.back();
        View.Viewport   = Viewport;
        View.bIsPrimary = bPrimary;
        View.Size       = Viewport->GetRenderTarget()->GetExtent();

        // Per-view clustered-lighting grid (built from this view's projection).
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FCluster) * NumClusters;
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = "Cluster SSBO";
            View.ClusterBuffer = GRenderContext->CreateBuffer(BufferDesc);
        }
        View.bClusterGridDirty = true;   // fresh buffer has undefined contents.

        InitViewImages(View);
        View.ViewportState = MakeViewportStateFromImage(View.Images[(int)ENamedImage::HDR]);
        CreateViewBindingSets(View);

        return View;
    }

    int32 FForwardRenderScene::RegisterCaptureView(const FUIntVector2& Size)
    {
        // Reuse a disabled capture view of the same size if one exists (avoids growing
        // SceneViews on repeated select/deselect); else allocate a new one.
        const FUIntVector2 ClampedSize = Math::Max(Size, FUIntVector2(1));
        for (int32 i = 1; i < (int32)SceneViews.size(); ++i)
        {
            if (!SceneViews[i].bEnabled && SceneViews[i].Size == ClampedSize)
            {
                return i;
            }
        }

        // Cap capture views so the reserved SceneViews never reallocates (which would dangle CurrentView).
        // The editor reuses one preview slot, so this only trips on many simultaneous captures.
        if (SceneViews.size() >= MaxSceneViews)
        {
            return -1;
        }

        // No WaitIdle: RHI creation is mutex-serialized, the new view isn't referenced by any in-flight
        // frame, and SceneViews is reserved so the push-back can't reallocate under the render thread.
        FRHIViewportRef Viewport = GRenderContext->CreateViewport(ClampedSize, "Capture View");
        const int32 Handle = (int32)SceneViews.size();
        AddSceneView(Viewport, /*bPrimary*/ false);
        return Handle;
    }

    void FForwardRenderScene::SetCaptureView(int32 Handle, const FViewVolume& View, bool bEnabled)
    {
        if (Handle <= 0 || Handle >= (int32)SceneViews.size())
        {
            return;
        }
        SceneViews[Handle].PendingViewVolume = View;
        SceneViews[Handle].bEnabled          = bEnabled;
    }

    FRHIImage* FForwardRenderScene::GetCaptureRenderTarget(int32 Handle) const
    {
        if (Handle <= 0 || Handle >= (int32)SceneViews.size() || !SceneViews[Handle].Viewport)
        {
            return nullptr;
        }
        return SceneViews[Handle].Viewport->GetRenderTarget();
    }

    void FForwardRenderScene::InitSharedResources()
    {
        FSharedRenderResources& Shared = GRenderManager->GetSharedRenderResources();

        if (!Shared.bInitialized)
        {
            Shared.BRDFLut    = BakeBRDFLUT();
            Shared.SMAAArea   = CreateSMAALUTImage(areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, EFormat::RG8_UNORM, AREATEX_PITCH, "SMAA AreaTex");
            Shared.SMAASearch = CreateSMAALUTImage(searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, EFormat::R8_UNORM, SEARCHTEX_PITCH, "SMAA SearchTex");

            #if USING(WITH_EDITOR)
            const FString Dir = Paths::GetEngineResourceDirectory();
            Shared.EditorIcons[0] = Import::Textures::CreateTextureFromImport(Dir + "/Textures/PointLight.png", false);
            Shared.EditorIcons[1] = Import::Textures::CreateTextureFromImport(Dir + "/Textures/DirectionalLight.png", false);
            Shared.EditorIcons[2] = Import::Textures::CreateTextureFromImport(Dir + "/Textures/SkyLight.png", false);
            Shared.EditorIcons[3] = Import::Textures::CreateTextureFromImport(Dir + "/Textures/SpotLight.png", false);
            Shared.EditorIcons[4] = Import::Textures::CreateTextureFromImport(Dir + "/Textures/CameraIcon.png", false);
            Shared.EditorIcons[5] = Import::Textures::CreateTextureFromImport(Dir + "/Textures/PersonIcon.png", false);
            Shared.EditorIcons[6] = Import::Textures::CreateTextureFromImport(Dir + "/Textures/Molecule.png", false);
            #endif

            Shared.bInitialized = true;
        }

        NamedImages[(int)ENamedImage::BRDFLut]    = Shared.BRDFLut;
        NamedImages[(int)ENamedImage::SMAAArea]   = Shared.SMAAArea;
        NamedImages[(int)ENamedImage::SMAASearch] = Shared.SMAASearch;

        #if USING(WITH_EDITOR)
        NamedImages[(int)ENamedImage::PointLightIcon]       = Shared.EditorIcons[0];
        NamedImages[(int)ENamedImage::DirectionalLightIcon] = Shared.EditorIcons[1];
        NamedImages[(int)ENamedImage::SkyLightIcon]         = Shared.EditorIcons[2];
        NamedImages[(int)ENamedImage::SpotLightIcon]        = Shared.EditorIcons[3];
        NamedImages[(int)ENamedImage::CameraIcon]           = Shared.EditorIcons[4];
        NamedImages[(int)ENamedImage::CharacterIcon]        = Shared.EditorIcons[5];
        NamedImages[(int)ENamedImage::ParticleSystemIcon]   = Shared.EditorIcons[6];
        #endif
    }

    void FForwardRenderScene::Shutdown()
    {
        GRenderContext->WaitIdle();

        FRenderManager::OnSwapchainResized.Remove(SwapchainResizedHandle);
    }

    void FForwardRenderScene::WaitForSlotConsumed(uint8 Slot, uint64 Target)
    {
        if (SlotConsumedCount[Slot].load(std::memory_order_acquire) >= Target)
        {
            return;
        }
        
        LUMINA_PROFILE_SECTION_COLORED("WaitForSlotConsumed", tracy::Color::Crimson);
        std::unique_lock<FMutex> Lock(SlotMutex);
        
        SlotCV.wait(Lock, [&]() 
        {
            return SlotConsumedCount[Slot].load(std::memory_order_acquire) >= Target;
        });
    }

    void FForwardRenderScene::SignalSlotConsumed(uint8 Slot)
    {
        bool Expected = true;
        if (!SlotHasPendingConsume[Slot].compare_exchange_strong(Expected, false, std::memory_order_acq_rel))
        {
            return;
        }
        SlotConsumedCount[Slot].fetch_add(1, std::memory_order_release);
        
        {
            std::scoped_lock<FMutex> Lock(SlotMutex);
        }
        SlotCV.notify_all();
    }

    namespace
    {
        // Defined in the terrain helper block below; game-thread Extract prep.
        void PrepareTerrainExtract(STerrainComponent& Terrain, const FMatrix4& WorldMatrix, FForwardRenderScene::FFrameData::FTerrainExtract& Out);
    }

    void FForwardRenderScene::Extract(const FViewVolume& ViewVolume, const SPostProcessSettings* PostProcess)
    {
        LUMINA_PROFILE_SCOPE();
        LUMINA_MEMORY_SCOPE("Render Scene");

        const uint8  Slot        = (uint8)(GRenderManager->GetCurrentFrameIndex() % FRAMES_IN_FLIGHT);
        const uint64 NextProduce = SlotProducedCount[Slot] + 1u;
        WaitForSlotConsumed(Slot, SlotProducedCount[Slot]);

        // Marks one outstanding produce so SignalSlotConsumed pairs it and ignores stray signals.
        SlotHasPendingConsume[Slot].store(true, std::memory_order_release);

        ExtractFrame = &FrameRing[Slot];
        FFrameData& Frame = *ExtractFrame;

        Frame.bExtractedThisFrame  = false;
        Frame.CachedWorldSettings  = World->GetDefaultWorldSettings();
        Frame.CachedWorldDeltaTime = (float)World->GetWorldDeltaTime();
        Frame.ViewVolume           = ViewVolume;

        // PostProcess is a stack temporary in CWorld::Extract -- value-copy it.
        if (PostProcess != nullptr)
        {
            Frame.PostProcess.ActivePostProcessStorage = *PostProcess;
            Frame.PostProcess.bHasActivePostProcess    = true;
        }
        else
        {
            Frame.PostProcess.bHasActivePostProcess    = false;
        }

        // Resolve post-process materials here (game thread, alive) and ref-hold their shaders, so a
        // deleted PP material can't dangle the render thread. Invalid/wrong-domain entries are dropped.
        Frame.PostProcess.ActivePostProcessMaterials.clear();
        for (CMaterialInterface* PPInterface : PendingPostProcessMaterials)
        {
            if (PPInterface == nullptr || !PPInterface->IsReadyForRender())
            {
                continue;
            }
            CMaterial* PPMaterial = PPInterface->GetMaterial();
            if (PPMaterial == nullptr || PPMaterial->GetMaterialType() != EMaterialType::PostProcess)
            {
                continue;
            }
            FRHIVertexShader* VS = PPMaterial->GetVertexShader();
            FRHIPixelShader*  PS = PPMaterial->GetPixelShader();
            if (VS == nullptr || PS == nullptr)
            {
                continue;
            }
            // VS/PS from the concrete material; index from the interface (instances own their param slot).
            FFrameData::FPostProcessMaterial& Out = Frame.PostProcess.ActivePostProcessMaterials.emplace_back();
            Out.Shaders.VertexShader = VS;
            Out.Shaders.PixelShader  = PS;
            Out.MaterialIndex        = (uint32)PPInterface->GetMaterialIndex();
        }

        // Extract drives the primary view via a LOCAL pointer -- never the shared SceneViewport (render-thread
        // state, repointed per view). Writing it here races RenderView and can flicker a capture onto the main RT.
        FRHIViewport* PrimaryViewport = SceneViews[0].Viewport.GetReference();
        PrimaryViewport->SetViewVolume(ViewVolume);

        FSceneGlobalData& SceneGlobalData = Frame.SceneGlobalData;
        SceneGlobalData.CameraData.Location             = FVector4(PrimaryViewport->GetViewVolume().GetViewPosition(), 1.0f);
        SceneGlobalData.CameraData.Up                   = FVector4(PrimaryViewport->GetViewVolume().GetUpVector(), 1.0f);
        SceneGlobalData.CameraData.Right                = FVector4(PrimaryViewport->GetViewVolume().GetRightVector(), 1.0f);
        SceneGlobalData.CameraData.Forward              = FVector4(PrimaryViewport->GetViewVolume().GetForwardVector(), 1.0f);
        SceneGlobalData.CameraData.View                 = PrimaryViewport->GetViewVolume().GetViewMatrix();
        SceneGlobalData.CameraData.InverseView          = PrimaryViewport->GetViewVolume().GetInverseViewMatrix();
        SceneGlobalData.CameraData.Projection           = PrimaryViewport->GetViewVolume().GetProjectionMatrix();
        SceneGlobalData.CameraData.InverseProjection    = PrimaryViewport->GetViewVolume().GetInverseProjectionMatrix();
        SceneGlobalData.ScreenSize                      = FVector4(PrimaryViewport->GetSize().x, PrimaryViewport->GetSize().y, 0.0f, 0.0f);
        SceneGlobalData.GridSize                        = FVector4(ClusterGridSizeX, ClusterGridSizeY, ClusterGridSizeZ, 0.0f);
        SceneGlobalData.Time                            = (float)World->GetTimeSinceWorldCreation();
        SceneGlobalData.DeltaTime                       = Frame.CachedWorldDeltaTime;
        SceneGlobalData.FarPlane                        = PrimaryViewport->GetViewVolume().GetFar();
        SceneGlobalData.NearPlane                       = PrimaryViewport->GetViewVolume().GetNear();
        // SSAO: static kernel + per-world tuning. AOTextureIndex stays the ~0u sentinel here; the render
        // thread patches it (or leaves the sentinel when SSAO is off) before upload. Capture views keep it.
        SceneGlobalData.SSAOSettings                    = CachedSSAOSettings;
        SceneGlobalData.SSAOSettings.Radius             = Frame.CachedWorldSettings.SSAORadius;
        SceneGlobalData.SSAOSettings.Intensity          = Frame.CachedWorldSettings.SSAOIntensity;
        SceneGlobalData.SSAOSettings.Power              = Frame.CachedWorldSettings.SSAOPower;
        SceneGlobalData.CullData.Frustum                = PrimaryViewport->GetViewVolume().GetFrustum();
        SceneGlobalData.CullData.ShadowFrustum          = SceneGlobalData.CullData.Frustum; // Rebuilt after directional light is processed.
        SceneGlobalData.CullData.bHasDirectional        = 0u;
        SceneGlobalData.CullData.InstanceNum            = (uint32)Frame.Geometry.Instances.size();
        SceneGlobalData.CullData.bFrustumCull           = RenderSettings.bFrustumCull;
        SceneGlobalData.CullData.bOcclusionCull         = RenderSettings.bOcclusionCull;
        // Fallback; ProcessDirectionalLight overrides this from the active sun's
        // ShadowMaxDistance. Only matters when no directional light is present.
        SceneGlobalData.CullData.ShadowMaxDistance      = 5000.0f;
        SceneGlobalData.CullData.bShadowOcclusionCull   = RenderSettings.bShadowOcclusionCull;
        SceneGlobalData.CullData.DebugMode              = (uint32)RenderSettings.Flags;

        if (GRenderContext->GetShaderCompiler()->HasPendingRequests())
        {
            SlotProducedCount[Slot] = NextProduce;
            ExtractFrame = nullptr;
            return;
        }

        // Clear CPU scene state before the gather; game-thread so the render thread
        // never sees half-populated containers.
        ResetPass_GameThread();

        // Snapshot the enabled capture views (camera + RT) before the gather, so BuildCullViews
        // (inside CompileDrawCommands_GameThread) can append a frustum-only cull view for each.
        for (int32 i = 1; i < (int32)SceneViews.size(); ++i)
        {
            if (!SceneViews[i].bEnabled)
            {
                continue;
            }
            FFrameData::FCaptureViewData Capture;
            Capture.ViewVolume     = SceneViews[i].PendingViewVolume;
            Capture.SceneViewIndex = i;
            Frame.Views.CaptureViews.push_back(Capture);
        }

        // CPU half: parallel ECS gather + cull setup.
        CompileDrawCommands_GameThread();

        // Finalize each capture view's per-view constants: inherit the primary's shared state
        // (debug mode, time, instance count) then override the camera-specific fields.
        for (FFrameData::FCaptureViewData& Capture : Frame.Views.CaptureViews)
        {
            const FViewVolume& VV = Capture.ViewVolume;
            FSceneGlobalData& Data = Capture.SceneGlobalData;
            Data = Frame.SceneGlobalData;
            Data.CameraData.Location          = FVector4(VV.GetViewPosition(), 1.0f);
            Data.CameraData.Up                = FVector4(VV.GetUpVector(), 1.0f);
            Data.CameraData.Right             = FVector4(VV.GetRightVector(), 1.0f);
            Data.CameraData.Forward           = FVector4(VV.GetForwardVector(), 1.0f);
            Data.CameraData.View              = VV.GetViewMatrix();
            Data.CameraData.InverseView       = VV.GetInverseViewMatrix();
            Data.CameraData.Projection        = VV.GetProjectionMatrix();
            Data.CameraData.InverseProjection = VV.GetInverseProjectionMatrix();
            const FUIntVector2 CaptureSize      = SceneViews[Capture.SceneViewIndex].Size;
            Data.ScreenSize                   = FUIntVector4(CaptureSize.x, CaptureSize.y, 0, 0);
            Data.FarPlane                     = VV.GetFar();
            Data.NearPlane                    = VV.GetNear();
            Data.CullData.Frustum             = VV.GetFrustum();
        }

                
        Frame.Lighting.AtlasTiles = ShadowAtlas.GetAllocatedTiles();

        Frame.bExtractedThisFrame = true;

        SlotProducedCount[Slot] = NextProduce;
        ExtractFrame = nullptr;
    }

    void FForwardRenderScene::RenderView_RenderThread(ICommandList& CmdList, uint8 FrameIndex)
    {
        LUMINA_PROFILE_SCOPE();
        LUMINA_MEMORY_SCOPE("Render Scene");

        const uint8 Slot = (uint8)(FrameIndex % FRAMES_IN_FLIGHT);
        RenderFrame = &FrameRing[Slot];
        FFrameData& Frame = FrameRing[Slot];

        // SyncMSAAState reads Frame.CachedWorldSettings, so RenderFrame must be set first.
        SyncMSAAState();

        if (!Frame.bExtractedThisFrame)
        {
            // SignalFrameConsumed at lambda tail releases the slot.
            RenderFrame = nullptr;
            return;
        }

        CurrentFrameSlot        = Slot;
        
        SyncIBLResolution(Frame.Volumetrics.IBLResolution);

        PointAtView(SceneViews[0]);
        CurrentCameraEarlyView = 0u;                                // primary's early/frustum cull view
        CurrentCameraLateView  = Frame.Views.CameraLateViewIndex;   // primary's late/occlusion cull view

        Frame.SceneGlobalData.CullData.PyramidWidth      = (float)GetNamedImage(ENamedImage::DepthPyramid)->GetSizeX();
        Frame.SceneGlobalData.CullData.PyramidHeight     = (float)GetNamedImage(ENamedImage::DepthPyramid)->GetSizeY();
        Frame.SceneGlobalData.CullData.DepthPyramidIndex = (uint32)GetNamedImage(ENamedImage::DepthPyramid)->GetResourceID();

        // Publish this frame's stats for the editor-side GetRenderStats() reader.
        RenderStats = Frame.FrameStats;

        GPU_PROFILE_SCOPE_COLOR(&CmdList, "RenderView", FColor(0.30f, 0.65f, 1.00f));

        for (const FRHIBufferRef& Buf : Frame.Geometry.PinnedMeshBuffersThisFrame)
        {
            CmdList.KeepAlive(Buf.GetReference());
        }

        // Widget RTs sampled by the widget/picker passes: hold them until this frame's GPU work
        // finishes so a mid-frame widget deletion can't free an RT the GPU is still reading.
        for (const FRHIImageRef& RT : Frame.Primitives.PinnedWidgetRTs)
        {
            CmdList.KeepAlive(RT.GetReference());
        }

        // Font atlases sampled by the text pass: same hazard as widget RTs above.
        for (const FRHIImageRef& Atlas : Frame.Primitives.PinnedFontAtlases)
        {
            CmdList.KeepAlive(Atlas.GetReference());
        }

        ResetPass_RenderThread(CmdList);
        CompileDrawCommands_RenderThread(CmdList);

        {
            GPU_PROFILE_SCOPE_COLOR(&CmdList, "Texture Paint", FColor(0.80f, 0.10f, 0.10f));
            TexturePaintPass(CmdList);
        }

        {
            LUMINA_PROFILE_SECTION("RenderPasses");

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Cull Early", FColor(1.00f, 0.40f, 0.70f));
                CullPassEarly(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Skinning", FColor(0.40f, 0.85f, 1.00f));
                SkinningPass(CmdList);
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
                // DBuffer decals: project onto opaque depth before the base pass composites them.
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Decals", FColor(0.90f, 0.55f, 0.20f));
                DecalPass(CmdList);
            }

            // After the depth prepass (full opaque depth available), before the base pass that samples it.
            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "SSAO", FColor(0.10f, 0.70f, 0.25f));
                SSAOPass(CmdList);
                SSAOBlurPass(CmdList);
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

            // VS-only early-Z so the heavy terrain PS shades each visible pixel once.
            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Terrain Depth", FColor(0.20f, 0.60f, 0.45f));
                TerrainDepthPrePass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Terrain Render", FColor(0.20f, 0.70f, 0.50f));
                TerrainRenderPass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Depth Pyramid (End)", FColor(1.00f, 0.55f, 0.20f));
                DepthPyramidPass(CmdList);
            }
            
            // After the opaque scene (so HDR holds the lit scene to refract/SSR), before translucency.
            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Water", FColor(0.20f, 0.70f, 0.90f));
                WaterPass(CmdList);
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
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Froxel Fog Inject", FColor(0.55f, 0.60f, 0.65f));
                FroxelInjectPass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Froxel Fog Integrate", FColor(0.65f, 0.55f, 0.75f));
                FroxelIntegratePass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Froxel Fog Apply", FColor(0.85f, 0.65f, 0.30f));
                FroxelApplyPass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Batched Solid Tris", FColor(0.30f, 0.85f, 0.45f));
                BatchedTriangleDraw(CmdList);
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

            // World-space text, pre-tone-map into HDR + Picker (one MRT pass), like billboards.
            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Text", FColor(0.95f, 0.85f, 0.20f));
                TextPass(CmdList);
            }

            #if USING(WITH_EDITOR)
            {
                // World-space widgets stamp their entity id into the Picker buffer here (their
                // color is drawn later, post-tone-map), so they stay click-selectable.
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Widget Picker", FColor(0.80f, 0.20f, 0.95f));
                WidgetPickerPass(CmdList);
            }
            {
                // After the last picker RT write; readback happens lazily in GetEntityAtPixel.
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Picker Readback", FColor(0.50f, 0.50f, 0.50f));
                IssuePickerReadback(CmdList);
            }
            #endif
        
            // Underwater absorption/distortion over the fully-composited HDR (per-ray path length, so the
            // half-submerged waterline falls out and above-water pixels are untouched). Before bloom/exposure.
            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Underwater", FColor(0.15f, 0.45f, 0.70f));
                UnderwaterPass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Bloom", FColor(0.95f, 0.75f, 0.30f));
                BloomPass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Auto Exposure", FColor(0.95f, 0.55f, 0.20f));
                AutoExposurePass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Tone Mapping", FColor(0.95f, 0.20f, 0.20f));
                ToneMappingPass(CmdList);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Post Process Materials", FColor(0.85f, 0.30f, 0.85f));
                PostProcessMaterialPass(CmdList);
            }

            if (Frame.CachedWorldSettings.SMAAQuality != ESMAAQuality::Off)
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
            
            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Widgets", FColor(0.80f, 0.20f, 0.95f));
                WidgetPass(CmdList);
            }

            #if !defined(LE_SHIPPING)
            {
                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Debug Text", FColor(0.95f, 0.85f, 0.20f));
                DebugTextPass(CmdList);
            }
            #endif
            
            for (const FFrameData::FCaptureViewData& Capture : Frame.Views.CaptureViews)
            {
                if (Capture.SceneViewIndex <= 0 || Capture.SceneViewIndex >= (int32)SceneViews.size())
                {
                    continue;
                }

                GPU_PROFILE_SCOPE_COLOR(&CmdList, "Capture View", FColor(0.30f, 0.85f, 0.65f));

                FSceneView& View = SceneViews[Capture.SceneViewIndex];
                PointAtView(View);

                View.Viewport->SetViewVolume(Capture.ViewVolume);
                CurrentCameraEarlyView = Capture.CameraViewIndex;
                CurrentCameraLateView  = ~0u;
                
                CurrentSceneRootAddr = BuildViewSceneRoot(CmdList, View, CmdList.CopyTransient(Capture.SceneGlobalData).Gpu);

                RenderCaptureView(CmdList);
            }
        }
        
        RenderFrame = nullptr;
    }

    void FForwardRenderScene::RenderCaptureView(ICommandList& CmdList)
    {
        if (RenderFrame->Geometry.OpaqueOccluderDrawList.empty())
        {
            CmdList.ClearImageUInt(GetNamedImage(ENamedImage::DepthAttachment), AllSubresources, 0);
        }
        
        DepthPrePassEarly(CmdList);
        ClusterBuildPass(CmdList);
        LightCullPass(CmdList);
        EnvironmentPass(CmdList);
        DecalPass(CmdList);
        BasePass(CmdList);
        TerrainCullPass(CmdList);
        TerrainDepthPrePass(CmdList);
        TerrainRenderPass(CmdList);
        WaterPass(CmdList);
        TransparentPass(CmdList);
        OITResolvePass(CmdList);
        FroxelInjectPass(CmdList);
        FroxelIntegratePass(CmdList);
        FroxelApplyPass(CmdList);
        BloomPass(CmdList);
        AutoExposurePass(CmdList);
        ToneMappingPass(CmdList);
        PostProcessMaterialPass(CmdList);

        if (RenderFrame->CachedWorldSettings.SMAAQuality != ESMAAQuality::Off)
        {
            SMAAEdgeDetectionPass(CmdList);
            SMAABlendWeightPass(CmdList);
            SMAANeighborhoodBlendPass(CmdList);
        }
    }

    void FForwardRenderScene::SignalFrameConsumed(uint8 FrameIndex)
    {
        SignalSlotConsumed((uint8)(FrameIndex % FRAMES_IN_FLIGHT));
    }
    
    void FForwardRenderScene::SwapchainResized(FVector2 NewSize)
    {
        // BindingCache is per-scene; safe to clear. GRenderContext->Clear* are NOT --
        // they'd wipe pipelines shared across scenes/RmlUi/ImGui -> device lost.
        BindingCache.ReleaseResources();

        // Only the primary view tracks the swapchain; capture views keep their own size.
        FSceneView& Primary = SceneViews[0];
        Primary.Viewport = GRenderContext->CreateViewport(NewSize, "Forward Renderer Viewport");
        Primary.Size     = FUIntVector2((uint32)NewSize.x, (uint32)NewSize.y);

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

    // Maps the authored IBL quality tier to concrete cube/prefilter resolutions. The Mips counts keep
    // roughness=1 on the smallest face >= 8px so the GGX lobe stays well sampled.
    static FIBLBakeResolution ResolveIBLQuality(EIBLQuality Quality)
    {
        switch (Quality)
        {
            case EIBLQuality::Low:    return FIBLBakeResolution{ 256u,  128u, 5u, 32u };
            case EIBLQuality::Medium: return FIBLBakeResolution{ 512u,  256u, 6u, 32u };
            case EIBLQuality::Ultra:  return FIBLBakeResolution{ 2048u, 512u, 7u, 64u };
            case EIBLQuality::High:
            default:                  return FIBLBakeResolution{ 1024u, 256u, 6u, 32u };
        }
    }

    void FForwardRenderScene::CompileDrawCommands_GameThread()
    {
        LUMINA_PROFILE_SCOPE();
        LUMINA_MEMORY_SCOPE("Render Scene");

        FFrameData& Frame = *ExtractFrame;
        auto& Instances              = Frame.Geometry.Instances;
        auto& BonesData              = Frame.Geometry.BonesData;
        auto& DrawCommands           = Frame.Geometry.DrawCommands;
        auto& OpaqueDrawList         = Frame.Geometry.OpaqueDrawList;
        auto& OpaqueOccluderDrawList = Frame.Geometry.OpaqueOccluderDrawList;
        auto& TranslucentDrawList    = Frame.Geometry.TranslucentDrawList;
        auto& PinnedMeshBuffersThisFrame = Frame.Geometry.PinnedMeshBuffersThisFrame;
        auto& SceneCullContext       = Frame.Geometry.SceneCullContext;
        auto& CullViews              = Frame.Views.CullViews;
        auto& IndirectArgs           = Frame.Views.IndirectArgs;
        auto& DrawMeshletStartOffsets= Frame.Geometry.DrawMeshletStartOffsets;
        auto& InstanceMeshletPrefix  = Frame.Geometry.InstanceMeshletPrefix;
        auto& LightData              = Frame.Lighting.LightData;
        auto& PackedShadows          = Frame.Lighting.PackedShadows;
        auto& EnvironmentParams      = Frame.Volumetrics.EnvironmentParams;
        auto& EnvironmentMapImage    = Frame.Volumetrics.EnvironmentMapImage;
        auto& SceneGlobalData        = Frame.SceneGlobalData;
        auto& SimpleVertices         = Frame.Primitives.SimpleVertices;
        auto& LineBatches            = Frame.Primitives.LineBatches;
        auto& BillboardInstances     = Frame.Primitives.BillboardInstances;
        auto& WidgetInstances        = Frame.Primitives.WidgetInstances;
        auto& PinnedWidgetRTs        = Frame.Primitives.PinnedWidgetRTs;
        auto& GlyphInstances         = Frame.Primitives.GlyphInstances;
        auto& TextBatches            = Frame.Primitives.TextBatches;
        auto& PinnedFontAtlases      = Frame.Primitives.PinnedFontAtlases;
        auto& FrameStats             = Frame.FrameStats;

        {
            LUMINA_PROFILE_SECTION("Compile Draw Commands");
            FEntityRegistry& Registry = World->GetEntityRegistry();
            TAtomic<uint32> LightCount{0};
            
            auto DirectionalView     = Registry.view<SDirectionalLightComponent>(entt::exclude<SDisabledTag>);
            auto SpotLightView       = Registry.view<SSpotLightComponent>(entt::exclude<SDisabledTag>);
            auto PointLightView      = Registry.view<SPointLightComponent>(entt::exclude<SDisabledTag>);
            auto CharacterView       = Registry.view<SCharacterControllerComponent>(entt::exclude<SDisabledTag>);
            auto CameraView          = Registry.view<SCameraComponent>(entt::exclude<SDisabledTag>);
            auto BillboardView       = Registry.view<SBillboardComponent>(entt::exclude<SDisabledTag>);
            auto WidgetView          = Registry.view<SWidgetComponent>(entt::exclude<SDisabledTag>);
            auto TextView            = Registry.view<STextComponent>(entt::exclude<SDisabledTag>);
            auto LineBatcherView     = Registry.view<FLineBatcherComponent>();
            auto TriangleBatcherView = Registry.view<FTriangleBatcherComponent>();
            auto EnvironmentView     = Registry.view<SEnvironmentComponent>(entt::exclude<SDisabledTag>);
            auto SkyLightView        = Registry.view<SSkyLightComponent>(entt::exclude<SDisabledTag>);
            auto FogView             = Registry.view<SExponentialHeightFogComponent>(entt::exclude<SDisabledTag>);
            auto StaticView          = Registry.view<SStaticMeshComponent>(entt::exclude<SDisabledTag>);
            auto SkeletalView        = Registry.view<SSkeletalMeshComponent>(entt::exclude<SDisabledTag>);
            auto TerrainAllView      = Registry.view<STerrainComponent>();
            auto TerrainView         = Registry.view<STerrainComponent>(entt::exclude<SDisabledTag>);
            auto ParticleAllView     = Registry.view<SParticleSystemComponent>();
            auto ParticleView        = Registry.view<SParticleSystemComponent>(entt::exclude<SDisabledTag>);
            auto DecalView           = Registry.view<SDecalComponent>(entt::exclude<SDisabledTag>);
            auto WaterView           = Registry.view<SWaterComponent>(entt::exclude<SDisabledTag>);
            auto& TransformStorage  = Registry.storage<STransformComponent>();
            
            ECS::Utils::ResolveAllDirtyTransforms(Registry);

            // Per-frame CPU reject volumes built before parallel gather so workers query lock-free.
            BuildSceneCullContext();

            const size_t EntityCount       = StaticView.size_hint() + SkeletalView.size_hint();
            const size_t EstimatedProxies  = EntityCount * 2;

            Instances.reserve(EstimatedProxies);
            DrawMeshletStartOffsets.reserve(EstimatedProxies);
            DrawCommands.reserve(EstimatedProxies);

            const uint32 NumThreads = GTaskSystem->GetNumTaskThreads();

            // 8MB: bone vector dominates (skeletons x bones x 48B); headroom for ~12k+ per thread.
            constexpr SIZE_T kArenaBlockSize = 8 * 1024 * 1024;
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

            // Persistent: outer storage keeps capacity, arena-backed members reset per frame.
            // NumThreads is process-constant, so this grows once.
            TVector<FThreadLocalDrawData>& ThreadLocal = ThreadLocalStorage;
            if (ThreadLocal.size() < NumThreads)
            {
                ThreadLocal.reserve(NumThreads);
                while (ThreadLocal.size() < NumThreads)
                {
                    ThreadLocal.emplace_back(FFrameArenaAllocator(FrameArenas[ThreadLocal.size()].get()));
                }
            }
            const uint32 ReservePerThread = (uint32)((EstimatedProxies + NumThreads - 1) / std::max(1u, NumThreads));
            for (uint32 t = 0; t < NumThreads; ++t)
            {
                ThreadLocal[t].ResetForFrame(FFrameArenaAllocator(FrameArenas[t].get()));
                ThreadLocal[t].Items.reserve(ReservePerThread);
            }
            
            
            // Ensure the engine default font's atlas is GPU-resident here, on the game thread, before the
            // text task (a worker) reads it. Creating/uploading the image from inside the worker task leaves
            // its pixels unready; doing it here matches the asset-load upload path that already works.
            if (CFont* DefaultFont = CFontManager::Get().GetDefaultFont())
            {
                DefaultFont->GetAtlasImage();
            }

            // Screen-space debug text (World::DrawDebugText): drain the queued lines and lay them out top-left
            // in pixels (the debug pass converts to NDC). Default font, single batch. Dev/Debug only.
            Frame.Primitives.DebugTextGlyphs.clear();
            Frame.Primitives.DebugTextBatch = {};
#if !defined(LE_SHIPPING)
            {
                TVector<FDebugTextLine> DebugLines;
                World->DrainDebugTextLines(DebugLines);

                CFont* DebugFont = CFontManager::Get().GetDefaultFont();
                FRHIImage* DebugAtlas = DebugFont ? DebugFont->GetAtlasImage() : nullptr;
                if (!DebugLines.empty() && DebugFont && DebugFont->HasAtlas() && DebugAtlas && DebugAtlas->GetResourceID() >= 0)
                {
                    // Fixed pixel size in the (fixed-resolution) world RT, so the text is a consistent size
                    // regardless of viewport aspect/size.
                    const float PxSize = 32.0f;   // pixels per em
                    const float Margin = 12.0f;
                    float       PenY   = Margin;

                    TVector<FShapedGlyph> DebugShaped;
                    for (const FDebugTextLine& Line : DebugLines)
                    {
                        const uint32 Color = PackColor(Line.Color);
                        if (DebugFont->ShapeText(Line.Text, 0.0f /*left*/, 0.0f, 1.0f, DebugShaped))
                        {
                            // ShapeText anchors the first line's top near em y=0 and stacks downward (em y<=0);
                            // larger em y = higher on screen, so screen Y = PenY - em*PxSize.
                            for (const FShapedGlyph& S : DebugShaped)
                            {
                                FGPUGlyph& G = Frame.Primitives.DebugTextGlyphs.emplace_back();
                                G.PlaneMin  = FVector2(Margin + S.Min.x * PxSize, PenY - S.Max.y * PxSize);
                                G.PlaneMax  = FVector2(Margin + S.Max.x * PxSize, PenY - S.Min.y * PxSize);
                                G.UVRect    = S.UV;
                                G.ColorPack = Color;
                            }
                        }

                        int32 NumLines = 1;
                        for (const char C : Line.Text)
                        {
                            if (C == '\n') ++NumLines;
                        }
                        PenY += (float)NumLines * DebugFont->GetLineHeight() * PxSize;
                    }

                    if (!Frame.Primitives.DebugTextGlyphs.empty())
                    {
                        FFrameData::FTextBatch& Batch = Frame.Primitives.DebugTextBatch;
                        Batch.AtlasIndex    = (uint32)DebugAtlas->GetResourceID();
                        Batch.AtlasWidth    = DebugFont->GetAtlasWidth();
                        Batch.AtlasHeight   = DebugFont->GetAtlasHeight();
                        Batch.DistanceRange = DebugFont->GetDistanceRange();
                        Batch.FirstInstance = 0;
                        Batch.Count         = (uint32)Frame.Primitives.DebugTextGlyphs.size();
                        Frame.Primitives.PinnedFontAtlases.push_back(DebugFont->GetAtlasImageRef());
                    }
                }
            }
#endif

            DrawTaskGraph.Reset();   // reuse the persistent graph (allocator block + capacity)
            FTaskGraph& Graph = DrawTaskGraph;

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
                        const STransformComponent&  TransformComponent = TransformStorage.get(Entity);
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
                        SSkeletalMeshComponent&     MeshComponent      = SkeletalView.get<SSkeletalMeshComponent>(Entity);
                        const STransformComponent&  TransformComponent = TransformStorage.get(Entity);
                        ProcessSkeletalMeshEntityInternal(Entity, MeshComponent, TransformComponent, Local);
                    }
                }
            });

            FTaskGraph::FNodeHandle MergeNode = Graph.Add([&]
            {
                MergeMeshDrawData(ThreadLocal);
            });
            
            FLineBatcherComponent* LineBatcher = nullptr;
            LineBatcherView.each([&](FLineBatcherComponent& C) { if (LineBatcher == nullptr)
                    {
                        LineBatcher = &C;
                    }
                });
            const uint32 LineChunkCount = (LineBatcher != nullptr) ? PrepareBatchedLines(*LineBatcher) : 0u;

            if (LineChunkCount > 0)
            {
                FTaskGraph::FNodeHandle LineBatchNode = Graph.AddParallelFor(LineChunkCount, 1, [this](const Task::FParallelRange& Range)
                {
                    BatchLineChunks(Range);
                });
                FTaskGraph::FNodeHandle LineFinalizeNode = Graph.Add([this, LineBatcher]
                {
                    FinalizeBatchedLines(*LineBatcher);
                });
                Graph.AddDependency(LineFinalizeNode, LineBatchNode);
            }

            // Triangles are low-volume; keep them as one High-priority node so they overlap the mesh fan-out
            // without needing the full chunk split.
            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Batched Triangle Processing");

                TriangleBatcherView.each([&](FTriangleBatcherComponent& TriangleBatcherComponent)
                {
                    ProcessBatchedTriangles(TriangleBatcherComponent);
                });
            }, ETaskPriority::High);

            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Process Widget Primitives");

                const FFrustum& WidgetFrustum = SceneGlobalData.CullData.Frustum;
                const bool      bCullWidgets   = SceneGlobalData.CullData.bFrustumCull != 0u;

                WidgetView.each([&](entt::entity Entity, SWidgetComponent& WidgetComponent)
                {
                    FWidgetRuntime& Runtime = WidgetComponent.Runtime;

                    // Frustum cull against the authoritative view (same one meshes use). Transform+size only,
                    // so it works pre-build; drives whether TickWorldWidgets lays out + rasterizes the RT.
                    const FMatrix4 World = TransformStorage.get(Entity).GetWorldMatrix();
                    const FVector3 Center = FVector3(World[3]);
                    const float ScaleXY = Math::Max(Math::Length(FVector3(World[0])), Math::Length(FVector3(World[1])));
                    const float Radius  = 0.5f * Math::Length(WidgetComponent.WorldSize) * Math::Max(1.0f, ScaleXY);

                    const bool bVisible = !bCullWidgets || WidgetFrustum.IntersectsSphere(Center, Radius);
                    Runtime.bVisible = bVisible;

                    if (!bVisible)
                    {
                        return;
                    }

                    // Runtime state is filled by RmlUi::TickWorldWidgets earlier this frame (same
                    // game thread). No document / id means the RT isn't ready (first frame, empty path).
                    if (Runtime.Document == nullptr || Runtime.ResourceID < 0)
                    {
                        return;
                    }

                    FWidgetInstance& Widget = WidgetInstances.emplace_back();
                    Widget.Transform        = World;
                    Widget.WorldSize        = WidgetComponent.WorldSize;
                    Widget.TextureIndex     = (uint32)Runtime.ResourceID;
                    Widget.Flags            = WidgetComponent.bBillboard ? WIDGET_FLAG_BILLBOARD : 0u;
                    Widget.ColorPack        = PackColor(WidgetComponent.Tint);
                    Widget.EntityID         = entt::to_integral(Entity);

                    // Keep the RT alive for this frame's GPU work: the widget/picker passes sample it
                    // bindlessly, and deleting the entity frees Runtime.Target. KeepAlive'd in RenderView.
                    if (Runtime.Target)
                    {
                        PinnedWidgetRTs.push_back(Runtime.Target);
                    }
                });

                // World-space UI blends with alpha and writes no depth, so overlapping widgets need
                // painter's order. Sort farthest-first by distance to the camera.
                if (WidgetInstances.size() > 1)
                {
                    const FVector3 CameraPos = FVector3(SceneGlobalData.CameraData.Location);
                    eastl::sort(WidgetInstances.begin(), WidgetInstances.end(),
                        [CameraPos](const FWidgetInstance& A, const FWidgetInstance& B)
                        {
                            const FVector3 DA = FVector3(A.Transform[3]) - CameraPos;
                            const FVector3 DB = FVector3(B.Transform[3]) - CameraPos;
                            return Math::Dot(DA, DA) > Math::Dot(DB, DB);
                        });
                }
            }, ETaskPriority::High);

            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Process Text Primitives");

                const FFrustum& TextFrustum = SceneGlobalData.CullData.Frustum;
                const bool      bCullText   = SceneGlobalData.CullData.bFrustumCull != 0u;
                const FVector3  CamRight    = FVector3(SceneGlobalData.CameraData.Right);
                const FVector3  CamUp       = FVector3(SceneGlobalData.CameraData.Up);

                TextView.each([&](entt::entity Entity, STextComponent& TextComponent)
                {
                    if (TextComponent.Text.empty())
                    {
                        return;
                    }

                    // Fall back to the engine default font when none is set, or its atlas failed to bake.
                    CFont* Font = TextComponent.Font.Get();
                    if (Font == nullptr || !Font->HasAtlas())
                    {
                        Font = CFontManager::Get().GetDefaultFont();
                    }
                    if (Font == nullptr || !Font->HasAtlas())
                    {
                        return;
                    }

                    FRHIImage* Atlas = Font->GetAtlasImage();
                    if (Atlas == nullptr || Atlas->GetResourceID() < 0)
                    {
                        return;
                    }

                    const FMatrix4 World  = TransformStorage.get(Entity).GetWorldMatrix();
                    const FVector3 Origin = FVector3(World[3]);

                    const float HAlign = (TextComponent.HorizontalAlign == ETextHorizontalAlign::Left)   ? 0.0f
                                       : (TextComponent.HorizontalAlign == ETextHorizontalAlign::Center) ? 0.5f : 1.0f;
                    // Top places the text above the origin, Bottom below (block bottom/top anchored at origin).
                    const float VAlign = (TextComponent.VerticalAlign == ETextVerticalAlign::Top)        ? 1.0f
                                       : (TextComponent.VerticalAlign == ETextVerticalAlign::Middle)     ? 0.5f : 0.0f;

                    // Reshape only when an input that affects layout changed (text/font/align/spacing). Color,
                    // size, billboard and transform are applied per-frame below and never invalidate the cache.
                    FTextRenderCache& Cache = TextComponent.RenderCache;
                    const uint64      TextHash = Hash::GetHash64(TextComponent.Text);

                    const bool bCacheValid =
                           Cache.bValid
                        && Cache.Font        == Font
                        && Cache.FontVersion == Font->GetShapeVersion()
                        && Cache.TextHash    == TextHash
                        && Cache.TextLength  == (uint32)TextComponent.Text.size()
                        && Cache.HAlign      == TextComponent.HorizontalAlign
                        && Cache.VAlign      == TextComponent.VerticalAlign
                        && Cache.LineSpacing == TextComponent.LineSpacing;

                    if (!bCacheValid)
                    {
                        if (!Font->ShapeText(TextComponent.Text, HAlign, VAlign, TextComponent.LineSpacing, Cache.Glyphs))
                        {
                            return;
                        }

                        // Cache the bounding extent (em units) for the cull sphere alongside the glyphs, so the
                        // per-frame path skips this scan too.
                        float EmExtent = 0.0f;
                        for (const FShapedGlyph& S : Cache.Glyphs)
                        {
                            EmExtent = Math::Max(EmExtent, Math::Max(Math::Abs(S.Min.x), Math::Abs(S.Max.x)));
                            EmExtent = Math::Max(EmExtent, Math::Max(Math::Abs(S.Min.y), Math::Abs(S.Max.y)));
                        }

                        Cache.EmExtent    = EmExtent;
                        Cache.TextHash    = TextHash;
                        Cache.TextLength  = (uint32)TextComponent.Text.size();
                        Cache.Font        = Font;
                        Cache.FontVersion = Font->GetShapeVersion();
                        Cache.HAlign      = TextComponent.HorizontalAlign;
                        Cache.VAlign      = TextComponent.VerticalAlign;
                        Cache.LineSpacing = TextComponent.LineSpacing;
                        Cache.bValid      = true;
                    }

                    const TVector<FShapedGlyph>& Shaped = Cache.Glyphs;
                    if (Shaped.empty())
                    {
                        return;
                    }

                    // Frustum cull on a bounding sphere sized from the SHAPED extent (em units * WorldSize),
                    // so long / multi-line text isn't culled by a fixed radius that ignores its real width.
                    if (bCullText && !TextFrustum.IntersectsSphere(Origin, Cache.EmExtent * TextComponent.WorldSize * 1.5f))
                    {
                        return;
                    }

                    // World axes for the text plane. Oriented text uses the entity's X/Y (Y is up, matching
                    // the widget oriented convention); billboard text uses the camera's right/up.
                    FVector3 RightDir, UpDir;
                    if (TextComponent.bBillboard)
                    {
                        RightDir = CamRight;
                        UpDir    = CamUp;
                    }
                    else
                    {
                        RightDir = Math::Normalize(FVector3(World[0]));
                        UpDir    = Math::Normalize(FVector3(World[1]));
                    }

                    const FVector3 RightScaled = RightDir * TextComponent.WorldSize;
                    const FVector3 UpScaled    = UpDir    * TextComponent.WorldSize;
                    const uint32   Color       = PackColor(TextComponent.Color);
                    const uint32   First       = (uint32)GlyphInstances.size();

                    for (const FShapedGlyph& S : Shaped)
                    {
                        FGPUGlyph& G = GlyphInstances.emplace_back();
                        G.Origin    = Origin;
                        G.Pad0      = 0.0f;
                        G.Right     = RightScaled;
                        G.Pad1      = 0.0f;
                        G.Up        = UpScaled;
                        G.Pad2      = 0.0f;
                        G.UVRect    = S.UV;
                        G.PlaneMin  = S.Min;
                        G.PlaneMax  = S.Max;
                        G.ColorPack = Color;
                        G.EntityID  = entt::to_integral(Entity);
                    }

                    FFrameData::FTextBatch& Batch = TextBatches.emplace_back();
                    Batch.AtlasIndex    = (uint32)Atlas->GetResourceID();
                    Batch.AtlasWidth    = Font->GetAtlasWidth();
                    Batch.AtlasHeight   = Font->GetAtlasHeight();
                    Batch.DistanceRange = Font->GetDistanceRange();
                    Batch.FirstInstance = First;
                    Batch.Count         = (uint32)GlyphInstances.size() - First;
                    Batch.bDepthTest    = TextComponent.bDepthTest;

                    PinnedFontAtlases.push_back(Font->GetAtlasImageRef());
                });
            }, ETaskPriority::High);

            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Process Billboard Primitives");

                BillboardView.each([this, &BillboardInstances, &TransformStorage](entt::entity Entity, const SBillboardComponent& BillboardComponent)
                {
                    if (!BillboardComponent.Texture.IsValid() || !BillboardComponent.Texture->GetRHIRef()->IsValid())
                    {
                        return;
                    }

                    FBillboardInstance& Billboard   = BillboardInstances.emplace_back();
                    Billboard.TextureIndex          = BillboardComponent.Texture->GetRHIRef()->GetResourceID();
                    Billboard.Position              = TransformStorage.get(Entity).WorldTransform.Location;
                    Billboard.Size                  = BillboardComponent.Scale;
                    Billboard.EntityID              = entt::to_integral(Entity);
                });
                
                #if USING(WITH_EDITOR)
                // Editor visualizers: billboards for lights/cameras/sky/particles. Skipped in game/thumbnail worlds.
                if (!World->IsGameWorld())
                {
                    auto EmplaceVisualizer = [this, &BillboardInstances](entt::entity Entity, const FVector3& Position, ENamedImage Icon, const FVector4& Color, float Size = 0.20f)
                    {
                        FBillboardInstance& Billboard = BillboardInstances.emplace_back();
                        Billboard.TextureIndex        = GetNamedImage(Icon)->GetResourceID();
                        Billboard.ColorPack           = PackColor(Color);
                        Billboard.Position            = Position;
                        Billboard.Size                = Size;
                        Billboard.EntityID            = entt::to_integral(Entity);
                    };

                    // Skip editor viewport camera so the billboard doesn't sit on the user's view.
                    CameraView.each([&](entt::entity Entity, SCameraComponent&)
                    {
                        if (Registry.all_of<FEditorComponent>(Entity))
                        {
                            return;
                        }
                        EmplaceVisualizer(Entity, TransformStorage.get(Entity).WorldTransform.Location, ENamedImage::CameraIcon, FColor::White);
                    });

                    CharacterView.each([&](entt::entity Entity, SCharacterControllerComponent&)
                    {
                        EmplaceVisualizer(Entity, TransformStorage.get(Entity).WorldTransform.Location, ENamedImage::CharacterIcon, FColor::White);
                    });

                    PointLightView.each([&](entt::entity Entity, const SPointLightComponent& Light)
                    {
                        EmplaceVisualizer(Entity, TransformStorage.get(Entity).WorldTransform.Location, ENamedImage::PointLightIcon, FVector4(Light.LightColor, 1.0f));
                    });

                    SpotLightView.each([&](entt::entity Entity, const SSpotLightComponent& Light)
                    {
                        EmplaceVisualizer(Entity, TransformStorage.get(Entity).WorldTransform.Location, ENamedImage::SpotLightIcon, FVector4(Light.LightColor, 1.0f));
                    });

                    DirectionalView.each([&](entt::entity Entity, const SDirectionalLightComponent& Light)
                    {
                        const auto& Transform = Registry.get<STransformComponent>(Entity);
                        EmplaceVisualizer(Entity, Transform.WorldTransform.Location, ENamedImage::DirectionalLightIcon, FVector4(Light.Color, 1.0f));
                    });

                    SkyLightView.each([&](entt::entity Entity, const SSkyLightComponent&)
                    {
                        const auto& Transform = Registry.get<STransformComponent>(Entity);
                        EmplaceVisualizer(Entity, Transform.WorldTransform.Location, ENamedImage::SkyLightIcon, FVector4(1.0f));
                    });

                    ParticleView.each([&](entt::entity Entity, const SParticleSystemComponent&)
                    {
                        EmplaceVisualizer(Entity, TransformStorage.get(Entity).WorldTransform.Location, ENamedImage::ParticleSystemIcon, FVector4(1.0f));
                    });
                }
                #endif
            }, ETaskPriority::High);

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
                        auto& Transform = TransformStorage.get(Entity);
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
                        auto& Transform = TransformStorage.get(Entity);
                        ProcessSpotLight(SpotLight, Transform, LightCount);
                    }
                }
            });
            
            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Extract Terrain");

                Frame.Extracts.TerrainExtracts.clear();
                Frame.Extracts.LiveTerrainEntities.clear();

                for (entt::entity Entity : TerrainAllView)
                {
                    Frame.Extracts.LiveTerrainEntities.push_back(Entity);
                }

                for (entt::entity Entity : TerrainView)
                {
                    STerrainComponent& Terrain = TerrainView.get<STerrainComponent>(Entity);

                    FFrameData::FTerrainExtract& Item = Frame.Extracts.TerrainExtracts.emplace_back();
                    Item.Entity      = Entity;
                    Item.WorldMatrix = TransformStorage.get(Entity).GetWorldMatrix();
                    PrepareTerrainExtract(Terrain, Item.WorldMatrix, Item);
                }
            }, ETaskPriority::High);

            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Extract Particles");

                Frame.Extracts.ParticleExtracts.clear();
                Frame.Extracts.LiveParticleEntities.clear();

                for (entt::entity Entity : ParticleAllView)
                {
                    Frame.Extracts.LiveParticleEntities.push_back(Entity);
                }

                ParticleView.each([&](entt::entity Entity, SParticleSystemComponent& Component)
                {
                    FFrameData::FParticleExtract& Item = Frame.Extracts.ParticleExtracts.emplace_back();
                    Item.Entity              = Entity;
                    Item.WorldMatrix         = TransformStorage.get(Entity).GetWorldMatrix();
                    Item.EmitterOffset       = Component.EmitterOffset;
                    Item.TimeScale           = Component.TimeScale;
                    Item.SpawnRateMultiplier = Component.SpawnRateMultiplier;
                    Item.bEmit               = Component.bEmit;
                    Item.bBurstOnSpawn       = Component.bBurstOnSpawn;
                    Item.bForceBurst         = Component.bForceBurst;
                    Item.bForceReset         = Component.bForceReset;

                    Component.bForceBurst = false;
                    Component.bForceReset = false;

                    CParticleSystem* PS = Component.ParticleSystem.Get();
                    Item.bReady = PS && PS->IsReadyForSimulation();
                    if (Item.bReady)
                    {
                        Item.Resolved          = ResolveParticleParams(*PS, Component);
                        Item.bUsesCustomShader = PS->UsesCustomShader();
                        if (Item.bUsesCustomShader)
                        {
                            Item.CustomComputeShader = PS->GetCustomComputeShader();
                        }
                        if (CTexture* Tex = PS->Texture.Get())
                        {
                            if (FRHIImage* Image = Tex->GetRHIRef())
                            {
                                const int32 CacheIdx = Image->GetResourceID();
                                if (CacheIdx > 0)
                                {
                                    Item.TextureIndex = (uint32)CacheIdx;
                                }
                            }
                        }
                    }
                });
            }, ETaskPriority::High);

            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Extract Decals");

                Frame.Primitives.DecalExtracts.clear();
                Frame.Primitives.DecalBatches.clear();
                DecalSortScratch.clear();
                DecalGroupMinSort.clear();

                DecalView.each([&](entt::entity Entity, const SDecalComponent& Decal)
                {
                    CMaterialInterface* Material = Decal.DecalMaterial.Get();
                    if (Material == nullptr || !Material->IsReadyForRender())
                    {
                        return;
                    }
                    CMaterial* ShaderOwner = Material->GetMaterial();
                    if (ShaderOwner == nullptr || ShaderOwner->GetMaterialType() != EMaterialType::Decal)
                    {
                        return;
                    }
                    const int32 MaterialIndex = Material->GetMaterialIndex();
                    if (MaterialIndex < 0)
                    {
                        return;
                    }

                    FGPUDecal Item;
                    Item.DecalToWorld  = Math::Scale(TransformStorage.get(Entity).GetWorldMatrix(), Decal.Size);
                    Item.WorldToDecal  = Math::Inverse(Item.DecalToWorld);
                    Item.FadeAngleCos  = Math::Cos(Math::Radians(Math::Clamp(Decal.FadeAngle, 0.0f, 89.9f)));
                    Item.Opacity       = Math::Clamp(Decal.Opacity, 0.0f, 1.0f);
                    Item.MaterialIndex = (uint32)MaterialIndex;
                    Item.Flags         = 0;

                    DecalSortScratch.push_back({ ShaderOwner, Decal.SortOrder, Item });
                });

                for (const FDecalSortEntry& E : DecalSortScratch)
                {
                    auto It = DecalGroupMinSort.find(E.ShaderOwner);
                    if (It == DecalGroupMinSort.end() || E.SortOrder < It->second)
                    {
                        DecalGroupMinSort[E.ShaderOwner] = E.SortOrder;
                    }
                }
                eastl::stable_sort(DecalSortScratch.begin(), DecalSortScratch.end(), [&](const FDecalSortEntry& A, const FDecalSortEntry& B)
                {
                    const int32 GA = DecalGroupMinSort[A.ShaderOwner];
                    const int32 GB = DecalGroupMinSort[B.ShaderOwner];
                    if (GA != GB)
                    {
                        return GA < GB;
                    }
                    if (A.ShaderOwner != B.ShaderOwner)
                    {
                        return A.ShaderOwner < B.ShaderOwner;
                    }
                    return A.SortOrder < B.SortOrder;
                });

                Frame.Primitives.DecalExtracts.reserve(DecalSortScratch.size());
                // Resolve RHI shaders here (material is alive); the batch stores refs, never the
                // CMaterial*, so deleting the decal asset can't dangle the render thread.
                CMaterial* PrevOwner = nullptr;
                for (uint32 i = 0; i < (uint32)DecalSortScratch.size(); ++i)
                {
                    Frame.Primitives.DecalExtracts.push_back(DecalSortScratch[i].Gpu);

                    CMaterial* Owner = DecalSortScratch[i].ShaderOwner;
                    if (Owner == PrevOwner && !Frame.Primitives.DecalBatches.empty())
                    {
                        Frame.Primitives.DecalBatches.back().Count++;
                    }
                    else
                    {
                        FFrameData::FDecalBatch& Batch = Frame.Primitives.DecalBatches.emplace_back();
                        Batch.Shaders.VertexShader = Owner->GetVertexShader();
                        Batch.Shaders.PixelShader  = Owner->GetPixelShader();
                        Batch.FirstInstance        = i;
                        Batch.Count                = 1u;
                        PrevOwner                  = Owner;
                    }
                }
            }, ETaskPriority::High);

            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Extract Water");

                Frame.Water.Surfaces.clear();
                Frame.Water.bUnderwaterActive = false;

                const FVector4& CamLoc = Frame.SceneGlobalData.CameraData.Location;
                const FVector3  CameraPos = FVector3(CamLoc.x, CamLoc.y, CamLoc.z);

                // Track the nearest water surface above the camera (largest local.y still below the plane).
                float BestUnderwaterLocalY = -1.0e30f;

                auto ResolveTexture = [](const TObjectPtr<CTexture>& Tex) -> uint32
                {
                    CTexture*  T     = Tex.Get();
                    FRHIImage* Image = T ? T->GetRHIRef() : nullptr;
                    return (Image && Image->GetResourceID() >= 0) ? (uint32)Image->GetResourceID() : ~0u;
                };

                WaterView.each([&](entt::entity Entity, const SWaterComponent& Water)
                {
                    const FMatrix4 WorldMatrix = TransformStorage.get(Entity).GetWorldMatrix();
                    const float ExtentX = Math::Max(Water.Extent.x, 0.01f);
                    const float ExtentZ = Math::Max(Water.Extent.y, 0.01f);

                    FGPUWater Item    = {};
                    Item.WaterToWorld = Math::Scale(WorldMatrix, FVector3(ExtentX, 1.0f, ExtentZ));
                    Item.WorldToWater = Math::Inverse(Item.WaterToWorld);

                    Item.ShallowColor = FVector4(Water.ShallowColor, 1.0f);
                    Item.DeepColor    = FVector4(Water.DeepColor, 1.0f);
                    Item.FoamColor    = FVector4(Water.FoamColor, 1.0f);

                    FVector2    Wind    = Water.WindDirection;
                    const float WindLen = Math::Sqrt(Wind.x * Wind.x + Wind.y * Wind.y);
                    Wind = (WindLen > 1e-4f) ? FVector2(Wind.x / WindLen, Wind.y / WindLen) : FVector2(1.0f, 0.0f);

                    Item.WindAndWave    = FVector4(Wind.x, Wind.y, Water.WindSpeed, Water.WaveAmplitude);
                    Item.WaveParams     = FVector4(Math::Clamp(Water.Choppiness, 0.0f, 1.0f),
                                                   Math::Max(Water.WaveScale, 0.05f),
                                                   (float)Math::Clamp(Water.WaveCount, 1, 8),
                                                   Math::Clamp(Water.DetailStrength, 0.0f, 1.0f));
                    Item.RefractReflect = FVector4(Math::Max(Water.RefractionStrength, 0.0f),
                                                   Math::Clamp(Water.ReflectionStrength, 0.0f, 1.0f),
                                                   Math::Clamp(Water.Roughness, 0.0f, 1.0f),
                                                   Math::Max(Water.FresnelPower, 1.0f));
                    Item.FoamAbsorb     = FVector4(Math::Max(Water.ShorelineFoamWidth, 0.0f),
                                                   Math::Clamp(Water.CrestFoamAmount, 0.0f, 1.0f),
                                                   Math::Max(Water.DepthFadeDistance, 0.01f),
                                                   Math::Max(Water.AbsorptionScale, 0.0f));
                    Item.SSRSpecOpacity = FVector4(Math::Max(Water.SSRMaxDistance, 1.0f),
                                                   (float)Math::Clamp(Water.SSRStepCount, 8, 128),
                                                   Math::Max(Water.SpecularIntensity, 0.0f),
                                                   Math::Clamp(Water.Opacity, 0.0f, 1.0f));
                    Item.DetailParams   = FVector4(Math::Max(Water.DetailTiling, 0.01f),
                                                   Math::Max(Water.DetailScrollSpeed, 0.0f),
                                                   Math::Max(Water.FoamTiling, 0.01f),
                                                   Math::Max(Water.FoamIntensity, 0.0f));

                    Item.DetailNormalIndex = ResolveTexture(Water.DetailNormalMap);
                    Item.FoamTextureIndex  = ResolveTexture(Water.FoamTexture);
                    Item.GridResolution    = (uint32)Math::Clamp(Water.GridResolution, 2, 512);
                    Item.Flags             = 0;

                    Frame.Water.Surfaces.push_back(Item);

                    // Underwater test: transform the camera into water-local space. Inside the XZ extent and
                    // below the surface (local.y < 0) means the camera is submerged in this body; keep the
                    // nearest surface overhead so the right body drives the underwater post-process.
                    const FVector4 LocalCam = Item.WorldToWater * FVector4(CameraPos, 1.0f);
                    if (Math::Abs(LocalCam.x) <= 0.5f && Math::Abs(LocalCam.z) <= 0.5f &&
                        LocalCam.y < 0.0f && LocalCam.y > BestUnderwaterLocalY)
                    {
                        BestUnderwaterLocalY = LocalCam.y;

                        const FVector3 SurfaceCenter = TransformStorage.get(Entity).GetWorldLocation();
                        Frame.Water.bUnderwaterActive = true;
                        Frame.Water.Underwater.PlaneNormalAndHeight = FVector4(0.0f, 1.0f, 0.0f, SurfaceCenter.y);
                        Frame.Water.Underwater.FogColorDensity      = FVector4(Water.UnderwaterFogColor, Math::Max(Water.UnderwaterFogDensity, 0.0f));
                        Frame.Water.Underwater.TintDistortion       = FVector4(Water.UnderwaterTint, Math::Max(Water.UnderwaterDistortion, 0.0f));
                        Frame.Water.Underwater.DeepColor            = FVector4(Water.DeepColor, 0.0f);
                    }
                });
            }, ETaskPriority::High);

            Graph.Add([&]
            {
                LUMINA_PROFILE_SECTION("Extract Paint Ops");
                Frame.Extracts.PaintOps.clear();
                World->DrainRenderTargetPaints(Frame.Extracts.PaintOps);
            }, ETaskPriority::High);

            Graph.AddDependency(PointLightTask, DLightTask);
            Graph.AddDependency(SpotLightTask, DLightTask);
            Graph.AddDependency(MergeNode, StaticNode);
            Graph.AddDependency(MergeNode, SkeletalNode);

            Graph.Dispatch();
            Graph.Wait();


            // LightCount can overshoot MAX_LIGHTS; clamp to match what Process*Light wrote.
            LightData.NumLights = Math::Min(LightCount.load(std::memory_order_acquire), (uint32)MAX_LIGHTS);

            // Serial fit/allocate after parallel light pass; shrinks when sum(area) exceeds atlas budget.
            AllocateShadowTiles();

            // Serial after parallel light pass; skylight below reads ActiveEnv + LightData.SunDirection set by ProcessDirectionalLight.
            const SEnvironmentComponent* ActiveEnv = nullptr;
            {
                LUMINA_PROFILE_SECTION("Environment Processing");

                RenderSettings.bHasEnvironment = false;
                RenderSettings.bSSAO           = false;
                EnvironmentParams              = FEnvironmentParams{};
                EnvironmentMapImage            = nullptr;
                // Set true below if any IBL input differs from the last bake snapshot.
                Frame.Volumetrics.bIBLDirty                = false;
                Frame.Volumetrics.bIBLConvolutionDirty     = false;

                EnvironmentView.each([this, &EnvironmentParams, &EnvironmentMapImage, &ActiveEnv] (const SEnvironmentComponent& Env)
                {
                    ActiveEnv = &Env;

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

                    EnvironmentParams.SolidSkyColor = FVector4(Env.SolidSkyColor, 0.0f);
                    EnvironmentParams.ZenithColor   = FVector4(Env.ZenithColor, Env.HorizonExponent);
                    EnvironmentParams.HorizonColor  = FVector4(Env.HorizonColor, 0.0f);
                    EnvironmentParams.GroundColor   = FVector4(Env.GroundColor, 0.0f);
                    EnvironmentParams.SunTint       = FVector4(Env.SunColorTint, Env.SunIntensity);
                    EnvironmentParams.Misc          = FVector4(SkyModeAsFloat,
                                                                Env.SunDiscScale,
                                                                Env.SkyExposure,
                                                                Env.MieAnisotropy);

                    EnvironmentParams.NightSkyColor = FVector4(Env.NightSkyColor, Env.NightBrightness);
                    EnvironmentParams.StarParams    = FVector4(Env.StarDensity,
                                                                Env.StarBrightness,
                                                                Env.StarTwinkleSpeed,
                                                                Env.StarSize);
                    EnvironmentParams.MoonParams    = FVector4(Env.MoonSize,
                                                                Env.MoonGlowSize,
                                                                Env.MoonBrightness,
                                                                Env.bMoonOpposeSun ? 1.0f : 0.0f);
                    EnvironmentParams.MoonDirection = FVector4(Env.MoonDirection, 0.0f);
                    EnvironmentParams.GalaxyParams  = FVector4(Env.GalaxyIntensity, Env.GalaxyTilt, 0.0f, 0.0f);
                });

                // Resolve the IBL bake resolution; the render thread rebuilds the cubes when it changes
                // (handled alongside the dirty-flag bookkeeping below). Keep the last tier if no env.
                Frame.Volumetrics.IBLResolution = ActiveEnv
                    ? ResolveIBLQuality(ActiveEnv->IBLQuality)
                    : LastExtractedIBLResolution;
            }

            // Skylight (ambient fill + IBL scale). Last enabled SSkyLightComponent wins.
            // bAmbientFromSky derives the color from the active sky (needs ActiveEnv + SunDirection).
            {
                LUMINA_PROFILE_SECTION("Skylight Processing");

                LightData.AmbientLight = FVector4(0.0f);

                SkyLightView.each([&LightData, ActiveEnv] (const SSkyLightComponent& Sky)
                {
                    if (!Sky.bAffectsWorld)
                    {
                        return;
                    }

                    FVector3 AmbientRGB = Sky.AmbientColor;
                    if (Sky.bAmbientFromSky && ActiveEnv)
                    {
                        if (ActiveEnv->SkyMode == ESkyMode::SolidColor)
                        {
                            AmbientRGB = ActiveEnv->SolidSkyColor;
                        }
                        else if (ActiveEnv->SkyMode == ESkyMode::Gradient)
                        {
                            // 70/30 zenith/horizon matches what an upward-facing surface would integrate.
                            AmbientRGB = ActiveEnv->ZenithColor * 0.7f + ActiveEnv->HorizonColor * 0.3f;
                        }
                        else // Dynamic / HDRI
                        {
                            // SunDirection points FROM surface TO sun, so .y is elevation.
                            const float SunHeight = LightData.bHasSun
                                ? Math::Clamp(LightData.SunDirection.y, -1.0f, 1.0f)
                                : 0.5f;
                            const float Day = Math::Clamp(SunHeight * 2.0f + 0.2f, 0.0f, 1.0f);
                            AmbientRGB = Math::Mix(FVector3(0.05f, 0.06f, 0.10f),
                                                  FVector3(0.40f, 0.55f, 0.85f),
                                                  Day);
                        }
                    }
                    LightData.AmbientLight = FVector4(AmbientRGB, Sky.Intensity);
                });

                // IBL cubes are only baked when an environment is present (otherwise cleared to black). Tell
                // the shader so a skylight-only scene falls back to a flat ambient instead of black.
                LightData.bHasIBL = RenderSettings.bHasEnvironment ? 1u : 0u;
            }

            // Exponential height fog. Last enabled component with density > 0 wins.
            {
                LUMINA_PROFILE_SECTION("Fog Processing");

                Frame.Volumetrics.bHasFog             = false;
                Frame.Volumetrics.bVolumetricFog      = false;
                Frame.Volumetrics.VolumetricStepCount = 16;
                Frame.Volumetrics.FogParams           = FExponentialHeightFogParams{};

                FogView.each([&Frame, &Registry] (entt::entity Entity, const SExponentialHeightFogComponent& Fog)
                {
                    if (!Fog.bEnabled || Fog.FogDensity <= 0.0f)
                    {
                        return;
                    }

                    float BaseHeight = Fog.FogBaseHeight;
                    if (const STransformComponent* Transform = Registry.try_get<STransformComponent>(Entity))
                    {
                        BaseHeight += Transform->WorldTransform.Location.y;
                    }

                    FExponentialHeightFogParams& P = Frame.Volumetrics.FogParams;
                    P.InscatteringColor = FVector4(Fog.FogInscatteringColor, Fog.FogDensity);
                    P.HeightParams      = FVector4(Fog.FogHeightFalloff, BaseHeight,
                                                    Fog.FogStartDistance, Fog.FogMaxOpacity);
                    P.DirectionalColor  = FVector4(Fog.DirectionalInscatteringColor,
                                                    Fog.DirectionalInscatteringExponent);
                    P.VolumetricParams  = FVector4(Fog.VolumetricScatteringIntensity,
                                                    Fog.VolumetricAnisotropy,
                                                    Fog.VolumetricMaxDistance,
                                                    Fog.DirectionalInscatteringStartDistance);

                    Frame.Volumetrics.bHasFog             = true;
                    Frame.Volumetrics.bVolumetricFog      = Fog.bVolumetricFog;
                    Frame.Volumetrics.VolumetricStepCount = (uint32)Math::Clamp(Fog.VolumetricStepCount, 4, 128);
                });
            }
        }


        SceneGlobalData.CullData.InstanceNum = (uint32)Instances.size();

        if (LightData.bHasSun)
        {
            const FVector3 SunDir = Math::Normalize(LightData.SunDirection);
            constexpr float ShadowSweepDistance = 2000.0f;
            SceneGlobalData.CullData.ShadowFrustum   = SceneGlobalData.CullData.Frustum.Extruded(SunDir, ShadowSweepDistance);
            SceneGlobalData.CullData.bHasDirectional = 1u;
        }
        else
        {
            SceneGlobalData.CullData.ShadowFrustum   = SceneGlobalData.CullData.Frustum;
            SceneGlobalData.CullData.bHasDirectional = 0u;
        }

        // Populates CullViews[]/IndirectArgs[]; runs after AllocateShadowTiles so shadow VPs are settled.
        // Use the snapshot's view volume -- SceneViewport is render-thread state and may be the capture's.
        BuildCullViews(ExtractFrame->ViewVolume);
    }

    void FForwardRenderScene::CompileDrawCommands_RenderThread(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SCOPE();
        LUMINA_MEMORY_SCOPE("Render Scene");

        FFrameData& Frame = *RenderFrame;
        auto& SceneGlobalData            = Frame.SceneGlobalData;
        const auto& Instances            = Frame.Geometry.Instances;
        const auto& BonesData            = Frame.Geometry.BonesData;
        const auto& CullViews            = Frame.Views.CullViews;
        const auto& IndirectArgs         = Frame.Views.IndirectArgs;
        const auto& InstanceMeshletPrefix= Frame.Geometry.InstanceMeshletPrefix;
        const auto& LightData            = Frame.Lighting.LightData;
        const auto& EnvironmentParams    = Frame.Volumetrics.EnvironmentParams;
        FRHIImage* EnvironmentMapImage   = Frame.Volumetrics.EnvironmentMapImage;
        const auto& BillboardInstances   = Frame.Primitives.BillboardInstances;
        const uint32 TotalMeshletBound   = Frame.Views.TotalMeshletBound;
        const uint32 NumDrawsPerView     = Frame.Views.NumDrawsPerView;
        bool& bIBLDirty                  = Frame.Volumetrics.bIBLDirty;
        bool& bIBLConvolutionDirty       = Frame.Volumetrics.bIBLConvolutionDirty;

        const uint32 NumCullViews                  = (uint32)CullViews.size();
        const uint32 NumDraws                      = NumDrawsPerView;
        SceneGlobalData.CullData.NumDraws          = NumDraws;
        SceneGlobalData.CullData.TotalMeshletBound = TotalMeshletBound;

        // Sizes for the persistent per-frame buffers (all other CPU-dynamic data is transient): the
        // GPU-written pre-skinned vertex buffer. Debug line/triangle geometry is ring-allocated at draw time.
        const SIZE_T PreSkinnedSize       = Math::Max<SIZE_T>(sizeof(FPreSkinnedVertex),
                                            (SIZE_T)Frame.Geometry.TotalPreSkinnedVertices * sizeof(FPreSkinnedVertex));

        // Shared draw list: NumViews * TotalMeshletBound FMeshletDraw; views own disjoint slices via FCullView.DrawListOffset.
        const SIZE_T MeshletDrawListSize = Math::Max<SIZE_T>(
            sizeof(uint32) * 2,
            (SIZE_T)NumCullViews * (SIZE_T)TotalMeshletBound * sizeof(uint32) * 2);

        // Shared indirect args: NumViews * NumDraws FDrawIndirectArguments.
        const SIZE_T IndirectArgsSize = Math::Max<SIZE_T>(
            sizeof(FDrawIndirectArguments),
            IndirectArgs.size() * sizeof(FDrawIndirectArguments));

        // Worst case: every meshlet HZB-occluded in phase 0 (first frame). Stride matches FMeshletDeferred.
        const SIZE_T DeferListSize = Math::Max<SIZE_T>(
            sizeof(uint32) * 4,
            (SIZE_T)TotalMeshletBound * sizeof(uint32) * 4);

        // These buffers are reached by device address (PreSkinned) or bound directly (indirect),
        // not through any persistent descriptor set, so a resize needs no binding-set rebuild.
        RenderUtils::ResizeBufferIfNeeded(PreSkinnedVerticesBuffer, (uint32)PreSkinnedSize,   1.2f, PreSkinnedVerticesLowUsage);
        for (uint32 Slot = 0; Slot < FRAMES_IN_FLIGHT; ++Slot)
        {
            RenderUtils::ResizeBufferIfNeeded(IndirectArgsRing[Slot], (uint32)IndirectArgsSize, 1.2f, IndirectArgsRingLowUsage[Slot]);
            RenderUtils::ResizeBufferIfNeeded(MeshletDrawListRing[Slot], (uint32)MeshletDrawListSize, 1.2f, MeshletDrawListRingLowUsage[Slot]);
            RenderUtils::ResizeBufferIfNeeded(MeshletDeferListRing[Slot], (uint32)DeferListSize, 1.2f, MeshletDeferListRingLowUsage[Slot]);
        }

        {
            LUMINA_PROFILE_SECTION_COLORED("Write Scene Buffers", tracy::Color::OrangeRed3);

            // The only CPU upload left to a persistent buffer: indirect args (GPU-consumed). Debug
            // line/triangle geometry is ring-allocated at its draw site; everything else dynamic goes
            // through the transient ring below.
            CmdList.SetBufferState(GetIndirectArgs(), EResourceStates::CopyDest);
            CmdList.CommitBarriers();

            CmdList.DisableAutomaticBarriers();

            // FirstInstance pre-seeded so each atomic append in CullMeshlets lands in its own slice.
            if (!IndirectArgs.empty())
            {
                CmdList.WriteBuffer(GetIndirectArgs(), IndirectArgs.data(), IndirectArgs.size() * sizeof(FDrawIndirectArguments));
            }

            // Env/fog params are uploaded to the transient ring at their pass sites (Environment /
            // VolumetricFog*); here we only track changes to gate the costly IBL convolution below.
            const bool bEnvParamsChanged = !bEnvironmentParamsUploaded || std::memcmp(&EnvironmentParams, &LastUploadedEnvironmentParams, sizeof(FEnvironmentParams)) != 0;
            if (bEnvParamsChanged)
            {
                LastUploadedEnvironmentParams = EnvironmentParams;
                bEnvironmentParamsUploaded   = true;
            }

            const bool bSunChanged = LastIBLSunDirection != LightData.SunDirection || bLastIBLHasSun != (LightData.bHasSun != 0);
            const bool bMapChanged = LastIBLEnvironmentMap != EnvironmentMapImage;
            
            // Quality-tier change forces a full re-bake into the freshly-sized cubes (recreated on
            // the render thread by SyncIBLResolution before SkyCubeCapturePass runs).
            const bool bResChanged = Frame.Volumetrics.IBLResolution != LastExtractedIBLResolution; 
            LastExtractedIBLResolution = Frame.Volumetrics.IBLResolution;

            if (RenderSettings.bHasEnvironment &&
                (!bIBLValid || bEnvParamsChanged || bSunChanged || bMapChanged || bResChanged))
            {
                bIBLDirty                  = true;
                LastIBLEnvironmentParams   = EnvironmentParams;
                LastIBLEnvironmentMap      = EnvironmentMapImage;
                LastIBLSunDirection        = LightData.SunDirection;
                bLastIBLHasSun             = (LightData.bHasSun != 0);
                bIBLValid                  = true;
            }

            // Gate the costly irradiance + GGX prefilter on an angular sun threshold so
            // TOD animation doesn't pay full convolution every frame. cos(0.5 deg) ~ 0.99996.
            constexpr float SunCosThreshold = 0.99996f;
            const bool bConvHasSunChanged = bLastConvolvedHasSun != (LightData.bHasSun != 0);
            float SunCos = 1.0f;
            if (bLastConvolvedHasSun && LightData.bHasSun)
            {
                SunCos = Math::Dot(LastConvolvedSunDirection, LightData.SunDirection);
            }
            const bool bConvSunChanged = bConvHasSunChanged || (SunCos < SunCosThreshold);
            const bool bConvParamsChanged = std::memcmp(&LastConvolvedEnvironmentParams, &EnvironmentParams, sizeof(FEnvironmentParams)) != 0;
            const bool bConvMapChanged =
                LastConvolvedEnvironmentMap != EnvironmentMapImage;

            if (RenderSettings.bHasEnvironment &&
                (!bIBLConvolutionValid || bConvParamsChanged || bConvSunChanged || bConvMapChanged || bResChanged))
            {
                bIBLConvolutionDirty           = true;
                LastConvolvedEnvironmentParams = EnvironmentParams;
                LastConvolvedEnvironmentMap    = EnvironmentMapImage;
                LastConvolvedSunDirection      = LightData.SunDirection;
                bLastConvolvedHasSun           = (LightData.bHasSun != 0);
                bIBLConvolutionValid           = true;
            }
            // --- Bindless scene root: transient-upload the per-frame dynamic buffers and capture their
            // device addresses; GPU-written + CPU-static buffers use their stable GetAddress(). SceneData,
            // Clusters and the IBL indices are per-view (added by BuildViewSceneRoot). ---
            SceneRootShared = FSceneRoot{};
            SceneRootShared.Lights = CmdList.CopyTransient(LightData).Gpu;
            if (!Instances.empty())
            {
                SceneRootShared.Instances = CmdList.CopyTransientArray(Instances.data(), Instances.size()).Gpu;
            }
            if (!BonesData.empty())
            {
                SceneRootShared.Bones = CmdList.CopyTransientArray(BonesData.data(), BonesData.size()).Gpu;
            }
            if (!BillboardInstances.empty())
            {
                SceneRootShared.Billboards = CmdList.CopyTransientArray(BillboardInstances.data(), BillboardInstances.size()).Gpu;
            }
            if (!CullViews.empty())
            {
                SceneRootShared.CullViews = CmdList.CopyTransientArray(CullViews.data(), CullViews.size()).Gpu;
            }
            if (!Frame.Geometry.SkinDescriptors.empty())
            {
                SceneRootShared.SkinDescriptors = CmdList.CopyTransientArray(Frame.Geometry.SkinDescriptors.data(), Frame.Geometry.SkinDescriptors.size()).Gpu;
            }
            if (!Frame.Primitives.WidgetInstances.empty())
            {
                SceneRootShared.Widgets = CmdList.CopyTransientArray(Frame.Primitives.WidgetInstances.data(), Frame.Primitives.WidgetInstances.size()).Gpu;
            }
            if (!InstanceMeshletPrefix.empty())
            {
                SceneRootShared.InstanceMeshletPrefix = CmdList.CopyTransientArray(InstanceMeshletPrefix.data(), InstanceMeshletPrefix.size()).Gpu;
            }
            
            SceneRootShared.Materials          = GRenderManager->GetMaterialManager().GetMaterialBuffer()->GetAddress();
            SceneRootShared.MeshletDrawList    = GetMeshletDrawList()->GetAddress();
            SceneRootShared.PreSkinnedVertices = GetPreSkinnedVerticesBuffer()->GetAddress();
            if (Frame.CachedWorldSettings.bEnableSSAO)
            {
                SceneGlobalData.SSAOSettings.AOTextureIndex = (uint32)CurrentView->Images[(int)ENamedImage::SSAOBlur]->GetResourceID();
            }
            CurrentSceneRootAddr = BuildViewSceneRoot(CmdList, *CurrentView, CmdList.CopyTransient(SceneGlobalData).Gpu);

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

    // Resolve the material-pure portion of a slot, cached per-thread by material so the
    // ~10 virtual calls below don't repeat for every surface sharing a material.
    template <typename TComponent>
    static const FForwardRenderScene::FCachedMaterialResolve& ResolveMaterialCached(
        FForwardRenderScene::FThreadLocalDrawData& Local, const TComponent& MeshComponent, int16 SlotIdx)
    {
        CMaterialInterface* RawMaterial = MeshComponent.GetMaterialForSlot(SlotIdx);

        // Linear scan: per-thread unique material count is tiny (matches FindOrAddLocalBatch).
        for (FForwardRenderScene::FMaterialCacheEntry& Entry : Local.MaterialCache)
        {
            if (Entry.Key == RawMaterial)
            {
                return Entry.Resolve;
            }
        }

        CMaterialInterface* Material = RawMaterial;
        // Non-surface domains have different pipeline layouts / vertex stages (e.g. the decal box VS reads
        // a buffer the mesh path never binds); misassignment to a mesh falls back to the default material.
        if (IsValid(Material))
        {
            const EMaterialType DomainType = Material->GetMaterialType();
            if (DomainType == EMaterialType::Terrain ||
                DomainType == EMaterialType::PostProcess ||
                DomainType == EMaterialType::UI ||
                DomainType == EMaterialType::Decal)
            {
                Material = nullptr;
            }
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

        FForwardRenderScene::FMaterialCacheEntry& NewEntry = Local.MaterialCache.emplace_back();
        NewEntry.Key = RawMaterial;
        FForwardRenderScene::FCachedMaterialResolve& R = NewEntry.Resolve;
        R.VertexShader          = Material->GetVertexShader();
        R.PixelShader           = Material->GetPixelShader();
        R.DepthVertexShader     = nullptr;
        R.ShadowVertexShader    = nullptr;
        if (CMaterial* ConcreteMaterial = Material->GetMaterial(); ConcreteMaterial && ConcreteMaterial->UsesWorldPositionOffset())
        {
            R.DepthVertexShader  = ConcreteMaterial->GetDepthPrepassVertexShader();
            R.ShadowVertexShader = ConcreteMaterial->GetShadowVertexShader();
        }
        R.MaterialID            = (uint64)Material->GetMaterial();
        R.MaterialIdx           = (uint16)Material->GetMaterialIndex();
        R.bTranslucent          = bTranslucent;
        R.bMasked               = bMasked;
        R.bAdditive             = bAdditive;
        R.bTwoSided             = bTwoSided;
        R.bMaterialCastsShadows = Material->DoesCastShadows();
        return R;
    }

    template <typename TComponent>
    static FResolvedSlot ResolveSlot(FForwardRenderScene::FThreadLocalDrawData& Local, const TComponent& MeshComponent, int16 SlotIdx, bool bSignificantOccluder)
    {
        const FForwardRenderScene::FCachedMaterialResolve& M = ResolveMaterialCached(Local, MeshComponent, SlotIdx);

        EInstanceFlags Extra = EInstanceFlags::None;
        if (MeshComponent.bCastShadow && M.bMaterialCastsShadows)
        {
            Extra |= EInstanceFlags::CastShadow;
        }
        if (M.bTranslucent)
        {
            Extra |= EInstanceFlags::Translucent;
        }
        if (M.bMasked)
        {
            Extra |= EInstanceFlags::Masked;
        }
        if (M.bTwoSided)
        {
            Extra |= EInstanceFlags::TwoSided;
        }

        FResolvedSlot R;
        R.VertexShader       = M.VertexShader;
        R.PixelShader        = M.PixelShader;
        R.DepthVertexShader  = M.DepthVertexShader;
        R.ShadowVertexShader = M.ShadowVertexShader;
        R.MaterialID         = M.MaterialID;
        R.ExtraFlags         = Extra;
        R.MaterialIdx        = M.MaterialIdx;
        R.bTranslucent       = M.bTranslucent;
        R.bMasked            = M.bMasked;
        R.bAdditive          = M.bAdditive;
        R.bDrawInDepthPass   = MeshComponent.bUseAsOccluder && !M.bTranslucent && bSignificantOccluder;
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
            return (uint32)Math::Min((int32)Surface.NumLODs - 1, ForcedLODIndex);
        }
        if (bUseLODs)
        {
            return SelectLODIndex(Surface, DistanceOverRadius);
        }
        return 0u;
    }
    
    static uint32 ResolveShadowLOD(const FGeometrySurface& Surface, uint32 CameraLOD, int32 ShadowLODBias)
    {
        if (Surface.NumLODs == 0)
        {
            return 0u;
        }
        const int32 Biased = (int32)CameraLOD + ShadowLODBias;
        const int32 MaxLOD = (int32)Math::Min<uint32>(Surface.NumLODs - 1u, MAX_SHADOW_LOD);
        return (uint32)Math::Clamp(Biased, 0, MaxLOD);
    }

    void FForwardRenderScene::ProcessStaticMeshEntityInternal(entt::entity Entity, const SStaticMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local)
    {
        const FFrameData& Frame = *ExtractFrame;
        const auto& SceneCullContext = Frame.Geometry.SceneCullContext;
        const auto& SceneGlobalData  = Frame.SceneGlobalData;

        CMesh* Mesh = MeshComponent.StaticMesh;
        if (!IsValid(Mesh))
        {
            return;
        }

        const FMatrix4& TransformMatrix = TransformComponent.CachedMatrix;

        // World bounds first so we can reject before paying for mesh/surface lookups.
        // BoundsScale inflates cull sphere when animation/displacement push past asset AABB.
        const float     CullScale   = Math::Max(MeshComponent.BoundsScale, 1.0f);
        const FAABB     BoundingBox = Mesh->GetAABB().ToWorld(TransformMatrix);
        const FVector3 Center      = (BoundingBox.Min + BoundingBox.Max) * 0.5f;
        const FVector3 Extents     = BoundingBox.Max - Center;
        const float     Radius      = Math::Length(Extents) * CullScale;
        const FVector4 SphereBounds = FVector4(Center, Radius);

        if (!SceneCullContext.ShouldKeep(
                Center,
                Radius,
                MeshComponent.bCastShadow,
                MeshComponent.MaxDrawDistance,
                FVector3(SceneGlobalData.CameraData.Location)))
        {
            ++Local.Stats.NumInstancesCulled;
            return;
        }

        const FMeshResource& Resource = Mesh->GetMeshResource();
        const FMeshResource::FMeshBuffers& MB = Mesh->GetMeshBuffers();
        const uint64 MeshletHeaderAddress = MB.MeshletHeaderBuffer
            ? MB.MeshletHeaderBuffer->GetAddress()
            : 0ull;

        // Skip the hash entirely for consecutive same-mesh entities; instanced
        // scenes hit long runs of one mesh. Only consult the dedupe map on change.
        if (MeshletHeaderAddress && Mesh != Local.LastPinnedMesh && Local.PinnedMeshDedupe.emplace(Mesh, uint8(0)).second)
        {
            Local.PinnedMeshBuffers.push_back(MB.MeshletHeaderBuffer);
            Local.PinnedMeshBuffers.push_back(MB.MeshletBuffer);
            Local.PinnedMeshBuffers.push_back(MB.MeshletBoundsBuffer);
            Local.PinnedMeshBuffers.push_back(MB.MeshletVertexBuffer);
            Local.PinnedMeshBuffers.push_back(MB.MeshletTriangleBuffer);
        }
        if (MeshletHeaderAddress)
        {
            Local.LastPinnedMesh = Mesh;
        }

        // Angular size proxy (2r/d, squared to skip sqrt). Tiny props skip depth pre-pass.
        const FVector3 CameraPos  = FVector3(SceneGlobalData.CameraData.Location);
        const FVector3 ToCamera   = Center - CameraPos;
        const float     DistSq     = Math::Dot(ToCamera, ToCamera);
        constexpr float kMinAngularSize = 0.05f;
        const bool bSignificantOccluder = (Radius * Radius) > DistSq * (kMinAngularSize * kMinAngularSize);

        // Distance in radii; one sqrt+divide hoisted out of the per-surface loop.
        const float DistanceOverRadius = (Radius > 0.0f)
            ? (Math::Sqrt(DistSq) / Radius)
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
        EntityRecord.SkinMeshletStart  = 0u;
        EntityRecord.SkinMeshletCount  = 0u;
        EntityRecord.SkinSpanStart     = 0u;
        EntityRecord.SkinSliceSize     = 0u;
        EntityRecord.GlobalSkinnedBase = 0u;

        for (const FGeometrySurface& Surface : Resource.GeometrySurfaces)
        {
            const FResolvedSlot Slot = ResolveSlot(Local, MeshComponent, Surface.MaterialIndex, bSignificantOccluder);

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

    void FForwardRenderScene::ProcessSkeletalMeshEntityInternal(entt::entity Entity, SSkeletalMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local)
    {
        const FFrameData& Frame = *ExtractFrame;
        const auto& SceneCullContext = Frame.Geometry.SceneCullContext;
        const auto& SceneGlobalData  = Frame.SceneGlobalData;

        CMesh* Mesh = MeshComponent.SkeletalMesh;
        if (!IsValid(Mesh))
        {
            return;
        }

        // CachedMatrix is authoritative (ResolveAllDirtyTransforms ran serially before this gather). Read it
        // directly -- GetWorldMatrix() would re-walk the parent chain and mutate the registry from a worker.
        const FMatrix4& TransformMatrix = TransformComponent.CachedMatrix;

        // Reject before uploading bones (biggest per-entity skeletal cost).
        const float     CullScale   = Math::Max(MeshComponent.BoundsScale, 1.0f);
        const FAABB     BoundingBox = Mesh->GetAABB().ToWorld(TransformMatrix);
        const FVector3 Center      = (BoundingBox.Min + BoundingBox.Max) * 0.5f;
        const FVector3 Extents     = BoundingBox.Max - Center;
        const float     Radius      = Math::Length(Extents) * CullScale;
        const FVector4 SphereBounds = FVector4(Center, Radius);

        if (!SceneCullContext.ShouldKeep(
                Center,
                Radius,
                MeshComponent.bCastShadow,
                MeshComponent.MaxDrawDistance,
                FVector3(SceneGlobalData.CameraData.Location)))
        {
            ++Local.Stats.NumInstancesCulled;
            return;
        }

        // Shadow-only casters intentionally excluded: a pose feeding only an off-screen shadow
        // doesn't need per-frame eval.
        if (SceneCullContext.IsCameraVisible(
                Center,
                Radius,
                MeshComponent.MaxDrawDistance,
                FVector3(SceneGlobalData.CameraData.Location)))
        {
            MeshComponent.LastRenderedTime = World->GetTimeSinceWorldCreation();
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
                // Pack each mat4 down to its 3 rows (last row is always (0,0,0,1) for affine).
                Local.BonesData.reserve(Local.BonesData.size() + SkeletonBoneCount);
                for (const FMatrix4& M : MeshComponent.BoneTransforms)
                {
                    Local.BonesData.push_back(PackBoneTransform(M));
                }
            }
            else
            {
                // No active animation: BoneWorld * InvBindMatrix collapses to identity for every bone.
                static const FBoneTransform IdentityBone{ FVector4(1,0,0,0), FVector4(0,1,0,0), FVector4(0,0,1,0) };
                Local.BonesData.resize(Local.BonesData.size() + SkeletonBoneCount, IdentityBone);
            }
        }

        const uint32 NumBones = SkeletonBoneCount;

        // Angular size proxy; bind-pose bounds are conservative but fine for this coarse test.
        const FVector3 CameraPos  = FVector3(SceneGlobalData.CameraData.Location);
        const FVector3 ToCamera   = Center - CameraPos;
        const float     DistSq     = Math::Dot(ToCamera, ToCamera);
        constexpr float kMinAngularSize = 0.05f;
        const bool bSignificantOccluder = (Radius * Radius) > DistSq * (kMinAngularSize * kMinAngularSize);

        const float DistanceOverRadius = (Radius > 0.0f)
            ? (Math::Sqrt(DistSq) / Radius)
            : 0.0f;

        // Skeletal assets always carry FSkinnedVertex; flag is unconditional.
        EInstanceFlags BaseFlags = EInstanceFlags::Skinned;
        if (MeshComponent.bReceiveShadow)
        {
            BaseFlags |= EInstanceFlags::ReceiveShadow;
        }

        const FMeshResource::FMeshBuffers& MB = Mesh->GetMeshBuffers();
        const uint64 MeshletHeaderAddress = MB.MeshletHeaderBuffer
            ? MB.MeshletHeaderBuffer->GetAddress()
            : 0ull;

        // Skip the hash entirely for consecutive same-mesh entities; instanced
        // scenes hit long runs of one mesh. Only consult the dedupe map on change.
        if (MeshletHeaderAddress && Mesh != Local.LastPinnedMesh && Local.PinnedMeshDedupe.emplace(Mesh, uint8(0)).second)
        {
            Local.PinnedMeshBuffers.push_back(MB.MeshletHeaderBuffer);
            Local.PinnedMeshBuffers.push_back(MB.MeshletBuffer);
            Local.PinnedMeshBuffers.push_back(MB.MeshletBoundsBuffer);
            Local.PinnedMeshBuffers.push_back(MB.MeshletVertexBuffer);
            Local.PinnedMeshBuffers.push_back(MB.MeshletTriangleBuffer);
        }
        if (MeshletHeaderAddress)
        {
            Local.LastPinnedMesh = Mesh;
        }

        const uint32 EntityRecordIdx = (uint32)Local.EntityRecords.size();
        FEntityRecord& EntityRecord = Local.EntityRecords.emplace_back();
        EntityRecord.Transform            = TransformMatrix;
        EntityRecord.SphereBounds         = SphereBounds;
        EntityRecord.MeshletHeaderAddress = MeshletHeaderAddress;
        EntityRecord.CustomData           = MeshComponent.CustomPrimitiveData.Data.Packed;
        EntityRecord.EntityID             = entt::to_integral(Entity);
        EntityRecord.LocalBoneOffset      = LocalBoneOffset;
        EntityRecord.GlobalSkinnedBase    = 0u;   // resolved during merge

        // Skin-span accumulation folded into the draw-item loop below (it already computes the per-surface
        // offsets/counts the span needs), avoiding a second pass over GeometrySurfaces that re-resolved every LOD.
        const bool bAccumulateSkinSpan = (LocalBoneOffset != ~0u && MeshletHeaderAddress != 0ull);
        uint32 MinMeshlet    = ~0u;
        uint32 MaxMeshletEnd = 0u;

        for (const FGeometrySurface& Surface : Resource.GeometrySurfaces)
        {
            const FResolvedSlot Slot = ResolveSlot(Local, MeshComponent, Surface.MaterialIndex, bSignificantOccluder);

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

            // Same offsets/counts the dropped second pass recomputed; fold them into the span here.
            if (bAccumulateSkinSpan)
            {
                if (SurfaceMeshletCount > 0u)
                {
                    MinMeshlet    = Math::Min(MinMeshlet, SurfaceMeshletOffset);
                    MaxMeshletEnd = Math::Max(MaxMeshletEnd, SurfaceMeshletOffset + SurfaceMeshletCount);
                }
                if (ShadowMeshletCount > 0u)
                {
                    MinMeshlet    = Math::Min(MinMeshlet, ShadowMeshletOffset);
                    MaxMeshletEnd = Math::Max(MaxMeshletEnd, ShadowMeshletOffset + ShadowMeshletCount);
                }
            }
        }

        // Meshlets are LOD-major so the span is contiguous; GlobalSkinnedBase = compacted base - span start.
        uint32 SkinSliceSize = 0u;
        if (bAccumulateSkinSpan && MaxMeshletEnd > MinMeshlet)
        {
            const TVector<FMeshlet>& Meshlets = Resource.MeshletData.Meshlets;
            const FMeshlet& First = Meshlets[MinMeshlet];
            const FMeshlet& Last  = Meshlets[MaxMeshletEnd - 1u];
            EntityRecord.SkinMeshletStart = MinMeshlet;
            EntityRecord.SkinMeshletCount = MaxMeshletEnd - MinMeshlet;
            EntityRecord.SkinSpanStart    = First.VertexOffset;
            SkinSliceSize                 = (Last.VertexOffset + Last.VertexCount) - First.VertexOffset;
        }
        EntityRecord.SkinSliceSize = SkinSliceSize;
        if (SkinSliceSize == 0u)
        {
            EntityRecord.SkinMeshletStart = 0u;
            EntityRecord.SkinMeshletCount = 0u;
            EntityRecord.SkinSpanStart    = 0u;
        }
    }

    void FForwardRenderScene::MergeMeshDrawData(TVector<FThreadLocalDrawData>& ThreadLocal)
    {
        LUMINA_PROFILE_SECTION("Merge Mesh Draw Data");

        FFrameData& Frame = *ExtractFrame;
        auto& Instances             = Frame.Geometry.Instances;
        auto& BonesData            = Frame.Geometry.BonesData;
        auto& DrawCommands     = Frame.Geometry.DrawCommands;
        auto& OpaqueDrawList            = Frame.Geometry.OpaqueDrawList;
        auto& OpaqueOccluderDrawList    = Frame.Geometry.OpaqueOccluderDrawList;
        auto& TranslucentDrawList       = Frame.Geometry.TranslucentDrawList;
        auto& PinnedMeshBuffersThisFrame = Frame.Geometry.PinnedMeshBuffersThisFrame;
        auto& DrawMeshletStartOffsets   = Frame.Geometry.DrawMeshletStartOffsets;
        auto& InstanceMeshletPrefix     = Frame.Geometry.InstanceMeshletPrefix;
        auto& FrameStats             = Frame.FrameStats;
        uint32& TotalMeshletBound    = Frame.Views.TotalMeshletBound;
        uint32& NumDrawsPerView      = Frame.Views.NumDrawsPerView;

        const uint32 NumThreads = (uint32)ThreadLocal.size();

        // Bones merged serially: skinned meshes reference by absolute index.
        // Persistent member; assign keeps capacity across frames.
        TVector<uint32>& ThreadBoneBase = MergeThreadBoneBase;
        ThreadBoneBase.assign(NumThreads, 0u);
        uint32 TotalInstances = 0;
        uint64 TotalInstancesCulled = 0;
        for (uint32 t = 0; t < NumThreads; ++t)
        {
            FThreadLocalDrawData& Local = ThreadLocal[t];
            ThreadBoneBase[t] = (uint32)BonesData.size();
            BonesData.insert(BonesData.end(), Local.BonesData.begin(), Local.BonesData.end());

            for (FEntityRecord& Rec : Local.EntityRecords)
            {
                if (Rec.LocalBoneOffset == ~0u || Rec.SkinSliceSize == 0u)
                {
                    continue;
                }
                // Combined base folds in the span start so SkinnedVertexBase + M.VertexOffset
                // lands in the compacted slice (unsigned wrap; rendered M.VertexOffset >= span start).
                const uint32 CompactedBase = Frame.Geometry.TotalPreSkinnedVertices;
                Rec.GlobalSkinnedBase      = CompactedBase - Rec.SkinSpanStart;

                // One descriptor per meshlet -> the dispatch runs one workgroup per meshlet,
                // so meshlets skin concurrently instead of looping serially within one group.
                const uint32 BoneOffset = ThreadBoneBase[t] + Rec.LocalBoneOffset;
                const uint32 MeshletEnd = Rec.SkinMeshletStart + Rec.SkinMeshletCount;
                for (uint32 m = Rec.SkinMeshletStart; m < MeshletEnd; ++m)
                {
                    FSkinDescriptor& Desc     = Frame.Geometry.SkinDescriptors.emplace_back();
                    Desc.MeshletHeaderAddress = Rec.MeshletHeaderAddress;
                    Desc.BoneOffset           = BoneOffset;
                    Desc.SkinnedVertexBase    = Rec.GlobalSkinnedBase;
                    Desc.MeshletIndex         = m;
                    Desc.Pad                  = 0u;
                }

                Frame.Geometry.TotalPreSkinnedVertices += Rec.SkinSliceSize;
            }

            TotalInstances += (uint32)Local.Items.size();
            TotalInstancesCulled += Local.Stats.NumInstancesCulled;

            // Move the refs out (heap-backed, arena-independent); the source is
            // cleared next frame by ResetForFrame, so transfer ownership now.
            if (!Local.PinnedMeshBuffers.empty())
            {
                PinnedMeshBuffersThisFrame.reserve(PinnedMeshBuffersThisFrame.size() + Local.PinnedMeshBuffers.size());
                for (FRHIBufferRef& Ref : Local.PinnedMeshBuffers)
                {
                    PinnedMeshBuffersThisFrame.push_back(eastl::move(Ref));
                }
                Local.PinnedMeshBuffers.clear();
            }
        }
        FrameStats.NumInstancesCulled += TotalInstancesCulled;

        if (TotalInstances == 0)
        {
            return;
        }

        // Scheduling dominates the merge for typical scenes (tens of batches, bookkeeping passes), so each
        // dispatch+join costs more than the work. Fan out only above the threshold; below it runs inline.
        const bool bParallelMerge = TotalInstances > 4096;

        // Linear search: per-thread batch tables are tiny (tens of entries).
        // Scratch lives on scene members so capacity is reused across frames.
        MergeGlobalBatchKeys.clear();
        if (MergeGlobalBatchKeys.capacity() < 64)
        {
            MergeGlobalBatchKeys.reserve(64);
        }
        if (DrawCommands.capacity() < 64)
        {
            DrawCommands.reserve(64);
        }

        for (FThreadLocalDrawData& Local : ThreadLocal)
        {
            for (FLocalBatchEntry& LocalBatch : Local.LocalBatches)
            {
                uint32 GlobalIdx = ~0u;
                const uint32 NumGlobal = (uint32)MergeGlobalBatchKeys.size();
                for (uint32 g = 0; g < NumGlobal; ++g)
                {
                    if (MergeGlobalBatchKeys[g] == LocalBatch.Key)
                    {
                        GlobalIdx = g;
                        break;
                    }
                }
                if (GlobalIdx == ~0u)
                {
                    GlobalIdx = NumGlobal;
                    MergeGlobalBatchKeys.push_back(LocalBatch.Key);

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

        const uint32 NumBatches = (uint32)MergeGlobalBatchKeys.size();

        // Group LocalBatches by global batch. Outer vector resized up only;
        // inner vectors clear() in place to keep their heap capacity warm.
        if (MergeBatchToLocals.size() < NumBatches)
        {
            MergeBatchToLocals.resize(NumBatches);
        }
        for (uint32 b = 0; b < NumBatches; ++b)
        {
            MergeBatchToLocals[b].clear();
        }
        for (FThreadLocalDrawData& Local : ThreadLocal)
        {
            for (FLocalBatchEntry& LocalBatch : Local.LocalBatches)
            {
                MergeBatchToLocals[LocalBatch.GlobalBatchIndex].push_back(&LocalBatch);

                const uint32 NumLocal = (uint32)LocalBatch.LocalDraws.size();
                LocalBatch.LocalToGlobalDraw.resize(NumLocal);
                LocalBatch.LocalDrawWriteBase.resize(NumLocal);
            }
        }
        
        if (MergeGlobalDrawsPerBatch.size() < NumBatches)
        {
            MergeGlobalDrawsPerBatch.resize(NumBatches);
        }
        for (uint32 b = 0; b < NumBatches; ++b)
        {
            MergeGlobalDrawsPerBatch[b].clear();
        }
        {
            auto DedupBody = [&](const Task::FParallelRange& Range)
            {
                TFrameHashMap<uint64, uint32> Dedupe(FFrameArenaAllocator(FrameArenas[Range.Thread].get()));
                for (uint32 b = Range.Start; b < Range.End; ++b)
                {
                    Dedupe.clear();
                    TVector<FDrawKey>& Globals = MergeGlobalDrawsPerBatch[b];
                    for (FLocalBatchEntry* LB : MergeBatchToLocals[b])
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
            };
            if (bParallelMerge)
            {
                DedupTaskGraph.Reset();   // reuse the persistent graph (allocator block + capacity)
                FTaskGraph& DedupGraph = DedupTaskGraph;
                DedupGraph.AddParallelFor(NumBatches, 1, DedupBody);
                DedupGraph.Dispatch();
                DedupGraph.Wait();
            }
            else
            {
                DedupBody(Task::FParallelRange{ 0u, NumBatches, 0u });
            }
        }

        // Each batch gets a contiguous block of draw args.
        MergeBatchDrawArgBase.resize(NumBatches);
        uint32 TotalDrawArgs = 0;
        for (uint32 b = 0; b < NumBatches; ++b)
        {
            MergeBatchDrawArgBase[b]            = TotalDrawArgs;
            DrawCommands[b].IndirectDrawOffset  = TotalDrawArgs;
            DrawCommands[b].DrawCount           = (uint32)MergeGlobalDrawsPerBatch[b].size();
            TotalDrawArgs                       += (uint32)MergeGlobalDrawsPerBatch[b].size();
        }

        DrawMeshletStartOffsets.resize(TotalDrawArgs);
        Instances.resize(TotalInstances);
        NumDrawsPerView = TotalDrawArgs;

        MergeDrawInstanceCounts.assign(TotalDrawArgs, 0u);
        MergeMeshletCountsPerDraw.assign(TotalDrawArgs, 0u);

        // Per-batch GlobalDraw ranges are disjoint, so writes don't race.
        {
            auto CountBody = [&](const Task::FParallelRange& Range)
            {
                for (uint32 b = Range.Start; b < Range.End; ++b)
                {
                    const uint32 BatchBase = MergeBatchDrawArgBase[b];
                    for (FLocalBatchEntry* LB : MergeBatchToLocals[b])
                    {
                        const uint32 NumLocal = (uint32)LB->LocalDrawCounts.size();
                        for (uint32 ld = 0; ld < NumLocal; ++ld)
                        {
                            const uint32 GlobalDraw = BatchBase + LB->LocalToGlobalDraw[ld];
                            MergeDrawInstanceCounts[GlobalDraw]   += LB->LocalDrawCounts[ld];
                            MergeMeshletCountsPerDraw[GlobalDraw] += LB->LocalMeshletCounts[ld];
                        }
                    }
                }
            };
            if (bParallelMerge) { Task::ParallelFor(NumBatches, CountBody, 1); }
            else                { CountBody(Task::FParallelRange{ 0u, NumBatches, 0u }); }
        }

        // Prefix sum for instance offsets (serial; data dependency).
        MergeDrawInstanceOffsets.resize(TotalDrawArgs);
        {
            uint32 Running = 0;
            for (uint32 d = 0; d < TotalDrawArgs; ++d)
            {
                MergeDrawInstanceOffsets[d] = Running;
                Running += MergeDrawInstanceCounts[d];
            }
            DEBUG_ASSERT(Running == TotalInstances);
        }

        // Per-batch DrawCursor slices are disjoint; race-free.
        MergeDrawCursor.assign(MergeDrawInstanceOffsets.begin(), MergeDrawInstanceOffsets.begin() + TotalDrawArgs);
        {
            auto CursorBody = [&](const Task::FParallelRange& Range)
            {
                for (uint32 b = Range.Start; b < Range.End; ++b)
                {
                    const uint32 BatchBase = MergeBatchDrawArgBase[b];
                    for (FLocalBatchEntry* LB : MergeBatchToLocals[b])
                    {
                        const uint32 NumLocal = (uint32)LB->LocalDrawCounts.size();
                        for (uint32 ld = 0; ld < NumLocal; ++ld)
                        {
                            const uint32 GlobalDraw = BatchBase + LB->LocalToGlobalDraw[ld];
                            LB->LocalDrawWriteBase[ld] = MergeDrawCursor[GlobalDraw];
                            MergeDrawCursor[GlobalDraw] += LB->LocalDrawCounts[ld];
                        }
                    }
                }
            };
            if (bParallelMerge) { Task::ParallelFor(NumBatches, CursorBody, 1); }
            else                { CursorBody(Task::FParallelRange{ 0u, NumBatches, 0u }); }
        }

        // Per-draw meshlet prefix sum; CullMeshlets atomically appends into per-view disjoint slices.
        uint32 MeshletRunning = 0u;
        for (uint32 d = 0; d < TotalDrawArgs; ++d)
        {
            DrawMeshletStartOffsets[d] = MeshletRunning;
            MeshletRunning            += MergeMeshletCountsPerDraw[d];
        }
        TotalMeshletBound = MeshletRunning;

        // Each worker only touches its own Local data; in-place cursor advance needs no sync.
        {
            LUMINA_PROFILE_SECTION("Parallel Instance Write");

            auto InstanceWriteBody = [&](const Task::FParallelRange& Range)
            {
                for (uint32 t = Range.Start; t < Range.End; ++t)
                {
                    FThreadLocalDrawData& Local = ThreadLocal[t];
                    const uint32 BoneBase = ThreadBoneBase[t];

                    for (FProcessedDrawItem& Item : Local.Items)
                    {
                        FLocalBatchEntry& LocalBatch = Local.LocalBatches[Item.LocalBatchIndex];
                        const uint32 GlobalDraw = MergeBatchDrawArgBase[LocalBatch.GlobalBatchIndex] + LocalBatch.LocalToGlobalDraw[Item.LocalDrawIndex];

                        const uint32 WriteIdx = LocalBatch.LocalDrawWriteBase[Item.LocalDrawIndex]++;
                        const FEntityRecord& Entity = Local.EntityRecords[Item.EntityRecordIndex];

                        const uint32 GlobalBoneOffset = Entity.LocalBoneOffset != ~0u ? (BoneBase + Entity.LocalBoneOffset) : 0u;

                        FGPUInstance& Out = Instances[WriteIdx];
                        Out.Transform                  = Entity.Transform;
                        Out.SphereBounds               = Entity.SphereBounds;
                        Out.ShadowMeshletOffset        = Item.ShadowMeshletOffset;
                        Out.ShadowMeshletCount         = Item.ShadowMeshletCount;
                        Out.MeshletHeaderAddress       = Entity.MeshletHeaderAddress;
                        Out.DrawIDAndFlags             = PackDrawIDAndFlags(GlobalDraw, Item.Flags);
                        Out.SurfaceMeshletOffset       = Item.SurfaceMeshletOffset;
                        Out.SurfaceMeshletCount        = Item.SurfaceMeshletCount;
                        Out.CustomData                 = Entity.CustomData;
                        Out.BoneOffset                 = GlobalBoneOffset;
                        Out.MaterialIndex              = Item.MaterialIndex;
                        Out.EntityID                   = Entity.EntityID;
                        Out.SkinnedVertexBase          = Entity.GlobalSkinnedBase;
                    }
                }
            };
            if (bParallelMerge) { Task::ParallelFor(NumThreads, InstanceWriteBody, 1); }
            else                { InstanceWriteBody(Task::FParallelRange{ 0u, NumThreads, 0u }); }
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

        FrameStats.NumBatches = NumBatches;
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

    bool FForwardRenderScene::ShouldRequestShadow(const FVector3& LightPosition, float LightRadius) const
    {
        return ExtractFrame->SceneGlobalData.CullData.Frustum.IntersectsSphere(LightPosition, LightRadius);
    }

    void FForwardRenderScene::BuildSceneCullContext()
    {
        LUMINA_PROFILE_SCOPE();

        FFrameData& Frame = *ExtractFrame;
        auto& SceneCullContext = Frame.Geometry.SceneCullContext;
        auto& SceneGlobalData  = Frame.SceneGlobalData;

        SceneCullContext.Reset();
        SceneCullContext.bEnabled = RenderSettings.bCPUInstanceCull;
        SceneCullContext.Frustum  = SceneGlobalData.CullData.Frustum;

        if (!SceneCullContext.bEnabled)
        {
            return;
        }

        // Union in each active capture (preview camera) frustum so instances visible only to
        // a preview survive the CPU pre-cull and reach the GPU per-view cull.
        for (const FFrameData::FCaptureViewData& Capture : Frame.Views.CaptureViews)
        {
            SceneCullContext.CaptureFrusta.push_back(Capture.ViewVolume.GetFrustum());
        }

        FEntityRegistry& Registry = World->GetEntityRegistry();

        // First enabled directional light wins (matches ProcessDirectionalLight).
        auto DirectionalView = Registry.view<SDirectionalLightComponent>(entt::exclude<SDisabledTag>);
        for (entt::entity Entity : DirectionalView)
        {
            const SDirectionalLightComponent& Light = DirectionalView.get<SDirectionalLightComponent>(Entity);
            const float DirLenSq = Math::Dot(Light.Direction, Light.Direction);
            if (DirLenSq > 0.0001f)
            {
                SceneCullContext.SunDirection = Math::Normalize(Light.Direction);
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
            const FVector3 Position = Transform.WorldTransform.Location;
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
            const FVector3 Position = Transform.WorldTransform.Location;
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
        FFrameData& Frame                       = *ExtractFrame;
        FSceneLightData& LightData              = Frame.Lighting.LightData;
        TVector<FShadowRequest>& ShadowRequests = Frame.Lighting.ShadowRequests;
        FMutex& ShadowRequestMutex              = Frame.Lighting.ShadowRequestMutex;

        auto Lights = LightCount.fetch_add(1, std::memory_order_acquire);
        if (Lights >= MAX_LIGHTS)
        {
            NotifyMaxLightsHit();
            return;
        }

        FLight Light                = {};
        Light.Flags                 = ELightFlags::Point;
        Light.Falloff               = PointLight.Falloff;
        Light.Color                 = PackColor(FVector4(PointLight.LightColor, 1.0));
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
            const FVector3 CamPos = ExtractFrame->ViewVolume.GetViewPosition();
            const float Dist = Math::Distance(CamPos, Light.Position);
            constexpr float ResolutionScale = 2048.0f;
            const uint32 DesiredPixels = (uint32)((Light.Radius / Math::Max(Dist, 0.01f)) * ResolutionScale);

            FShadowRequest Req;
            Req.LightIndex      = Lights;
            Req.Type            = ELightType::Point;
            Req.DesiredPixels   = DesiredPixels;
            Req.DistanceToCamera = Dist;
            Req.Position        = Light.Position;
            Req.Direction       = FVector3(0.0f);
            Req.Up              = FVector3(0.0f);
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
        FFrameData& Frame = *ExtractFrame;
        auto& LightData         = Frame.Lighting.LightData;
        auto& ShadowRequests    = Frame.Lighting.ShadowRequests;
        auto& ShadowRequestMutex= Frame.Lighting.ShadowRequestMutex;

        auto Lights = LightCount.fetch_add(1, std::memory_order_acquire);
        if (Lights >= MAX_LIGHTS)
        {
            NotifyMaxLightsHit();
            return;
        }

        const FQuat WorldRotation = TransformComponent.GetWorldRotation();
        FVector3 UpdatedForward    = WorldRotation * FViewVolume::ForwardAxis;
        FVector3 UpdatedUp         = WorldRotation * FViewVolume::UpAxis;

        float InnerDegrees = SpotLight.InnerConeAngle;
        float OuterDegrees = SpotLight.OuterConeAngle;

        float InnerCos = Math::Cos(Math::Radians(InnerDegrees));
        float OuterCos = Math::Cos(Math::Radians(OuterDegrees));

        FLight Light                = {};
        Light.Flags                 = ELightFlags::Spot;
        Light.Position              = TransformComponent.WorldTransform.Location;
        Light.Direction             = Math::Normalize(UpdatedForward);
        Light.Falloff               = SpotLight.Falloff;
        Light.Color                 = PackColor(FVector4(SpotLight.LightColor, 1.0));
        Light.Intensity             = SpotLight.Intensity;
        Light.Radius                = SpotLight.Attenuation;
        Light.Angles                = FVector2(InnerCos, OuterCos);
        Light.ShadowDataIndex       = INDEX_NONE;
        if (SpotLight.bVolumetric)
        {
            Light.Flags             |= ELightFlags::Volumetric;
            Light.VolumetricIntensity = SpotLight.VolumetricIntensity;
        }

        if (SpotLight.bCastShadows && ShouldRequestShadow(Light.Position, Light.Radius))
        {
            const FVector3 CamPos = ExtractFrame->ViewVolume.GetViewPosition();
            const float Dist = Math::Distance(CamPos, Light.Position);
            constexpr float ResolutionScale = 2048.0f;
            const uint32 DesiredPixels = (uint32)((Light.Radius / Math::Max(Dist, 0.01f)) * ResolutionScale);

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
        FFrameData& Frame = *ExtractFrame;
        auto& LightData       = Frame.Lighting.LightData;
        auto& ShadowRequests  = Frame.Lighting.ShadowRequests;
        auto& ShadowDataCount = Frame.Lighting.ShadowDataCount;
        auto& PackedShadows   = Frame.Lighting.PackedShadows;

        if (ShadowRequests.empty())
        {
            return;
        }

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

        // Round-up-pow2 clamped to [Min, Max], matching FShadowAtlas::AllocateTile's
        // quantization so the area sum equals what the allocator consumes.
        TVector<uint32> Sizes;
        Sizes.resize(NumRequests);
        for (uint32 i = 0; i < NumRequests; ++i)
        {
            uint32 V = ShadowRequests[i].DesiredPixels;
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
            Sizes[i] = Math::Clamp(V, MinTile, MaxTile);
        }

        // Point lights cost six tiles (one per cube face), spots one; reflect that
        // in the area accounting so the shrink loop doesn't underestimate point cost.
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

        // Halve the largest tile until the set fits budget; that's the biggest
        // single-step area reduction available.
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
                // All at the floor and still over budget; let the overflow request
                // drop via AllocateTile INDEX_NONE rather than spin.
                break;
            }
            Sizes[LargestIdx] = LargestVal >> 1;
        }

        // Largest-first allocation keeps the quad-tree from fragmenting; small-first
        // would waste root splits on leaves.
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
                // Allocate all six cube-face tiles up front so a partial allocation
                // doesn't leave the light half-shadowed.
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

                // Near plane scales with radius; a fixed 0.01 collapses NDC z to the
                // last ~0.001 of the depth buffer, leaving no precision for PCF.
                const float ShadowNear = Math::Max(Req.Attenuation * 0.01f, 0.1f);
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

                const float ShadowNear = Math::Max(Req.Attenuation * 0.01f, 0.1f);
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
        FFrameData& Frame = *ExtractFrame;
        auto& CullViews                = Frame.Views.CullViews;
        auto& IndirectArgs             = Frame.Views.IndirectArgs;
        auto& DrawMeshletStartOffsets  = Frame.Geometry.DrawMeshletStartOffsets;
        auto& LightData                = Frame.Lighting.LightData;
        auto& PackedShadows            = Frame.Lighting.PackedShadows;
        auto& PointShadowCullViewBases = Frame.Views.PointShadowCullViewBases;
        auto& SpotShadowCullViewBases  = Frame.Views.SpotShadowCullViewBases;
        uint32& CascadeViewBase        = Frame.Views.CascadeViewBase;
        uint32& CameraLateViewIndex    = Frame.Views.CameraLateViewIndex;
        const uint32 TotalMeshletBound = Frame.Views.TotalMeshletBound;

        // Per-view: DrawList slice v = [v*TotalMeshletBound, (v+1)*TotalMeshletBound),
        // IndirectArgs slot (v,d) = v*NumDraws + d. CullMeshlets owns all atomic appends.
        const uint32 NumDraws = Frame.Views.NumDrawsPerView;

        auto PushView = [&](const FMatrix4& ViewProjection, const FVector3& Origin, uint32 Flags)
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
            View.ViewOriginAndFlags = FVector4(Origin, FlagsAsFloat);
            View.DrawListOffset     = ViewIndex * TotalMeshletBound;
            View.DrawListCapacity   = TotalMeshletBound;
            View.IndirectArgsOffset = ViewIndex * NumDraws;
            View.NumDraws           = NumDraws;
            CullViews.push_back(View);

            // Seed this view's indirect slice; FirstInstance lands inside the view's
            // draw-list slice so CullMeshlets' atomic-append can't overflow into another.
            const uint32 ViewDrawListBase = ViewIndex * TotalMeshletBound;
            for (uint32 d = 0; d < NumDraws; ++d)
            {
                FDrawIndirectArguments Arg;
                Arg.VertexCount           = MESHLET_VERTICES_PER_DRAW;
                Arg.InstanceCount         = 0u;
                Arg.StartVertexLocation   = 0u;
                Arg.StartInstanceLocation = ViewDrawListBase + DrawMeshletStartOffsets[d];
                IndirectArgs.push_back(Arg);
            }
            return ViewIndex;
        };

        // NumViews <= GMaxCullViews (guaranteed by AllocateShadowTiles). The +1 camera-late view is
        // appended last so earlier view-base indices stay valid; capture views come after that.
        const uint32 NumViews =
            1u +                                                        // Camera (early)
            (LightData.bHasSun ? (uint32)NumCascades : 0u) +            // CSM cascades
            (uint32)PackedShadows[(uint32)ELightType::Point].size() * 6u +
            (uint32)PackedShadows[(uint32)ELightType::Spot].size() +
            1u +                                                        // Camera (late, phase 1)
            (uint32)Frame.Views.CaptureViews.size();                          // Capture cameras (frustum-only)

        ASSERT(NumViews <= (uint32)GMaxCullViews);

        CullViews.reserve(NumViews);
        IndirectArgs.reserve((size_t)NumViews * (size_t)NumDraws);

        CascadeViewBase = ~0u;
        CameraLateViewIndex = ~0u;
        PointShadowCullViewBases.clear();
        PointShadowCullViewBases.reserve(PackedShadows[(uint32)ELightType::Point].size());
        SpotShadowCullViewBases.clear();
        SpotShadowCullViewBases.reserve(PackedShadows[(uint32)ELightType::Spot].size());

        // View 0: main camera. Frustum + occlusion gated by render settings so the
        // toggles disable culling at runtime; cone is always cheap so it stays.
        {
            const FMatrix4 CameraVP = ViewVolume.GetProjectionMatrix() * ViewVolume.GetViewMatrix();
            uint32 CameraFlags = ECullViewFlags::Cone;
            if (RenderSettings.bFrustumCull)
            {
                CameraFlags |= ECullViewFlags::Frustum;
            }
            if (RenderSettings.bOcclusionCull)
            {
                CameraFlags |= ECullViewFlags::Occlusion;
            }
            PushView(CameraVP, ViewVolume.GetViewPosition(), CameraFlags);
        }

        // CSM cascades: sun-aligned cone + cast-shadow-only + distance; frustum still
        // cheap and catches casters outside the cascade volume.
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

        // Point lights: 6 views each (one per cube face), cone apex at light position.
        // Parallel array records each face-0 view index for draw-pass lookup.
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
            const FMatrix4 CameraVP = ViewVolume.GetProjectionMatrix() * ViewVolume.GetViewMatrix();
            const uint32 CameraLateFlags =
                ECullViewFlags::Occlusion |
                ECullViewFlags::PhaseLate;

            CameraLateViewIndex = (uint32)CullViews.size();
            PushView(CameraVP, ViewVolume.GetViewPosition(), CameraLateFlags);
        }

        // Capture cameras: frustum-only (no occlusion, no two-pass HZB, no shadow flags). The shared cull
        // fills each one's draw-list slice, indexed via CameraViewIndex. Appended last so indices stay valid.
        for (FFrameData::FCaptureViewData& Capture : Frame.Views.CaptureViews)
        {
            const FMatrix4 CaptureVP = Capture.ViewVolume.GetProjectionMatrix() * Capture.ViewVolume.GetViewMatrix();
            const uint32 CaptureFlags = ECullViewFlags::Frustum | ECullViewFlags::Cone;
            Capture.CameraViewIndex = PushView(CaptureVP, Capture.ViewVolume.GetViewPosition(), CaptureFlags);
        }
    }

    // CCT -> linear RGB tint (Tanner Helland approx), normalized so the brightest channel is 1.0 -- tints
    // the sun without changing intensity (the separate Intensity multiplier does that). ~6500K ≈ white.
    static FVector3 ColorTemperatureToRGB(float Kelvin)
    {
        const float Temp = Math::Clamp(Kelvin, 1000.0f, 40000.0f) / 100.0f;

        float R;
        float G;
        float B;

        if (Temp <= 66.0f)
        {
            R = 255.0f;
            G = 99.4708025861f * Math::Log(Temp) - 161.1195681661f;
        }
        else
        {
            R = 329.698727446f * Math::Pow(Temp - 60.0f, -0.1332047592f);
            G = 288.1221695283f * Math::Pow(Temp - 60.0f, -0.0755148492f);
        }

        if (Temp >= 66.0f)
        {
            B = 255.0f;
        }
        else if (Temp <= 19.0f)
        {
            B = 0.0f;
        }
        else
        {
            B = 138.5177312231f * Math::Log(Temp - 10.0f) - 305.0447927307f;
        }

        FVector3 RGB = Math::Clamp(FVector3(R, G, B) / 255.0f, FVector3(0.0f), FVector3(1.0f));
        const float MaxC = Math::Max(RGB.x, Math::Max(RGB.y, RGB.z));
        return MaxC > 1e-4f ? RGB / MaxC : FVector3(1.0f);
    }

    void FForwardRenderScene::ProcessDirectionalLight(const SDirectionalLightComponent& DirectionalLight, TAtomic<uint32>& LightCount)
    {
        FFrameData& Frame = *ExtractFrame;
        auto& LightData            = Frame.Lighting.LightData;
        auto& ShadowDataCount      = Frame.Lighting.ShadowDataCount;
        auto& SceneGlobalData      = Frame.SceneGlobalData;

        LightData.bHasSun = true;
        // Primary camera (frame snapshot), not the render-thread SceneViewport: CSM cascades
        // fit to the primary view; capture views reuse these cascades rather than fitting their own.
        const FViewVolume& ViewVolume = Frame.ViewVolume;

        const float NearClip = ViewVolume.GetNear();
        const float FarClip  = ViewVolume.GetFar();

        // Optional black-body tint: physical sun color from correlated color temperature.
        FVector3 LightColor = DirectionalLight.Color;
        if (DirectionalLight.bUseTemperature)
        {
            LightColor *= ColorTemperatureToRGB(DirectionalLight.Temperature);
        }

        FLight Light            = {};
        Light.Flags             = ELightFlags::Directional;
        Light.Color             = PackColor(FVector4(LightColor, 1.0));
        Light.Intensity         = DirectionalLight.Intensity;
        Light.Direction         = Math::Normalize(DirectionalLight.Direction);
        Light.ShadowDataIndex   = INDEX_NONE;
        LightData.SunDirection  = Light.Direction;
        if (DirectionalLight.bVolumetric)
        {
            Light.Flags             |= ELightFlags::Volumetric;
            Light.VolumetricIntensity = DirectionalLight.VolumetricIntensity;
        }

        // Allocate a shadow slot only when the light casts shadows; ShadowDataIndex stays
        // INDEX_NONE otherwise, the sentinel everything downstream uses to skip shadow work.
        uint32 ShadowSlot                  = 0u;
        FLightShadowData* CascadeShadowData = nullptr;
        if (DirectionalLight.bCastShadows)
        {
            ShadowSlot = ShadowDataCount.fetch_add(1, std::memory_order_acquire);
            if (ShadowSlot < (uint32)MAX_SHADOWS)
            {
                Light.ShadowDataIndex = (int32)ShadowSlot;
                CascadeShadowData     = &LightData.Shadows[ShadowSlot];
            }
        }
        
        // Cascade shadows are now configured per-light. Feed the cull pass this
        // light's max distance so shadow-caster culling matches the cascades.
        SceneGlobalData.CullData.ShadowMaxDistance = DirectionalLight.ShadowMaxDistance;

        // Shadow tuning forwarded to the lit pixel shaders via the light buffer.
        LightData.ShadowParams  = FVector4(DirectionalLight.ShadowNormalBias,
                                            DirectionalLight.ShadowDepthBias,
                                            DirectionalLight.ShadowSoftness,
                                            DirectionalLight.CascadeBlend);
        LightData.ShadowParams2 = FVector4(DirectionalLight.ShadowDistanceFade,
                                            float(DirectionalLight.ShadowSampleCount),
                                            0.0f, 0.0f);

        const float CascadeSplitLambda = Math::Clamp(DirectionalLight.CascadeSplitLambda, 0.0f, 1.0f);

        constexpr float ShadowMinDistance   = 1.0f;

        const float ShadowFar  = Math::Min(FarClip, DirectionalLight.ShadowMaxDistance);
        const float ShadowNear = Math::Max(NearClip, ShadowMinDistance);
        const float ClipRange  = ShadowFar - ShadowNear;
        const float MinDepth   = ShadowNear;
        const float MaxDepth   = ShadowFar;
        const float DepthRatio = MaxDepth / Math::Max(MinDepth, 0.0001f);
        
        float CascadeFarDistances[NumCascades];
        for (int i = 0; i < NumCascades; ++i)
        {
            const float P       = (float)(i + 1) / (float)NumCascades;
            const float LogD    = MinDepth * Math::Pow(DepthRatio, P);
            const float UniD    = MinDepth + ClipRange * P;
            const float D       = CascadeSplitLambda * (LogD - UniD) + UniD;
            CascadeFarDistances[i]      = D;
            LightData.CascadeSplits[i]  = D; // World-distance, view-space Z.
        }
        
        const FMatrix4& CamView   = ViewVolume.GetViewMatrix();
        const float      CamFOV    = ViewVolume.GetFOV();
        const float      CamAspect = ViewVolume.GetAspectRatio();
        const FVector3  LightDir  = Light.Direction; // Toward the sun.
        
        float LastSplitDistance = ShadowNear;
        for (int i = 0; i < NumCascades; ++i)
        {
            const float SplitNear = LastSplitDistance;
            const float SplitFar  = CascadeFarDistances[i];

            // Per-cascade resolution (outer cascades smaller); texel-size math below
            // reads it so snap step and world-space texel pitch shift accordingly.
            const int   CascadeRes        = GCSMCascadeSizes[i];
            const float CascadeResFloat   = (float)CascadeRes;
            LightData.CascadeResolutions[i] = CascadeResFloat;

            // World-space corners of sub-frustum [SplitNear, SplitFar]. Standard-Z perspective
            // so ComputeFrustumCorners un-projects the canonical NDC cube despite reverse-Z.
            const FMatrix4 SliceProj = Math::Perspective(Math::Radians(CamFOV), CamAspect, SplitNear, SplitFar);
            const FMatrix4 SliceVP   = SliceProj * CamView;

            FVector3 Corners[8];
            FFrustum::ComputeFrustumCorners(SliceVP, Corners);

            // Bound the slice with a sphere, rotation-invariant, so the cascade
            // size doesn't pulse as the camera turns.
            FVector3 SphereCenter(0.0f);
            for (int j = 0; j < 8; ++j)
            {
                SphereCenter += Corners[j];
            }
            SphereCenter /= 8.0f;

            float Radius = 0.0f;
            for (int j = 0; j < 8; ++j)
            {
                Radius = Math::Max(Radius, Math::Length(Corners[j] - SphereCenter));
            }

            
            const float Octave    = std::exp2(std::floor(std::log2(Math::Max(Radius, 1e-4f))));
            const float QuantStep = Octave / 8.0f;
            Radius = std::ceil(Radius / QuantStep) * QuantStep;
            const float TexelSize = (Radius * 2.0f) / CascadeResFloat;

            // BackDistance pushes the light eye behind the cascade so off-screen occluders
            // still write depth; low sun angles need larger values (D/tan(theta) light-space height).
            const float BackDistance = Math::Max(DirectionalLight.CascadeBackDistance, 1.0f);
            const float OrthoRange   = Radius * 2.0f + BackDistance;

            // lookAt target = origin (not SphereCenter) so the rotation
            // depends only on LightDir; otherwise the texel snap below collapses.
            const FMatrix4 LightRotation = Math::LookAt(
                LightDir * (Radius + BackDistance),
                FVector3(0.0f),
                FViewVolume::UpAxis);


            FVector4 CenterLS = LightRotation * FVector4(SphereCenter, 1.0f);
            CenterLS.x = std::round(CenterLS.x / TexelSize) * TexelSize;
            CenterLS.y = std::round(CenterLS.y / TexelSize) * TexelSize;
            const FVector3 SnappedCenter = FVector3(Math::Inverse(LightRotation) * CenterLS);

            const FMatrix4 LightView = Math::LookAt(
                SnappedCenter + LightDir * (Radius + BackDistance),
                SnappedCenter,
                FViewVolume::UpAxis);
            
            FMatrix4 LightProjection = Math::Ortho(
                -Radius, +Radius,
                -Radius, +Radius,
                0.0f, OrthoRange);
            LightProjection[1][1] *= -1.0f;

            const FMatrix4 CascadeVP = LightProjection * LightView;
            if (CascadeShadowData)
            {
                CascadeShadowData->ViewProjection[i] = CascadeVP;
                
                FLightShadow& CascadeTile = CascadeShadowData->Shadow[i];
                CascadeTile.AtlasUVOffset = FVector2(
                    (float)GCSMCascadeOriginX[i] / (float)GCSMAtlasWidth,
                    (float)GCSMCascadeOriginY[i] / (float)GCSMAtlasHeight);
                CascadeTile.AtlasUVScale = FVector2(
                    (float)GCSMCascadeSizes[i]  / (float)GCSMAtlasWidth,
                    (float)GCSMCascadeSizes[i]  / (float)GCSMAtlasHeight);
                CascadeTile.ShadowMapIndex  = INDEX_NONE;
                CascadeTile.LightIndex      = 0;
                CascadeTile.ShadowDataIndex = (int32)ShadowSlot;
                CascadeTile._Padding        = 0;
            }

            // Expose half-extent so the lit pixel shader converts a shadow texel to a
            // world-space length for normal-offset bias.
            LightData.CascadeRadii[i] = Radius;

            // Feed this cascade's frustum to the shadow cull pass so small casters
            // that only touch cascade 0 don't pay VPC cost on cascades 1/2.
            SceneGlobalData.CullData.CascadeFrustum[i] = FFrustum::FromViewProjection(CascadeVP);

            LastSplitDistance = SplitFar;
        }

        LightCount.fetch_add(1, std::memory_order_acquire);
        LightData.Lights[0] = Light;
    }

    uint32 FForwardRenderScene::PrepareBatchedLines(FLineBatcherComponent& Batcher)
    {
        using FLineInstance = FLineBatcherComponent::FLineInstance;
        constexpr uint32 kMaxBuckets = FLineBatchScratch::kMaxBuckets;

        // Gather the input as fixed-size chunks over the persistent carry-over list + each per-worker
        // produce buffer. No drain/copy: each chunk points straight into its source. Chunking (rather than
        // one task per buffer) keeps the parallel batch balanced even when one producer slot emitted far
        // more lines than the others. Runs inline before graph Dispatch, so the batch node's chunks are
        // ready and enqueued up front (no runtime bubble).
        constexpr uint32 kChunkLines = 4096;

        LineChunkScratch.clear();
        uint32 LineCount = 0;
        auto AddSource = [&](const FLineInstance* Data, uint32 Num)
        {
            for (uint32 Off = 0; Off < Num; Off += kChunkLines)
            {
                LineChunkScratch.push_back(FLineChunk{ Data + Off, Math::Min(kChunkLines, Num - Off) });
            }
            LineCount += Num;
        };

        if (!Batcher.Lines.empty())
        {
            AddSource(Batcher.Lines.data(), (uint32)Batcher.Lines.size());
        }
        for (TVector<FLineInstance>& Buffer : Batcher.ThreadBuffers)
        {
            if (!Buffer.empty())
            {
                AddSource(Buffer.data(), (uint32)Buffer.size());
            }
        }

        if (LineCount == 0)
        {
            return 0;
        }

        const uint32 NumThreads = GTaskSystem->GetNumTaskThreads();
        if (LineBatchScratch.size() < NumThreads)
        {
            LineBatchScratch.resize(NumThreads);
        }
        for (uint32 t = 0; t < NumThreads; ++t)
        {
            FLineBatchScratch& S = LineBatchScratch[t];
            S.NumBuckets = 0;
            S.Survivors.clear();
            for (uint32 b = 0; b < kMaxBuckets; ++b)
            {
                S.BucketVerts[b].clear();
            }
        }

        return (uint32)LineChunkScratch.size();
    }

    // Parallel cull + per-worker bucketing over the prepared chunks. Each worker writes only its own scratch
    // slot (FParallelRange::Thread is a dense worker slot, and a worker runs one chunk at a time), so there
    // is no contention. A copy of the line is taken so the lifetime decrement is local; surviving persistent
    // lines are collected and rebuilt into Lines by FinalizeBatchedLines.
    void FForwardRenderScene::BatchLineChunks(const Task::FParallelRange& Range)
    {
        LUMINA_PROFILE_SECTION("Batch Lines");

        using FLineInstance = FLineBatcherComponent::FLineInstance;
        constexpr uint32 kMaxBuckets = FLineBatchScratch::kMaxBuckets;

        const float    Dt      = ExtractFrame->SceneGlobalData.DeltaTime;
        FFrustum       Frustum = ExtractFrame->SceneGlobalData.CullData.Frustum; // local: IsInside is non-const
        FLineBatchScratch&     S      = LineBatchScratch[Range.Thread];
        const FLineChunk* const Chunks = LineChunkScratch.data();

        for (uint32 c = Range.Start; c < Range.End; ++c)
        {
            const FLineChunk& Chunk = Chunks[c];
            for (uint32 i = 0; i < Chunk.Count; ++i)
            {
                FLineInstance Line = Chunk.Data[i];

                const FAABB LineBounds(Math::Min(Line.Start, Line.End), Math::Max(Line.Start, Line.End));
                if (Frustum.IsInside(LineBounds))
                {
                    uint32 Idx = ~0u;
                    for (uint32 b = 0; b < S.NumBuckets; ++b)
                    {
                        if (S.BucketDepthTest[b] == Line.bDepthTest &&
                            Math::EpsilonEqual(S.BucketThickness[b], Line.Thickness, LE_SMALL_NUMBER))
                        {
                            Idx = b;
                            break;
                        }
                    }
                    if (Idx == ~0u)
                    {
                        // Clamp to the last bucket if the (thickness, depth-test) combos ever exceed
                        // the cap; the original serial path made the same <= kMaxBuckets assumption.
                        Idx = (S.NumBuckets < kMaxBuckets) ? S.NumBuckets++ : (kMaxBuckets - 1);
                        S.BucketThickness[Idx] = Line.Thickness;
                        S.BucketDepthTest[Idx] = Line.bDepthTest;
                    }

                    TVector<FSimpleElementVertex>& V = S.BucketVerts[Idx];
                    V.push_back({ Line.Start, Line.ColorPacked });
                    V.push_back({ Line.End,   Line.ColorPacked });
                }

                if (Line.bSingleFrame)
                {
                    continue;
                }

                Line.RemainingLifetime -= Dt;
                if (Line.RemainingLifetime > 0.0f)
                {
                    S.Survivors.push_back(Line);
                }
            }
        }
    }

    // Runs after the batch node: merge per-worker buckets, lay out a contiguous vertex range, scatter, and
    // rebuild the persistent line list. Reads the same LineBatchScratch the batch node filled.
    void FForwardRenderScene::FinalizeBatchedLines(FLineBatcherComponent& Batcher)
    {
        using FLineInstance = FLineBatcherComponent::FLineInstance;
        constexpr uint32 kMaxBuckets = FLineBatchScratch::kMaxBuckets;

        FFrameData& Frame       = *ExtractFrame;
        auto& SimpleVertices    = Frame.Primitives.SimpleVertices;
        auto& LineBatches       = Frame.Primitives.LineBatches;

        TVector<FLineInstance>& Lines = Batcher.Lines;
        auto& ThreadBuffers           = Batcher.ThreadBuffers;

        const uint32 NumThreads = GTaskSystem->GetNumTaskThreads();

        // Merge per-worker buckets into a global table keyed by (thickness, depth-test), and
        // accumulate each global bucket's vertex count.
        struct FGlobalBucket
        {
            float   Thickness;
            uint8   bDepthTest;
            uint32  VertexCount;
            uint32  StartVertex;
        };
        TFixedVector<FGlobalBucket, kMaxBuckets> Global;

        for (uint32 t = 0; t < NumThreads; ++t)
        {
            FLineBatchScratch& S = LineBatchScratch[t];
            for (uint32 b = 0; b < S.NumBuckets; ++b)
            {
                const uint32 VC = (uint32)S.BucketVerts[b].size();
                if (VC == 0)
                {
                    S.GlobalBucket[b] = ~0u;
                    continue;
                }

                uint32 G = ~0u;
                for (uint32 k = 0, n = (uint32)Global.size(); k < n; ++k)
                {
                    if (Global[k].bDepthTest == S.BucketDepthTest[b] &&
                        Math::EpsilonEqual(Global[k].Thickness, S.BucketThickness[b], LE_SMALL_NUMBER))
                    {
                        G = k;
                        break;
                    }
                }
                if (G == ~0u)
                {
                    G = (Global.size() < kMaxBuckets) ? (uint32)Global.size() : (kMaxBuckets - 1);
                    if (G == (uint32)Global.size())
                    {
                        Global.emplace_back(FGlobalBucket{ S.BucketThickness[b], S.BucketDepthTest[b], 0u, 0u });
                    }
                }
                Global[G].VertexCount += VC;
                S.GlobalBucket[b] = G;
            }
        }

        // Prefix sum to give each global bucket a contiguous range in SimpleVertices.
        const uint32 BaseVertex = (uint32)SimpleVertices.size();
        uint32 Cursor = BaseVertex;
        for (FGlobalBucket& B : Global)
        {
            B.StartVertex = Cursor;
            Cursor += B.VertexCount;
        }
        SimpleVertices.resize(Cursor);

        const bool bParallel = (Cursor - BaseVertex) > 4096;

        // Hand each (worker, bucket) a disjoint sub-range within its global bucket so the copy is race-free.
        TFixedVector<uint32, kMaxBuckets> GlobalWrite;
        GlobalWrite.resize(Global.size());
        for (uint32 k = 0, n = (uint32)Global.size(); k < n; ++k)
        {
            GlobalWrite[k] = Global[k].StartVertex;
        }
        for (uint32 t = 0; t < NumThreads; ++t)
        {
            FLineBatchScratch& S = LineBatchScratch[t];
            for (uint32 b = 0; b < S.NumBuckets; ++b)
            {
                const uint32 VC = (uint32)S.BucketVerts[b].size();
                if (VC == 0)
                {
                    continue;
                }
                const uint32 G = S.GlobalBucket[b];
                S.WriteCursor[b] = GlobalWrite[G];
                GlobalWrite[G] += VC;
            }
        }

        // Parallel scatter: each worker copies its buckets into their reserved slices.
        FSimpleElementVertex* const Dst = SimpleVertices.data();
        auto CopyBody = [&](const Task::FParallelRange& Range)
        {
            LUMINA_PROFILE_SECTION("Copy Batched Lines");
            for (uint32 t = Range.Start; t < Range.End; ++t)
            {
                FLineBatchScratch& S = LineBatchScratch[t];
                for (uint32 b = 0; b < S.NumBuckets; ++b)
                {
                    const TVector<FSimpleElementVertex>& V = S.BucketVerts[b];
                    if (V.empty())
                    {
                        continue;
                    }
                    std::memcpy(Dst + S.WriteCursor[b], V.data(), V.size() * sizeof(FSimpleElementVertex));
                }
            }
        };
        if (bParallel) { Task::ParallelFor(NumThreads, CopyBody, 1); }
        else           { CopyBody(Task::FParallelRange{ 0u, NumThreads, 0u }); }

        LineBatches.reserve(LineBatches.size() + Global.size());
        for (const FGlobalBucket& B : Global)
        {
            LineBatches.emplace_back(B.StartVertex, B.VertexCount, B.Thickness, (bool)B.bDepthTest);
        }

        // Rebuild the persistent line list from surviving (non-single-frame) lines; order is irrelevant.
        uint32 SurvivorTotal = 0;
        for (uint32 t = 0; t < NumThreads; ++t)
        {
            SurvivorTotal += (uint32)LineBatchScratch[t].Survivors.size();
        }
        if (SurvivorTotal == 0)
        {
            Lines.clear();
        }
        else
        {
            LineCompactScratch.clear();
            LineCompactScratch.reserve(SurvivorTotal);
            for (uint32 t = 0; t < NumThreads; ++t)
            {
                const TVector<FLineInstance>& Sv = LineBatchScratch[t].Survivors;
                LineCompactScratch.insert(LineCompactScratch.end(), Sv.begin(), Sv.end());
            }
            Lines.swap(LineCompactScratch);
        }

        // Reset the per-worker produce buffers for next frame (capacity retained, so no per-frame realloc).
        for (TVector<FLineInstance>& Buffer : ThreadBuffers)
        {
            Buffer.clear();
        }
    }

    void FForwardRenderScene::ProcessBatchedTriangles(FTriangleBatcherComponent& Batcher)
    {
        FFrameData& Frame       = *ExtractFrame;
        auto& SceneGlobalData   = Frame.SceneGlobalData;
        auto& SolidVertices     = Frame.Primitives.SolidVertices;
        auto& SolidBatches      = Frame.Primitives.SolidBatches;

        Batcher.DrainQueue();

        TVector<FTriangleBatcherComponent::FBatchInstance>& Batches = Batcher.Batches;
        if (Batches.empty())
        {
            return;
        }

        const float Dt = SceneGlobalData.DeltaTime;
        SIZE_T WriteIdx = 0;
        for (SIZE_T i = 0, N = Batches.size(); i < N; ++i)
        {
            FTriangleBatcherComponent::FBatchInstance& Batch = Batches[i];
            if (!Batch.Vertices.empty())
            {
                const uint32 Start = (uint32)SolidVertices.size();
                SolidVertices.insert(SolidVertices.end(), Batch.Vertices.begin(), Batch.Vertices.end());
                SolidBatches.emplace_back(Start, (uint32)Batch.Vertices.size(), (bool)Batch.bDepthTest);
            }

            if (Batch.bSingleFrame)
            {
                continue;
            }

            Batch.RemainingLifetime -= Dt;
            if (Batch.RemainingLifetime > 0.0f)
            {
                if (WriteIdx != i)
                {
                    Batches[WriteIdx] = std::move(Batch);
                }
                ++WriteIdx;
            }
        }
        Batches.resize(WriteIdx);
    }

    void FForwardRenderScene::NotifyMaxLightsHit()
    {
        LOG_WARN("[Rendering] - Maximum Lights Hit! {}", MAX_LIGHTS);
    }

    void FForwardRenderScene::DrawBillboard(FRHIImage* Image, const FVector3& Location, float Scale)
    {
        if (Image->GetResourceID() == -1 || ExtractFrame == nullptr)
        {
            return;
        }

        FBillboardInstance& Billboard   = ExtractFrame->Primitives.BillboardInstances.emplace_back();
        Billboard.TextureIndex          = Image->GetResourceID();
        Billboard.Position              = Location;
        Billboard.Size                  = Scale;
        Billboard.EntityID              = entt::null;
    }

    void FForwardRenderScene::ResetPass_GameThread()
    {
        FFrameData& Frame = *ExtractFrame;

        Frame.Primitives.SimpleVertices.clear();
        Frame.Primitives.LineBatches.clear();
        Frame.Primitives.SolidVertices.clear();
        Frame.Primitives.SolidBatches.clear();
        Frame.Geometry.DrawCommands.clear();
        Frame.Geometry.OpaqueDrawList.clear();
        Frame.Geometry.OpaqueOccluderDrawList.clear();
        Frame.Geometry.TranslucentDrawList.clear();
        Frame.Geometry.DrawMeshletStartOffsets.clear();
        Frame.Views.IndirectArgs.clear();
        Frame.Views.CullViews.clear();
        Frame.Views.CaptureViews.clear();
        Frame.Views.TotalMeshletBound = 0;
        Frame.Views.NumDrawsPerView   = 0;
        Frame.Views.CameraLateViewIndex = ~0u;
        Frame.Geometry.Instances.clear();
        Frame.Geometry.InstanceMeshletPrefix.clear();
        Memory::Memzero(&Frame.Lighting.LightData, sizeof(Frame.Lighting.LightData));
        Frame.Lighting.ShadowDataCount.store(0, std::memory_order_release);
        ShadowAtlas.FreeTiles();
        Frame.Lighting.ShadowRequests.clear();
        Frame.Lighting.AtlasTiles.clear();
        Frame.Geometry.BonesData.clear();
        Frame.Geometry.SkinDescriptors.clear();
        Frame.Geometry.TotalPreSkinnedVertices = 0;
        Frame.Primitives.BillboardInstances.clear();
        Frame.Primitives.WidgetInstances.clear();
        Frame.Geometry.PinnedMeshBuffersThisFrame.clear();
        Frame.Primitives.PinnedWidgetRTs.clear();
        Frame.Primitives.GlyphInstances.clear();
        Frame.Primitives.TextBatches.clear();
        Frame.Primitives.PinnedFontAtlases.clear();
        Frame.FrameStats = {};

        for (int i = 0; i < (int)ELightType::Num; ++i)
        {
            Frame.Lighting.PackedShadows[i].clear();
        }
    }

    void FForwardRenderScene::ResetPass_RenderThread(ICommandList& CmdList)
    {
        // DepthPrePassEarly clears the depth target when it runs (occluders non-empty).
        // Only clear here as the no-occluder fallback to avoid a redundant re-clear.
        if (RenderFrame->Geometry.OpaqueOccluderDrawList.empty())
        {
            CmdList.ClearImageUInt(GetNamedImage(ENamedImage::DepthAttachment), AllSubresources, 0);
        }

        // Atlas/cascade clears stay unconditional: shadow passes only clear their own
        // tiles, so regions no light renders into still need clearing here.
        CmdList.ClearImageUInt(ShadowAtlas.GetImage(), AllSubresources, 1);
        CmdList.ClearImageUInt(GetNamedImage(ENamedImage::Cascade), AllSubresources, 1);
    }


    struct FCullMeshletPushConstants
    {
        uint32 NumViews;
        uint32 Phase;
        uint32 CameraLateViewIndex;
        uint32 Pad;
        // Device addresses of the cull-private scratch buffers (matches the
        // pointer fields in CullMeshlets.slang's FPushConstants, 8-byte aligned).
        uint64 IndirectArgsAddr;
        uint64 DeferListAddr;
        uint64 DeferCountAddr;
    };

    void FForwardRenderScene::CullPassEarly(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands     = Frame.Geometry.DrawCommands;
        const auto& CullViews        = Frame.Views.CullViews;
        const uint32 TotalMeshletBound = Frame.Views.TotalMeshletBound;
        const uint32 CameraLateViewIndex = Frame.Views.CameraLateViewIndex;

        if (DrawCommands.empty() || CullViews.empty() || TotalMeshletBound == 0u)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Cull Pass (Early)", tracy::Color::Pink2);

        FRHIBuffer* DifferCount = GetDeferCount();
        CmdList.FillBuffer(DifferCount, 0u, DifferCount->GetSize(), 0);

        FRHIComputeShaderRef CullShader = FShaderLibrary::GetComputeShader("CullMeshlets.slang");

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.SetComputeShader(CullShader);
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);
        
        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        State.Reads(GetNamedImage(ENamedImage::DepthPyramid));
        // Cull-private scratch is bound by device address, not a descriptor set; declaring
        // the accesses drives the tracker's barriers and keeps the buffers alive.
        State.Writes(GetIndirectArgs());
        State.Writes(GetMeshletDeferList());
        State.Writes(GetDeferCount());
        State.Writes(GetMeshletDrawList());
        CmdList.SetComputeState(State);

        FCullMeshletPushConstants PC = {};
        PC.NumViews            = (uint32)CullViews.size();
        PC.Phase               = (uint32)ECullPhase::Early;
        PC.CameraLateViewIndex = CameraLateViewIndex;
        PC.IndirectArgsAddr    = GetIndirectArgs()->GetAddress();
        PC.DeferListAddr       = GetMeshletDeferList()->GetAddress();
        PC.DeferCountAddr      = GetDeferCount()->GetAddress();
        PushRootConstants(CmdList, PC);

        // Flat thread-per-meshlet; workgroups beyond the 65535 X cap fold into Y.
        const uint32 NumWorkgroups = (TotalMeshletBound + 63u) / 64u;
        constexpr uint32 MaxDispatchAxis = 65535u;
        const uint32 DispatchX = NumWorkgroups < MaxDispatchAxis ? NumWorkgroups : MaxDispatchAxis;
        const uint32 DispatchY = (NumWorkgroups + MaxDispatchAxis - 1u) / MaxDispatchAxis;
        CmdList.Dispatch(DispatchX, DispatchY, 1u);
    }

    void FForwardRenderScene::CullPassLate(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands     = Frame.Geometry.DrawCommands;
        const auto& CullViews        = Frame.Views.CullViews;
        const uint32 TotalMeshletBound = Frame.Views.TotalMeshletBound;
        const uint32 CameraLateViewIndex = Frame.Views.CameraLateViewIndex;

        if (DrawCommands.empty() || CullViews.empty() || TotalMeshletBound == 0)
        {
            return;
        }

        if (CameraLateViewIndex == ~0u)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Cull Pass (Late)", tracy::Color::Pink3);

        // Worst-case dispatch (every camera meshlet deferred); threads past uDeferCount
        // early-out. No indirect-dispatch readback: the round-trip outweighs the tiny dispatch.
        FRHIComputeShaderRef CullShader = FShaderLibrary::GetComputeShader("CullMeshlets.slang");

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.SetComputeShader(CullShader);
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        State.Writes(GetNamedImage(ENamedImage::DepthPyramid));
        // Phase 1 reads the defer list/count phase 0 wrote and appends to indirect
        // args; declared as writes so the UAV barrier orders phase 0 before this.
        State.Writes(GetIndirectArgs());
        State.Writes(GetMeshletDeferList());
        State.Writes(GetDeferCount());
        State.Writes(GetMeshletDrawList());
        CmdList.SetComputeState(State);

        FCullMeshletPushConstants PC = {};
        PC.NumViews            = (uint32)CullViews.size();
        PC.Phase               = (uint32)ECullPhase::Late;
        PC.CameraLateViewIndex = CameraLateViewIndex;
        PC.IndirectArgsAddr    = GetIndirectArgs()->GetAddress();
        PC.DeferListAddr       = GetMeshletDeferList()->GetAddress();
        PC.DeferCountAddr      = GetDeferCount()->GetAddress();
        PushRootConstants(CmdList, PC);

        // Same X/Y fold as CullPassEarly so Vulkan's 65535 per-axis workgroup
        // limit doesn't cap us on very large scenes.
        const uint32 NumWorkgroups = (TotalMeshletBound + 63u) / 64u;
        constexpr uint32 MaxDispatchAxis = 65535u;
        const uint32 DispatchX = NumWorkgroups < MaxDispatchAxis ? NumWorkgroups : MaxDispatchAxis;
        const uint32 DispatchY = (NumWorkgroups + MaxDispatchAxis - 1u) / MaxDispatchAxis;
        CmdList.Dispatch(DispatchX, DispatchY, 1u);
    }


    void FForwardRenderScene::SkinningPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const uint32 DescriptorCount = (uint32)Frame.Geometry.SkinDescriptors.size();
        if (DescriptorCount == 0)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Skinning Pass", tracy::Color::SkyBlue);

        FRHIComputeShaderRef SkinShader = FShaderLibrary::GetComputeShader("Skinning.slang");
        if (!SkinShader)
        {
            return;
        }

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.SetComputeShader(SkinShader);
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        PipelineDesc.DebugName = "Skinning";

        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        State.Writes(GetPreSkinnedVerticesBuffer());
        CmdList.SetComputeState(State);

        struct FSkinningPushConstants { uint32 DescriptorCount; } PC{ DescriptorCount };
        PushRootConstants(CmdList, PC);

        // One workgroup per skinned entity; fold across X/Y past the 65535 per-axis cap.
        constexpr uint32 MaxDispatchAxis = 65535u;
        const uint32 DispatchX = DescriptorCount < MaxDispatchAxis ? DescriptorCount : MaxDispatchAxis;
        const uint32 DispatchY = (DescriptorCount + MaxDispatchAxis - 1u) / MaxDispatchAxis;
        CmdList.Dispatch(DispatchX, DispatchY, 1u);
    }

    void FForwardRenderScene::TexturePaintPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        if (Frame.Extracts.PaintOps.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Texture Paint Pass", tracy::Color::Red);

        FRHIComputeShaderRef PaintShader = FShaderLibrary::GetComputeShader("TexturePaint.slang");
        if (!PaintShader)
        {
            return;
        }

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.SetComputeShader(PaintShader);
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        PipelineDesc.DebugName = "TexturePaint";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        // Push-constant block mirroring TexturePaint.slang; fields aligned so offsets match under scalar
        // and std430. Color is four scalars (not a vec4) to keep it that way.
        struct FPaintPC
        {
            uint32      TargetIndex;
            int32       BrushIndex;
            uint32      TargetSize[2];
            uint32      RectMin[2];
            uint32      RectMax[2];
            float       CenterPx[2];
            float       RadiusPx;
            float       Strength;
            float       Hardness;
            float       ColorR;
            float       ColorG;
            float       ColorB;
            float       ColorA;
        };

        for (const FTexturePaintOp& Op : Frame.Extracts.PaintOps)
        {
            FRHIImage* Target = Op.Target.GetReference();
            if (Target == nullptr)
            {
                continue;
            }

            if (Op.Mode == FTexturePaintOp::EMode::Clear)
            {
                CmdList.ClearImageFloat(Target, AllSubresources,
                    FColor(Op.Color.r, Op.Color.g, Op.Color.b, Op.Color.a));
                continue;
            }

            const int32 UAVIndex = Target->GetMipUAVIndex(0);
            if (UAVIndex < 0)
            {
                continue;
            }

            const uint32 W = Target->GetSizeX();
            const uint32 H = Target->GetSizeY();
            const float  CenterX = Op.CenterUV.x * (float)W;
            const float  CenterY = Op.CenterUV.y * (float)H;
            // Radius is relative to the longer side so the brush stays circular in pixels.
            const float  RadiusPx = std::max(Op.RadiusUV * (float)std::max(W, H), 1.0f);

            const int32 MinX = std::clamp((int32)std::floor(CenterX - RadiusPx), 0, (int32)W);
            const int32 MinY = std::clamp((int32)std::floor(CenterY - RadiusPx), 0, (int32)H);
            const int32 MaxX = std::clamp((int32)std::ceil (CenterX + RadiusPx), 0, (int32)W);
            const int32 MaxY = std::clamp((int32)std::ceil (CenterY + RadiusPx), 0, (int32)H);
            if (MaxX <= MinX || MaxY <= MinY)
            {
                continue;
            }

            FComputeState State;
            State.SetPipeline(Pipeline);
            State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            State.Writes(Target);   // -> UnorderedAccess (and a UAV barrier between same-target ops)
            CmdList.SetComputeState(State);

            FPaintPC PC = {};
            PC.TargetIndex   = (uint32)UAVIndex;
            PC.BrushIndex    = Op.BrushIndex;
            PC.TargetSize[0] = W;             PC.TargetSize[1] = H;
            PC.RectMin[0]    = (uint32)MinX;  PC.RectMin[1]    = (uint32)MinY;
            PC.RectMax[0]    = (uint32)MaxX;  PC.RectMax[1]    = (uint32)MaxY;
            PC.CenterPx[0]   = CenterX;       PC.CenterPx[1]   = CenterY;
            PC.RadiusPx      = RadiusPx;
            PC.Strength      = Op.Strength;
            PC.Hardness      = Op.Hardness;
            PC.ColorR        = Op.Color.r;
            PC.ColorG        = Op.Color.g;
            PC.ColorB        = Op.Color.b;
            PC.ColorA        = Op.Color.a;
            PushRootConstants(CmdList, PC);

            const uint32 DispatchX = RenderUtils::GetGroupCount((uint32)(MaxX - MinX), 8u);
            const uint32 DispatchY = RenderUtils::GetGroupCount((uint32)(MaxY - MinY), 8u);
            CmdList.Dispatch(DispatchX, DispatchY, 1u);
        }

        // Restore every painted target to ShaderResource + emit barriers now: geometry passes sample these
        // via the bindless table without declaring Reads, so nothing else transitions them out of UAV/transfer-dst.
        for (const FTexturePaintOp& Op : Frame.Extracts.PaintOps)
        {
            if (FRHIImage* Target = Op.Target.GetReference())
            {
                CmdList.SetImageState(Target, AllSubresources, EResourceStates::ShaderResource);
            }
        }
        CmdList.CommitBarriers();
    }

    static void RecordDepthPrePassSlice(
        ICommandList& CmdList,
        const TVector<FMeshDrawCommand>& DrawCommands,
        const TVector<uint32>& OpaqueOccluderDrawList,
        FRHIImage* DepthImage,
        FRHIImage* SizedToImage,
        const FViewportState& SceneViewportState,
        FRHIBuffer* IndirectArgsBuffer,
        uint32 ViewIndex,
        uint32 NumDrawsPerView,
        bool bClearDepth,
        uint64 SceneRootAddr,
        FRHIBuffer* MeshletDrawListBuf,
        FRHIBuffer* PreSkinnedBuf,
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
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

            if (Batch.bMasked)
            {
                // Masked materials need their pixel shader to discard; if WPO, prefer the
                // per-material depth VS so displaced+masked geometry matches.
                Desc.SetVertexShader(Batch.DepthVertexShader ? Batch.DepthVertexShader : Batch.VertexShader);
                Desc.SetPixelShader(Batch.PixelShader);
            }
            else
            {
                // WPO materials get their own depth VS (writes displaced
                // depth so [earlydepthstencil] in the base pass matches).
                FRHIVertexShader* DepthVS = Batch.DepthVertexShader ? Batch.DepthVertexShader.GetReference() : DepthOnlyVertexShader.GetReference();
                Desc.SetVertexShader(DepthVS);
            }

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FGraphicsState GraphicsState;
            GraphicsState.SetRenderPass(RenderPass);
            GraphicsState.SetViewportState(SceneViewportState);
            GraphicsState.SetPipeline(Pipeline);
            GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            GraphicsState.SetIndirectParams(IndirectArgsBuffer);
            GraphicsState.Reads(MeshletDrawListBuf);
            GraphicsState.Reads(PreSkinnedBuf);

            CmdList.SetGraphicsState(GraphicsState);
            FRootConstants RC; RC.RootAddr = SceneRootAddr; RC.PassAddr = 0;
            CmdList.SetPushConstants(&RC, sizeof(RC));
            CmdList.DrawIndirect(Batch.DrawCount, (ViewBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
        }
    }

    void FForwardRenderScene::DepthPrePassEarly(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands           = Frame.Geometry.DrawCommands;
        const auto& OpaqueOccluderDrawList = Frame.Geometry.OpaqueOccluderDrawList;
        const uint32 NumDrawsPerView       = Frame.Views.NumDrawsPerView;

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
            SceneViewportState,
            GetIndirectArgs(),
            CurrentCameraEarlyView,
            NumDrawsPerView,
            true,
            CurrentSceneRootAddr,
            GetMeshletDrawList(),
            GetPreSkinnedVerticesBuffer(),
            GetSceneDepthResolve());
    }

    void FForwardRenderScene::DepthPrePassLate(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands           = Frame.Geometry.DrawCommands;
        const auto& OpaqueOccluderDrawList = Frame.Geometry.OpaqueOccluderDrawList;
        const uint32 NumDrawsPerView       = Frame.Views.NumDrawsPerView;
        const uint32 CameraLateViewIndex   = Frame.Views.CameraLateViewIndex;

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
            SceneViewportState,
            GetIndirectArgs(),
            CameraLateViewIndex,
            NumDrawsPerView,
            false,
            CurrentSceneRootAddr,
            GetMeshletDrawList(),
            GetPreSkinnedVerticesBuffer(),
            GetSceneDepthResolve());
    }

    void FForwardRenderScene::DepthPyramidPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands = Frame.Geometry.DrawCommands;

        if (DrawCommands.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Depth Pyramid Pass (SPD)", tracy::Color::Orange);

        FRHIImage* DepthPyramid = GetNamedImage(ENamedImage::DepthPyramid);
        FRHIImage* DepthSource  = GetNamedImage(ENamedImage::DepthAttachment);
        FRHIBuffer* SpdCounter  = GetSpdCounter();

        const uint32 PyramidW = DepthPyramid->GetSizeX();
        const uint32 PyramidH = DepthPyramid->GetSizeY();
        const uint32 MipCount = (uint32)DepthPyramid->GetDescription().NumMips;

        constexpr uint32 SpdMaxMips = 12;
        const uint32 NumMips = std::min(MipCount, SpdMaxMips);

        CmdList.FillBuffer(SpdCounter, 0u, SpdCounter->GetSize(), 0);

        FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("DepthPyramidSPD.slang");
        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        PipelineDesc.CS = ComputeShader;
        PipelineDesc.DebugName = "Depth Pyramid SPD";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        State.Reads(DepthSource);
        State.Writes(DepthPyramid);
        State.Writes(SpdCounter);
        CmdList.SetComputeState(State);

        struct FSpdPushConstants
        {
            uint32 PyramidSize[2];
            uint32 NumMips;
            uint32 NumWorkGroups;
            float  InvPyramidSize[2];
            uint32 SrcDepthIndex;
            uint32 Pad0;
            uint64 AtomicCounter;
            uint32 MipUAV[SpdMaxMips];
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
        PC.SrcDepthIndex      = (uint32)DepthSource->GetResourceID();
        PC.AtomicCounter      = SpdCounter->GetAddress();
        for (uint32 i = 0; i < SpdMaxMips; ++i)
        {
            const uint32 SrcMip = (i < MipCount) ? i : 0u;
            PC.MipUAV[i] = (uint32)DepthPyramid->GetMipUAVIndex(SrcMip);
        }
        PushRootConstants(CmdList, PC);

        CmdList.Dispatch(DispatchX, DispatchY, 1);
    }

    void FForwardRenderScene::ClusterBuildPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands = Frame.Geometry.DrawCommands;

        const bool bHasTerrain = !Frame.Extracts.TerrainExtracts.empty();
        if (DrawCommands.empty() && !bHasTerrain)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Cluster Build Pass", tracy::Color::Pink2);

        // Grid params live in the per-view scene snapshot (uSceneData) the shader reads via gPC.Root,
        // matching LightCull and the base pass. AABBs are rebuilt every frame so they always track the
        // snapshot frustum -- sourcing them from live render-thread projection/RT size (which lead the
        // snapshot by the extract->render latency) bins lights into a frustum the base pass never looks
        // up, lighting whole tiles with the wrong lights.
        FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("ClusterBuild.slang");

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.SetComputeShader(ComputeShader);
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        State.Writes(CurrentView->ClusterBuffer);
        CmdList.SetComputeState(State);

        PushRootConstants(CmdList);

        constexpr uint32 ClusterBuildGroupSize = 64;
        constexpr uint32 ClusterDispatchGroups = (NumClusters + ClusterBuildGroupSize - 1) / ClusterBuildGroupSize;
        CmdList.Dispatch(ClusterDispatchGroups, 1, 1);
            
    }

    void FForwardRenderScene::LightCullPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands = Frame.Geometry.DrawCommands;

        const bool bHasTerrain = !Frame.Extracts.TerrainExtracts.empty();
        if (DrawCommands.empty() && !bHasTerrain)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Light Cull Pass", tracy::Color::Pink2);
            
        FRHIComputeShaderRef ComputeShader = FShaderLibrary::GetComputeShader("LightCull.slang");

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.SetComputeShader(ComputeShader);
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
                
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);
            
        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        State.Writes(CurrentView->ClusterBuffer);   // per-cluster light lists
        CmdList.SetComputeState(State);

        FMatrix4 ViewProj = SceneViewport->GetViewVolume().GetViewMatrix();

        PushRootConstants(CmdList, ViewProj);
        
        constexpr uint32 LightCullGroupSize = 128;
        constexpr uint32 LightCullGroups    = (NumClusters + LightCullGroupSize - 1) / LightCullGroupSize;
        CmdList.Dispatch(LightCullGroups, 1, 1);
            
    }

    void FForwardRenderScene::PointShadowPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands             = Frame.Geometry.DrawCommands;
        const auto& OpaqueDrawList           = Frame.Geometry.OpaqueDrawList;
        const auto& LightData                = Frame.Lighting.LightData;
        const auto& PackedShadows            = Frame.Lighting.PackedShadows;
        const auto& AtlasTiles               = Frame.Lighting.AtlasTiles;
        const auto& PointShadowCullViewBases = Frame.Views.PointShadowCullViewBases;
        const uint32 NumDrawsPerView         = Frame.Views.NumDrawsPerView;

        LUMINA_PROFILE_SECTION_COLORED("Point Light Shadow Pass", tracy::Color::DeepPink2);

        if (DrawCommands.empty() || PackedShadows[(uint32)ELightType::Point].empty())
        {
            return;
        }

        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("ShadowMappingPixel.slang");
        
        FRenderState RenderState; RenderState
                .SetDepthStencilState(FDepthStencilState()
                .SetDepthFunc(EComparisonFunc::Less))
                .SetRasterState(FRasterState()
                    .SetSlopeScaleDepthBias(1.5f)
                    .SetDepthBias(1)
                    .SetCullBack());
        
        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("ShadowMappingVert.slang");

        const TVector<FLightShadow>& PointShadows = PackedShadows[(uint32)ELightType::Point];

        FRenderPassDesc::FAttachment Depth; Depth
            .SetLoadOp(ERenderLoadOp::Clear)
            .SetDepthClearValue(1.0)
            .SetImage(ShadowAtlas.GetImage());

        FRenderPassDesc RenderPass; RenderPass
            .SetDepthAttachment(Depth)
            .SetRenderArea(FUIntVector2(GShadowAtlasResolution, GShadowAtlasResolution));
        
        FGraphicsPipelineDesc DescTemplate; DescTemplate
            .SetDebugName("Point Light Shadow Pass")
            .SetRenderState(RenderState)
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
                const FShadowTile& Tile = AtlasTiles[FaceShadow.ShadowMapIndex];
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

                    // Per-material shadow VS for WPO materials so the shadow tracks
                    // displaced geometry, not the rest pose.
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
                        .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                        .SetIndirectParams(GetIndirectArgs());

                    GraphicsState.Reads(GetMeshletDrawList());
                    GraphicsState.Reads(GetPreSkinnedVerticesBuffer());
                    CmdList.SetGraphicsState(GraphicsState);
                    PushRootConstants(CmdList, PointPush);
                    CmdList.DrawIndirect(Batch.DrawCount, (FaceBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
                }
            }
        }
    }

    void FForwardRenderScene::SpotShadowPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands             = Frame.Geometry.DrawCommands;
        const auto& OpaqueDrawList           = Frame.Geometry.OpaqueDrawList;
        const auto& PackedShadows            = Frame.Lighting.PackedShadows;
        const auto& AtlasTiles               = Frame.Lighting.AtlasTiles;
        const auto& SpotShadowCullViewBases  = Frame.Views.SpotShadowCullViewBases;
        const uint32 NumDrawsPerView         = Frame.Views.NumDrawsPerView;

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


        // Load (not Clear) to preserve the point-shadow tiles PointShadowPass
        // already wrote into this same 2D atlas.
        FRenderPassDesc::FAttachment Depth; Depth
            .SetLoadOp(ERenderLoadOp::Load)
            .SetImage(ShadowAtlas.GetImage());

        FRenderPassDesc RenderPass; RenderPass
            .SetDepthAttachment(Depth)
            .SetRenderArea(FUIntVector2(GShadowAtlasResolution, GShadowAtlasResolution));
        
        // Pipeline desc reused across batches; per-batch loop swaps in a WPO
        // VS variant when the material has WorldPositionOffset connected.
        FGraphicsPipelineDesc PipelineDescTemplate; PipelineDescTemplate
            .SetDebugName("Spot Shadow Pass")
            .SetRenderState(RenderState)
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

            const FShadowTile& Tile = AtlasTiles[Shadow.ShadowMapIndex];
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
                    .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                    .SetIndirectParams(GetIndirectArgs());

                GraphicsState.Reads(GetMeshletDrawList());
                GraphicsState.Reads(GetPreSkinnedVerticesBuffer());
                CmdList.SetGraphicsState(GraphicsState);
                PushRootConstants(CmdList, SpotPush);
                CmdList.DrawIndirect(Batch.DrawCount, (ViewBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
            }
        }
    }

    void FForwardRenderScene::CascadedShowPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands     = Frame.Geometry.DrawCommands;
        const auto& OpaqueDrawList   = Frame.Geometry.OpaqueDrawList;
        const auto& LightData        = Frame.Lighting.LightData;
        const uint32 NumDrawsPerView = Frame.Views.NumDrawsPerView;
        const uint32 CascadeViewBase = Frame.Views.CascadeViewBase;

        // No work without a shadow-casting sun or caster meshes; terrain-only scenes
        // still read valid (cleared 1.0) shadow data from ResetPass.
        if (!LightData.bHasSun || DrawCommands.empty())
        {
            return;
        }
        if (LightData.Lights[0].ShadowDataIndex == INDEX_NONE)
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
            .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
            .SetVertexShader(VertexShader);

        // Each cascade maps to its own cull view; BuildCullViews recorded the base.
        // Bail if the sun got no shadow slot (MaxShadows exceeded).
        if (CascadeViewBase == ~0u)
        {
            return;
        }

        for (uint32 c = 0; c < (uint32)NumCascades; ++c)
        {
            // Cascade 0 clears the whole atlas (depth=1.0); outer cascades load so each
            // viewport overwrites only its own tile, leaving other tiles untouched.
            FRenderPassDesc::FAttachment Depth; Depth
                .SetLoadOp(c == 0u ? ERenderLoadOp::Clear : ERenderLoadOp::Load)
                .SetDepthClearValue(1.0f)
                .SetImage(GetNamedImage(ENamedImage::Cascade));

            FRenderPassDesc RenderPass; RenderPass
                .SetDepthAttachment(Depth)
                .SetRenderArea(FUIntVector2(GCSMAtlasWidth, GCSMAtlasHeight));

            // Per-cascade viewport: only this tile rasterizes; coords from the
            // GCSMCascadeOrigin/Sizes packing table.
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
                    .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                    .SetIndirectParams(GetIndirectArgs());

                GraphicsState.Reads(GetMeshletDrawList());
                GraphicsState.Reads(GetPreSkinnedVerticesBuffer());
                CmdList.SetGraphicsState(GraphicsState);
                PushRootConstants(CmdList, CascadePush);
                CmdList.DrawIndirect(Batch.DrawCount, (ViewBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
            }
        }
    }

    void FForwardRenderScene::DecalPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const TVector<FGPUDecal>& Decals = Frame.Primitives.DecalExtracts;

        FRHIImage* DBufferA = GetNamedImage(ENamedImage::DBufferA);
        FRHIImage* DBufferB = GetNamedImage(ENamedImage::DBufferB);
        FRHIImage* DBufferC = GetNamedImage(ENamedImage::DBufferC);

        // Cleared to transmittance = 1 (alpha) / zero color, so the base pass reads a no-op where no decal lands.
        const FVector4 ClearVal(0.0f, 0.0f, 0.0f, 1.0f);

        // No decals: still clear so the base pass DBuffer sample is a guaranteed no-op. Clear via a
        // LoadOp::Clear render pass rather than ClearImageFloat (vkCmdClearColorImage): the latter forces
        // the targets through CopyDst (RT->CopyDst->SRV = 6 barriers + 3 transfer clears), whereas a
        // clear-only pass leaves them in RenderTarget so the base pass needs only one RT->SRV each.
        if (Decals.empty())
        {
            FRenderPassDesc::FAttachment C0; C0.SetImage(DBufferA).SetLoadOp(ERenderLoadOp::Clear).SetClearColor(ClearVal);
            FRenderPassDesc::FAttachment C1; C1.SetImage(DBufferB).SetLoadOp(ERenderLoadOp::Clear).SetClearColor(ClearVal);
            FRenderPassDesc::FAttachment C2; C2.SetImage(DBufferC).SetLoadOp(ERenderLoadOp::Clear).SetClearColor(ClearVal);

            FRenderPassDesc ClearPass;
            ClearPass.AddColorAttachment(C0)
                     .AddColorAttachment(C1)
                     .AddColorAttachment(C2)
                     .SetRenderArea(DBufferA->GetExtent());

            CmdList.BeginRenderPass(ClearPass);
            CmdList.EndRenderPass();
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Decal Pass", tracy::Color::Orange);

        FRenderPassDesc::FAttachment RT0; RT0.SetImage(DBufferA).SetLoadOp(ERenderLoadOp::Clear).SetClearColor(ClearVal);
        FRenderPassDesc::FAttachment RT1; RT1.SetImage(DBufferB).SetLoadOp(ERenderLoadOp::Clear).SetClearColor(ClearVal);
        FRenderPassDesc::FAttachment RT2; RT2.SetImage(DBufferC).SetLoadOp(ERenderLoadOp::Clear).SetClearColor(ClearVal);

        FRenderPassDesc RenderPass;
        RenderPass.AddColorAttachment(RT0)
                  .AddColorAttachment(RT1)
                  .AddColorAttachment(RT2)
                  .SetRenderArea(DBufferA->GetExtent());

        // Render back faces (robust when the camera is inside the box -- its far faces still fill the
        // screen); no depth test -- the pixel shader reconstructs the surface from depth and rejects
        // out-of-box pixels itself.
        FRasterState RasterState;
        RasterState.SetCullFront().EnableDepthClip();

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest();
        DepthState.DisableDepthWrite();

        // Transmittance compositing: RGB = SrcAlpha "over", A *= (1 - coverage) so alpha accumulates transmittance.
        FBlendState::RenderTarget DecalBlend;
        DecalBlend.SetBlendEnable(true)
                  .SetSrcBlend(EBlendFactor::SrcAlpha)
                  .SetDestBlend(EBlendFactor::OneMinusSrcAlpha)
                  .SetBlendOp(EBlendOp::Add)
                  .SetSrcBlendAlpha(EBlendFactor::Zero)
                  .SetDestBlendAlpha(EBlendFactor::OneMinusSrcAlpha)
                  .SetBlendOpAlpha(EBlendOp::Add);

        FBlendState BlendState;
        BlendState.SetRenderTarget(0, DecalBlend);
        BlendState.SetRenderTarget(1, DecalBlend);
        BlendState.SetRenderTarget(2, DecalBlend);

        FRenderState RenderState;
        RenderState.SetRasterState(RasterState)
                   .SetDepthStencilState(DepthState)
                   .SetBlendState(BlendState);

        // No private descriptor set: the decal array is read by device address and the scene depth by
        // bindless index, both carried in the push constant (matches FDecalPushConstants in DecalCommon.slang).
        FRHIImage* SceneDepth = GetNamedImage(ENamedImage::DepthAttachment);

        auto DecalAlloc = CmdList.CopyTransientArray(Decals.data(), Decals.size());

        struct FDecalPushConstants
        {
            uint64 DecalsAddr;
            uint32 DepthIndex;
            uint32 Pad;
        };
        static_assert(sizeof(FDecalPushConstants) == 16, "FDecalPushConstants must match the slang push block.");

        FDecalPushConstants PC = {};
        PC.DecalsAddr = DecalAlloc.Gpu;
        PC.DepthIndex = (uint32)SceneDepth->GetResourceID();

        // One instanced draw per shader batch.
        for (const FFrameData::FDecalBatch& Batch : Frame.Primitives.DecalBatches)
        {
            FRHIVertexShader* VS = Batch.Shaders.VertexShader;
            FRHIPixelShader*  PS = Batch.Shaders.PixelShader;
            if (VS == nullptr || PS == nullptr)
            {
                continue;
            }

            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("Decal Pass");
            Desc.SetRenderState(RenderState);
            Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
            Desc.SetVertexShader(VS);
            Desc.SetPixelShader(PS);

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FGraphicsState GraphicsState;
            GraphicsState.SetPipeline(Pipeline);
            GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            // Scene depth is read by bindless index, not a descriptor; declare the access so the tracker
            // transitions it to shader-read. (The transient decal buffer manages its own ring lifetime.)
            GraphicsState.Reads(SceneDepth);
            GraphicsState.SetRenderPass(RenderPass);
            GraphicsState.SetViewportState(MakeViewportStateFromImage(DBufferA));

            CmdList.SetGraphicsState(GraphicsState);
            PushRootConstants(CmdList, PC);

            CmdList.Draw(36, Batch.Count, 0, Batch.FirstInstance);
        }
    }

    void FForwardRenderScene::BasePass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands         = Frame.Geometry.DrawCommands;
        const auto& OpaqueDrawList       = Frame.Geometry.OpaqueDrawList;
        const uint32 NumDrawsPerView     = Frame.Views.NumDrawsPerView;
        // The view being shaded (primary: 0 / CameraLateViewIndex; capture: its frustum view / ~0).
        const uint32 CameraEarlyViewIndex = CurrentCameraEarlyView;
        const uint32 CameraLateViewIndex  = CurrentCameraLateView;

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
            .SetFillMode(RenderSettings.bWireframe ? ERasterFillMode::Wireframe : ERasterFillMode::Solid);
    
        // GREATER_EQUAL: occluders re-write their pre-pass depth; non-occluders fill the rest.
        FDepthStencilState DepthState; DepthState
            .SetDepthFunc(EComparisonFunc::GreaterOrEqual)
            .EnableDepthWrite();
        
        FRenderState RenderState; RenderState
            .SetRasterState(RasterState)
            .SetDepthStencilState(DepthState);

        // DBuffer decal targets (opaque only), written by DecalPass earlier this frame; read here
        // bindlessly via push-constant indices.
        FRHIImage* DBufferA = GetNamedImage(ENamedImage::DBufferA);
        FRHIImage* DBufferB = GetNamedImage(ENamedImage::DBufferB);
        FRHIImage* DBufferC = GetNamedImage(ENamedImage::DBufferC);

        struct FDBufferPushConstants
        {
            uint32 DBufferAIndex;
            uint32 DBufferBIndex;
            uint32 DBufferCIndex;
            uint32 _Pad;
        };
        static_assert(sizeof(FDBufferPushConstants) == 16, "FDBufferPushConstants must match the slang push block.");

        // No decals this frame -> sentinel index so the base pass PS skips the three DBuffer samples
        // (they would composite a guaranteed no-op), removing 3 tex loads from every opaque pixel.
        FDBufferPushConstants DBufferPC = {};
        if (Frame.Primitives.DecalExtracts.empty())
        {
            DBufferPC.DBufferAIndex = 0xFFFFFFFFu;
            DBufferPC.DBufferBIndex = 0xFFFFFFFFu;
            DBufferPC.DBufferCIndex = 0xFFFFFFFFu;
        }
        else
        {
            DBufferPC.DBufferAIndex = (uint32)DBufferA->GetResourceID();
            DBufferPC.DBufferBIndex = (uint32)DBufferB->GetResourceID();
            DBufferPC.DBufferCIndex = (uint32)DBufferC->GetResourceID();
        }

        for (uint32 Idx : OpaqueDrawList)
        {
            const FMeshDrawCommand& Batch = DrawCommands[Idx];

            FGraphicsPipelineDesc Desc; Desc
                .SetDebugName("Forward Base Pass")
                .SetRenderState(RenderState)
                .SetVertexShader(Batch.VertexShader)
                .SetPixelShader(Batch.PixelShader)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                .SetVariableRateShadingState(MakeWorldShadingRate(Frame.CachedWorldSettings));

            FGraphicsState GraphicsState; GraphicsState
                .SetRenderPass(RenderPass)
                .SetViewportState(SceneViewportState)
                .SetPipeline(GRenderContext->CreateGraphicsPipeline(Desc, RenderPass))
                .SetIndirectParams(GetIndirectArgs())
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            
            GraphicsState.Reads(DBufferA);
            GraphicsState.Reads(DBufferB);
            GraphicsState.Reads(DBufferC);
            GraphicsState.Reads(GetMeshletDrawList());
            GraphicsState.Reads(CurrentView->ClusterBuffer);
            GraphicsState.Reads(GetPreSkinnedVerticesBuffer());
            GraphicsState.Reads(GetNamedImage(ENamedImage::Cascade));
            GraphicsState.Reads(ShadowAtlas.GetImage());
            GraphicsState.Reads(GetNamedImage(ENamedImage::SSAOBlur));

            CmdList.SetGraphicsState(GraphicsState);
            PushRootConstants(CmdList, DBufferPC);

            const uint32 EarlyBase = CameraEarlyViewIndex * NumDrawsPerView;
            CmdList.DrawIndirect(Batch.DrawCount, (EarlyBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));

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

        const FFrameData& Frame = *RenderFrame;
        const float DeltaTime   = Frame.CachedWorldDeltaTime;
        
        // Reclaim GPU/sim state for destroyed emitters (disabled ones stay in LiveParticleEntities).
        // KeepAlive each resource on this frame's cmd list before dropping the ref so in-flight work finishes.
        if (!ParticleGPUStates.empty())
        {
            const TVector<entt::entity>& Live = Frame.Extracts.LiveParticleEntities;
            auto IsLive = [&](entt::entity E)
            {
                return std::find(Live.begin(), Live.end(), E) != Live.end();
            };

            for (auto It = ParticleGPUStates.begin(); It != ParticleGPUStates.end();)
            {
                if (!IsLive(It->first))
                {
                    FParticleGPUState& Dead = It->second;
                    auto Keep = [&](const auto& Ref) { if (Ref)
                        {
                            CmdList.KeepAlive(Ref.GetReference());
                        }
                    };
                    Keep(Dead.ParticleBuffer);
                    Keep(Dead.SimParamsBuffer);
                    Keep(Dead.RenderParamsBuffer);
                    Keep(Dead.SpawnCounterBuffer);
                    It = ParticleGPUStates.erase(It);
                }
                else
                {
                    ++It;
                }
            }
        }

        for (const FFrameData::FParticleExtract& Item : Frame.Extracts.ParticleExtracts)
        {
            if (!Item.bReady)
            {
                continue;
            }

            const FResolvedParticleParams& Resolved = Item.Resolved;

            const uint32 MaxParticles = (uint32)Resolved.MaxParticles;
            if (MaxParticles == 0)
            {
                continue;
            }

            // GPUState is render-thread-owned (this map); everything else comes from the
            // game-thread snapshot in Item, never from the live component or its asset.
            FParticleGPUState& State = ParticleGPUStates[Item.Entity];

            const bool bNeedsAlloc = (!State.ParticleBuffer) || (State.AllocatedMax != MaxParticles);
            if (bNeedsAlloc)
            {
                const FString AssetName = FString("Particles_") + std::to_string((uint32)Item.Entity).c_str();

                // Particle SSBOs cross queues: written on async compute, read on graphics.
                // Concurrent sharing avoids ownership-transfer barriers.
                FRHIBufferDesc ParticleDesc;
                ParticleDesc.Size       = (uint64)MaxParticles * 64ull;
                ParticleDesc.Stride     = 64u;
                ParticleDesc.DebugName  = AssetName + "_Particles";
                ParticleDesc.bKeepInitialState = true;
                ParticleDesc.bConcurrentSharing = true;
                ParticleDesc.InitialState = EResourceStates::UnorderedAccess;
                ParticleDesc.Usage.SetFlag(BUF_StorageBuffer);
                State.ParticleBuffer = GRenderContext->CreateBuffer(ParticleDesc);

                FRHIBufferDesc SimParamsDesc;
                SimParamsDesc.Size      = sizeof(FParticleSimParamsGPU);
                SimParamsDesc.DebugName = AssetName + "_SimParams";
                SimParamsDesc.bKeepInitialState = true;
                SimParamsDesc.bConcurrentSharing = true;
                SimParamsDesc.InitialState = EResourceStates::UnorderedAccess;
                SimParamsDesc.Usage.SetFlag(BUF_UniformBuffer);
                State.SimParamsBuffer = GRenderContext->CreateBuffer(SimParamsDesc);

                FRHIBufferDesc RenderParamsDesc;
                RenderParamsDesc.Size       = sizeof(FParticleRenderParamsGPU);
                RenderParamsDesc.DebugName  = AssetName + "_RenderParams";
                RenderParamsDesc.Usage.SetFlag(BUF_UniformBuffer);
                RenderParamsDesc.bKeepInitialState = true;
                RenderParamsDesc.bConcurrentSharing = true;
                RenderParamsDesc.InitialState = EResourceStates::UnorderedAccess;
                State.RenderParamsBuffer = GRenderContext->CreateBuffer(RenderParamsDesc);

                FRHIBufferDesc SpawnCounterDesc;
                SpawnCounterDesc.Size       = sizeof(uint32);
                SpawnCounterDesc.Stride     = sizeof(uint32);
                SpawnCounterDesc.DebugName  = AssetName + "_SpawnCounter";
                SpawnCounterDesc.Usage.SetFlag(BUF_StorageBuffer);
                SpawnCounterDesc.bKeepInitialState = true;
                SpawnCounterDesc.bConcurrentSharing = true;
                SpawnCounterDesc.InitialState = EResourceStates::UnorderedAccess;
                State.SpawnCounterBuffer = GRenderContext->CreateBuffer(SpawnCounterDesc);

                // Zero-fill the particle buffer so all entries start dead.
                CmdList.FillBuffer(State.ParticleBuffer, 0u, State.ParticleBuffer->GetSize(), 0);

                State.AllocatedMax      = MaxParticles;
                State.SpawnAccumulator  = 0.0f;
                State.SystemAge         = 0.0f;
                State.bBurstPending     = true;
            }

            // Apply the game-thread Activate()/Deactivate() intents to the render-owned sim state.
            if (Item.bForceReset)
            {
                CmdList.FillBuffer(State.ParticleBuffer, 0u, State.ParticleBuffer->GetSize(), 0);
                State.AliveTimeRemaining = 0.0f;
                State.SpawnAccumulator   = 0.0f;
                State.SystemAge          = 0.0f;
            }
            if (Item.bForceBurst)
            {
                State.bBurstPending = true;
            }

            const float ScaledDelta = DeltaTime * Item.TimeScale;
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

            const bool bEmitActive = Item.bEmit && !(bDurationExpired && !Resolved.bLooping);

            uint32 SpawnCount = 0;
            if (bEmitActive && Resolved.SpawnRate > 0.0f && Item.SpawnRateMultiplier > 0.0f)
            {
                State.SpawnAccumulator += DeltaTime * Resolved.SpawnRate * Item.SpawnRateMultiplier;
                SpawnCount = (uint32)State.SpawnAccumulator;
                State.SpawnAccumulator -= (float)SpawnCount;
            }
            else
            {
                State.SpawnAccumulator = 0.0f;
            }

            const bool bDoBurst = bEmitActive && Item.bBurstOnSpawn && State.bBurstPending && Resolved.BurstCount > 0;
            if (bDoBurst)
            {
                SpawnCount += (uint32)Resolved.BurstCount;
                State.bBurstPending = false;
            }
            else if (!Item.bBurstOnSpawn)
            {
                State.bBurstPending = false;
            }

            SpawnCount = eastl::min(SpawnCount, MaxParticles);

            // Upper bound on remaining alive-particle lifetime; a spawn resets it to max
            // lifetime, else it counts down. At zero with no spawn, skip the dispatch.
            const float MaxLifetime = eastl::max(Resolved.LifetimeRange.y, 0.0f);
            if (SpawnCount > 0)
            {
                State.AliveTimeRemaining = eastl::max(State.AliveTimeRemaining, MaxLifetime);
            }
            State.AliveTimeRemaining = eastl::max(State.AliveTimeRemaining - ScaledDelta, 0.0f);

            if (SpawnCount == 0 && State.AliveTimeRemaining <= 0.0f)
            {
                continue;
            }

            // Zero the spawn counter only when we are actually going to
            // dispatch. Idle systems no longer pay for it.
            CmdList.FillBuffer(State.SpawnCounterBuffer, 0u, State.SpawnCounterBuffer->GetSize(), 0);

            const FMatrix4 WorldMat = Item.WorldMatrix;
            const FVector3 EmitterWorld = FVector3(WorldMat * FVector4(Item.EmitterOffset, 1.0f));
            const FVector3 EmitterRight   = Math::Normalize(FVector3(WorldMat[0]));
            const FVector3 EmitterUp      = Math::Normalize(FVector3(WorldMat[1]));
            const FVector3 EmitterForward = Math::Normalize(FVector3(WorldMat[2]));
            
            FVector3 EmitterVelocity(0.0f);
            if (State.bHasPrevPosition && DeltaTime > 0.0f)
            {
                EmitterVelocity = (EmitterWorld - State.PrevEmitterPosition) / DeltaTime;
            }
            State.PrevEmitterPosition = EmitterWorld;
            State.bHasPrevPosition    = true;

            const float InheritFactor = Math::Clamp(Resolved.InheritEmitterVelocity, 0.0f, 1.0f);

            State.FrameSeed = (State.FrameSeed + 2654435761u) ^ (uint32)Item.Entity;

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
            SimParams.EmitterPosition   = FVector4(EmitterWorld, 1.0f);
            SimParams.EmitterForward    = FVector4(EmitterForward, EmitterVelocity.x);
            SimParams.EmitterRight      = FVector4(EmitterRight,   EmitterVelocity.y);
            SimParams.EmitterUp         = FVector4(EmitterUp,      EmitterVelocity.z);
            SimParams.Counts            = FUIntVector4(MaxParticles, SpawnCount, State.FrameSeed, SimFlags);
            SimParams.Modes             = FUIntVector4((uint32)Resolved.Shape, (uint32)Resolved.VelocityMode, 0u, 0u);
            SimParams.ShapeSize         = FVector4(Resolved.ShapeSize, Math::Radians(Resolved.ShapeAngle));
            SimParams.VelocityMin       = FVector4(Resolved.VelocityMin, 0.0f);
            SimParams.VelocityMax       = FVector4(Resolved.VelocityMax, 0.0f);
            SimParams.SpeedAndLifetime  = FVector4(Resolved.SpeedRange.x, Resolved.SpeedRange.y, Resolved.LifetimeRange.x, Resolved.LifetimeRange.y);
            SimParams.Gravity           = FVector4(Resolved.Gravity, Resolved.Drag);
            SimParams.StartColor        = Resolved.StartColor;
            SimParams.EndColor          = Resolved.EndColor;
            SimParams.SizeRange         = FVector4(Resolved.StartSizeRange.x, Resolved.StartSizeRange.y, Resolved.EndSizeRange.x, Resolved.EndSizeRange.y);
            SimParams.RotationRange     = FVector4(Resolved.RotationRange.x, Resolved.RotationRange.y, Resolved.RotationSpeedRange.x, Resolved.RotationSpeedRange.y);
            SimParams.NoiseStrength     = FVector4(Resolved.NoiseStrength, Resolved.NoiseScale);
            SimParams.NoiseParams       = FVector4(Resolved.NoiseSpeed, InheritFactor, 0.0f, 0.0f);
            SimParams.Timing            = FVector4(ScaledDelta, State.TotalTime, State.SystemAge, 0.0f);

            CmdList.WriteBuffer(State.SimParamsBuffer, &SimParams, sizeof(SimParams));

            FRHIComputeShaderRef ComputeShader;
            if (Item.bUsesCustomShader)
            {
                ComputeShader = Item.CustomComputeShader;
            }
            else
            {
                ComputeShader = FShaderLibrary::GetComputeShader("ParticleSimulate.slang");
            }

            if (!ComputeShader)
            {
                continue;
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
        }
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

        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands = Frame.Geometry.DrawCommands;

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("ParticleVertex.slang");
        FRHIPixelShaderRef  PixelShader  = FShaderLibrary::GetPixelShader("ParticlePixel.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        // Only Load when an earlier pass wrote these targets; in the particle preview world
        // BasePass/DepthPrePass early-return, so the first pass must clear them itself.
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

        // Particle array read by device address; per-emitter render params inlined into the push
        // constant (matches FParticlePushConstants in ParticleVertex.slang). No pass-local set.
        struct FParticlePushConstants
        {
            uint64   ParticlesAddr;
            uint32   TextureIndex;
            uint32   BillboardToCamera;
            FVector4 Tint;
        };
        static_assert(sizeof(FParticlePushConstants) == 32, "FParticlePushConstants must match the slang push block.");

        for (const FFrameData::FParticleExtract& Item : Frame.Extracts.ParticleExtracts)
        {
            if (!Item.bReady)
            {
                continue;
            }

            // GPUState is render-owned; the snapshot supplies everything else.
            auto ParticleStateIt = ParticleGPUStates.find(Item.Entity);
            if (ParticleStateIt == ParticleGPUStates.end())
            {
                continue;
            }
            FParticleGPUState& State = ParticleStateIt->second;
            if (!State.ParticleBuffer || !State.RenderParamsBuffer)
            {
                continue;
            }

            const FResolvedParticleParams& Resolved = Item.Resolved;

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
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

            Desc.SetVariableRateShadingState(MakeWorldShadingRate(Frame.CachedWorldSettings));

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FGraphicsState GraphicsState;
            GraphicsState.SetRenderPass(RenderPass)
                .SetViewportState(MakeViewportStateFromImage(GetNamedImage(ENamedImage::HDR)))
                .SetPipeline(Pipeline)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            // Particle array read by device address, not a descriptor; declare it for the tracker.
            GraphicsState.Reads(State.ParticleBuffer);

            FParticlePushConstants PC = {};
            PC.ParticlesAddr     = State.ParticleBuffer->GetAddress();
            PC.TextureIndex      = Item.TextureIndex;
            PC.BillboardToCamera = Resolved.bBillboardToCamera ? 1u : 0u;
            PC.Tint              = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

            CmdList.SetGraphicsState(GraphicsState);
            PushRootConstants(CmdList, PC);
            CmdList.Draw(6u * State.AllocatedMax, 1u, 0u, 0u);
        }
    }

    namespace
    {
        // Bilinear resample of a square row-major grid from OldRes^2 to NewRes^2.
        template <typename T>
        static void ResampleGrid(const T* Src, int32 OldRes, T* Dst, int32 NewRes)
        {
            for (int32 Y = 0; Y < NewRes; ++Y)
            {
                const float Fy = (NewRes > 1) ? float(Y) / float(NewRes - 1) * float(OldRes - 1) : 0.0f;
                const int32 Y0 = int(Fy);
                const int32 Y1 = std::min(Y0 + 1, OldRes - 1);
                const float Ty = Fy - float(Y0);
                for (int32 X = 0; X < NewRes; ++X)
                {
                    const float Fx = (NewRes > 1) ? float(X) / float(NewRes - 1) * float(OldRes - 1) : 0.0f;
                    const int32 X0 = int(Fx);
                    const int32 X1 = std::min(X0 + 1, OldRes - 1);
                    const float Tx = Fx - float(X0);
                    const float V00 = float(Src[size_t(Y0) * OldRes + X0]);
                    const float V10 = float(Src[size_t(Y0) * OldRes + X1]);
                    const float V01 = float(Src[size_t(Y1) * OldRes + X0]);
                    const float V11 = float(Src[size_t(Y1) * OldRes + X1]);
                    const float V   = Math::Mix(Math::Mix(V00, V10, Tx), Math::Mix(V01, V11, Tx), Ty);
                    if constexpr (std::is_integral_v<T>)
                    {
                        Dst[size_t(Y) * NewRes + X] = T(Math::Clamp(V + 0.5f, 0.0f, 255.0f));
                    }
                    else
                    {
                        Dst[size_t(Y) * NewRes + X] = T(V);
                    }
                }
            }
        }

        // Resize CPU backing stores to declared dimensions (lazy, so designers tweak Resolution/LayerCount
        // without rebooting). A resolution change resamples existing height + weights rather than wiping them.
        static void EnsureTerrainCpuBuffers(STerrainComponent& Terrain)
        {
            const int32  NewRes        = Terrain.Resolution;
            const size_t NeededHeights = size_t(NewRes) * size_t(NewRes);
            if (Terrain.Heightmap.size() != NeededHeights)
            {
                const size_t OldCount = Terrain.Heightmap.size();
                const int32  OldRes   = (int32)std::llround(std::sqrt((double)OldCount));
                const bool   bResample = !Terrain.Heightmap.empty() && NewRes >= 2 && OldRes >= 2
                                       && size_t(OldRes) * size_t(OldRes) == OldCount;
                if (bResample)
                {
                    TVector<float> Resampled(NeededHeights);
                    ResampleGrid(Terrain.Heightmap.data(), OldRes, Resampled.data(), NewRes);
                    Terrain.Heightmap = std::move(Resampled);

                    const int32 LayerCount = (int32)Terrain.Layers.size();
                    if (LayerCount > 0 && Terrain.LayerWeights.size() == size_t(LayerCount) * OldCount)
                    {
                        TVector<uint8> NewWeights(size_t(LayerCount) * NeededHeights);
                        for (int32 L = 0; L < LayerCount; ++L)
                        {
                            ResampleGrid(Terrain.LayerWeights.data() + size_t(L) * OldCount, OldRes,
                                         NewWeights.data() + size_t(L) * NeededHeights, NewRes);
                        }
                        Terrain.LayerWeights = std::move(NewWeights);
                        Terrain.CPUState.bFullWeightsDirty = true;
                    }
                }
                else
                {
                    Terrain.Heightmap.assign(NeededHeights, 0.0f);
                }
                Terrain.CPUState.bFullHeightmapDirty = true;
            }
            const size_t NeededWeights = size_t(Terrain.Layers.size()) * NeededHeights;
            if (Terrain.LayerWeights.size() != NeededWeights)
            {
                Terrain.LayerWeights.resize(NeededWeights, 0u);
                Terrain.CPUState.bFullWeightsDirty = true;
            }
        }

        // Game-thread terrain prep (during Extract, exclusive ECS access): rebuild dirty metadata and copy
        // the bytes the render thread needs into the extract, so a concurrent sculpt can't realloc under it.
        static void PrepareTerrainExtract(STerrainComponent& Terrain, const FMatrix4& WorldMatrix,
                                          FForwardRenderScene::FFrameData::FTerrainExtract& Out)
        {
            EnsureTerrainCpuBuffers(Terrain);

            FTerrainCPUState& CPU = Terrain.CPUState;

            const int32  Res        = Terrain.Resolution;
            const int32  LayerCount = (int32)std::max<size_t>(Terrain.Layers.size(), 1u);
            const size_t SlicePixels = size_t(Res) * size_t(Res);

            // Snapshot the scalar params (render passes read these, not the component).
            Out.Resolution      = Res;
            Out.ChunkResolution = Terrain.ChunkResolution;
            Out.TileWorldSize   = Terrain.TileWorldSize;
            Out.MaxHeight       = Terrain.MaxHeight;
            Out.LayerCount      = (int32)Terrain.Layers.size();
            Out.bCastShadow     = Terrain.bCastShadow;
            Out.bReceiveShadow  = Terrain.bReceiveShadow;

            // Resolve shaders here (game thread, material alive) and ref-hold them; the render
            // thread never touches the CMaterial. A wrong-domain material leaves the shaders null
            // (terrain skipped); null/not-ready falls back to the default terrain material.
            CMaterialInterface* TerrainMaterial = Terrain.Material.Get();
            if (!TerrainMaterial || TerrainMaterial->GetMaterialType() == EMaterialType::Terrain)
            {
                if (!TerrainMaterial || !TerrainMaterial->IsReadyForRender())
                {
                    TerrainMaterial = CMaterial::GetDefaultTerrainMaterial();
                }
                if (TerrainMaterial && TerrainMaterial->IsReadyForRender())
                {
                    Out.Shaders.VertexShader = TerrainMaterial->GetVertexShader();
                    Out.Shaders.PixelShader  = TerrainMaterial->GetPixelShader();
                    Out.MaterialIndex        = (uint32)std::max(TerrainMaterial->GetMaterialIndex(), 0);
                }
            }

            Out.HeightUpload      = 0;
            Out.WeightUpload      = 0;
            Out.WeightSliceMask   = 0u;
            Out.bGeometryRebuilt  = false;
            Out.bStructuralChange = false;
            Out.HeightBytes.clear();
            Out.WeightBytes.clear();
            Out.Chunks.clear();
            Out.Meshlets.clear();

            if (Res < 2 || Terrain.ChunkResolution < 2)
            {
                return;
            }

            // A dimension change vs what we last prepared forces a full re-seed and a
            // GPU texture realloc on the render side.
            const bool bStructural = CPU.PreparedResolution      != Res
                                  || CPU.PreparedChunkResolution != Terrain.ChunkResolution
                                  || CPU.PreparedLayerCount      != Out.LayerCount;
            Out.bStructuralChange = bStructural;

            // Height upload
            const bool bFullHeight = CPU.bFullHeightmapDirty || bStructural;
            const bool bRectHeight = !bFullHeight && (CPU.HeightDirtyMax.x >= CPU.HeightDirtyMin.x);

            FIntVector2 RectMin = FIntVector2(0);
            FIntVector2 RectMax = FIntVector2(Res - 1);
            if (bFullHeight && Terrain.Heightmap.size() == SlicePixels)
            {
                Out.HeightUpload = 1;
                Out.HeightRectMin = FIntVector2(0);
                Out.HeightRectMax = FIntVector2(Res - 1);
                Out.HeightBytes.assign(Terrain.Heightmap.begin(), Terrain.Heightmap.end());
            }
            else if (bRectHeight && Terrain.Heightmap.size() == SlicePixels)
            {
                RectMin = Math::Clamp(CPU.HeightDirtyMin, FIntVector2(0), FIntVector2(Res - 1));
                RectMax = Math::Clamp(CPU.HeightDirtyMax, FIntVector2(0), FIntVector2(Res - 1));
                const int32 RegionW = RectMax.x - RectMin.x + 1;
                const int32 RegionH = RectMax.y - RectMin.y + 1;
                Out.HeightUpload  = 2;
                Out.HeightRectMin = RectMin;
                Out.HeightRectMax = RectMax;
                Out.HeightBytes.resize(size_t(RegionW) * size_t(RegionH));
                for (int32 Row = 0; Row < RegionH; ++Row)
                {
                    const float* Src = Terrain.Heightmap.data() + size_t(RectMin.y + Row) * Res + RectMin.x;
                    std::memcpy(Out.HeightBytes.data() + size_t(Row) * RegionW, Src, size_t(RegionW) * sizeof(float));
                }
            }

            // Weight upload: whole slices, matching the GPU upload granularity.
            const bool bFullWeights = CPU.bFullWeightsDirty || bStructural;
            if (!Terrain.LayerWeights.empty())
            {
                if (bFullWeights)
                {
                    uint32 Mask = 0u;
                    for (int32 L = 0; L < LayerCount && (size_t(L + 1) * SlicePixels) <= Terrain.LayerWeights.size(); ++L)
                    {
                        Mask |= (1u << L);
                    }
                    if (Mask != 0u)
                    {
                        Out.WeightUpload    = 1;
                        Out.WeightSliceMask = Mask;
                        // Pack only the present slices back-to-back, in ascending slice order.
                        for (int32 L = 0; L < LayerCount; ++L)
                        {
                            if ((Mask & (1u << L)) == 0u) continue;
                            const uint8* Slice = Terrain.LayerWeights.data() + size_t(L) * SlicePixels;
                            Out.WeightBytes.insert(Out.WeightBytes.end(), Slice, Slice + SlicePixels);
                        }
                    }
                }
                else if (CPU.WeightDirtyLayerMask != 0u)
                {
                    uint32 Mask = 0u;
                    for (int32 L = 0; L < LayerCount; ++L)
                    {
                        if ((CPU.WeightDirtyLayerMask & (1u << L)) == 0u) continue;
                        if ((size_t(L + 1) * SlicePixels) > Terrain.LayerWeights.size()) continue;
                        Mask |= (1u << L);
                        const uint8* Slice = Terrain.LayerWeights.data() + size_t(L) * SlicePixels;
                        Out.WeightBytes.insert(Out.WeightBytes.end(), Slice, Slice + SlicePixels);
                    }
                    if (Mask != 0u)
                    {
                        Out.WeightUpload    = 2;
                        Out.WeightSliceMask = Mask;
                    }
                }
            }

            // Chunk / meshlet metadata
            if (CPU.bChunksDirty || bStructural)
            {
                const FVector3 WorldOrigin = FVector3(WorldMatrix[3]);
                const bool bFullRebuild = bStructural || !bRectHeight || CPU.Chunks.empty();
                if (bFullRebuild)
                {
                    TerrainMeshletBuilder::Build(Terrain, WorldOrigin);
                }
                else
                {
                    TerrainMeshletBuilder::UpdateRegion(Terrain, WorldOrigin, RectMin, RectMax);
                }

                if (!CPU.Chunks.empty() && !CPU.Meshlets.empty())
                {
                    Out.bGeometryRebuilt = true;
                    Out.Chunks.assign(CPU.Chunks.begin(), CPU.Chunks.end());
                    Out.Meshlets.assign(CPU.Meshlets.begin(), CPU.Meshlets.end());
                }
            }

            // Consume the dirty state now that it's captured for this frame.
            CPU.bFullHeightmapDirty = false;
            CPU.bFullWeightsDirty   = false;
            CPU.bChunksDirty        = false;
            CPU.HeightDirtyMin      = FIntVector2(INT32_MAX);
            CPU.HeightDirtyMax      = FIntVector2(INT32_MIN);
            CPU.WeightDirtyMin      = FIntVector2(INT32_MAX);
            CPU.WeightDirtyMax      = FIntVector2(INT32_MIN);
            CPU.WeightDirtyLayerMask = 0u;
            CPU.PreparedResolution      = Res;
            CPU.PreparedChunkResolution = Terrain.ChunkResolution;
            CPU.PreparedLayerCount      = Out.LayerCount;
        }

        static FRHIImageRef CreateTerrainImage(const FString& DebugName, uint32 Size, uint16 ArraySize, EFormat Format, bool bUav, bool bArrayView = false)
        {
            FRHIImageDesc Desc;
            Desc.Extent       = FUIntVector2(Size, Size);
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

        const FFrameData& Frame = *RenderFrame;

        // Reclaim GPU state for destroyed terrains (disabled ones stay in LiveTerrainEntities).
        // KeepAlive each resource on this frame's cmd list before dropping the ref so in-flight work finishes.
        if (!TerrainGPUStates.empty())
        {
            const TVector<entt::entity>& Live = Frame.Extracts.LiveTerrainEntities;
            auto IsLive = [&](entt::entity E)
            {
                return std::find(Live.begin(), Live.end(), E) != Live.end();
            };

            for (auto It = TerrainGPUStates.begin(); It != TerrainGPUStates.end();)
            {
                if (!IsLive(It->first))
                {
                    FTerrainGPUState& Dead = It->second;
                    auto Keep = [&](const auto& Ref) { if (Ref) CmdList.KeepAlive(Ref.GetReference()); };
                    Keep(Dead.HeightmapTexture);
                    Keep(Dead.NormalTexture);
                    Keep(Dead.LayerWeightTexture);
                    Keep(Dead.ChunkInfoBuffer);
                    Keep(Dead.MeshletInfoBuffer);
                    Keep(Dead.VisibleMeshletBuffer);
                    Keep(Dead.IndirectDrawBuffer);
                    It = TerrainGPUStates.erase(It);
                }
                else
                {
                    ++It;
                }
            }
        }

        for (const FFrameData::FTerrainExtract& TerrainItem : Frame.Extracts.TerrainExtracts)
        {
            const entt::entity Entity = TerrainItem.Entity;
            if (TerrainItem.Resolution < 2 || TerrainItem.ChunkResolution < 2)
            {
                continue;
            }

            // GPUState is render-thread-owned (this map); the CPU payload comes entirely
            // from the game-thread snapshot in TerrainItem, never from the live component.
            FTerrainGPUState& State = TerrainGPUStates[TerrainItem.Entity];
            const uint32 Res        = (uint32)TerrainItem.Resolution;
            const uint32 LayerCount = (uint32)std::max(TerrainItem.LayerCount, 1);
            const size_t SlicePixels = size_t(Res) * size_t(Res);

            // (Re)allocate GPU textures when the GPU dimensions no longer match what the
            // game thread prepared. A structural change always ships Full payloads below.
            const bool bRealloc = State.AllocatedResolution != Res || State.AllocatedLayerCount != LayerCount;
            if (bRealloc)
            {
                const FString Name = FString("Terrain_") + std::to_string((uint32)Entity).c_str();
                State.HeightmapTexture   = CreateTerrainImage(Name + "_Height",  Res, 1u,          EFormat::R32_FLOAT, false);
                State.NormalTexture      = CreateTerrainImage(Name + "_Normal",  Res, 1u,          EFormat::RGBA8_UNORM, true);
                State.LayerWeightTexture = CreateTerrainImage(Name + "_Weights", Res, (uint16)std::max(LayerCount, 1u), EFormat::R8_UNORM, false, true);
                State.AllocatedResolution = Res;
                State.AllocatedLayerCount = LayerCount;

                // Vulkan doesn't zero new image memory; with no weight payload this frame, clear every
                // slice so a sampler can't read garbage (otherwise the weight upload below seeds them).
                if (TerrainItem.WeightUpload == 0)
                {
                    CmdList.ClearImageFloat(
                        State.LayerWeightTexture,
                        FTextureSubresourceSet(0u, 1u, 0u, (uint16)std::max(LayerCount, 1u)),
                        FColor(0.0f, 0.0f, 0.0f, 0.0f));
                }
            }

            // Height upload (from the snapshot)
            const int32 ResI = (int32)Res;
            const bool  bHeightDirty = TerrainItem.HeightUpload != 0;
            FIntVector2 RectMin = FIntVector2(0);
            FIntVector2 RectMax = FIntVector2(ResI - 1);

            if (TerrainItem.HeightUpload == 1 && TerrainItem.HeightBytes.size() == SlicePixels)
            {
                CmdList.WriteImage(State.HeightmapTexture, 0u, 0u, TerrainItem.HeightBytes.data(), Res * (uint32)sizeof(float), 0u);
            }
            else if (TerrainItem.HeightUpload == 2)
            {
                RectMin = TerrainItem.HeightRectMin;
                RectMax = TerrainItem.HeightRectMax;
                const uint32 RegionW = uint32(RectMax.x - RectMin.x + 1);
                const uint32 RegionH = uint32(RectMax.y - RectMin.y + 1);
                // Snapshot rect is tightly packed, so the source row pitch is RegionW, not Res.
                CmdList.WriteImageRegion(State.HeightmapTexture, 0u, 0u,
                                         (uint32)RectMin.x, (uint32)RectMin.y, RegionW, RegionH,
                                         TerrainItem.HeightBytes.data(), RegionW * (uint32)sizeof(float));
            }

            // Weight upload: whole slices, packed ascending in the snapshot.
            if (TerrainItem.WeightUpload != 0 && !TerrainItem.WeightBytes.empty())
            {
                const uint8* Cursor = TerrainItem.WeightBytes.data();
                const uint8* End    = Cursor + TerrainItem.WeightBytes.size();
                for (uint32 L = 0; L < LayerCount; ++L)
                {
                    if ((TerrainItem.WeightSliceMask & (1u << L)) == 0u)
                    {
                        continue;
                    }
                    if (Cursor + SlicePixels > End)
                    {
                        break;
                    }
                    CmdList.WriteImage(State.LayerWeightTexture, L, 0u, Cursor, Res, 0u);
                    Cursor += SlicePixels;
                }
            }

            if (bHeightDirty && NormalShader)
            {
                // Central-difference normals read one neighbor each side, so dilate the
                // recompute region by a texel and clamp to the map.
                const int32 NMinX = std::max(RectMin.x - 1, 0);
                const int32 NMinY = std::max(RectMin.y - 1, 0);
                const int32 NMaxX = std::min(RectMax.x + 1, ResI - 1);
                const int32 NMaxY = std::min(RectMax.y + 1, ResI - 1);
                const int32 NW    = NMaxX - NMinX + 1;
                const int32 NH    = NMaxY - NMinY + 1;

                FTerrainNormalParams NormalParams{};
                NormalParams.Resolution    = ResI;
                NormalParams.RegionMinX    = NMinX;
                NormalParams.RegionMinY    = NMinY;
                NormalParams.RegionSizeX   = NW;
                NormalParams.RegionSizeY   = NH;
                NormalParams.TileWorldSize = TerrainItem.TileWorldSize;
                NormalParams.MaxHeight     = TerrainItem.MaxHeight;

                auto NormalParamsAlloc = CmdList.CopyTransient(NormalParams);

                FRHISamplerRef ClampSampler = TStaticRHISampler<true, false, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

                FBindingLayoutDesc NormalLayoutDesc;
                NormalLayoutDesc.SetBindingIndex(0)
                    .SetVisibility(ERHIShaderType::Compute)
                    .AddItem(FBindingLayoutItem::Texture_SRV(0))
                    .AddItem(FBindingLayoutItem::Texture_UAV(1));
                FRHIBindingLayout* NormalLayout = BindingCache.GetOrCreateBindingLayout(NormalLayoutDesc);

                FBindingSetDesc NormalSetDesc;
                NormalSetDesc.AddItem(FBindingSetItem::TextureSRV(0, State.HeightmapTexture, ClampSampler))
                             .AddItem(FBindingSetItem::TextureUAV(1, State.NormalTexture));
                FRHIBindingSetRef NormalSet = GRenderContext->CreateBindingSet(NormalSetDesc, NormalLayout);

                FComputePipelineDesc NormalPipelineDesc;
                NormalPipelineDesc.SetComputeShader(NormalShader)
                                  .AddBindingLayout(NormalLayout);
                FRHIComputePipelineRef NormalPipeline = GRenderContext->CreateComputePipeline(NormalPipelineDesc);

                FComputeState NormalComputeState;
                NormalComputeState.SetPipeline(NormalPipeline)
                                  .AddBindingSet(NormalSet);

                CmdList.SetComputeState(NormalComputeState);
                CmdList.SetPushConstants(&NormalParamsAlloc.Gpu, sizeof(uint64));
                CmdList.Dispatch((uint32(NW) + 7u) / 8u, (uint32(NH) + 7u) / 8u, 1u);
            }

            // Upload chunk + meshlet metadata the game thread rebuilt this frame; the
            // cull pass tests these AABBs, so it must land before the next cull.
            if (TerrainItem.bGeometryRebuilt)
            {
                const uint32 ChunkCount   = (uint32)TerrainItem.Chunks.size();
                const uint32 MeshletCount = (uint32)TerrainItem.Meshlets.size();

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
                            // bKeepInitialState pins Common so the tracker can move the
                            // buffer freely between SRV/UAV/Indirect at use sites.
                            Desc.bKeepInitialState = true;
                            Desc.InitialState      = EResourceStates::Common;
                            Buffer = GRenderContext->CreateBuffer(Desc);
                        }
                    };

                    AllocSSBO(State.ChunkInfoBuffer,      uint64(ChunkCount)   * sizeof(FTerrainChunkInfo),      DebugBase + "_Chunks",   false);
                    AllocSSBO(State.MeshletInfoBuffer,    uint64(MeshletCount) * sizeof(FTerrainMeshletInfo),    DebugBase + "_Meshlets", false);
                    AllocSSBO(State.VisibleMeshletBuffer, uint64(MeshletCount) * sizeof(FTerrainVisibleMeshlet), DebugBase + "_Visible",  false);
                    AllocSSBO(State.IndirectDrawBuffer,   sizeof(FDrawIndirectArguments),                        DebugBase + "_Indirect", true);

                    CmdList.WriteBuffer(State.ChunkInfoBuffer,   TerrainItem.Chunks.data(),   ChunkCount * sizeof(FTerrainChunkInfo));
                    CmdList.WriteBuffer(State.MeshletInfoBuffer, TerrainItem.Meshlets.data(), MeshletCount * sizeof(FTerrainMeshletInfo));

                    State.AllocatedChunkCount   = ChunkCount;
                    State.AllocatedMeshletCount = MeshletCount;
                }
            }
        }
    }

    void FForwardRenderScene::TerrainCullPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        if (Frame.Extracts.TerrainExtracts.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Terrain Cull", tracy::Color::SeaGreen);

        FRHIComputeShaderRef CullShader = FShaderLibrary::GetComputeShader("TerrainCull.slang");
        if (!CullShader)
        {
            return;
        }

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.SetComputeShader(CullShader)
                    .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        FRHIComputePipelineRef CullPipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        for (const FFrameData::FTerrainExtract& TerrainItem : Frame.Extracts.TerrainExtracts)
        {
            auto TerrainStateIt = TerrainGPUStates.find(TerrainItem.Entity);
            if (TerrainStateIt == TerrainGPUStates.end())
            {
                continue;
            }
            FTerrainGPUState& State = TerrainStateIt->second;
            if (!State.ChunkInfoBuffer || !State.MeshletInfoBuffer || !State.VisibleMeshletBuffer || !State.IndirectDrawBuffer)
            {
                continue;
            }
            if (State.AllocatedChunkCount == 0u || State.AllocatedMeshletCount == 0u)
            {
                continue;
            }

            // Seed the indirect args slot. Six verts per quad * meshletQuads^2
            // is the per-meshlet vertex count the terrain VS expects.
            FDrawIndirectArguments InitialArgs{};
            InitialArgs.VertexCount           = (uint32)(GTerrainMeshletMaxQuads * 6);
            InitialArgs.InstanceCount         = 0u;
            InitialArgs.StartVertexLocation   = 0u;
            InitialArgs.StartInstanceLocation = 0u;
            CmdList.WriteBuffer(State.IndirectDrawBuffer, &InitialArgs, sizeof(InitialArgs));

            FComputeState ComputeState;
            ComputeState.SetPipeline(CullPipeline)
                        .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            // Buffers read/written by device address, not descriptors; declare for the tracker.
            ComputeState.Reads(State.ChunkInfoBuffer);
            ComputeState.Reads(State.MeshletInfoBuffer);
            ComputeState.Writes(State.VisibleMeshletBuffer);
            ComputeState.Writes(State.IndirectDrawBuffer);
            CmdList.SetComputeState(ComputeState);

            FTerrainCullPushConstants Push{};
            Push.ChunksAddr          = State.ChunkInfoBuffer->GetAddress();
            Push.MeshletsAddr        = State.MeshletInfoBuffer->GetAddress();
            Push.VisibleMeshletsAddr = State.VisibleMeshletBuffer->GetAddress();
            Push.TerrainIndirectAddr = State.IndirectDrawBuffer->GetAddress();
            Push.ChunkCount   = State.AllocatedChunkCount;
            Push.MeshletCount = State.AllocatedMeshletCount;
            PushRootConstants(CmdList, Push);

            // One workgroup per chunk, one thread per meshlet; the chunk-level test on
            // thread 0 gates the rest via groupshared.
            CmdList.Dispatch(State.AllocatedChunkCount, 1u, 1u);
        }
    }

    // Depth-only pre-pass over culled terrain meshlets (reverse-Z, DepthWrite on) so the heavy shaded pass
    // early-Z rejects overdraw and runs its ~80-tap PBR once per pixel. Shares the material VS; no PS bound.
    void FForwardRenderScene::TerrainDepthPrePass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands = Frame.Geometry.DrawCommands;

        if (Frame.Extracts.TerrainExtracts.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Terrain Depth", tracy::Color::SeaGreen);

        for (const FFrameData::FTerrainExtract& TerrainItem : Frame.Extracts.TerrainExtracts)
        {
            if (TerrainItem.Resolution < 2 || TerrainItem.ChunkResolution < 2)
            {
                continue;
            }

            auto TerrainStateIt = TerrainGPUStates.find(TerrainItem.Entity);
            if (TerrainStateIt == TerrainGPUStates.end())
            {
                continue;
            }
            FTerrainGPUState& State = TerrainStateIt->second;
            if (!State.HeightmapTexture || !State.NormalTexture || !State.LayerWeightTexture)
            {
                continue;
            }
            if (!State.ChunkInfoBuffer || !State.MeshletInfoBuffer || !State.VisibleMeshletBuffer || !State.IndirectDrawBuffer)
            {
                continue;
            }
            if (State.AllocatedMeshletCount == 0u)
            {
                continue;
            }

            // Shaders were resolved + ref-held at extract (Extract Terrain); null VS => skip.
            FRHIVertexShader* VertexShader = TerrainItem.Shaders.VertexShader;
            if (!VertexShader)
            {
                continue;
            }

            const entt::entity Entity = TerrainItem.Entity;
            const uint32 Res = (uint32)TerrainItem.Resolution;

            const FMatrix4 WorldMat    = TerrainItem.WorldMatrix;
            const FVector3 WorldOrigin = FVector3(WorldMat[3]);
            const float HalfSize        = TerrainItem.TileWorldSize * 0.5f;

            const int32 QuadsPerChunk        = std::max(1, TerrainItem.ChunkResolution - 1);
            const int32 ChunksPerSide        = std::max(1, ((int32)Res - 1) / QuadsPerChunk);
            const int32 MeshletsPerChunkSide = (QuadsPerChunk + GTerrainMeshletQuads - 1) / GTerrainMeshletQuads;

            FTerrainRenderParams RenderParams{};
            RenderParams.OriginXZ             = FVector2(WorldOrigin.x - HalfSize, WorldOrigin.z - HalfSize);
            RenderParams.TileWorldSize        = TerrainItem.TileWorldSize;
            RenderParams.MaxHeight            = TerrainItem.MaxHeight;
            RenderParams.Resolution           = (int32)Res;
            RenderParams.ChunkResolution      = TerrainItem.ChunkResolution;
            RenderParams.ChunksPerSide        = ChunksPerSide;
            RenderParams.LayerCount           = TerrainItem.LayerCount;
            RenderParams.WorldOriginY         = FVector3(WorldOrigin.y, 0.0f, 0.0f);
            RenderParams.EntityID             = (uint32)Entity;
            RenderParams.MaterialIndex        = TerrainItem.MaterialIndex;
            RenderParams.MeshletsPerChunkSide = MeshletsPerChunkSide;
            RenderParams.MeshletQuadSide      = GTerrainMeshletQuads;

            auto RenderParamsAlloc = CmdList.CopyTransient(RenderParams);

            // First depth writer when there are no meshes; otherwise the mesh
            // depth pre-pass / base pass already populated this view's depth.
            FRenderPassDesc::FAttachment Depth;
            Depth.SetImage(GetSceneDepthRT())
                 .SetDepthClearValue(0.0f);
            if (!DrawCommands.empty())
            {
                Depth.SetLoadOp(ERenderLoadOp::Load);
            }

            FRenderPassDesc RenderPass;
            RenderPass.SetDepthAttachment(Depth)
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

            // No pass-local set: heightmap/normal read bindlessly, cull buffers by device address.
            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("TerrainDepth")
                .SetRenderState(RenderState)
                .SetVertexShader(VertexShader)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FGraphicsState GraphicsState;
            GraphicsState.SetRenderPass(RenderPass)
                .SetViewportState(MakeViewportStateFromImage(GetNamedImage(ENamedImage::HDR)))
                .SetPipeline(Pipeline)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                .SetIndirectParams(State.IndirectDrawBuffer);
            // Bindless/BDA reads (the depth VS samples height+normal and walks the cull buffers).
            GraphicsState.Reads(State.HeightmapTexture);
            GraphicsState.Reads(State.NormalTexture);
            GraphicsState.Reads(State.ChunkInfoBuffer);
            GraphicsState.Reads(State.MeshletInfoBuffer);
            GraphicsState.Reads(State.VisibleMeshletBuffer);

            FTerrainPushConstants Push{};
            Push.ParamsAddr        = RenderParamsAlloc.Gpu;
            Push.ChunksAddr        = State.ChunkInfoBuffer->GetAddress();
            Push.MeshletsAddr      = State.MeshletInfoBuffer->GetAddress();
            Push.VisibleAddr       = State.VisibleMeshletBuffer->GetAddress();
            Push.HeightmapIndex    = (uint32)State.HeightmapTexture->GetResourceID();
            Push.NormalIndex       = (uint32)State.NormalTexture->GetResourceID();
            Push.LayerWeightsIndex = (uint32)State.LayerWeightTexture->GetResourceID();

            CmdList.SetGraphicsState(GraphicsState);
            PushRootConstants(CmdList, Push);

            CmdList.DrawIndirect(1u, 0u);
        }
    }

    void FForwardRenderScene::TerrainRenderPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands = Frame.Geometry.DrawCommands;

        if (Frame.Extracts.TerrainExtracts.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Terrain Render", tracy::Color::SeaGreen);

        for (const FFrameData::FTerrainExtract& TerrainItem : Frame.Extracts.TerrainExtracts)
        {
            const entt::entity Entity  = TerrainItem.Entity;
            if (TerrainItem.Resolution < 2 || TerrainItem.ChunkResolution < 2)
            {
                continue;
            }

            auto TerrainStateIt = TerrainGPUStates.find(TerrainItem.Entity);
            if (TerrainStateIt == TerrainGPUStates.end())
            {
                continue;
            }
            FTerrainGPUState& State = TerrainStateIt->second;
            if (!State.HeightmapTexture || !State.NormalTexture || !State.LayerWeightTexture)
            {
                continue;
            }
            // Cull output makes the indirect draw legal; bail when it isn't built
            // yet (first frame, or heightmap dirty before TerrainUpdatePass ran).
            if (!State.ChunkInfoBuffer || !State.MeshletInfoBuffer || !State.VisibleMeshletBuffer || !State.IndirectDrawBuffer)
            {
                continue;
            }
            if (State.AllocatedMeshletCount == 0u)
            {
                continue;
            }


            // The extract enforced the terrain domain + default fallback and ref-held the shaders on the
            // game thread, so the render thread never touches the CMaterial. Null VS/PS => skip.
            FRHIVertexShader* VertexShader = TerrainItem.Shaders.VertexShader;
            FRHIPixelShader*  PixelShader  = TerrainItem.Shaders.PixelShader;
            if (!VertexShader || !PixelShader)
            {
                continue;
            }

            const uint32 Res = (uint32)TerrainItem.Resolution;

            const FMatrix4 WorldMat = TerrainItem.WorldMatrix;
            const FVector3 WorldOrigin = FVector3(WorldMat[3]);
            const float HalfSize = TerrainItem.TileWorldSize * 0.5f;

            const int32 QuadsPerChunk        = std::max(1, TerrainItem.ChunkResolution - 1);
            const int32 ChunksPerSide        = std::max(1, ((int32)Res - 1) / QuadsPerChunk);
            const int32 MeshletsPerChunkSide = (QuadsPerChunk + GTerrainMeshletQuads - 1) / GTerrainMeshletQuads;

            FTerrainRenderParams RenderParams{};
            RenderParams.OriginXZ             = FVector2(WorldOrigin.x - HalfSize, WorldOrigin.z - HalfSize);
            RenderParams.TileWorldSize        = TerrainItem.TileWorldSize;
            RenderParams.MaxHeight            = TerrainItem.MaxHeight;
            RenderParams.Resolution           = (int32)Res;
            RenderParams.ChunkResolution      = TerrainItem.ChunkResolution;
            RenderParams.ChunksPerSide        = ChunksPerSide;
            RenderParams.LayerCount           = TerrainItem.LayerCount;
            RenderParams.WorldOriginY         = FVector3(WorldOrigin.y, 0.0f, 0.0f);
            RenderParams.EntityID             = (uint32)Entity;
            RenderParams.MaterialIndex        = TerrainItem.MaterialIndex;
            RenderParams.MeshletsPerChunkSide = MeshletsPerChunkSide;
            RenderParams.MeshletQuadSide      = GTerrainMeshletQuads;

            // Transient ring upload; shader pulls via buffer-device-address
            // through the push constant set further down.
            auto RenderParamsAlloc = CmdList.CopyTransient(RenderParams);

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

            // TerrainDepthPrePass laid down terrain depth (and cleared when there
            // were no meshes), so always load and let early-Z drop the overdraw.
            FRenderPassDesc::FAttachment Depth;
            Depth.SetImage(GetSceneDepthRT())
                 .SetResolveImage(GetSceneDepthResolve())
                 .SetDepthClearValue(0.0f)
                 .SetLoadOp(ERenderLoadOp::Load);

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
                      .DisableDepthWrite();

            FRenderState RenderState;
            RenderState.SetRasterState(RasterState)
                       .SetDepthStencilState(DepthState);

            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("Terrain")
                .SetRenderState(RenderState)
                .SetVertexShader(VertexShader)
                .SetPixelShader(PixelShader)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FGraphicsState GraphicsState;
            GraphicsState.SetRenderPass(RenderPass)
                .SetViewportState(MakeViewportStateFromImage(GetNamedImage(ENamedImage::HDR)))
                .SetPipeline(Pipeline)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable())
                .SetIndirectParams(State.IndirectDrawBuffer);
            // VS samples height+normal and walks the cull buffers; PS samples layer weights, the
            // cluster light lists and the shadow atlases -- all by index, so declare them for barriers.
            GraphicsState.Reads(State.HeightmapTexture);
            GraphicsState.Reads(State.NormalTexture);
            GraphicsState.Reads(State.LayerWeightTexture);
            GraphicsState.Reads(State.ChunkInfoBuffer);
            GraphicsState.Reads(State.MeshletInfoBuffer);
            GraphicsState.Reads(State.VisibleMeshletBuffer);
            GraphicsState.Reads(CurrentView->ClusterBuffer);
            GraphicsState.Reads(GetNamedImage(ENamedImage::Cascade));
            GraphicsState.Reads(ShadowAtlas.GetImage());

            FTerrainPushConstants Push{};
            Push.ParamsAddr        = RenderParamsAlloc.Gpu;
            Push.ChunksAddr        = State.ChunkInfoBuffer->GetAddress();
            Push.MeshletsAddr      = State.MeshletInfoBuffer->GetAddress();
            Push.VisibleAddr       = State.VisibleMeshletBuffer->GetAddress();
            Push.HeightmapIndex    = (uint32)State.HeightmapTexture->GetResourceID();
            Push.NormalIndex       = (uint32)State.NormalTexture->GetResourceID();
            Push.LayerWeightsIndex = (uint32)State.LayerWeightTexture->GetResourceID();

            CmdList.SetGraphicsState(GraphicsState);
            PushRootConstants(CmdList, Push);

            // Cull populated the single indirect args slot (VertexCount = MeshletQuads^2*6,
            // InstanceCount = surviving meshlets). One GPU-driven draw.
            CmdList.DrawIndirect(1u, 0u);
        }
    }
    
    void FForwardRenderScene::SSAOPass(ICommandList& CmdList)
    {
        FFrameData& Frame = *RenderFrame;
        if (Frame.Geometry.DrawCommands.empty() || !Frame.CachedWorldSettings.bEnableSSAO)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("SSAO Pass", tracy::Color::Red);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef  PixelShader  = FShaderLibrary::GetPixelShader("SSAOPixel.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        FRHIImage* OutputImage     = GetNamedImage(ENamedImage::SSAO);
        FRHIImage* DepthAttachment = GetNamedImage(ENamedImage::DepthAttachment);

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
        Desc.SetDebugName("SSAO Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        GraphicsState.Reads(DepthAttachment);
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(MakeViewportStateFromImage(OutputImage));

        CmdList.SetGraphicsState(GraphicsState);

        struct FData
        {
            uint32 DepthIndex;
            uint32 NoiseIndex;
        } PC;

        PC.DepthIndex = (uint32)DepthAttachment->GetResourceID();
        PC.NoiseIndex = (uint32)SSAONoiseTexture->GetResourceID();

        PushRootConstants(CmdList, PC);

        CmdList.Draw(3, 1, 0, 0);
    }

    void FForwardRenderScene::SSAOBlurPass(ICommandList& CmdList)
    {
        FFrameData& Frame = *RenderFrame;
        if (Frame.Geometry.DrawCommands.empty() || !Frame.CachedWorldSettings.bEnableSSAO)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("SSAO Blur", tracy::Color::Red);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef  PixelShader  = FShaderLibrary::GetPixelShader("SSAOBlurPixel.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        FRHIImage* OutputImage = GetNamedImage(ENamedImage::SSAOBlur);
        FRHIImage* InputImage  = GetNamedImage(ENamedImage::SSAO);

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
        Desc.SetDebugName("SSAO Blur Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        GraphicsState.Reads(InputImage);
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(MakeViewportStateFromImage(OutputImage));

        CmdList.SetGraphicsState(GraphicsState);

        struct FData
        {
            uint32 AOIndex;
        } PC;

        PC.AOIndex = (uint32)InputImage->GetResourceID();

        PushRootConstants(CmdList, PC);

        CmdList.Draw(3, 1, 0, 0);
    }

    void FForwardRenderScene::BillboardPass(ICommandList& CmdList)
    {
        //@TODO BROKEN, GPU CRASH ACCESSING TEXTURE
        
        return;
        
        const FFrameData& Frame = *RenderFrame;
        const auto& BillboardInstances = Frame.Primitives.BillboardInstances;
        const auto& DrawCommands       = Frame.Geometry.DrawCommands;

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
            .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        
        FGraphicsState GraphicsState; GraphicsState
            .SetRenderPass(RenderPass)
            .SetViewportState(MakeViewportStateFromImage(GetNamedImage(ENamedImage::HDR)))
            .SetPipeline(GRenderContext->CreateGraphicsPipeline(Desc, RenderPass))
            .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        
        CmdList.SetGraphicsState(GraphicsState);
        PushRootConstants(CmdList);
        CmdList.Draw(6, BillboardInstances.size(), 0, 0);   
    }
    
    void FForwardRenderScene::WidgetPickerPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& WidgetInstances = Frame.Primitives.WidgetInstances;

        if (WidgetInstances.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Widget Picker Pass", tracy::Color::Magenta);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("WidgetVert.slang");
        FRHIPixelShaderRef PixelShader   = FShaderLibrary::GetPixelShader("WidgetPickerPixel.slang");

        // Before the picker readback: stamp the widget's entity id into the Picker buffer where it's opaque.
        // Depth-tested (no write) to match WidgetPass, so a widget picks only where it's visible.
        FRHIImage* PickerImage = GetNamedImage(ENamedImage::Picker);

        FRenderPassDesc::FAttachment PickerAttachment; PickerAttachment
            .SetImage(PickerImage)
            .SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc::FAttachment Depth; Depth
            .SetImage(GetNamedImage(ENamedImage::DepthAttachment))
            .SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc RenderPass; RenderPass
            .AddColorAttachment(PickerAttachment)
            .SetDepthAttachment(Depth)
            .SetRenderArea(PickerImage->GetExtent());

        FDepthStencilState DepthState; DepthState
            .DisableDepthWrite()
            .SetDepthTestEnable(true)
            .SetDepthFunc(EComparisonFunc::GreaterOrEqual);

        FRasterState RasterState; RasterState.SetCullNone();

        FRenderState RenderState; RenderState
            .SetDepthStencilState(DepthState)
            .SetRasterState(RasterState);

        FGraphicsPipelineDesc Desc; Desc
            .SetDebugName("Widget Picker Pass")
            .SetRenderState(RenderState)
            .SetVertexShader(VertexShader)
            .SetPixelShader(PixelShader)
            .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

        FGraphicsState GraphicsState; GraphicsState
            .SetRenderPass(RenderPass)
            .SetViewportState(MakeViewportStateFromImage(PickerImage))
            .SetPipeline(GRenderContext->CreateGraphicsPipeline(Desc, RenderPass))
            .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());

        CmdList.SetGraphicsState(GraphicsState);
        PushRootConstants(CmdList);
        CmdList.Draw(6, WidgetInstances.size(), 0, 0);
    }

    void FForwardRenderScene::WidgetPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& WidgetInstances = Frame.Primitives.WidgetInstances;

        if (WidgetInstances.empty())
        {
            return;
        }

        FRHIImage* OutputImage = GetViewOutputTarget();
        if (OutputImage == nullptr)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Widget Pass", tracy::Color::Magenta);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("WidgetVert.slang");
        FRHIPixelShaderRef PixelShader   = FShaderLibrary::GetPixelShader("WidgetPixel.slang");

        // Drawn AFTER tone mapping onto the display-referred target so widget colors match the screen UI.
        // RT/HDR/depth share one extent, so we still depth-test against scene depth (occluded). No depth write.
        FRenderPassDesc::FAttachment RenderTarget; RenderTarget
            .SetImage(OutputImage)
            .SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc::FAttachment Depth; Depth
            .SetImage(GetNamedImage(ENamedImage::DepthAttachment))
            .SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc RenderPass; RenderPass
            .AddColorAttachment(RenderTarget)
            .SetDepthAttachment(Depth)
            .SetRenderArea(OutputImage->GetExtent());

        // Reversed-Z: GreaterOrEqual keeps fragments at/in front of scene depth, discards occluded.
        FDepthStencilState DepthState; DepthState
            .DisableDepthWrite()
            .SetDepthTestEnable(true)
            .SetDepthFunc(EComparisonFunc::GreaterOrEqual);

        FRasterState RasterState; RasterState.SetCullNone();

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
            .SetRasterState(RasterState)
            .SetBlendState(BlendState);

        FGraphicsPipelineDesc Desc; Desc
            .SetDebugName("Widget Pass")
            .SetRenderState(RenderState)
            .SetVertexShader(VertexShader)
            .SetPixelShader(PixelShader)
            .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

        FGraphicsState GraphicsState; GraphicsState
            .SetRenderPass(RenderPass)
            .SetViewportState(MakeViewportStateFromImage(OutputImage))
            .SetPipeline(GRenderContext->CreateGraphicsPipeline(Desc, RenderPass))
            .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());

        CmdList.SetGraphicsState(GraphicsState);
        PushRootConstants(CmdList);
        CmdList.Draw(6, WidgetInstances.size(), 0, 0);
    }

    void FForwardRenderScene::TextPass(ICommandList& CmdList)
    {
        const FFrameData& Frame   = *RenderFrame;
        const auto&       Glyphs  = Frame.Primitives.GlyphInstances;
        const auto&       Batches = Frame.Primitives.TextBatches;

        if (Glyphs.empty() || Batches.empty())
        {
            return;
        }

        FRHIImage* HDRImage = GetNamedImage(ENamedImage::HDR);
        if (HDRImage == nullptr)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Text Pass", tracy::Color::Yellow);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("TextVert.slang");
        FRHIPixelShaderRef  PixelShader  = FShaderLibrary::GetPixelShader("TextPixel.slang");

        // Drawn pre-tone-map into HDR like billboards: one MRT pass writes color (SV_Target0) and stamps the
        // glyph's entity id into the Picker buffer (SV_Target1), so text stays click-selectable without a
        // second pass. Per-component bDepthTest selects between depth-tested+written (occluded by the scene)
        // and always-on-top; the scene depth is bound either way so both pipelines share one render pass.
        FRenderPassDesc::FAttachment RenderTarget; RenderTarget
            .SetImage(HDRImage)
            .SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc::FAttachment PickerAttachment; PickerAttachment
            .SetImage(GetNamedImage(ENamedImage::Picker))
            .SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc::FAttachment DepthAttachment; DepthAttachment
            .SetImage(GetNamedImage(ENamedImage::DepthAttachment))
            .SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc RenderPass; RenderPass
            .AddColorAttachment(RenderTarget)
            .AddColorAttachment(PickerAttachment)
            .SetDepthAttachment(DepthAttachment)
            .SetRenderArea(HDRImage->GetExtent());

        FRasterState RasterState; RasterState.SetCullNone();

        // Blend only the color target; the Picker (uint id) must not blend.
        FBlendState BlendState; BlendState
            .Targets[0]
                .SetBlendEnable(true)
                .SetSrcBlend(EBlendFactor::SrcAlpha)
                .SetDestBlend(EBlendFactor::OneMinusSrcAlpha)
                .SetBlendOp(EBlendOp::Add)
                .SetSrcBlendAlpha(EBlendFactor::One)
                .SetDestBlendAlpha(EBlendFactor::OneMinusSrcAlpha)
                .SetBlendOpAlpha(EBlendOp::Add);

        auto MakePipeline = [&](bool bDepth) -> FRHIGraphicsPipelineRef
        {
            FDepthStencilState DepthState;
            if (bDepth)
            {
                // Reversed-Z: GreaterOrEqual keeps fragments at/in front of scene depth, and writes depth so
                // the text occludes things behind it.
                DepthState.SetDepthTestEnable(true).SetDepthFunc(EComparisonFunc::GreaterOrEqual).EnableDepthWrite();
            }
            else
            {
                DepthState.DisableDepthTest().DisableDepthWrite();
            }

            FRenderState RenderState; RenderState
                .SetDepthStencilState(DepthState)
                .SetRasterState(RasterState)
                .SetBlendState(BlendState);

            FGraphicsPipelineDesc Desc; Desc
                .SetDebugName("Text Pass")
                .SetRenderState(RenderState)
                .SetVertexShader(VertexShader)
                .SetPixelShader(PixelShader)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

            return GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);
        };

        FRHIGraphicsPipelineRef PipelineDepth = MakePipeline(true);
        FRHIGraphicsPipelineRef PipelineOnTop = MakePipeline(false);

        // All glyphs across every batch share one transient array; batches index it via FirstInstance.
        auto GlyphAlloc = CmdList.CopyTransientArray(Glyphs.data(), Glyphs.size());

        struct FTextPushConstants
        {
            uint64 GlyphsAddr;
            uint32 AtlasIndex;
            uint32 AtlasWidth;
            uint32 AtlasHeight;
            float  DistanceRange;
            uint32 ScreenWidth;   // 0 for world text (only the debug screen-space pass uses these)
            uint32 ScreenHeight;
        };
        static_assert(sizeof(FTextPushConstants) == 32, "FTextPushConstants must match TextCommon.slang.");

        auto MakeState = [&](const FRHIGraphicsPipelineRef& Pipeline)
        {
            FGraphicsState State; State
                .SetRenderPass(RenderPass)
                .SetViewportState(MakeViewportStateFromImage(HDRImage))
                .SetPipeline(Pipeline)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            return State;
        };
        const FGraphicsState StateDepth = MakeState(PipelineDepth);
        const FGraphicsState StateOnTop = MakeState(PipelineOnTop);

        auto DrawBatch = [&](const FFrameData::FTextBatch& Batch)
        {
            FTextPushConstants PC = {};
            PC.GlyphsAddr    = GlyphAlloc.Gpu;
            PC.AtlasIndex    = Batch.AtlasIndex;
            PC.AtlasWidth    = Batch.AtlasWidth;
            PC.AtlasHeight   = Batch.AtlasHeight;
            PC.DistanceRange = Batch.DistanceRange;

            PushRootConstants(CmdList, PC);
            CmdList.Draw(6, Batch.Count, 0, Batch.FirstInstance);
        };

        // Depth-tested text first (sorts against the scene), then always-on-top text last so it stays on top.
        CmdList.SetGraphicsState(StateDepth);
        for (const FFrameData::FTextBatch& Batch : Batches)
        {
            if (Batch.bDepthTest)
            {
                DrawBatch(Batch);
            }
        }

        CmdList.SetGraphicsState(StateOnTop);
        for (const FFrameData::FTextBatch& Batch : Batches)
        {
            if (!Batch.bDepthTest)
            {
                DrawBatch(Batch);
            }
        }
    }

    void FForwardRenderScene::DebugTextPass(ICommandList& CmdList)
    {
        const FFrameData& Frame  = *RenderFrame;
        const auto&       Glyphs = Frame.Primitives.DebugTextGlyphs;
        const auto&       Batch  = Frame.Primitives.DebugTextBatch;

        if (Glyphs.empty() || Batch.Count == 0)
        {
            return;
        }

        FRHIImage* OutputImage = GetViewOutputTarget();
        if (OutputImage == nullptr)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Debug Text Pass", tracy::Color::Yellow);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("DebugTextVert.slang");
        FRHIPixelShaderRef  PixelShader  = FShaderLibrary::GetPixelShader("DebugTextPixel.slang");

        // Screen-space overlay onto the final display-referred target (post-tone-map, like the screen UI),
        // top-left stack. No depth, alpha blend, colors written as authored.
        FRenderPassDesc::FAttachment RenderTarget; RenderTarget
            .SetImage(OutputImage)
            .SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc RenderPass; RenderPass
            .AddColorAttachment(RenderTarget)
            .SetRenderArea(OutputImage->GetExtent());

        FDepthStencilState DepthState; DepthState.DisableDepthTest().DisableDepthWrite();
        FRasterState RasterState; RasterState.SetCullNone();

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
            .SetRasterState(RasterState)
            .SetBlendState(BlendState);

        FGraphicsPipelineDesc Desc; Desc
            .SetDebugName("Debug Text Pass")
            .SetRenderState(RenderState)
            .SetVertexShader(VertexShader)
            .SetPixelShader(PixelShader)
            .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

        auto GlyphAlloc = CmdList.CopyTransientArray(Glyphs.data(), Glyphs.size());

        struct FTextPushConstants
        {
            uint64 GlyphsAddr;
            uint32 AtlasIndex;
            uint32 AtlasWidth;
            uint32 AtlasHeight;
            float  DistanceRange;
            uint32 ScreenWidth;
            uint32 ScreenHeight;
        };
        static_assert(sizeof(FTextPushConstants) == 32, "FTextPushConstants must match TextCommon.slang.");

        // Use the DISPLAY/panel size (not the RT extent) for px->NDC: the world RT is a fixed size stretched
        // to the editor panel, so converting against the RT would let that stretch distort the text. The
        // panel size cancels the RT->panel stretch, keeping glyphs square and a true pixel size on screen.
        const FVector4 PanelSize = Frame.SceneGlobalData.ScreenSize;
        const uint32   ScreenW   = PanelSize.x > 1.0f ? (uint32)PanelSize.x : OutputImage->GetSizeX();
        const uint32   ScreenH   = PanelSize.y > 1.0f ? (uint32)PanelSize.y : OutputImage->GetSizeY();

        FTextPushConstants PC = {};
        PC.GlyphsAddr    = GlyphAlloc.Gpu;
        PC.AtlasIndex    = Batch.AtlasIndex;
        PC.AtlasWidth    = Batch.AtlasWidth;
        PC.AtlasHeight   = Batch.AtlasHeight;
        PC.DistanceRange = Batch.DistanceRange;
        PC.ScreenWidth   = ScreenW;
        PC.ScreenHeight  = ScreenH;

        FGraphicsState GraphicsState; GraphicsState
            .SetRenderPass(RenderPass)
            .SetViewportState(MakeViewportStateFromImage(OutputImage))
            .SetPipeline(GRenderContext->CreateGraphicsPipeline(Desc, RenderPass))
            .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());

        CmdList.SetGraphicsState(GraphicsState);
        PushRootConstants(CmdList, PC);
        CmdList.Draw(6, Batch.Count, 0, 0);
    }

    void FForwardRenderScene::TransparentPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& DrawCommands        = Frame.Geometry.DrawCommands;
        const auto& TranslucentDrawList = Frame.Geometry.TranslucentDrawList;
        const uint32 NumDrawsPerView    = Frame.Views.NumDrawsPerView;
        const uint32 ViewBase           = CurrentCameraEarlyView * NumDrawsPerView;

        if (TranslucentDrawList.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Transparent Pass", tracy::Color::CadetBlue);

        FRenderPassDesc::FAttachment RenderTarget0;
        RenderTarget0.SetImage(GetNamedImage(ENamedImage::Accum))
                    .SetLoadOp(ERenderLoadOp::Clear)
                    .SetClearColor(FVector4(0.0));
        
        FRenderPassDesc::FAttachment RenderTarget1;
        RenderTarget1.SetImage(GetNamedImage(ENamedImage::Revealage))
                    .SetLoadOp(ERenderLoadOp::Clear)
                    .SetClearColor(FVector4(1.0));
        
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
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                .SetVariableRateShadingState(MakeWorldShadingRate(Frame.CachedWorldSettings));

            FGraphicsState GraphicsState;
            GraphicsState.SetRenderPass(RenderPass)
                         .SetViewportState(SceneViewportState)
                         .SetPipeline(GRenderContext->CreateGraphicsPipeline(Desc, RenderPass))
                         .SetIndirectParams(GetIndirectArgs())
                         .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            // Cull/skinning outputs + shadow atlases are read by index; declare them so the
            // tracker inserts the UAV/depth -> shader-read barriers.
            GraphicsState.Reads(GetMeshletDrawList());
            GraphicsState.Reads(CurrentView->ClusterBuffer);
            GraphicsState.Reads(GetPreSkinnedVerticesBuffer());
            GraphicsState.Reads(GetNamedImage(ENamedImage::Cascade));
            GraphicsState.Reads(ShadowAtlas.GetImage());

            CmdList.SetGraphicsState(GraphicsState);
            PushRootConstants(CmdList);
            CmdList.DrawIndirect(Batch.DrawCount, (ViewBase + Batch.IndirectDrawOffset) * sizeof(FDrawIndirectArguments));
        }
    }

    void FForwardRenderScene::OITResolvePass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& TranslucentDrawList = Frame.Geometry.TranslucentDrawList;

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

    
        FRHIImage* AccumTex     = GetNamedImage(ENamedImage::Accum);
        FRHIImage* RevealageTex = GetNamedImage(ENamedImage::Revealage);

        FGraphicsPipelineDesc Desc;
        Desc.SetDebugName("OITResolve Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        // Accum/Revealage read by bindless index, not descriptors; declare for the barrier tracker.
        GraphicsState.Reads(AccumTex);
        GraphicsState.Reads(RevealageTex);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(SceneViewportState);

        struct FOITResolvePushConstants
        {
            uint32 AccumIndex;
            uint32 RevealageIndex;
            uint32 _Pad0;
            uint32 _Pad1;
        };
        static_assert(sizeof(FOITResolvePushConstants) == 16, "FOITResolvePushConstants must match the slang push block.");

        FOITResolvePushConstants PC = {};
        PC.AccumIndex     = (uint32)AccumTex->GetResourceID();
        PC.RevealageIndex = (uint32)RevealageTex->GetResourceID();

        CmdList.SetGraphicsState(GraphicsState);
        PushRootConstants(CmdList, PC);
        CmdList.Draw(3, 1, 0, 0);
    }


    namespace
    {
        // Mirrors the FPushConstants in the three VolumetricFog*.slang shaders.
        struct FFroxelInjectPushConstants
        {
            uint32 GridSize[3];
            float  NearPlane;
            float  FogRange;            // froxel far plane (max fog distance, view units)
            uint32 bSunVolumetric;      // 1 if light 0 (sun) opted into volumetrics
            uint32 NumLocalVolumetric;  // <= GFroxelMaxLocalLights
            float  Time;
            uint32 LocalLightIndices[GFroxelMaxLocalLights];
            uint64 FogAddr;             // fog params UBO (transient device address) -- offset 96, 8-aligned
            uint32 ScatterUAV;          // bindless 3D UAV index of the scatter volume
            uint32 _Pad0;
        };
        static_assert(sizeof(FFroxelInjectPushConstants) <= 128, "Froxel inject PC must fit 128B");

        struct FFroxelIntegratePushConstants
        {
            uint32 GridSize[3];
            float  NearPlane;
            float  FogRange;
            uint32 ScatterSRV;     // bindless 3D SRV index of the scatter volume
            uint32 IntegratedUAV;  // bindless 3D UAV index of the integrated volume
            uint32 _Pad0;
        };

        struct FFroxelApplyPushConstants
        {
            uint64 FogAddr;          // device address of the fog-params UBO (transient)
            uint32 DepthIndex;       // bindless 2D SRV: scene depth
            uint32 IntegratedIndex;  // bindless 3D SRV: integrated froxel volume
            uint32 GridZ;
            float  NearPlane;
            float  FogRange;
            uint32 _Pad0;
        };
        static_assert(sizeof(FFroxelApplyPushConstants) == 32, "Froxel apply PC must match the slang push block.");
    }

    void FForwardRenderScene::FroxelInjectPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        if (!Frame.Volumetrics.bHasFog || !CVarVolFogEnabled.GetValue())
        {
            return;
        }

        const auto& LightData       = Frame.Lighting.LightData;
        const auto& SceneGlobalData = Frame.SceneGlobalData;

        // Same volumetric-light policy as the lit passes: sun (light 0) is special-cased;
        // local point/spot lights opt in via ELightFlags::Volumetric.
        bool   bSunVolumetric = false;
        uint32 LocalIndices[GFroxelMaxLocalLights];
        uint32 NumLocal = 0;

        if (LightData.NumLights > 0
            && EnumHasAnyFlags(LightData.Lights[0].Flags, ELightFlags::Directional)
            && EnumHasAnyFlags(LightData.Lights[0].Flags, ELightFlags::Volumetric))
        {
            bSunVolumetric = true;
        }
        for (uint32 i = 1; i < LightData.NumLights; ++i)
        {
            if (!EnumHasAnyFlags(LightData.Lights[i].Flags, ELightFlags::Volumetric))
            {
                continue;
            }
            if (NumLocal >= GFroxelMaxLocalLights)
            {
                break;
            }
            LocalIndices[NumLocal++] = i;
        }

        LUMINA_PROFILE_SECTION_COLORED("Froxel Inject Pass", tracy::Color::SlateBlue);

        FRHIComputeShaderRef CS = FShaderLibrary::GetComputeShader("VolumetricFogInject.slang");
        if (!CS)
        {
            return;
        }

        FRHIImage* Scatter = GetNamedImage(ENamedImage::FroxelScatter);

        // Scatter volume written via bindless 3D UAV; shadow atlases sampled bindlessly.
        auto FogAlloc = CmdList.CopyTransient(Frame.Volumetrics.FogParams);

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        PipelineDesc.CS = CS;
        PipelineDesc.DebugName = "Froxel Inject";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        State.Writes(Scatter);
        State.Reads(GetNamedImage(ENamedImage::Cascade));   // shadow atlases sampled bindlessly
        State.Reads(ShadowAtlas.GetImage());
        CmdList.SetComputeState(State);

        const float FogRange = Math::Clamp(Frame.Volumetrics.FogParams.VolumetricParams.z, 1.0f, SceneGlobalData.FarPlane);

        FFroxelInjectPushConstants PC = {};
        PC.FogAddr            = FogAlloc.Gpu;
        PC.ScatterUAV         = (uint32)Scatter->GetMipUAVIndex(0);
        PC.GridSize[0]        = GFroxelGridX;
        PC.GridSize[1]        = GFroxelGridY;
        PC.GridSize[2]        = GFroxelGridZ;
        PC.NearPlane          = Math::Max(SceneGlobalData.NearPlane, 0.05f);
        PC.FogRange           = FogRange;
        PC.bSunVolumetric     = bSunVolumetric ? 1u : 0u;
        PC.NumLocalVolumetric = NumLocal;
        PC.Time               = SceneGlobalData.Time;
        for (uint32 i = 0; i < NumLocal; ++i)
        {
            PC.LocalLightIndices[i] = LocalIndices[i];
        }
        PushRootConstants(CmdList, PC);

        CmdList.Dispatch(RenderUtils::GetGroupCount(GFroxelGridX, 4),
                         RenderUtils::GetGroupCount(GFroxelGridY, 4),
                         RenderUtils::GetGroupCount(GFroxelGridZ, 4));
    }

    void FForwardRenderScene::FroxelIntegratePass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        if (!Frame.Volumetrics.bHasFog)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Froxel Integrate Pass", tracy::Color::MediumPurple);

        FRHIComputeShaderRef CS = FShaderLibrary::GetComputeShader("VolumetricFogIntegrate.slang");
        if (!CS)
        {
            return;
        }

        FRHIImage* Scatter    = GetNamedImage(ENamedImage::FroxelScatter);
        FRHIImage* Integrated = GetNamedImage(ENamedImage::FroxelIntegrated);

        // Scatter read via bindless 3D SRV, integrated written via bindless 3D UAV.
        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        PipelineDesc.CS = CS;
        PipelineDesc.DebugName = "Froxel Integrate";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        State.Reads(Scatter);
        State.Writes(Integrated);
        CmdList.SetComputeState(State);

        const float FogRange = Math::Clamp(Frame.Volumetrics.FogParams.VolumetricParams.z, 1.0f, Frame.SceneGlobalData.FarPlane);

        FFroxelIntegratePushConstants PC = {};
        PC.GridSize[0]    = GFroxelGridX;
        PC.GridSize[1]    = GFroxelGridY;
        PC.GridSize[2]    = GFroxelGridZ;
        PC.NearPlane      = Math::Max(Frame.SceneGlobalData.NearPlane, 0.05f);
        PC.FogRange       = FogRange;
        PC.ScatterSRV     = (uint32)Scatter->GetResourceID();
        PC.IntegratedUAV  = (uint32)Integrated->GetMipUAVIndex(0);
        PushRootConstants(CmdList, PC);

        // One thread per (x,y) column; each marches the full Z range.
        CmdList.Dispatch(RenderUtils::GetGroupCount(GFroxelGridX, 8),
                         RenderUtils::GetGroupCount(GFroxelGridY, 8),
                         1u);
    }

    void FForwardRenderScene::FroxelApplyPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        if (!Frame.Volumetrics.bHasFog || !CVarVolFogEnabled.GetValue())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Froxel Apply Pass", tracy::Color::Orange3);

        FRHIVertexShaderRef VS = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef  PS = FShaderLibrary::GetPixelShader("VolumetricFogApply.slang");
        if (!VS || !PS)
        {
            return;
        }

        FRHIImage* HDR        = GetNamedImage(ENamedImage::HDR);
        FRHIImage* SceneDepth = GetNamedImage(ENamedImage::DepthAttachment);
        FRHIImage* Integrated = GetNamedImage(ENamedImage::FroxelIntegrated);

        FRasterState RasterState;
        RasterState.SetCullNone();

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest();
        DepthState.DisableDepthWrite();

        // result = inScatter + HDR * transmittance, via src=(One), dst=(InvSrcAlpha)
        // with the shader writing alpha = 1 - transmittance.
        FBlendState::RenderTarget OverBlend;
        OverBlend.SetBlendEnable(true);
        OverBlend.SetSrcBlend(EBlendFactor::One);
        OverBlend.SetDestBlend(EBlendFactor::InvSrcAlpha);
        OverBlend.SetBlendOp(EBlendOp::Add);
        OverBlend.SetSrcBlendAlpha(EBlendFactor::Zero);
        OverBlend.SetDestBlendAlpha(EBlendFactor::One);
        OverBlend.SetBlendOpAlpha(EBlendOp::Add);

        FBlendState BlendState;
        BlendState.SetRenderTarget(0, OverBlend);

        auto FogAlloc = CmdList.CopyTransient(Frame.Volumetrics.FogParams);

        FRenderPassDesc::FAttachment Attachment; Attachment
            .SetImage(HDR)
            .SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc RenderPass; RenderPass
            .AddColorAttachment(Attachment)
            .SetRenderArea(HDR->GetExtent());

        FRenderState RenderState;
        RenderState.SetRasterState(RasterState);
        RenderState.SetBlendState(BlendState);
        RenderState.SetDepthStencilState(DepthState);

        FGraphicsPipelineDesc Desc;
        Desc.SetDebugName("Froxel Fog Apply");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.SetVertexShader(VS);
        Desc.SetPixelShader(PS);
        Desc.SetVariableRateShadingState(MakeWorldShadingRate(Frame.CachedWorldSettings));

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        // Depth + integrated volume read by bindless index, not descriptors; declare for the tracker.
        GraphicsState.Reads(SceneDepth);
        GraphicsState.Reads(Integrated);
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(SceneViewportState);

        FFroxelApplyPushConstants PC = {};
        PC.FogAddr         = FogAlloc.Gpu;
        PC.DepthIndex      = (uint32)SceneDepth->GetResourceID();
        PC.IntegratedIndex = (uint32)Integrated->GetResourceID();
        PC.GridZ           = GFroxelGridZ;
        PC.NearPlane       = Math::Max(Frame.SceneGlobalData.NearPlane, 0.05f);
        PC.FogRange        = Math::Clamp(Frame.Volumetrics.FogParams.VolumetricParams.z, 1.0f, Frame.SceneGlobalData.FarPlane);

        CmdList.SetGraphicsState(GraphicsState);
        PushRootConstants(CmdList, PC);

        CmdList.Draw(3, 1, 0, 0);
    }

    void FForwardRenderScene::WaterPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const TVector<FGPUWater>& Waters = Frame.Water.Surfaces;
        if (Waters.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Water Pass", tracy::Color::CadetBlue);

        FRHIVertexShaderRef VS = FShaderLibrary::GetVertexShader("WaterVert.slang");
        FRHIPixelShaderRef  PS = FShaderLibrary::GetPixelShader("WaterPixel.slang");
        if (!VS || !PS)
        {
            return;
        }

        FRHIImage* HDR        = GetNamedImage(ENamedImage::HDR);
        FRHIImage* SceneColor = GetNamedImage(ENamedImage::WaterRefraction);
        FRHIImage* SceneDepth = GetNamedImage(ENamedImage::DepthAttachment);
        FRHIImage* Prefilter  = GetNamedImage(ENamedImage::SkyPrefilter);

        // Copy the lit opaque scene so the shader can sample "behind the water" (refraction + SSR) without
        // reading the HDR target it also writes.
        CmdList.CopyImage(HDR, FTextureSlice(), SceneColor, FTextureSlice());

        // No depth attachment: the PS rejects fragments behind the opaque scene by sampling SceneDepth, so we
        // can read depth freely with no sample-while-bound hazard (one transparent layer -> early-Z unneeded).
        FRenderPassDesc::FAttachment ColorAttachment;
        ColorAttachment.SetImage(HDR).SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc RenderPass;
        RenderPass.AddColorAttachment(ColorAttachment)
                  .SetRenderArea(HDR->GetExtent());

        // Double-sided so the surface is visible from below (camera submerged) too.
        FRasterState RasterState;
        RasterState.SetCullNone().EnableDepthClip();

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest();
        DepthState.DisableDepthWrite();

        // Standard alpha "over": the PS composites scene+water; alpha softens the shoreline edge.
        FBlendState::RenderTarget WaterBlend;
        WaterBlend.SetBlendEnable(true)
                  .SetSrcBlend(EBlendFactor::SrcAlpha)
                  .SetDestBlend(EBlendFactor::OneMinusSrcAlpha)
                  .SetBlendOp(EBlendOp::Add)
                  .SetSrcBlendAlpha(EBlendFactor::One)
                  .SetDestBlendAlpha(EBlendFactor::OneMinusSrcAlpha)
                  .SetBlendOpAlpha(EBlendOp::Add);

        FBlendState BlendState;
        BlendState.SetRenderTarget(0, WaterBlend);

        FRenderState RenderState;
        RenderState.SetRasterState(RasterState)
                   .SetDepthStencilState(DepthState)
                   .SetBlendState(BlendState);

        auto WaterAlloc = CmdList.CopyTransientArray(Waters.data(), Waters.size());

        struct FWaterPushConstants
        {
            uint64 WatersAddr;
            uint32 SceneColorIndex;
            uint32 SceneDepthIndex;
        };
        static_assert(sizeof(FWaterPushConstants) == 16, "FWaterPushConstants must match Includes/Water.slang.");

        FWaterPushConstants PC = {};
        PC.WatersAddr      = WaterAlloc.Gpu;
        PC.SceneColorIndex = (uint32)SceneColor->GetResourceID();
        PC.SceneDepthIndex = (uint32)SceneDepth->GetResourceID();

        FGraphicsPipelineDesc Desc;
        Desc.SetDebugName("Water Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.SetVertexShader(VS);
        Desc.SetPixelShader(PS);
        Desc.SetVariableRateShadingState(MakeWorldShadingRate(Frame.CachedWorldSettings));

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        // One draw per water body; the body index arrives via SV_VulkanInstanceID (FirstInstance). Grid is
        // (Res-1)^2 cells, 6 verts each, generated procedurally from SV_VertexID in the VS.
        for (uint32 i = 0; i < (uint32)Waters.size(); ++i)
        {
            const uint32 Res = Waters[i].GridResolution;
            const uint32 VertexCount = (Res > 1u) ? (Res - 1u) * (Res - 1u) * 6u : 0u;
            if (VertexCount == 0u)
            {
                continue;
            }

            FGraphicsState GraphicsState;
            GraphicsState.SetPipeline(Pipeline);
            GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            // Scene color/depth and the sky prefilter cube are read by bindless index; declare them so the
            // tracker inserts the shader-read transitions.
            GraphicsState.Reads(SceneColor);
            GraphicsState.Reads(SceneDepth);
            GraphicsState.Reads(Prefilter);
            GraphicsState.SetRenderPass(RenderPass);
            GraphicsState.SetViewportState(SceneViewportState);

            CmdList.SetGraphicsState(GraphicsState);
            PushRootConstants(CmdList, PC);

            CmdList.Draw(VertexCount, 1, 0, i);
        }
    }

    void FForwardRenderScene::UnderwaterPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        if (!Frame.Water.bUnderwaterActive)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Underwater Pass", tracy::Color::SteelBlue);

        FRHIVertexShaderRef VS = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef  PS = FShaderLibrary::GetPixelShader("WaterUnderwater.slang");
        if (!VS || !PS)
        {
            return;
        }

        FRHIImage* HDR        = GetNamedImage(ENamedImage::HDR);
        FRHIImage* SceneColor = GetNamedImage(ENamedImage::WaterRefraction);
        FRHIImage* SceneDepth = GetNamedImage(ENamedImage::DepthAttachment);

        // Sample the fully-composited scene from a copy; the PS recomputes every pixel (above-water pixels
        // pass through unchanged), so the half-submerged screen split falls out of the per-ray path length.
        CmdList.CopyImage(HDR, FTextureSlice(), SceneColor, FTextureSlice());

        FRenderPassDesc::FAttachment ColorAttachment;
        ColorAttachment.SetImage(HDR).SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc RenderPass;
        RenderPass.AddColorAttachment(ColorAttachment)
                  .SetRenderArea(HDR->GetExtent());

        FRasterState RasterState;
        RasterState.SetCullNone();

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest();
        DepthState.DisableDepthWrite();

        FRenderState RenderState;
        RenderState.SetRasterState(RasterState);
        RenderState.SetDepthStencilState(DepthState);

        auto ParamsAlloc = CmdList.CopyTransient(Frame.Water.Underwater);

        struct FUnderwaterPushConstants
        {
            uint64 ParamsAddr;
            uint32 SceneColorIndex;
            uint32 SceneDepthIndex;
        };
        static_assert(sizeof(FUnderwaterPushConstants) == 16, "FUnderwaterPushConstants must match WaterUnderwater.slang.");

        FUnderwaterPushConstants PC = {};
        PC.ParamsAddr      = ParamsAlloc.Gpu;
        PC.SceneColorIndex = (uint32)SceneColor->GetResourceID();
        PC.SceneDepthIndex = (uint32)SceneDepth->GetResourceID();

        FGraphicsPipelineDesc Desc;
        Desc.SetDebugName("Underwater Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.SetVertexShader(VS);
        Desc.SetPixelShader(PS);
        Desc.SetVariableRateShadingState(MakeWorldShadingRate(Frame.CachedWorldSettings));

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        GraphicsState.Reads(SceneColor);
        GraphicsState.Reads(SceneDepth);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(SceneViewportState);

        CmdList.SetGraphicsState(GraphicsState);
        PushRootConstants(CmdList, PC);

        CmdList.Draw(3, 1, 0, 0);
    }
    
    static constexpr uint32 GPrefilterSampleCount = 256;

    void FForwardRenderScene::SkyCubeCapturePass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("Sky Cube Capture", tracy::Color::SkyBlue);
        
        const FFrameData& Frame = *RenderFrame;
        const auto& LightData         = Frame.Lighting.LightData;
        const auto& SceneGlobalData   = Frame.SceneGlobalData;
        FRHIImage* EnvironmentMapImage= Frame.Volumetrics.EnvironmentMapImage;
        const bool bIBLDirty          = Frame.Volumetrics.bIBLDirty;

        if (!RenderSettings.bHasEnvironment)
        {
            if (FRHIImage* SkyCube = GetNamedImage(ENamedImage::SkyCube))
            {
                CmdList.ClearImageFloat(SkyCube, AllSubresources, FColor::Black);
            }
            if (FRHIImage* Irradiance = GetNamedImage(ENamedImage::SkyIrradiance))
            {
                CmdList.ClearImageFloat(Irradiance, AllSubresources, FColor::Black);
            }
            if (FRHIImage* Prefilter = GetNamedImage(ENamedImage::SkyPrefilter))
            {
                CmdList.ClearImageFloat(Prefilter, AllSubresources, FColor::Black);
            }
            return;
        }

        if (!bIBLDirty)
        {
            return;
        }


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

            FComputePipelineDesc PipelineDesc;
            PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
            PipelineDesc.CS = ComputeShader;
            PipelineDesc.DebugName = "Equirect To Cubemap";
            FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

            FComputeState State;
            State.SetPipeline(Pipeline);
            State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            State.Reads(EnvironmentMapImage);
            State.Writes(SkyCube);
            CmdList.SetComputeState(State);

            struct FEquirectPC { uint32 EquirectSRV; uint32 SkyCubeUAV; uint32 _Pad0; uint32 _Pad1; };
            FEquirectPC PC = {};
            PC.EquirectSRV = (uint32)EnvironmentMapImage->GetResourceID();
            PC.SkyCubeUAV  = (uint32)SkyCube->GetMipUAVIndex(0);
            PushRootConstants(CmdList, PC);

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

        // Cube written via bindless 2D-array UAV (6 layers); IBL passes sample it as a cube SRV.
        // Env params via transient device address.
        auto EnvAlloc = CmdList.CopyTransient(Frame.Volumetrics.EnvironmentParams);

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        PipelineDesc.CS = ComputeShader;
        PipelineDesc.DebugName = "Sky Cube Capture";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        State.Writes(SkyCube);
        CmdList.SetComputeState(State);

        struct FSkyCapturePC
        {
            uint64   EnvAddr;
            uint32   SkyCubeUAV;
            float    Time;
            FVector3 SunDirection;
            float    _Pad;
        } PC = {};
        PC.EnvAddr    = EnvAlloc.Gpu;
        PC.SkyCubeUAV = (uint32)SkyCube->GetMipUAVIndex(0);

        // Same source EnvironmentPass uses (SunDirection points FROM surface TO sun);
        // falls back to a daytime direction if no sun so the cube still has IBL structure.
        if (LightData.bHasSun)
        {
            PC.SunDirection = Math::Normalize(LightData.SunDirection);
        }
        else
        {
            PC.SunDirection = Math::Normalize(FVector3(0.3f, 0.8f, 0.4f));
        }
        // Star twinkle samples Time, but the cube only re-bakes on env/sun change so
        // it effectively freezes (stars blur to nothing in convolution anyway).
        PC.Time = SceneGlobalData.Time;
        PushRootConstants(CmdList, PC);

        constexpr uint32 SkyCaptureTile = 8u;
        const uint32 FaceSize = SkyCube->GetSizeX();
        const uint32 GroupsXY = RenderUtils::GetGroupCount(FaceSize, SkyCaptureTile);
        // Z = 6 layers, one per cube face -- each thread owns one (face, x, y).
        CmdList.Dispatch(GroupsXY, GroupsXY, 6u);
    }

    void FForwardRenderScene::IrradianceConvolutionPass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("Sky Irradiance Convolution", tracy::Color::SkyBlue1);

        const FFrameData& Frame = *RenderFrame;
        const bool bIBLConvolutionDirty = Frame.Volumetrics.bIBLConvolutionDirty;

        if (!RenderSettings.bHasEnvironment)
        {
            return;
        }

        // Skip the per-texel convolution when the source cube hasn't moved; the wide
        // diffuse lobe makes sub-degree sun deltas invisible in the persistent cube.
        if (!bIBLConvolutionDirty)
        {
            return;
        }
        
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

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        PipelineDesc.CS = ComputeShader;
        PipelineDesc.DebugName = "Sky Irradiance Convolution";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        // SkyCube read as a bindless cube SRV, irradiance written as a bindless 2D-array UAV;
        // declare both so the tracker transitions them.
        State.Reads(SkyCube);
        State.Writes(IrradianceCube);
        CmdList.SetComputeState(State);

        struct FIrradiancePC { uint32 SrcCubeSRV; uint32 OutCubeUAV; uint32 _Pad0; uint32 _Pad1; };
        FIrradiancePC PC = {};
        PC.SrcCubeSRV = (uint32)SkyCube->GetResourceID();
        PC.OutCubeUAV = (uint32)IrradianceCube->GetMipUAVIndex(0);
        PushRootConstants(CmdList, PC);

        constexpr uint32 IrradianceTile = 8u;
        const uint32 FaceSize = IrradianceCube->GetSizeX();
        const uint32 GroupsXY = RenderUtils::GetGroupCount(FaceSize, IrradianceTile);
        CmdList.Dispatch(GroupsXY, GroupsXY, 6u);
    }

    void FForwardRenderScene::PrefilterEnvMapPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const bool bIBLConvolutionDirty = Frame.Volumetrics.bIBLConvolutionDirty;

        if (!RenderSettings.bHasEnvironment)
        {
            return;
        }

        // Same dirty gate as irradiance: re-running 256 GGX samples per texel per mip
        // is wasted when the persistent prefilter cube's source hasn't moved enough.
        if (!bIBLConvolutionDirty)
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

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        PipelineDesc.CS = ComputeShader;
        PipelineDesc.DebugName = "Sky Prefilter Convolution";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        struct FPrefilterPC
        {
            uint32 SrcCubeSRV;
            uint32 OutMipUAV;
            float  Roughness;
            uint32 NumSamples;
        };

        const uint32 NumMips     = (uint32)PrefilterCube->GetDescription().NumMips;
        const uint32 BaseFaceSize = PrefilterCube->GetSizeX();

        constexpr uint32 PrefilterTile = 8u;

        // One dispatch per mip, each writing a bindless 2D-array UAV view of just that mip; roughness is
        // uniform across the dispatch, threaded in via push constant. SkyCube read as a bindless cube SRV.
        for (uint32 Mip = 0; Mip < NumMips; ++Mip)
        {
            FComputeState State;
            State.SetPipeline(Pipeline);
            State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            State.Reads(SkyCube);
            State.Writes(PrefilterCube);
            CmdList.SetComputeState(State);

            FPrefilterPC PC = {};
            PC.SrcCubeSRV = (uint32)SkyCube->GetResourceID();
            PC.OutMipUAV  = (uint32)PrefilterCube->GetMipUAVIndex(Mip);
            // Roughness even across mips (mip 0 mirror, last fully rough); matches
            // SamplePrefilter()'s runtime mip select.
            PC.Roughness  = (NumMips <= 1u) ? 0.0f
                                            : (float)Mip / (float)(NumMips - 1u);
            PC.NumSamples = GPrefilterSampleCount;
            PushRootConstants(CmdList, PC);

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

        FRHIImage* SkyCube = GetNamedImage(ENamedImage::SkyCube);
        FRHIImage* Equirect = RenderFrame->Volumetrics.EnvironmentMapImage
            ? RenderFrame->Volumetrics.EnvironmentMapImage
            : GetNamedImage(ENamedImage::BRDFLut);

        // SkyCube is filled by SkyCubeCapturePass; the Reads() below drives its UAV->SRV barrier.
        auto EnvAlloc = CmdList.CopyTransient(RenderFrame->Volumetrics.EnvironmentParams);

        FGraphicsPipelineDesc Desc;
        Desc.SetDebugName("Environment Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);
        Desc.SetVariableRateShadingState(MakeWorldShadingRate(RenderFrame->CachedWorldSettings));

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        // Cube + equirect read by bindless index, not descriptors; declare for the barrier tracker.
        GraphicsState.Reads(SkyCube);
        GraphicsState.Reads(Equirect);
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(SceneViewportState);

        struct FEnvPushConstants
        {
            uint64 EnvAddr;
            uint32 SkyCubeIndex;
            uint32 EquirectIndex;
            uint32 EquirectWidth;   // HDRI mode: drives the screen-derivative LOD that anti-aliases the sky.
            uint32 _Pad1;
        };
        static_assert(sizeof(FEnvPushConstants) == 24, "FEnvPushConstants must match the slang push block.");

        FEnvPushConstants PC = {};
        PC.EnvAddr       = EnvAlloc.Gpu;
        PC.SkyCubeIndex  = (uint32)SkyCube->GetResourceID();
        PC.EquirectIndex = (uint32)Equirect->GetResourceID();
        PC.EquirectWidth = Equirect->GetSizeX();

        CmdList.SetGraphicsState(GraphicsState);
        PushRootConstants(CmdList, PC);
        CmdList.Draw(3, 1, 0, 0);
    }

    namespace
    {
        // Mirrors FSimpleElementPass in SimpleElementVertex.slang: device address of the transient debug
        // vertex array, passed through the per-pass constants block (gPC.PassAddr).
        struct FSimpleElementPassData { uint64 Vertices = 0; };
    }

    void FForwardRenderScene::BatchedLineDraw(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& SimpleVertices     = Frame.Primitives.SimpleVertices;
        const auto& LineBatches        = Frame.Primitives.LineBatches;
        const auto& DrawCommands       = Frame.Geometry.DrawCommands;
        const auto& BillboardInstances = Frame.Primitives.BillboardInstances;

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

        FRasterState RasterState; RasterState
            .EnableDepthClip();

        // Two pipelines for the per-batch bDepthTest flag: depth-tested lines occlude (reversed-Z Greater +
        // depth write); X-ray lines draw on top, no depth write. A single pipeline ignored bDepthTest=false.
        auto BuildLinePipeline = [&](bool bDepthTest)
        {
            FDepthStencilState DepthState;
            if (bDepthTest)
            {
                DepthState.SetDepthFunc(EComparisonFunc::Greater).EnableDepthTest().EnableDepthWrite();
            }
            else
            {
                DepthState.DisableDepthTest().DisableDepthWrite();
            }

            FRenderState RenderState; RenderState
                .SetRasterState(RasterState)
                .SetDepthStencilState(DepthState);

            // No input layout: the VS pulls vertices from PassAddr by SV_VertexID.
            FGraphicsPipelineDesc Desc; Desc
                .SetDebugName(bDepthTest ? "Batched Line Draw (Depth)" : "Batched Line Draw (XRay)")
                .SetPrimType(EPrimitiveType::LineList)
                .SetRenderState(RenderState)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                .SetVertexShader(VertexShader)
                .SetPixelShader(PixelShader);

            return GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);
        };

        auto BuildLineState = [&](const FRHIGraphicsPipelineRef& Pipeline)
        {
            FGraphicsState GraphicsState; GraphicsState
                .SetRenderPass(RenderPass)
                .SetViewportState(SceneViewportState)
                .SetPipeline(Pipeline)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            return GraphicsState;
        };

        FGraphicsState DepthTestedState = BuildLineState(BuildLinePipeline(true));
        FGraphicsState XRayState        = BuildLineState(BuildLinePipeline(false));

        // Vertices live in the transient ring for this submission; the VS reads them by device address.
        const FSimpleElementPassData VertsPass
        { 
            CmdList.CopyTransientArray(SimpleVertices.data(), SimpleVertices.size()).Gpu 
        };

        // Re-bind only when the depth mode changes between consecutive batches.
        int CurrentDepthMode = -1;
        for (const FLineBatch& Batch : LineBatches)
        {
            const int DepthMode = Batch.bDepthTest ? 1 : 0;
            if (DepthMode != CurrentDepthMode)
            {
                CmdList.SetGraphicsState(Batch.bDepthTest ? DepthTestedState : XRayState);
                CurrentDepthMode = DepthMode;
            }
            CmdList.SetLineWidth(Batch.Thickness);
            PushRootConstants(CmdList, VertsPass);

            // Draw args built straight into the ring; DrawIndirect reads them by VkBuffer+offset, with no
            // persistent indirect buffer and no IndirectArgument transition. StartVertexLocation feeds
            // SV_VertexID so the VS indexes into the full vertex array.
            auto Args = CmdList.AllocTransient<FDrawIndirectArguments>();
            Args.Data->VertexCount           = Batch.VertexCount;
            Args.Data->InstanceCount          = 1;
            Args.Data->StartVertexLocation    = Batch.StartVertex;
            Args.Data->StartInstanceLocation  = 0;
            CmdList.DrawIndirect(Args.Buffer, Args.Offset, 1, sizeof(FDrawIndirectArguments));
        }
    }

    void FForwardRenderScene::BatchedTriangleDraw(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& SolidVertices = Frame.Primitives.SolidVertices;
        const auto& SolidBatches  = Frame.Primitives.SolidBatches;

        if (SolidVertices.empty() || SolidBatches.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Batched Triangle Draw", tracy::Color::Green2);

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("SimpleElementVertex.slang");
        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("SimpleElementPixel.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        FRenderPassDesc::FAttachment RenderTarget;
        RenderTarget.SetImage(GetNamedImage(ENamedImage::HDR)).SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc::FAttachment Depth; Depth
            .SetImage(GetNamedImage(ENamedImage::DepthAttachment))
            .SetLoadOp(ERenderLoadOp::Load);

        FRenderPassDesc RenderPass; RenderPass
            .AddColorAttachment(RenderTarget)
            .SetDepthAttachment(Depth)
            .SetRenderArea(GetNamedImage(ENamedImage::HDR)->GetExtent());

        // Two-sided so the surface reads from any angle.
        FRasterState RasterState; RasterState
            .EnableDepthClip()
            .SetCullNone();

        FBlendState::RenderTarget BlendTarget; BlendTarget
            .EnableBlend()
            .SetSrcBlend(EBlendFactor::SrcAlpha)
            .SetDestBlend(EBlendFactor::InvSrcAlpha)
            .SetSrcBlendAlpha(EBlendFactor::One)
            .SetDestBlendAlpha(EBlendFactor::InvSrcAlpha);
        FBlendState BlendState; BlendState.SetRenderTarget(0, BlendTarget);

        // Depth-tested batches read reversed-Z but never write depth, so the translucent
        // overlay is occluded by solid geometry without blocking later draws. XRay always draws on top.
        auto BuildPipeline = [&](bool bDepthTest)
        {
            FDepthStencilState DepthState;
            if (bDepthTest)
            {
                DepthState.SetDepthFunc(EComparisonFunc::Greater).EnableDepthTest().DisableDepthWrite();
            }
            else
            {
                DepthState.DisableDepthTest().DisableDepthWrite();
            }

            FRenderState RenderState; RenderState
                .SetRasterState(RasterState)
                .SetDepthStencilState(DepthState)
                .SetBlendState(BlendState);

            // No input layout: the VS pulls vertices from PassAddr by SV_VertexID.
            FGraphicsPipelineDesc Desc; Desc
                .SetDebugName(bDepthTest ? "Batched Triangle Draw (Depth)" : "Batched Triangle Draw (XRay)")
                .SetPrimType(EPrimitiveType::TriangleList)
                .SetRenderState(RenderState)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout())
                .SetVertexShader(VertexShader)
                .SetPixelShader(PixelShader);

            return GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);
        };

        auto BuildState = [&](const FRHIGraphicsPipelineRef& Pipeline)
        {
            FGraphicsState GraphicsState; GraphicsState
                .SetRenderPass(RenderPass)
                .SetViewportState(SceneViewportState)
                .SetPipeline(Pipeline)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            return GraphicsState;
        };

        FGraphicsState DepthTestedState = BuildState(BuildPipeline(true));
        FGraphicsState XRayState        = BuildState(BuildPipeline(false));

        const FSimpleElementPassData VertsPass{ CmdList.CopyTransientArray(SolidVertices.data(), SolidVertices.size()).Gpu };

        int CurrentDepthMode = -1;
        for (const FSolidBatch& Batch : SolidBatches)
        {
            const int DepthMode = Batch.bDepthTest ? 1 : 0;
            if (DepthMode != CurrentDepthMode)
            {
                CmdList.SetGraphicsState(Batch.bDepthTest ? DepthTestedState : XRayState);
                CurrentDepthMode = DepthMode;
            }
            PushRootConstants(CmdList, VertsPass);

            auto Args = CmdList.AllocTransient<FDrawIndirectArguments>();
            Args.Data->VertexCount            = Batch.VertexCount;
            Args.Data->InstanceCount          = 1;
            Args.Data->StartVertexLocation    = Batch.StartVertex;
            Args.Data->StartInstanceLocation  = 0;
            CmdList.DrawIndirect(Args.Buffer, Args.Offset, 1, sizeof(FDrawIndirectArguments));
        }
    }

    namespace
    {
        struct FColorGradingConstants
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

            FVector4 ColorFilter;

            // .a slots carry film-grain knobs (Shadows.a=Intensity, Midtones.a=Size,
            // Highlights.a=Response) -- avoids growing past Vulkan's 128B push guarantee.
            FVector4 Shadows;
            FVector4 Midtones;
            FVector4 Highlights;
            FVector4 VignetteColor;

            // .rgb = bloom tint, .a = chromatic aberration intensity.
            FVector4 BloomTint;

            float    AutoExposureKey;    // middle-grey key; <= 0 disables auto-exposure.
            float    AutoExposureMinMul; // 2^MinEV clamp on the adapted multiplier.
            float    AutoExposureMaxMul; // 2^MaxEV clamp on the adapted multiplier.
            float    _PadAE;
        };
        static_assert(sizeof(FColorGradingConstants) == 160, "FColorGradingConstants layout must match ColorGrading.slang::FColorGradingConstants.");
        
        FColorGradingConstants MakeDefaultColorGrading(float Time)
        {
            FColorGradingConstants PC{};
            PC.Exposure           = 1.0f;
            PC.Contrast           = 1.0f;
            PC.Saturation         = 1.0f;
            PC.Gamma              = 1.0f;
            PC.WhiteTemp          = 0.0f;
            PC.WhiteTint          = 0.0f;
            PC.VignetteIntensity  = 0.0f;
            PC.VignetteSmoothness = 0.5f;
            PC.VignetteRoundness  = 1.0f;
            PC.TonemapMode        = (uint32)EToneMapper::ACES;
            PC.Time               = Time;
            PC.BloomIntensity     = 0.0f;
            PC.ColorFilter        = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
            PC.Shadows            = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
            PC.Midtones           = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
            PC.Highlights         = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
            PC.VignetteColor      = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
            PC.BloomTint          = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
            PC.AutoExposureKey    = 0.0f;
            PC.AutoExposureMinMul = 0.0f;
            PC.AutoExposureMaxMul = 1.0f;
            return PC;
        }

        FColorGradingConstants BuildColorGradingConstants(const SPostProcessSettings* Settings, float Time)
        {
            if (Settings == nullptr || !Settings->bEnabled)
            {
                return MakeDefaultColorGrading(Time);
            }

            FColorGradingConstants PC{};
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
            PC.ColorFilter        = FVector4(Settings->ColorFilter, Settings->ColorFilterIntensity);
            PC.Shadows            = FVector4(Settings->Shadows,    Settings->FilmGrainIntensity);
            PC.Midtones           = FVector4(Settings->Midtones,   std::max(Settings->FilmGrainSize, 0.0001f));
            PC.Highlights         = FVector4(Settings->Highlights, Settings->FilmGrainResponse);
            PC.VignetteColor      = FVector4(Settings->VignetteColor, 0.0f);
            PC.BloomTint          = FVector4(Settings->BloomTint, Settings->ChromaticAberration);
            // 0.18 == photographic middle grey. Key <= 0 tells the shader to
            // ignore the adapted luminance and use the manual exposure alone.
            PC.AutoExposureKey    = Settings->bAutoExposure ? 0.18f : 0.0f;
            PC.AutoExposureMinMul = std::exp2(Settings->AutoExposureMinEV);
            PC.AutoExposureMaxMul = std::exp2(std::max(Settings->AutoExposureMaxEV, Settings->AutoExposureMinEV));
            return PC;
        }
    }

    namespace
    {
        constexpr uint32 BloomMaxMips = 5;

        // Push constants for BloomDownsampleSPD.slang. Bindless: HDRIndex picks the
        // source SRV, MipUAV[i] picks the per-mip UAV, no pass-local binding set.
        struct FBloomDownSPDPushConstants
        {
            FUIntVector2 PyramidSize;     // bloom mip 0 size (= HDR / 2)
            uint32     NumMips;
            uint32     HDRIndex;

            FVector2  InvHDRSize;
            float      Threshold;
            float      _Pad0;

            FVector3  KneeCurve;
            float      _Pad1;

            uint32     MipUAV[BloomMaxMips];
        };
        static_assert(sizeof(FBloomDownSPDPushConstants) == 68, "FBloomDownSPDPushConstants must match BloomDownsampleSPD.slang::FPushConstants.");

        // Push constants for BloomUpsampleCS.slang. SrcIndex is the all-mips
        // bindless SRV of BloomChain; SrcMip picks the level via SampleLevel.
        struct FBloomUpCSPushConstants
        {
            FVector2  SrcTexelSize;
            float      Radius;
            uint32     SrcIndex;

            FUIntVector2 DstSize;
            uint32     DstUAV;
            float      SrcMip;
        };
        static_assert(sizeof(FBloomUpCSPushConstants) == 32,
            "FBloomUpCSPushConstants must match BloomUpsampleCS.slang::FPushConstants.");

        // SPD tile in destination-mip-0 pixels. The 16x16 thread group writes
        // a 32x32 patch of mip 0 (each thread owns a 2x2 sub-quad).
        constexpr uint32 BloomSPDTileSize = 32;
    }

    void FForwardRenderScene::BloomPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const SPostProcessSettings* ActivePostProcess = Frame.PostProcess.bHasActivePostProcess ? &Frame.PostProcess.ActivePostProcessStorage : nullptr;

        if (ActivePostProcess == nullptr || !ActivePostProcess->bEnabled || ActivePostProcess->BloomIntensity <= 0.0f)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Bloom Pass", tracy::Color::Yellow3);

        FRHIComputeShaderRef DownCS = FShaderLibrary::GetComputeShader("BloomDownsampleSPD.slang");
        FRHIComputeShaderRef UpCS   = FShaderLibrary::GetComputeShader("BloomUpsampleCS.slang");
        if (!DownCS || !UpCS)
        {
            return;
        }

        FRHIImage* HDR        = GetNamedImage(ENamedImage::HDR);
        FRHIImage* Bloom      = BloomChain;
        const uint32 HDRWidth = HDR->GetSizeX();
        const uint32 HDRHght  = HDR->GetSizeY();
        const uint32 Mip0W    = eastl::max<uint32>(HDRWidth >> 1u, 1u);
        const uint32 Mip0H    = eastl::max<uint32>(HDRHght  >> 1u, 1u);

        const float Threshold = ActivePostProcess->BloomThreshold;
        const float Knee      = ActivePostProcess->BloomSoftKnee * Threshold + 1e-5f;

        {
            const FVector3 KneeCurve(Threshold - Knee, 2.0f * Knee, 0.25f / Knee);

            FComputePipelineDesc PipelineDesc;
            PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
            PipelineDesc.CS = DownCS;
            PipelineDesc.DebugName = "Bloom Downsample SPD";
            FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

            FComputeState State;
            State.SetPipeline(Pipeline);
            State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            State.Reads(HDR);
            State.Writes(Bloom);
            CmdList.SetComputeState(State);

            FBloomDownSPDPushConstants PC = {};
            PC.PyramidSize  = FUIntVector2(Mip0W, Mip0H);
            PC.NumMips      = BLOOM_MIP_COUNT;
            PC.HDRIndex     = (uint32)HDR->GetResourceID();
            PC.InvHDRSize   = FVector2(1.0f / (float)HDRWidth, 1.0f / (float)HDRHght);
            PC.Threshold    = Threshold;
            PC.KneeCurve    = KneeCurve;
            for (uint32 i = 0; i < BloomMaxMips; ++i)
            {
                const uint32 SrcMip = (i < BLOOM_MIP_COUNT) ? i : 0u;
                PC.MipUAV[i] = (uint32)Bloom->GetMipUAVIndex(SrcMip);
            }
            PushRootConstants(CmdList, PC);

            const uint32 GroupsX = RenderUtils::GetGroupCount(Mip0W, BloomSPDTileSize);
            const uint32 GroupsY = RenderUtils::GetGroupCount(Mip0H, BloomSPDTileSize);
            CmdList.Dispatch(GroupsX, GroupsY, 1);
        }

        FComputePipelineDesc UpPipelineDesc;
        UpPipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        UpPipelineDesc.CS = UpCS;
        UpPipelineDesc.DebugName = "Bloom Upsample CS";
        FRHIComputePipelineRef UpPipeline = GRenderContext->CreateComputePipeline(UpPipelineDesc);

        constexpr uint32 BloomUpTileSize = 8;
        for (uint32 i = BLOOM_MIP_COUNT - 1; i > 0; --i)
        {
            const uint32 SrcMip = i;
            const uint32 DstMip = i - 1;
            const uint32 SrcW   = eastl::max<uint32>(Mip0W >> SrcMip, 1u);
            const uint32 SrcH   = eastl::max<uint32>(Mip0H >> SrcMip, 1u);
            const uint32 DstW   = eastl::max<uint32>(Mip0W >> DstMip, 1u);
            const uint32 DstH   = eastl::max<uint32>(Mip0H >> DstMip, 1u);

            FComputeState State;
            State.SetPipeline(UpPipeline);
            State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            // Per-subresource: only SrcMip is sampled (ShaderResource), only DstMip
            // is written (UnorderedAccess). Other mips' states don't matter here.
            State.Reads(Bloom, FTextureSubresourceSet(SrcMip, 1, 0, 1));
            State.Writes(Bloom, FTextureSubresourceSet(DstMip, 1, 0, 1));
            CmdList.SetComputeState(State);

            FBloomUpCSPushConstants PC = {};
            PC.SrcTexelSize = FVector2(1.0f / (float)SrcW, 1.0f / (float)SrcH);
            PC.Radius       = 1.0f;
            PC.SrcIndex     = (uint32)Bloom->GetResourceID();
            PC.DstSize      = FUIntVector2(DstW, DstH);
            PC.DstUAV       = (uint32)Bloom->GetMipUAVIndex(DstMip);
            PC.SrcMip       = (float)SrcMip;
            PushRootConstants(CmdList, PC);

            const uint32 GroupsX = RenderUtils::GetGroupCount(DstW, BloomUpTileSize);
            const uint32 GroupsY = RenderUtils::GetGroupCount(DstH, BloomUpTileSize);
            CmdList.Dispatch(GroupsX, GroupsY, 1);
        }
    }

    namespace
    {
        // 16 B push block for AutoExposure.slang. Mirrors its FPushConstants.
        struct FAutoExposurePushConstants
        {
            uint32 HDRIndex;
            uint32 AdaptUAV;
            float  DeltaTime;
            float  AdaptationSpeed;
        };
        static_assert(sizeof(FAutoExposurePushConstants) == 16,
            "FAutoExposurePushConstants must match AutoExposure.slang::FPushConstants.");
    }

    void FForwardRenderScene::AutoExposurePass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const SPostProcessSettings* ActivePostProcess = Frame.PostProcess.bHasActivePostProcess ? &Frame.PostProcess.ActivePostProcessStorage : nullptr;

        // Skipped entirely when disabled; ColorGrading reads the persistent
        // AdaptedLuminance image but ignores it (AutoExposureKey <= 0).
        if (ActivePostProcess == nullptr || !ActivePostProcess->bEnabled || !ActivePostProcess->bAutoExposure)
        {
            return;
        }

        FRHIComputeShaderRef CS = FShaderLibrary::GetComputeShader("AutoExposure.slang");
        if (!CS)
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Auto Exposure Pass", tracy::Color::Orange3);

        FRHIImage* HDR     = GetNamedImage(ENamedImage::HDR);
        FRHIImage* Adapted = GetNamedImage(ENamedImage::AdaptedLuminance);

        FComputePipelineDesc PipelineDesc;
        PipelineDesc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        PipelineDesc.CS = CS;
        PipelineDesc.DebugName = "Auto Exposure";
        FRHIComputePipelineRef Pipeline = GRenderContext->CreateComputePipeline(PipelineDesc);

        FComputeState State;
        State.SetPipeline(Pipeline);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        State.Reads(HDR);
        State.Writes(Adapted);
        CmdList.SetComputeState(State);

        FAutoExposurePushConstants PC = {};
        PC.HDRIndex        = (uint32)HDR->GetResourceID();
        PC.AdaptUAV        = (uint32)Adapted->GetMipUAVIndex(0);
        PC.DeltaTime       = Frame.SceneGlobalData.DeltaTime;
        PC.AdaptationSpeed = ActivePostProcess->AutoExposureSpeed;
        PushRootConstants(CmdList, PC);

        CmdList.Dispatch(1, 1, 1);
    }

    void FForwardRenderScene::ToneMappingPass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("Color Grading + Tone Map Pass", tracy::Color::Red2);

        const FFrameData& Frame = *RenderFrame;
        const auto& CachedWorldSettings        = Frame.CachedWorldSettings;
        const auto& ActivePostProcessMaterials = Frame.PostProcess.ActivePostProcessMaterials;
        const SPostProcessSettings* ActivePostProcess = Frame.PostProcess.bHasActivePostProcess ? &Frame.PostProcess.ActivePostProcessStorage : nullptr;
        const auto& SceneGlobalData            = Frame.SceneGlobalData;

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("ColorGrading.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        // Render to an LDR intermediate when SMAA or a post-process chain is active
        // (both need to ping-pong before the final blit); otherwise straight to RT.
        const bool bSMAAEnabled = CachedWorldSettings.SMAAQuality != ESMAAQuality::Off;
        const bool bPPMaterials = !ActivePostProcessMaterials.empty();
        FRHIImage* OutputImage = (bSMAAEnabled || bPPMaterials) ? GetNamedImage(ENamedImage::LDR) : GetViewOutputTarget();

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

        FRHIImage* HDRTex     = GetNamedImage(ENamedImage::HDR);
        FRHIImage* BloomTex   = BloomChain;
        FRHIImage* AdaptedTex = GetNamedImage(ENamedImage::AdaptedLuminance);

        FGraphicsPipelineDesc Desc;
        Desc.SetDebugName("Tone Mapping Pass");
        Desc.SetRenderState(RenderState);
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FColorGradingConstants Constants = BuildColorGradingConstants(ActivePostProcess, SceneGlobalData.Time);
        auto ConstantsAlloc = CmdList.CopyTransient(Constants);

        FGraphicsState GraphicsState;
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        // Inputs read via bindless index, not descriptors; declare them for the barrier tracker.
        GraphicsState.Reads(HDRTex);
        GraphicsState.Reads(BloomTex);
        GraphicsState.Reads(AdaptedTex);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(MakeViewportStateFromImage(OutputImage));

        struct FComposePushConstants
        {
            uint64 ConstantsAddr;
            uint32 HDRIndex;
            uint32 BloomIndex;
            uint32 AdaptedLumIndex;
            uint32 _Pad;
        };
        static_assert(sizeof(FComposePushConstants) == 24, "FComposePushConstants must match the slang push block.");

        FComposePushConstants PC = {};
        PC.ConstantsAddr   = ConstantsAlloc.Gpu;
        PC.HDRIndex        = (uint32)HDRTex->GetResourceID();
        PC.BloomIndex      = (uint32)BloomTex->GetResourceID();
        PC.AdaptedLumIndex = (uint32)AdaptedTex->GetResourceID();

        CmdList.SetGraphicsState(GraphicsState);
        PushRootConstants(CmdList, PC);
        CmdList.Draw(3, 1, 0, 0);
    }

    namespace
    {
        // 16 B push block for the PostProcess material template.
        // Mirrors PostProcessPixelPass.slang::FPostProcessPushConstants.
        struct FPostProcessMaterialPushConstants
        {
            uint32 MaterialIndex;
            uint32 SceneColorIndex;   // bindless SRV: ping-pong source
            uint32 SceneDepthIndex;   // bindless SRV: opaque depth
            uint32 HDRIndex;          // bindless SRV: pre-tone-map HDR
        };
        static_assert(sizeof(FPostProcessMaterialPushConstants) == 16,
            "FPostProcessMaterialPushConstants must match the slang push block.");
    }

    void FForwardRenderScene::PostProcessMaterialPass(ICommandList& CmdList)
    {
        const FFrameData& Frame = *RenderFrame;
        const auto& CachedWorldSettings        = Frame.CachedWorldSettings;
        const auto& ActivePostProcessMaterials = Frame.PostProcess.ActivePostProcessMaterials;

        if (ActivePostProcessMaterials.empty())
        {
            return;
        }

        LUMINA_PROFILE_SECTION_COLORED("Post Process Material Pass", tracy::Color::Magenta);

        // Post-process inputs (chain source, opaque depth, pre-tonemap HDR) are read bindlessly via
        // push-constant indices -- no pass-local set. Depth's point-clamp vs the linear-clamp others
        // is selected by sampler index inside the shader.
        FRHIImage* DepthTex = GetNamedImage(ENamedImage::DepthAttachment);
        FRHIImage* HDRTex   = GetNamedImage(ENamedImage::HDR);

        FRasterState RasterState;
        RasterState.SetCullNone();

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest();
        DepthState.DisableDepthWrite();

        FRenderState RenderState;
        RenderState.SetRasterState(RasterState);
        RenderState.SetDepthStencilState(DepthState);

        const bool bSMAAEnabled = CachedWorldSettings.SMAAQuality != ESMAAQuality::Off;

        // Chain reads Source, writes Dest, swapping each pass. ToneMappingPass forced its
        // output into LDR when PP materials are present, so the first read is always LDR.
        FRHIImage* Source = GetNamedImage(ENamedImage::LDR);
        FRHIImage* Dest   = GetNamedImage(ENamedImage::PostProcessScratch);

        for (const FFrameData::FPostProcessMaterial& PPMaterial : ActivePostProcessMaterials)
        {
            // Resolved + ref-held at extract; the render thread never touches the CMaterial.
            FRHIVertexShader* VS = PPMaterial.Shaders.VertexShader;
            FRHIPixelShader*  PS = PPMaterial.Shaders.PixelShader;
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
            Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
            Desc.SetVertexShader(VS);
            Desc.SetPixelShader(PS);

            FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

            FGraphicsState GraphicsState;
            GraphicsState.SetPipeline(Pipeline);
            GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            // Inputs read by bindless index, not descriptors; declare them for the barrier tracker.
            GraphicsState.Reads(Source);
            GraphicsState.Reads(DepthTex);
            GraphicsState.Reads(HDRTex);
            GraphicsState.SetRenderPass(RenderPass);
            GraphicsState.SetViewportState(MakeViewportStateFromImage(Dest));

            FPostProcessMaterialPushConstants PC = {};
            // Interface's index (resolved at extract): instances own their own buffer slot where
            // parameter overrides live, so the parent's slot would ignore them.
            PC.MaterialIndex    = PPMaterial.MaterialIndex;
            PC.SceneColorIndex  = (uint32)Source->GetResourceID();
            PC.SceneDepthIndex  = (uint32)DepthTex->GetResourceID();
            PC.HDRIndex         = (uint32)HDRTex->GetResourceID();

            CmdList.SetGraphicsState(GraphicsState);
            PushRootConstants(CmdList, PC);
            CmdList.Draw(3, 1, 0, 0);

            eastl::swap(Source, Dest);
        }

        // Source holds the latest result; copy it where each consumer expects --
        // LDR for SMAA, the swapchain RT for the no-SMAA path.
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
            CmdList.CopyImage(Source, FTextureSlice(), GetViewOutputTarget(), FTextureSlice());
        }
    }

    struct FSMAAPushConstants
    {
        FVector4 RTMetrics;  // x = 1/w, y = 1/h, z = w, w = h
        float     EdgeThreshold;
        float     DebugMode;
        uint32    TexIndex0;  // bindless SRV index of the pass's primary input
        uint32    TexIndex1;  // pass-specific extra input (0 if unused)
        uint32    TexIndex2;  // pass-specific extra input (0 if unused)
        uint32    _Pad0;
        uint32    _Pad1;
        uint32    _Pad2;
    };
    static_assert(sizeof(FSMAAPushConstants) == 48, "FSMAAPushConstants must match the slang push block.");

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
        PC.RTMetrics      = FVector4(1.0f / W, 1.0f / H, W, H);
        PC.EdgeThreshold  = GetSMAAEdgeThreshold(Settings.SMAAQuality);
        PC.DebugMode      = 0.0f;
        PC.TexIndex0 = 0; PC.TexIndex1 = 0; PC.TexIndex2 = 0;
        PC._Pad0 = 0; PC._Pad1 = 0; PC._Pad2 = 0;
        return PC;
    }

    void FForwardRenderScene::SMAAEdgeDetectionPass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("SMAA Edge Detection", tracy::Color::Red2);

        const FFrameData& Frame = *RenderFrame;
        const auto& CachedWorldSettings = Frame.CachedWorldSettings;

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("SMAAEdgeDetection.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        FRHIImage* OutputImage = GetNamedImage(ENamedImage::SMAAEdges);
        FRHIImage* InputColor  = GetNamedImage(ENamedImage::LDR);

        FRenderPassDesc::FAttachment Attachment; Attachment
            .SetImage(OutputImage)
            .SetClearColor(FVector4(0.0f));

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
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        // Input read via bindless index, not a descriptor; declare it for the barrier tracker.
        GraphicsState.Reads(InputColor);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(MakeViewportStateFromImage(OutputImage));

        CmdList.SetGraphicsState(GraphicsState);

        FSMAAPushConstants PC = BuildSMAAPushConstants(OutputImage, CachedWorldSettings);
        PC.TexIndex0 = (uint32)InputColor->GetResourceID();
        PushRootConstants(CmdList, PC);
        CmdList.Draw(3, 1, 0, 0);
    }

    void FForwardRenderScene::SMAABlendWeightPass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("SMAA Blend Weight", tracy::Color::Red2);

        const FFrameData& Frame = *RenderFrame;
        const auto& CachedWorldSettings = Frame.CachedWorldSettings;

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("SMAABlendWeight.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        FRHIImage* OutputImage = GetNamedImage(ENamedImage::SMAABlend);
        FRHIImage* EdgesTex    = GetNamedImage(ENamedImage::SMAAEdges);
        FRHIImage* AreaTex     = GetNamedImage(ENamedImage::SMAAArea);
        FRHIImage* SearchTex   = GetNamedImage(ENamedImage::SMAASearch);

        FRenderPassDesc::FAttachment Attachment; Attachment
            .SetImage(OutputImage)
            .SetClearColor(FVector4(0.0f));

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
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        // Inputs read via bindless index, not descriptors; declare them for the barrier tracker.
        // (The area/search LUTs are permanently in ShaderResource, so Reads is a no-op for them.)
        GraphicsState.Reads(EdgesTex);
        GraphicsState.Reads(AreaTex);
        GraphicsState.Reads(SearchTex);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(MakeViewportStateFromImage(OutputImage));

        CmdList.SetGraphicsState(GraphicsState);

        FSMAAPushConstants PC = BuildSMAAPushConstants(OutputImage, CachedWorldSettings);
        PC.TexIndex0 = (uint32)EdgesTex->GetResourceID();
        PC.TexIndex1 = (uint32)AreaTex->GetResourceID();
        PC.TexIndex2 = (uint32)SearchTex->GetResourceID();
        PushRootConstants(CmdList, PC);
        CmdList.Draw(3, 1, 0, 0);
    }

    void FForwardRenderScene::SMAANeighborhoodBlendPass(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SECTION_COLORED("SMAA Neighborhood Blend", tracy::Color::Red2);

        const FFrameData& Frame = *RenderFrame;
        const auto& CachedWorldSettings = Frame.CachedWorldSettings;

        FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("FullscreenQuad.slang");
        FRHIPixelShaderRef PixelShader = FShaderLibrary::GetPixelShader("SMAANeighborhoodBlend.slang");
        if (!VertexShader || !PixelShader)
        {
            return;
        }

        FRHIImage* OutputImage = GetViewOutputTarget();
        FRHIImage* InputColor  = GetNamedImage(ENamedImage::LDR);
        FRHIImage* BlendTex    = GetNamedImage(ENamedImage::SMAABlend);

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
        Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
        Desc.SetVertexShader(VertexShader);
        Desc.SetPixelShader(PixelShader);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);

        FGraphicsState GraphicsState;
        GraphicsState.SetPipeline(Pipeline);
        GraphicsState.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        // Inputs read via bindless index, not descriptors; declare them for the barrier tracker.
        GraphicsState.Reads(InputColor);
        GraphicsState.Reads(BlendTex);
        GraphicsState.SetRenderPass(RenderPass);
        GraphicsState.SetViewportState(MakeViewportStateFromImage(OutputImage));

        CmdList.SetGraphicsState(GraphicsState);

        FSMAAPushConstants PC = BuildSMAAPushConstants(OutputImage, CachedWorldSettings);
        PC.TexIndex0 = (uint32)InputColor->GetResourceID();
        PC.TexIndex1 = (uint32)BlendTex->GetResourceID();
        PushRootConstants(CmdList, PC);
        CmdList.Draw(3, 1, 0, 0);
    }
    
    void FForwardRenderScene::InitBuffers()
    {
        // Cluster grid is per-view (created in AddSceneView). All CPU-dynamic scene data (instances,
        // bones, lights, billboards, widgets, cull views, skin descriptors, env/fog params, meshlet
        // prefix) is uploaded to the command-list transient ring each frame -- no persistent buffer.
        // What remains persistent: GPU-written rings + pre-skinned vertices. Debug line/triangle geometry
        // is ring-allocated at its draw site and pulled by device address (see BatchedLineDraw).

        {
            // GPU pre-skinning output: written by Skinning.slang (UAV), read by every draw
            // VS (SRV). Device-local (not Dynamic) -- the GPU produces it, no CPU upload.
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FPreSkinnedVertex) * 64 * 1024;
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::ShaderResource;
            BufferDesc.DebugName = "Pre-Skinned Vertices";
            PreSkinnedVerticesBuffer = GRenderContext->CreateBuffer(BufferDesc);
        }

        // Unified meshlet draw list (NumViews * TotalMeshletBound); CullMeshlets appends
        // surviving meshlets into each view's slice via FCullView.DrawListOffset.
        for (uint32 Slot = 0; Slot < FRAMES_IN_FLIGHT; ++Slot)
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(uint32) * 2;
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = FString("Meshlet Draw List [") + eastl::to_string(Slot) + "]";
            MeshletDrawListRing[Slot] = GRenderContext->CreateBuffer(BufferDesc);
        }

        // Unified indirect draw args (NumViews * NumDraws), manually ringed: it's both
        // GPU-atomic-written and DrawIndirect-consumed, incompatible with BUF_Dynamic.
        for (uint32 Slot = 0; Slot < FRAMES_IN_FLIGHT; ++Slot)
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(FDrawIndirectArguments);
            BufferDesc.Stride = sizeof(FDrawIndirectArguments);
            BufferDesc.Usage.SetMultipleFlags(BUF_Indirect, BUF_StorageBuffer);
            BufferDesc.InitialState = EResourceStates::IndirectArgument;
            BufferDesc.bKeepInitialState = true;
            BufferDesc.DebugName = FString("Indirect Args [") + eastl::to_string(Slot) + "]";
            IndirectArgsRing[Slot] = GRenderContext->CreateBuffer(BufferDesc);
        }

        // Two-pass cull defer list: phase 0 appends prev-frame-HZB rejects, phase 1
        // re-tests them. Stride matches FMeshletDeferred (4x uint32).
        for (uint32 Slot = 0; Slot < FRAMES_IN_FLIGHT; ++Slot)
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(uint32) * 4;
            BufferDesc.Stride = sizeof(uint32) * 4;
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = FString("Meshlet Defer List [") + eastl::to_string(Slot) + "]";
            MeshletDeferListRing[Slot] = GRenderContext->CreateBuffer(BufferDesc);
        }

        // Atomic counter paired with MeshletDeferList. ResetPass zeroes it
        // every frame via FillBuffer before phase 0 runs.
        for (uint32 Slot = 0; Slot < FRAMES_IN_FLIGHT; ++Slot)
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(uint32);
            BufferDesc.Stride = sizeof(uint32);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = FString("Meshlet Defer Count [") + eastl::to_string(Slot) + "]";
            DeferCountRing[Slot] = GRenderContext->CreateBuffer(BufferDesc);
        }

        // SPD hand-off counter: phase 1 (per-tile mips 0..5) to phase 2 (last workgroup,
        // mips 6..11). Zeroed before each dispatch; phase 2 resets it so it stays zero.
        for (uint32 Slot = 0; Slot < FRAMES_IN_FLIGHT; ++Slot)
        {
            FRHIBufferDesc BufferDesc;
            BufferDesc.Size = sizeof(uint32);
            BufferDesc.Stride = sizeof(uint32);
            BufferDesc.Usage.SetFlag(BUF_StorageBuffer);
            BufferDesc.bKeepInitialState = true;
            BufferDesc.InitialState = EResourceStates::UnorderedAccess;
            BufferDesc.DebugName = FString("SPD Counter [") + eastl::to_string(Slot) + "]";
            SpdCounterRing[Slot] = GRenderContext->CreateBuffer(BufferDesc);
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

    void FForwardRenderScene::AllocateMSAAImages(FSceneView& View, const FUIntVector2& Extent)
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
            View.Images[(int)ENamedImage::HDR_MS] = GRenderContext->CreateImage(ImageDesc);
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
            View.Images[(int)ENamedImage::Depth_MS] = GRenderContext->CreateImage(ImageDesc);
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
            View.Images[(int)ENamedImage::Picker_MS] = GRenderContext->CreateImage(ImageDesc);
        }
    }

    void FForwardRenderScene::SyncMSAAState()
    {
        if (RenderFrame == nullptr)
        {
            return;
        }
        const uint8 Desired = ::Lumina::GetMSAASampleCount(RenderFrame->CachedWorldSettings.MSAASampleCount);

        if (Desired == MSAASampleCount)
        {
            return;
        }

        MSAASampleCount = Desired;

        // MS scratch is per-view; reallocate (or drop) it for every view + rebuild that
        // view's binding sets so geometry passes pick up the new sample count.
        for (FSceneView& View : SceneViews)
        {
            View.Images[(int)ENamedImage::HDR_MS]    = nullptr;
            View.Images[(int)ENamedImage::Depth_MS]  = nullptr;
            View.Images[(int)ENamedImage::Picker_MS] = nullptr;

            if (MSAASampleCount > 1)
            {
                AllocateMSAAImages(View, View.Size);
            }
        }
    }

    void FForwardRenderScene::InitViewImages(FSceneView& View)
    {
        const FUIntVector2 Extent = View.Size;

        // Seed with the scene's shared images (BRDF LUT, sky cubes, SMAA LUTs, cascade atlas, icons) so
        // GetNamedImage() reads them uniformly through CurrentView; the per-view slots below override.
        View.Images = NamedImages;

        {
            FRHIImageDesc ImageDesc = View.Viewport->GetRenderTarget()->GetDescription();
            ImageDesc.Format = EFormat::RGBA16_FLOAT;
            ImageDesc.DebugName = "HDR";
            View.Images[(int)ENamedImage::HDR] = GRenderContext->CreateImage(ImageDesc);
        }

        {
            FRHIImageDesc ImageDesc = View.Viewport->GetRenderTarget()->GetDescription();
            ImageDesc.DebugName = "LDR";
            View.Images[(int)ENamedImage::LDR] = GRenderContext->CreateImage(ImageDesc);
        }

        // Ping-pong scratch for the post-process material chain.
        {
            FRHIImageDesc ImageDesc = View.Viewport->GetRenderTarget()->GetDescription();
            ImageDesc.DebugName = "PostProcessScratch";
            View.Images[(int)ENamedImage::PostProcessScratch] = GRenderContext->CreateImage(ImageDesc);
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
            View.Images[(int)ENamedImage::SMAAEdges] = GRenderContext->CreateImage(ImageDesc);
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
            View.Images[(int)ENamedImage::SMAABlend] = GRenderContext->CreateImage(ImageDesc);
        }

        {
            // Single-channel AO factor; SSAOPass renders into it, the base pass samples it.
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = Extent / 2;
            ImageDesc.Format            = EFormat::R8_UNORM;
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.InitialState      = EResourceStates::RenderTarget;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName         = "SSAO";
            View.Images[(int)ENamedImage::SSAO] = GRenderContext->CreateImage(ImageDesc);

            // Blurred AO (box blur of SSAO over the noise tile); this is what the base pass samples.
            ImageDesc.DebugName         = "SSAO Blur";
            View.Images[(int)ENamedImage::SSAOBlur] = GRenderContext->CreateImage(ImageDesc);
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

            View.Images[(int)ENamedImage::DepthAttachment] = GRenderContext->CreateImage(ImageDesc);
        }

        {
            uint32 Width = PreviousPow2(Extent.x);
            uint32 Height = PreviousPow2(Extent.y);

            // R16_FLOAT HZB: reverse-Z [0,1], min-reduced; quantization error is conservative.
            FRHIImageDesc ImageDesc;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage);
            ImageDesc.Extent            = FUIntVector2(Width, Height);
            ImageDesc.Format            = EFormat::R16_FLOAT;
            ImageDesc.NumMips           = (uint8)RenderUtils::CalculateMipCount(Width, Height);
            ImageDesc.InitialState      = EResourceStates::ShaderResource;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.DebugName         = "Depth Pyramid";

            View.Images[(int)ENamedImage::DepthPyramid] = GRenderContext->CreateImage(ImageDesc);
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

            View.Images[(int)ENamedImage::Picker] = GRenderContext->CreateImage(ImageDesc);
        }

        AllocateMSAAImages(View, Extent);

        {
            FRHIImageDesc ImageDesc = {};
            ImageDesc.Extent = Extent;
            ImageDesc.Format = EFormat::RGBA16_FLOAT;
            ImageDesc.Dimension = EImageDimension::Texture2D;
            ImageDesc.InitialState = EResourceStates::RenderTarget;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName = "Accum";

            View.Images[(int)ENamedImage::Accum] = GRenderContext->CreateImage(ImageDesc);
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

            View.Images[(int)ENamedImage::Revealage] = GRenderContext->CreateImage(ImageDesc);
        }

        // Scene-color copy for the water + underwater passes (HDR is blitted here, then sampled for
        // refraction / SSR / distortion so those passes never read the HDR target they also write).
        {
            FRHIImageDesc ImageDesc = {};
            ImageDesc.Extent = Extent;
            ImageDesc.Format = EFormat::RGBA16_FLOAT;
            ImageDesc.Dimension = EImageDimension::Texture2D;
            ImageDesc.InitialState = EResourceStates::ShaderResource;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);
            ImageDesc.DebugName = "Water Refraction";

            View.Images[(int)ENamedImage::WaterRefraction] = GRenderContext->CreateImage(ImageDesc);
        }

        // DBuffer decal targets: BaseColor / WorldNormal / Roughness-Metallic-AO, each with transmittance
        // in alpha. RGBA8_UNORM; written by DecalPass, sampled by the base pass.
        {
            const ENamedImage DBufferSlots[3] = { ENamedImage::DBufferA, ENamedImage::DBufferB, ENamedImage::DBufferC };
            const char* DBufferNames[3]       = { "DBufferA", "DBufferB", "DBufferC" };
            for (int i = 0; i < 3; ++i)
            {
                FRHIImageDesc ImageDesc = {};
                ImageDesc.Extent = Extent;
                ImageDesc.Format = EFormat::RGBA8_UNORM;
                ImageDesc.Dimension = EImageDimension::Texture2D;
                ImageDesc.InitialState = EResourceStates::RenderTarget;
                ImageDesc.bKeepInitialState = true;
                ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);
                ImageDesc.DebugName = DBufferNames[i];
                View.Images[(int)DBufferSlots[i]] = GRenderContext->CreateImage(ImageDesc);
            }
        }

        {
            // Froxel fog volumes: fixed 3D grid (swapchain-independent). RGBA16F = (in-scatter, a) where a is
            // extinction (Scatter) or transmittance (Integrated). Storage for the UAVs, ShaderResource to sample.
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = FUIntVector2(GFroxelGridX, GFroxelGridY);
            ImageDesc.Depth             = (uint16)GFroxelGridZ;
            ImageDesc.Format            = EFormat::RGBA16_FLOAT;
            ImageDesc.Dimension         = EImageDimension::Texture3D;
            ImageDesc.NumMips           = 1;
            ImageDesc.InitialState      = EResourceStates::ShaderResource;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::Storage, EImageCreateFlags::ShaderResource);

            ImageDesc.DebugName = "Froxel Scatter";
            View.Images[(int)ENamedImage::FroxelScatter] = GRenderContext->CreateImage(ImageDesc);

            ImageDesc.DebugName = "Froxel Integrated";
            View.Images[(int)ENamedImage::FroxelIntegrated] = GRenderContext->CreateImage(ImageDesc);
        }

        {
            // Bloom mip chain (half-res, R11G11B10_FLOAT). SPD writes mips 0..N-1 from
            // HDR in one dispatch, then per-mip upsamples accumulate into mip[i-1].
            const uint32 BloomW = eastl::max<uint32>(Extent.x / 2u, 1u);
            const uint32 BloomH = eastl::max<uint32>(Extent.y / 2u, 1u);

            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = FUIntVector2(BloomW, BloomH);
            ImageDesc.Format            = EFormat::R11G11B10_FLOAT;
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.NumMips           = (uint8)BLOOM_MIP_COUNT;
            ImageDesc.InitialState      = EResourceStates::ShaderResource;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage);
            ImageDesc.DebugName         = "Bloom Chain";

            View.BloomChainImage = GRenderContext->CreateImage(ImageDesc);
        }

        {
            // Auto-exposure adapted luminance: 1x1 persistent R32F carrying eye-adaptation across frames.
            // Kept ShaderResource so ColorGrading can read it even when auto-exposure is disabled.
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = FUIntVector2(1, 1);
            ImageDesc.Format            = EFormat::R32_FLOAT;
            ImageDesc.Dimension         = EImageDimension::Texture2D;
            ImageDesc.NumMips           = 1;
            ImageDesc.InitialState      = EResourceStates::ShaderResource;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage);
            ImageDesc.DebugName         = "Adapted Luminance";

            View.Images[(int)ENamedImage::AdaptedLuminance] = GRenderContext->CreateImage(ImageDesc);
        }
    }

    FRHIImageRef FForwardRenderScene::BakeBRDFLUT()
    {
        constexpr uint32 BRDFLutSize = 256u;

        FRHIImageDesc ImageDesc;
        ImageDesc.Extent            = FUIntVector2(BRDFLutSize, BRDFLutSize);
        ImageDesc.Format            = EFormat::RG16_FLOAT;
        ImageDesc.Dimension         = EImageDimension::Texture2D;
        ImageDesc.NumMips           = 1;
        ImageDesc.InitialState      = EResourceStates::ShaderResource;
        ImageDesc.bKeepInitialState = true;
        ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage);
        ImageDesc.DebugName         = "BRDF LUT";

        FRHIImageRef BRDFLut = GRenderContext->CreateImage(ImageDesc);

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

        constexpr uint32 BRDFLutTile = 8u;
        const uint32 GroupsX = RenderUtils::GetGroupCount(BRDFLutSize, BRDFLutTile);
        const uint32 GroupsY = RenderUtils::GetGroupCount(BRDFLutSize, BRDFLutTile);
        CmdList->Dispatch(GroupsX, GroupsY, 1);

        CmdList->Close();
        GRenderContext->ExecuteCommandList(CmdList);

        return BRDFLut;
    }

    void FForwardRenderScene::InitSkyCube(uint32 FaceSize)
    {
        // Face size drives the IBL source resolution and (in HDRI mode) the angular detail the
        // visible sky reflects. Bilinear filtering still supplies per-pixel sky detail, so the cube
        // need not match screen size. Set by the active environment's IBLQuality tier.
        FRHIImageDesc ImageDesc;
        ImageDesc.Extent            = FUIntVector2(FaceSize, FaceSize);
        ImageDesc.Format            = EFormat::R11G11B10_FLOAT;
        ImageDesc.Dimension         = EImageDimension::TextureCube;
        ImageDesc.ArraySize         = 6;
        ImageDesc.NumMips           = 1;
        ImageDesc.InitialState      = EResourceStates::ShaderResource;
        ImageDesc.bKeepInitialState = true;
        ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage, EImageCreateFlags::CubeCompatible);
        ImageDesc.DebugName         = "Sky Cube";

        NamedImages[(int)ENamedImage::SkyCube] = GRenderContext->CreateImage(ImageDesc);
    }

    void FForwardRenderScene::InitIBLConvolutionTargets(const FIBLBakeResolution& Resolution)
    {
        {
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = FUIntVector2(Resolution.Irradiance, Resolution.Irradiance);
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

        // Pre-filtered specular: roughness spread evenly across mips. Smallest mip = fully rough;
        // the roughness=1 GGX lobe is wide enough that a tiny face suffices.
        {
            FRHIImageDesc ImageDesc;
            ImageDesc.Extent            = FUIntVector2(Resolution.Prefilter, Resolution.Prefilter);
            ImageDesc.Format            = EFormat::R11G11B10_FLOAT;
            ImageDesc.Dimension         = EImageDimension::TextureCube;
            ImageDesc.ArraySize         = 6;
            ImageDesc.NumMips           = (uint8)Resolution.Mips;
            ImageDesc.InitialState      = EResourceStates::ShaderResource;
            ImageDesc.bKeepInitialState = true;
            ImageDesc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource, EImageCreateFlags::Storage, EImageCreateFlags::CubeCompatible);
            ImageDesc.DebugName         = "Sky Prefilter";

            NamedImages[(int)ENamedImage::SkyPrefilter] = GRenderContext->CreateImage(ImageDesc);
        }
    }

    void FForwardRenderScene::SyncIBLResolution(const FIBLBakeResolution& Resolution)
    {
        if (Resolution == AppliedIBLResolution)
        {
            return;
        }

        // Rare (editor-driven quality change). Drain the GPU so no in-flight frame still reads the old
        // cubes through their bindless slots, then recreate them. The bake passes read sizes dynamically
        // (GetSizeX / NumMips), so they adapt with no further changes.
        GRenderContext->WaitIdle();

        InitSkyCube(Resolution.SkyCube);
        InitIBLConvolutionTargets(Resolution);

        // Views snapshot the shared images (InitViewImages: View.Images = NamedImages); repoint the three
        // IBL slots in every view so GetNamedImage / BuildViewSceneRoot pick up the new cubes.
        for (FSceneView& View : SceneViews)
        {
            View.Images[(int)ENamedImage::SkyCube]      = NamedImages[(int)ENamedImage::SkyCube];
            View.Images[(int)ENamedImage::SkyIrradiance] = NamedImages[(int)ENamedImage::SkyIrradiance];
            View.Images[(int)ENamedImage::SkyPrefilter]  = NamedImages[(int)ENamedImage::SkyPrefilter];
        }

        // The freshly-sized cubes have undefined contents, but the game thread set bIBLDirty +
        // bIBLConvolutionDirty on this same resolution change (bResChanged), so the bake refills them
        // this frame. Don't touch bIBL*Valid here -- those are game-thread-owned (avoids a data race).
        AppliedIBLResolution = Resolution;
    }

    void FForwardRenderScene::InitFrameResources()
    {
        // Swapchain resize: rebuild the primary view's images + viewport state + binding sets.
        // The per-view Scene/Cluster buffers are size-independent and persist across resize.
        FSceneView& Primary = SceneViews[0];
        Primary.Size = Primary.Viewport->GetRenderTarget()->GetExtent();
        InitViewImages(Primary);
        Primary.ViewportState = MakeViewportStateFromImage(Primary.Images[(int)ENamedImage::HDR]);
        CreateViewBindingSets(Primary);
    }

    template<typename T>
    void FForwardRenderScene::PushRootConstants(ICommandList& CmdList, const T& PassData)
    {
        FRootConstants RC;
        RC.RootAddr = CurrentSceneRootAddr;
        RC.PassAddr = CmdList.CopyTransient(PassData).Gpu;
        CmdList.SetPushConstants(&RC, sizeof(RC));
    }

    void FForwardRenderScene::PushRootConstants(ICommandList& CmdList)
    {
        FRootConstants RC;
        RC.RootAddr = CurrentSceneRootAddr;
        RC.PassAddr = 0;
        CmdList.SetPushConstants(&RC, sizeof(RC));
    }

    uint64 FForwardRenderScene::BuildViewSceneRoot(ICommandList& CmdList, FSceneView& View, uint64 SceneDataAddr)
    {
        auto Root = CmdList.AllocTransient<FSceneRoot>();
        *Root = SceneRootShared;
        Root->SceneData          = SceneDataAddr;
        Root->Clusters           = View.ClusterBuffer->GetAddress();
        Root->BRDFLutIndex       = (uint32)View.Images[(int)ENamedImage::BRDFLut]->GetResourceID();
        Root->SkyIrradianceIndex = (uint32)View.Images[(int)ENamedImage::SkyIrradiance]->GetResourceID();
        {
            FRHIImage* Prefilter = View.Images[(int)ENamedImage::SkyPrefilter];
            uint32 PrefilterID   = (uint32)Prefilter->GetResourceID();
            Root->SkyPrefilterIndex = (PrefilterID & 0x00FFFFFFu) | ((uint32)Prefilter->GetNumMips() << 24);
        }
        Root->SkyCubeIndex       = (uint32)View.Images[(int)ENamedImage::SkyCube]->GetResourceID();
        Root->ShadowCascadeIndex = (uint32)GetNamedImage(ENamedImage::Cascade)->GetResourceID();
        Root->ShadowAtlasIndex   = (uint32)ShadowAtlas.GetImage()->GetResourceID();
        return Root.Gpu;
    }

    void FForwardRenderScene::CreateViewBindingSets(FSceneView& View)
    {
        // Scene data reaches shaders by device address (FSceneRoot) + the bindless texture table, so a
        // view owns no descriptor sets. Only refresh the live per-view render aliases here.
        BloomChain         = View.BloomChainImage;
        SceneViewportState = View.ViewportState;
        SceneViewport      = View.Viewport.GetReference();
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
        // Editor-facing: always the primary view's RT, independent of the live SceneViewport
        // (which tracks CurrentView during RenderView and would otherwise leak a capture's RT).
        if (SceneViews.empty() || !SceneViews[0].Viewport)
        {
            return nullptr;
        }
        return SceneViews[0].Viewport->GetRenderTarget();
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
        // Pick the newest complete slot whose window covers (X,Y). A slot is safe to map
        // without a semaphore once it's >= FRAMES_IN_FLIGHT older than the latest issue.
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
            if (X < Slot.OriginX || X >= Slot.OriginX + Slot.Width ||
                Y < Slot.OriginY || Y >= Slot.OriginY + Slot.Height)
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
            // No completed slot covers this pixel (startup/resize, early click, or fast
            // cursor motion); caller treats this as "no entity under cursor".
            return entt::null;
        }

        const FPickerReadbackSlot& Slot = PickerReadbackRing[BestSlotIdx];
        const uint32 LocalX = X - Slot.OriginX;
        const uint32 LocalY = Y - Slot.OriginY;

        size_t RowPitch = 0;
        void* MappedMemory = GRenderContext->MapStagingTexture(Slot.Staging, FTextureSlice(), ERHIAccess::HostRead, &RowPitch);
        if (!MappedMemory)
        {
            return entt::null;
        }

        uint8* RowStart = static_cast<uint8*>(MappedMemory) + LocalY * RowPitch;
        uint32* PixelPtr = reinterpret_cast<uint32*>(RowStart) + LocalX;
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
    void FForwardRenderScene::SetPickerCursor(uint32 X, uint32 Y, bool bOverViewport)
    {
        const uint64 Packed = (bOverViewport ? 1ull : 0ull)
                            | ((uint64(X) & 0x1FFFFF) << 1)
                            | ((uint64(Y) & 0x1FFFFF) << 22);
        PickerCursorPacked.store(Packed, std::memory_order_relaxed);
    }

    void FForwardRenderScene::IssuePickerReadback(ICommandList& CmdList)
    {
        const uint64 Packed = PickerCursorPacked.load(std::memory_order_relaxed);
        const bool bOverViewport = (Packed & 1ull) != 0;
        if (!bOverViewport)
        {
            // Cursor isn't over the viewport: no pick can happen this frame, so skip
            // the copy (and its layout transition) entirely.
            return;
        }

        FRHIImage* PickerImage = GetNamedImage(ENamedImage::Picker);
        if (!PickerImage)
        {
            return;
        }

        const uint32 ImgW = PickerImage->GetDescription().Extent.x;
        const uint32 ImgH = PickerImage->GetDescription().Extent.y;
        if (ImgW == 0 || ImgH == 0)
        {
            return;
        }

        const uint32 CursorX = Math::Min((uint32)((Packed >> 1) & 0x1FFFFF), ImgW - 1);
        const uint32 CursorY = Math::Min((uint32)((Packed >> 22) & 0x1FFFFF), ImgH - 1);

        // Copy a small window around the cursor, not the whole RT; clamp it inside the
        // image so the region size is fixed and the cursor pixel stays inside.
        const uint32 RegionW = Math::Min(PickerRegionExtent, ImgW);
        const uint32 RegionH = Math::Min(PickerRegionExtent, ImgH);
        const uint32 OriginX = Math::Min(CursorX - Math::Min(CursorX, RegionW / 2), ImgW - RegionW);
        const uint32 OriginY = Math::Min(CursorY - Math::Min(CursorY, RegionH / 2), ImgH - RegionH);

        FPickerReadbackSlot& Slot = PickerReadbackRing[PickerReadbackWriteIndex];

        if (!Slot.Staging || Slot.Width != RegionW || Slot.Height != RegionH)
        {
            // First use of this slot, or region size changed (post-resize). Allocate a
            // staging image sized to the region; bPending stays false until the copy below.
            FRHIImageDesc StagingDesc = PickerImage->GetDescription();
            StagingDesc.Extent = FUIntVector2(RegionW, RegionH);
            Slot.Staging = GRenderContext->CreateStagingImage(StagingDesc, ERHIAccess::HostRead);
            Slot.Width = RegionW;
            Slot.Height = RegionH;
        }

        FTextureSlice SrcSlice;
        SrcSlice.X = OriginX;
        SrcSlice.Y = OriginY;
        SrcSlice.Width = RegionW;
        SrcSlice.Height = RegionH;
        SrcSlice.Depth = 1;

        CmdList.CopyImage(PickerImage, SrcSlice, Slot.Staging, FTextureSlice());

        Slot.OriginX = OriginX;
        Slot.OriginY = OriginY;
        Slot.SubmittedFrame = PickerReadbackFrame;
        Slot.bPending = true;

        ++PickerReadbackFrame;
        PickerReadbackWriteIndex = (PickerReadbackWriteIndex + 1) % PickerReadbackRingSize;
    }
    #endif
}
