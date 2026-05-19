#include "ThumbnailScene.h"

#include "Core/Math/Math.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Object/Package/Thumbnail/PackageThumbnail.h"
#include "Renderer/CommandList.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RenderThread.h"
#include "Renderer/RHIGlobals.h"
#include "World/WorldTypes.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Scene/RenderScene/RenderScene.h"
#include "World/Scene/RenderScene/SceneRenderTypes.h"

namespace Lumina
{
    static constexpr uint32 kThumbnailDownsample = 256;

    FThumbnailScene::FThumbnailScene(uint32 RenderTargetSize)
        : RTSize(RenderTargetSize)
    {
    }

    FThumbnailScene::~FThumbnailScene()
    {
        End();
    }

    void FThumbnailScene::Begin()
    {
        if (bInitialized)
        {
            return;
        }

        World = NewObject<CWorld>(nullptr, "ThumbnailWorld", FGuid::New(), OF_Transient);
        World->InitializeWorld(EWorldType::Editor);
        
        if (IRenderScene* Scene = World->GetRenderer())
        {
            Scene->Resize(glm::uvec2(RTSize, RTSize));
            // Editor-only debug overlays don't belong in saved thumbnails.
            Scene->GetSceneRenderSettings().bDrawBillboards = false;
        }

        CameraEntity = World->ConstructEntity("ThumbnailCamera");
        SCameraComponent& Camera = World->GetEntityRegistry().emplace<SCameraComponent>(CameraEntity);
        Camera.SetAspectRatio(1.0f);
        World->SetActiveCamera(CameraEntity);

        bInitialized = true;
    }

    void FThumbnailScene::End()
    {
        if (!bInitialized)
        {
            return;
        }

        if (World.IsValid())
        {
            World->TeardownWorld();
            World->ForceDestroyNow();
        }
        World        = nullptr;
        CameraEntity = entt::null;
        bInitialized = false;
    }

    void FThumbnailScene::SetCameraTransform(const glm::vec3& Position, const glm::vec3& Target, float FOVDegrees)
    {
        if (!bInitialized || CameraEntity == entt::null)
        {
            return;
        }

        STransformComponent& Transform = World->GetEntityRegistry().get<STransformComponent>(CameraEntity);
        Transform.SetLocation(Position);
        const glm::quat Rotation = Math::FindLookAtRotation(Target, Position);
        Transform.SetRotation(Rotation);

        // Push the transform straight onto the camera. The world is never
        // ticked, so CameraSystem doesn't run — we mirror its work here.
        // Forward/Up are derived from Position→Target directly so we don't
        // depend on the quat-to-axis convention matching SetView's contract.
        SCameraComponent& Camera = World->GetEntityRegistry().get<SCameraComponent>(CameraEntity);
        Camera.SetFOV(FOVDegrees);
        Camera.SetAspectRatio(1.0f);
        const glm::vec3 Forward = glm::normalize(Target - Position);
        const glm::vec3 WorldUp(0.0f, 1.0f, 0.0f);
        const glm::vec3 Right   = glm::normalize(glm::cross(WorldUp, Forward));
        const glm::vec3 Up      = glm::normalize(glm::cross(Forward, Right));
        Camera.SetView(Position, Forward, Up);
    }

