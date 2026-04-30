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
#include "World/Entity/Components/EnvironmentComponent.h"
#include "World/Entity/Components/LightComponent.h"
#include "World/Entity/Components/ParticleSystemComponent.h"
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
        
        {
            TUniquePtr<FMeshResource> Resource = MakeUnique<FMeshResource>();
            PrimitiveMeshes::GenerateCube(Resource->Vertices.emplace<TVector<FVertex>>(), Resource->Indices);
            
            FGeometrySurface Surface;
            Surface.ID = "CubeMesh";
            Surface.IndexCount = (uint32)Resource->Indices.size();
            Surface.StartIndex = 0;
            Surface.MaterialIndex = 0;
            Resource->GeometrySurfaces.push_back(Surface);

            CubeMesh = NewObject<CStaticMesh>(nullptr, "ThumbnailCubeMesh", FGuid::New(), OF_Transient);
            CubeMesh->Materials.resize(1);
            CubeMesh->SetMeshResource(Move(Resource));
        }

        {
            TUniquePtr<FMeshResource> Resource = MakeUnique<FMeshResource>();
            PrimitiveMeshes::GenerateSphere(Resource->Vertices.emplace<TVector<FVertex>>(), Resource->Indices);
            
            FGeometrySurface Surface;
            Surface.ID = "SphereMesh";
            Surface.IndexCount = (uint32)Resource->Indices.size();
            Surface.StartIndex = 0;
            Surface.MaterialIndex = 0;
            Resource->GeometrySurfaces.push_back(Surface);

            SphereMesh = NewObject<CStaticMesh>(nullptr, "ThumbnailSphereMesh", FGuid::New(), OF_Transient);
            SphereMesh->Materials.resize(1);
            SphereMesh->SetMeshResource(Move(Resource));
        }

        {
            TUniquePtr<FMeshResource> Resource = MakeUnique<FMeshResource>();
            PrimitiveMeshes::GeneratePlane(Resource->Vertices.emplace<TVector<FVertex>>(), Resource->Indices);
            
            FGeometrySurface Surface;
            Surface.ID = "PlaneMesh";
            Surface.IndexCount = (uint32)Resource->Indices.size();
            Surface.StartIndex = 0;
            Surface.MaterialIndex = 0;
            Resource->GeometrySurfaces.push_back(Surface);

            PlaneMesh = NewObject<CStaticMesh>(nullptr, "ThumbnailPlaneMesh", FGuid::New(), OF_Transient);
            PlaneMesh->Materials.resize(1);
            PlaneMesh->SetMeshResource(Move(Resource));
        }

        // Built-in renderers. Game/editor code can call RegisterThumbnailRenderer
        // to add or override entries for project-specific asset types.
        // Studio framing constants. With FOV 35° and aspect 1, a sphere of
        // radius R exactly fills the vertical at distance R / tan(17.5°) ≈
        // R * 3.17. Multipliers below leave a deliberate margin per asset
        // type — meshes get a wider crop, materials get a tight crop because
        // the sphere is the subject.
        constexpr float kThumbnailFOV       = 35.0f;
        constexpr float kMeshFramingScale   = 3.6f;   // ~12% margin around bounds
        constexpr float kSphereFramingScale = 5.5f;   // sphere fills ~60% of frame

        // Add a directional light + sky so thumbnails sit on the engine's
        // default skybox background. Caller supplies a tinted color so each
        // asset class can pick a slightly different mood; defaults are mild.
        auto SetupStudioLighting = [](CWorld* World)
        {
            FEntityRegistry& Registry = World->GetEntityRegistry();
            entt::entity Light = World->ConstructEntity("StudioLight");
            Registry.emplace<SDirectionalLightComponent>(Light);
            Registry.emplace<SEnvironmentComponent>(Light);
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
                const glm::vec3 Cen  = Bounds.GetCenter();
                const float Radius   = glm::max(glm::length(Bounds.GetSize() * 0.5f), 0.5f);
                const glm::vec3 Dir  = glm::normalize(glm::vec3(1.0f, 0.6f, 1.0f));
                const glm::vec3 CamPos = Cen + Dir * (Radius * kMeshFramingScale);

                Scene.SetCameraTransform(CamPos, Cen, kThumbnailFOV);
            });

        RegisterThumbnailRenderer(CMaterialInterface::StaticClass(), [this, SetupStudioLighting](FThumbnailScene& Scene, CObject* Asset)
            {
                CMaterialInterface* Material = Cast<CMaterialInterface>(Asset);
                if (Material == nullptr)
                {
                    return;
                }

                CWorld* World = Scene.GetWorld();
                FEntityRegistry& Registry = World->GetEntityRegistry();

                SetupStudioLighting(World);

                entt::entity MeshEntity = World->ConstructEntity("PreviewSphere");
                SStaticMeshComponent& MeshComp = Registry.emplace<SStaticMeshComponent>(MeshEntity);
                MeshComp.StaticMesh = SphereMesh;
                MeshComp.MaterialOverrides.push_back(Material);

                // Sphere mesh has unit radius.
                const glm::vec3 Dir = glm::normalize(glm::vec3(0.0f, 0.25f, 1.0f));
                Scene.SetCameraTransform(Dir * kSphereFramingScale, glm::vec3(0.0f), kThumbnailFOV);
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

                // Particles emit around origin; pull the camera back enough to
                // capture a typical spawn radius without trying to derive it
                // from the asset (no AABB on a particle system).
                const glm::vec3 Dir = glm::normalize(glm::vec3(0.0f, 0.25f, 1.0f));
                Scene.SetCameraTransform(Dir * 4.0f, glm::vec3(0.0f), kThumbnailFOV);
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

        // Walk up the asset's class hierarchy looking for a registered renderer.
        // Lets a single entry on a base class (CMaterialInterface) cover all
        // derivatives (CMaterial, CMaterialInstance) without duplicate setup.
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
