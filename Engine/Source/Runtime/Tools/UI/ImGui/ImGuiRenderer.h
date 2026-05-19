#pragma once

#include "imgui.h"
#include "ImDrawDataSnapshot.h"
#include "ImGuiX.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RenderTypes.h"
#include "Subsystems/Subsystem.h"

struct ImPlotContext;

namespace Lumina
{
    class ICommandList;
    class FRenderManager;
}

namespace Lumina
{
    class IImGuiRenderer
    {
    public:

        virtual ~IImGuiRenderer() = default;

        virtual void Initialize();
        virtual void Deinitialize();

        void StartFrame(const FUpdateContext& UpdateContext);

        // Game thread: ImGui::Render() then swap DrawData into the snapshot
        // slot for FrameIndex. Returns a pointer into the persistent ring -
        // the renderer owns the storage, the caller just forwards it to the
        // render thread. Returns nullptr if there's no valid draw data.
        FImDrawDataSnapshot* BuildFrame_GameThread(uint8 FrameIndex);

        // Render thread: record the snapshot's draw lists onto CmdList.
        void RecordFrame_RenderThread(ICommandList& CmdList, FImDrawDataSnapshot& Snapshot);

        // Releases persistent snapshot storage. Must be called BEFORE
        // ImGui::DestroyContext() since pooled ImDrawLists use its allocator.
        void ClearSnapshots();

        virtual void OnStartFrame(const FUpdateContext& UpdateContext) = 0;
        virtual void OnEndFrame(ICommandList& CmdList, FImDrawDataSnapshot& Snapshot) = 0;

        // Copy this frame's referenced texture refs into the snapshot so the
        // render thread keeps them alive while recording.
        virtual void FillReferencedImagesSnapshot(TVector<FRHIImageRef>& Out) = 0;

        virtual ImTextureID GetOrCreateImTexture(FStringView Path) = 0;
        virtual ImTextureID GetOrCreateImTexture(FRHIImage* Image, const FTextureSubresourceSet& Subresources = AllSubresources) = 0;
        virtual void DestroyImTexture(uint64 Hash) = 0;

        // Draws the renderer-debug body (tabs + content) into the current ImGui window.
        // Caller owns the surrounding window — this just emits content.
        virtual void DrawRenderDebugContents(const FUpdateContext& Context) = 0;
        
        RUNTIME_API ImGuiContext* GetImGuiContext() const { return Context; }
        RUNTIME_API ImPlotContext* GetImPlotContext() const { return ImPlotContext; }
        
    protected:

        ImGuiContext* Context = nullptr;
        ImPlotContext* ImPlotContext = nullptr;

        // Persistent ring keyed by render-thread frame index. Each slot owns
        // a pool of ImDrawList copies that get reused across frames - after
        // warm-up SnapUsingSwap is allocation-free unless buffers grow.
        FImDrawDataSnapshot Snapshots[FRAMES_IN_FLIGHT];
    };
}