    bool FThumbnailScene::Capture(FPackageThumbnail& Thumbnail)
    {
        if (!bInitialized || !World.IsValid() || World->GetRenderer() == nullptr)
        {
            return false;
        }

        // Synchronous capture on the game thread; flush any in-flight render work first.
        FlushRenderingCommands();

        // Extract uses the live CurrentFrameIndex slot; Render must read the same one,
        // and we must signal it consumed afterwards or the slot's Produced/Consumed
        // counters drift and the next capture deadlocks.
        const uint8 FrameIndex = (uint8)GRenderManager->GetCurrentFrameIndex();
        World->Extract();

        FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
        CommandList->Open();
        World->Render(*CommandList, FrameIndex);

        FRHIImage* RenderTarget = World->GetRenderer()->GetRenderTarget();
        if (RenderTarget == nullptr)
        {
            CommandList->Close();
            return false;
        }

        FRHIStagingImageRef StagingImage = GRenderContext->CreateStagingImage(RenderTarget->GetDescription(), ERHIAccess::HostRead);
        CommandList->CopyImage(RenderTarget, FTextureSlice(), StagingImage, FTextureSlice());

        CommandList->Close();
        GRenderContext->ExecuteCommandList(CommandList);
        GRenderContext->WaitIdle();

        // Balance the Produced++ that Extract did, since we bypassed the render lambda.
        if (IRenderScene* Scene = World->GetRenderer())
        {
            Scene->SignalFrameConsumed(FrameIndex);
        }

        size_t RowPitch = 0;
        void* MappedMemory = GRenderContext->MapStagingTexture(StagingImage, FTextureSlice(), ERHIAccess::HostRead, &RowPitch);
        if (MappedMemory == nullptr)
        {
            return false;
        }

        const uint32 SourceWidth  = RenderTarget->GetDescription().Extent.x;
        const uint32 SourceHeight = RenderTarget->GetDescription().Extent.y;

        Thumbnail.LoadState.store(FPackageThumbnail::EState::None, std::memory_order_relaxed);
        Thumbnail.ImageWidth  = kThumbnailDownsample;
        Thumbnail.ImageHeight = kThumbnailDownsample;

        constexpr size_t BytesPerPixel = 4;
        constexpr size_t TotalBytes = kThumbnailDownsample * kThumbnailDownsample * BytesPerPixel;
        Thumbnail.ImageData.resize(TotalBytes);

        const uint8* SourceData = static_cast<const uint8*>(MappedMemory);
        uint8* DestData = Thumbnail.ImageData.data();

        const float ScaleX = static_cast<float>(SourceWidth) / kThumbnailDownsample;
        const float ScaleY = static_cast<float>(SourceHeight) / kThumbnailDownsample;

        for (uint32 DestY = 0; DestY < kThumbnailDownsample; ++DestY)
        {
            const uint32 FlippedDestY = kThumbnailDownsample - 1 - DestY;

            for (uint32 DestX = 0; DestX < kThumbnailDownsample; ++DestX)
            {
                const float SrcX = DestX * ScaleX;
                const float SrcY = DestY * ScaleY;

                const uint32 X0 = static_cast<uint32>(SrcX);
                const uint32 Y0 = static_cast<uint32>(SrcY);
                const uint32 X1 = Math::Min(X0 + 1, SourceWidth - 1);
                const uint32 Y1 = Math::Min(Y0 + 1, SourceHeight - 1);

                const float FracX = SrcX - X0;
                const float FracY = SrcY - Y0;

                const uint8* P00 = SourceData + (Y0 * RowPitch) + (X0 * BytesPerPixel);
                const uint8* P10 = SourceData + (Y0 * RowPitch) + (X1 * BytesPerPixel);
                const uint8* P01 = SourceData + (Y1 * RowPitch) + (X0 * BytesPerPixel);
                const uint8* P11 = SourceData + (Y1 * RowPitch) + (X1 * BytesPerPixel);

                uint8* DestPixel = DestData + (FlippedDestY * kThumbnailDownsample * BytesPerPixel) + (DestX * BytesPerPixel);

                for (uint32 Channel = 0; Channel < BytesPerPixel; ++Channel)
                {
                    const float Top    = Math::Lerp(static_cast<float>(P00[Channel]), static_cast<float>(P10[Channel]), FracX);
                    const float Bottom = Math::Lerp(static_cast<float>(P01[Channel]), static_cast<float>(P11[Channel]), FracX);
                    const float Result = Math::Lerp(Top, Bottom, FracY);

                    DestPixel[Channel] = static_cast<uint8>(Result + 0.5f);
                }
            }
        }

        GRenderContext->UnMapStagingTexture(StagingImage);
        return true;
    }
}
