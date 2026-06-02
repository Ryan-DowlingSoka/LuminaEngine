#include "ThumbnailScene.h"
#include "ThumbnailUtils.h"

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
            Scene->Resize(FUIntVector2(RTSize, RTSize));
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
        }
        // Releasing the only strong ref to the transient preview world drops its refcount to zero and
        // frees it, no ForceDestroyNow (which would dangle this TObjectPtr, then touch freed memory).
        World        = nullptr;
        CameraEntity = entt::null;
        bInitialized = false;
    }

    void FThumbnailScene::SetCameraTransform(const FVector3& Position, const FVector3& Target, float FOVDegrees)
    {
        if (!bInitialized || CameraEntity == entt::null)
        {
            return;
        }

        STransformComponent& Transform = World->GetEntityRegistry().get<STransformComponent>(CameraEntity);
        Transform.SetLocation(Position);
        const FQuat Rotation = Math::FindLookAtRotation(Target, Position);
        Transform.SetRotation(Rotation);

        // World is never ticked so CameraSystem doesn't run; set view directly here.
        SCameraComponent& Camera = World->GetEntityRegistry().get<SCameraComponent>(CameraEntity);
        Camera.SetFOV(FOVDegrees);
        Camera.SetAspectRatio(1.0f);
        const FVector3 Forward = Math::Normalize(Target - Position);
        const FVector3 WorldUp(0.0f, 1.0f, 0.0f);
        const FVector3 Right   = Math::Normalize(Math::Cross(WorldUp, Forward));
        const FVector3 Up      = Math::Normalize(Math::Cross(Forward, Right));
        Camera.SetView(Position, Forward, Up);
    }

    bool FThumbnailScene::Capture(FPackageThumbnail& Thumbnail)
    {
        if (!bInitialized || !World.IsValid() || World->GetRenderer() == nullptr)
        {
            return false;
        }

        // Flush so the thumbnail world (created in Begin) is realized before we render it.
        FlushRenderingCommands();

        // Extract reads the ECS (game thread) and bumps the frame slot's Produced count;
        // SignalFrameConsumed below balances it or the next capture deadlocks.
        const uint8 FrameIndex = (uint8)GRenderManager->GetCurrentFrameIndex();
        World->Extract();

        FRHIImage*          RenderTarget = nullptr;
        FRHIStagingImageRef StagingImage;

        // Submit on the render thread (the sole graphics submitter) so we never race the
        // swapchain frame. Capture is game-thread only, so EnqueueAndWait can't self-deadlock.
        auto RecordCapture = [&]()
        {
            FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
            CommandList->Open();
            World->Render(*CommandList, FrameIndex);

            RenderTarget = World->GetRenderer()->GetRenderTarget();
            if (RenderTarget == nullptr)
            {
                CommandList->Close();
                return;
            }

            StagingImage = GRenderContext->CreateStagingImage(RenderTarget->GetDescription(), ERHIAccess::HostRead);
            CommandList->CopyImage(RenderTarget, FTextureSlice(), StagingImage, FTextureSlice());

            CommandList->Close();
            GRenderContext->ExecuteCommandList(CommandList);

            // Wait on just this submission, not the whole device.
            FRHIEventQueryRef Query = GRenderContext->CreateEventQuery();
            GRenderContext->SetEventQuery(Query, ECommandQueue::Graphics);
            GRenderContext->WaitEventQuery(Query);
        };

        if (GRenderThread != nullptr && GRenderThread->IsRunning())
        {
            GRenderThread->EnqueueAndWait("ThumbnailCapture", [&RecordCapture]() { RecordCapture(); });
        }
        else
        {
            RecordCapture();
        }

        // Balance the Produced++ that Extract did, since we bypassed the render lambda.
        if (IRenderScene* Scene = World->GetRenderer())
        {
            Scene->SignalFrameConsumed(FrameIndex);
        }

        if (RenderTarget == nullptr || !StagingImage.IsValid())
        {
            return false;
        }

        size_t RowPitch = 0;
        void* MappedMemory = GRenderContext->MapStagingTexture(StagingImage, FTextureSlice(), ERHIAccess::HostRead, &RowPitch);
        if (MappedMemory == nullptr)
        {
            return false;
        }

        const uint32 SourceWidth  = RenderTarget->GetDescription().Extent.x;
        const uint32 SourceHeight = RenderTarget->GetDescription().Extent.y;

        ThumbnailUtils::StoreDownsampledRGBA(Thumbnail, static_cast<const uint8*>(MappedMemory),
            SourceWidth, SourceHeight, RowPitch);

        GRenderContext->UnMapStagingTexture(StagingImage);
        return true;
    }
}
