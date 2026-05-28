#include "ThumbnailManager.h"
#include "ThumbnailScene.h"
#include "Assets/AssetTypes/Material/Material.h"
#include "Assets/AssetTypes/Material/MaterialInstance.h"
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Assets/AssetTypes/ParticleSystem/ParticleSystem.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Class.h"
#include "Core/Object/Package/Package.h"
#include "Core/Object/Package/Thumbnail/PackageThumbnail.h"
#include "Paths/Paths.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RHIGlobals.h"

#include "TaskSystem/TaskSystem.h"
#include "Tools/PrimitiveManager/PrimitiveManager.h"
#include "World/Entity/Components/EnvironmentComponent.h"
#include "World/Entity/Components/SkyLightComponent.h"
#include "World/Entity/Components/LightComponent.h"
#include "World/Entity/Components/ParticleSystemComponent.h"
#include "World/Entity/Components/PostProcessComponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Scene/RenderScene/SceneMeshes.h"


namespace Lumina
{

    static CThumbnailManager* ThumbnailManagerSingleton = nullptr;

    CThumbnailManager::CThumbnailManager()
    {
    }

    void CThumbnailManager::Initialize()
    {
        (void)CPackage::OnPackageDestroyed.AddMember(this, &ThisClass::OnPackageDestroyed);

        constexpr float kThumbnailFOV       = 35.0f;
        constexpr float kMeshFramingScale   = 3.2f;   // Margin around bounds
        constexpr float kSphereFramingScale = 5.5f;   // Sphere fills ~60% of frame
        
        auto SetupStudioLighting = [](CWorld* World)
        {
            FEntityRegistry& Registry = World->GetEntityRegistry();
            entt::entity Light = World->ConstructEntity("StudioLight");
            Registry.emplace<SDirectionalLightComponent>(Light);
            Registry.emplace<SEnvironmentComponent>(Light);
            Registry.emplace<SSkyLightComponent>(Light);
        };

        RegisterThumbnailRenderer(CStaticMesh::StaticClass(), [SetupStudioLighting](FThumbnailScene& Scene, CObject* Asset)
            {
                CStaticMesh* Mesh = Cast<CStaticMesh>(Asset);
                if (Mesh == nullptr)
                {
                    return;
                }

                CWorld* World = Scene.GetWorld();
                FEntityRegistry& Registry = World->GetEntityRegistry();

                SetupStudioLighting(World);

                entt::entity MeshEntity = World->ConstructEntity("Mesh");
                Registry.emplace<SStaticMeshComponent>(MeshEntity).StaticMesh = Mesh;

                // Use the bounding-sphere radius (extent length) so wide-flat
                // and tall-narrow meshes frame the same as cubes.
                const FAABB Bounds   = Mesh->GetAABB();
                const FVector3 Cen  = Bounds.GetCenter();
                const float Radius   = Math::Max(Math::Length(Bounds.GetSize() * 0.5f), 0.5f);
                const FVector3 Dir  = Math::Normalize(FVector3(1.0f, 0.6f, 1.0f));
                const FVector3 CamPos = Cen + Dir * (Radius * kMeshFramingScale);

                Scene.SetCameraTransform(CamPos, Cen, kThumbnailFOV);
            });

        RegisterThumbnailRenderer(CMaterialInterface::StaticClass(), [SetupStudioLighting](FThumbnailScene& Scene, CObject* Asset)
            {
                CMaterialInterface* Material = Cast<CMaterialInterface>(Asset);
                if (Material == nullptr)
                {
                    return;
                }

                CWorld* World = Scene.GetWorld();
                FEntityRegistry& Registry = World->GetEntityRegistry();

                SetupStudioLighting(World);

                // PP materials are screen-space; mesh override falls back to default. Use a volume.
                CMaterial* BaseMaterial = Material->GetMaterial();
                const bool bIsPostProcess = BaseMaterial != nullptr && BaseMaterial->GetMaterialType() == EMaterialType::PostProcess;

                entt::entity MeshEntity = World->ConstructEntity("PreviewSphere");
                SStaticMeshComponent& MeshComp = Registry.emplace<SStaticMeshComponent>(MeshEntity);
                MeshComp.StaticMesh = CPrimitiveManager::Get().SphereMesh;
                if (!bIsPostProcess)
                {
                    MeshComp.MaterialOverrides.push_back(Material);
                }

                if (bIsPostProcess)
                {
                    entt::entity VolumeEntity = World->ConstructEntity("PreviewPostProcessVolume");
                    SPostProcessComponent& Volume = Registry.emplace<SPostProcessComponent>(VolumeEntity);
                    Volume.bInfiniteExtent = true;
                    Volume.PostProcessMaterials.push_back(Material);
                }

                // Sphere mesh has unit radius.
                const FVector3 Dir = Math::Normalize(FVector3(0.0f, 0.25f, 1.0f));
                Scene.SetCameraTransform(Dir * kSphereFramingScale, FVector3(0.0f), kThumbnailFOV);
            });

        RegisterThumbnailRenderer(CParticleSystem::StaticClass(), [SetupStudioLighting](FThumbnailScene& Scene, CObject* Asset)
            {
                CParticleSystem* PS = Cast<CParticleSystem>(Asset);
                if (PS == nullptr)
                {
                    return;
                }

                CWorld* World = Scene.GetWorld();
                FEntityRegistry& Registry = World->GetEntityRegistry();

                SetupStudioLighting(World);

                entt::entity ParticleEntity = World->ConstructEntity("ParticleSystem");
                Registry.emplace<SParticleSystemComponent>(ParticleEntity).ParticleSystem = PS;

                // No AABB on a particle system; fixed pull-back for typical spawn radius.
                const FVector3 Dir = Math::Normalize(FVector3(0.0f, 0.25f, 1.0f));
                Scene.SetCameraTransform(Dir * 4.0f, FVector3(0.0f), kThumbnailFOV);
            });
    }

    CThumbnailManager& CThumbnailManager::Get()
    {
        static std::once_flag Flag;
        std::call_once(Flag, []()
        {
            ThumbnailManagerSingleton = NewObject<CThumbnailManager>();
            ThumbnailManagerSingleton->Initialize();
        });

        return *ThumbnailManagerSingleton;
    }

    void CThumbnailManager::AsyncLoadThumbnailsForPackage(const FName& Package)
    {
        Task::AsyncTask(1, 1, [this, Package](uint32, uint32, uint32)
        {
            CPackage* MaybePackage = CPackage::LoadPackage(Package.c_str());
            if (MaybePackage == nullptr)
            {
                return;
            }
            
            FPackageThumbnail* Thumbnail = MaybePackage->GetPackageThumbnail();
            
            FPackageThumbnail::EState Expected = FPackageThumbnail::EState::None;
            if (!Thumbnail->LoadState.compare_exchange_strong(Expected, FPackageThumbnail::EState::Loading, std::memory_order_acquire))
            {
                return;
            }
            
            if (Thumbnail->ImageData.empty())
            {
                FWriteScopeLock Lock(ThumbnailLock);
                Thumbnails.insert_or_assign(Package, Thumbnail);
                return;
            }
            
            FRHIImageDesc ImageDesc;
            ImageDesc.Dimension = EImageDimension::Texture2D;
            ImageDesc.Extent = {256, 256};
            ImageDesc.Format = EFormat::RGBA8_UNORM;
            ImageDesc.Flags.SetFlag(EImageCreateFlags::ShaderResource);
            FRHIImageRef Image = GRenderContext->CreateImage(ImageDesc);
            
            FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Transfer());
            CommandList->Open();
            
            const uint8 BytesPerPixel = RHI::Format::BytesPerBlock(ImageDesc.Format);
            const uint32 RowBytes = ImageDesc.Extent.x * BytesPerPixel;
            
            TVector<uint8> FlippedData(Thumbnail->ImageData.size());
            uint8* Destination = FlippedData.data();
            const uint8* Source = Thumbnail->ImageData.data();

            for (uint32 y = 0; y < ImageDesc.Extent.y; ++y)
            {
                const uint32 FlippedY = ImageDesc.Extent.y - 1 - y;
                Memory::Memcpy(Destination + FlippedY * RowBytes, Source + y * RowBytes, RowBytes);
            }
    
            const uint32 RowPitch = RowBytes;
            constexpr uint32 DepthPitch = 0;
            
            CommandList->BeginTrackingImageState(Image, AllSubresources, EResourceStates::Unknown);
            CommandList->WriteImage(Image, 0, 0, FlippedData.data(), RowPitch, DepthPitch);
            CommandList->SetPermanentImageState(Image, EResourceStates::ShaderResource);
            
            CommandList->Close();
            GRenderContext->ExecuteCommandList(CommandList, ECommandQueue::Transfer);
            
            Thumbnail->LoadedImage = Image;
            Thumbnail->LoadState.store(FPackageThumbnail::EState::Loaded, std::memory_order_release);

            FWriteScopeLock Lock(ThumbnailLock);
            Thumbnails.insert_or_assign(Package, Thumbnail);
        });
    }

    FPackageThumbnail* CThumbnailManager::GetThumbnailForPackage(const FName& Package)
    {
        {
            FReadScopeLock Lock(ThumbnailLock);
            auto It = Thumbnails.find(Package);
            if (It != Thumbnails.end())
            {
                FPackageThumbnail* CachedThumbnail = It->second;
                if (CachedThumbnail && CachedThumbnail->IsReadyForRender())
                {
                    return CachedThumbnail;
                }
                
                if (CachedThumbnail)
                {
                    auto LoadState = CachedThumbnail->LoadState.load(std::memory_order_acquire);
                    if (LoadState == FPackageThumbnail::EState::None) // The package thumbnail may be dirty.
                    {
                        AsyncLoadThumbnailsForPackage(Package);
                    }
                }
                
                return nullptr;
            }
        }
    
        AsyncLoadThumbnailsForPackage(Package);
        return nullptr;
    }

    void CThumbnailManager::RegisterThumbnailRenderer(CClass* AssetClass, FThumbnailRendererFn Renderer)
    {
        if (AssetClass == nullptr)
        {
            return;
        }
        ThumbnailRenderers.insert_or_assign(AssetClass, Move(Renderer));
    }

    bool CThumbnailManager::GenerateThumbnail(CObject* Asset, CPackage* Package)
    {
        if (Asset == nullptr || Package == nullptr)
        {
            return false;
        }

        FThumbnailRendererFn* Renderer = nullptr;
        for (CClass* Klass = Asset->GetClass(); Klass != nullptr; Klass = Cast<CClass>(Klass->GetSuperClass()))
        {
            auto It = ThumbnailRenderers.find(Klass);
            if (It != ThumbnailRenderers.end())
            {
                Renderer = &It->second;
                break;
            }
        }

        if (Renderer == nullptr)
        {
            return false;
        }

        FThumbnailScene Scene(512);
        Scene.Begin();

        if (Scene.GetWorld() == nullptr)
        {
            return false;
        }

        (*Renderer)(Scene, Asset);

        FPackageThumbnail* Thumbnail = Package->GetPackageThumbnail();
        if (Thumbnail == nullptr)
        {
            return false;
        }

        const bool bCaptured = Scene.Capture(*Thumbnail);
        Scene.End();
        return bCaptured;
    }

    void CThumbnailManager::OnPackageDestroyed(FName Package)
    {
        FWriteScopeLock Lock(ThumbnailLock);

        auto It = Thumbnails.find(Package);
        if (It != Thumbnails.end())
        {
            Thumbnails.erase(It);
        }
    }
}
