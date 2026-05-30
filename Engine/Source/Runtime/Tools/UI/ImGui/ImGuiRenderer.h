#pragma once

#include "imgui.h"
#include "ImDrawDataSnapshot.h"
#include "ImGuiX.h"
#include "Containers/Array.h"
#include "Core/Threading/Atomic.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RenderTypes.h"

#include <condition_variable>
#include <mutex>

struct ImPlotContext;

namespace Lumina
{
    class ICommandList;
    class FRenderManager;
    class FUpdateContext;
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

        // Game thread: ImGui::Render() then swap DrawData into FrameIndex's slot. Returns a pointer
        // into the renderer-owned ring (forward to render thread), or nullptr if no valid draw data.
        FImDrawDataSnapshot* BuildFrame_GameThread(uint8 FrameIndex);

        // Render thread: record the snapshot's draw lists onto CmdList, then
        // render+present any secondary viewports captured for this frame's slot.
        void RecordFrame_RenderThread(ICommandList& CmdList, FImDrawDataSnapshot& Snapshot, uint8 FrameIndex);

        // Render thread: call after RecordFrame_RenderThread to release the slot; the game thread
        // may be blocked in BuildFrame_GameThread waiting for it to wrap back around.
        void SignalSnapshotSlotConsumed(uint8 FrameIndex);

        // Releases persistent snapshot storage. Must be called BEFORE
        // ImGui::DestroyContext() since pooled ImDrawLists use its allocator.
        void ClearSnapshots();

        virtual void OnStartFrame(const FUpdateContext& UpdateContext) = 0;
        virtual void OnEndFrame(ICommandList& CmdList, FImDrawDataSnapshot& Snapshot) = 0;

        // Game thread captures secondary viewports' draw data into the slot; render thread presents
        // them, so every vkQueuePresentKHR runs on one thread (SyncVal can't track cross-thread present).
        virtual void CaptureViewports_GameThread(uint8 Slot) {}
        virtual void RenderViewports_RenderThread(uint8 Slot) {}

        // Copy this frame's referenced texture refs into the snapshot so the
        // render thread keeps them alive while recording.
        virtual void FillReferencedImagesSnapshot(TVector<FRHIImageRef>& Out) = 0;

        // Game thread: process pending ImGui textures before handing off the snapshot, else 1.92's
        // backend does it lazily in RenderDrawData against shared state and races. Hold the gfx-queue lock.
        virtual void ProcessTextureUpdates_GameThread() {}

        virtual ImTextureID GetOrCreateImTexture(FStringView Path) = 0;
        virtual ImTextureID GetOrCreateImTexture(FRHIImage* Image, const FTextureSubresourceSet& Subresources = AllSubresources) = 0;
        virtual void DestroyImTexture(uint64 Hash) = 0;

        RUNTIME_API ImGuiContext* GetImGuiContext() const { return Context; }
        RUNTIME_API ImPlotContext* GetImPlotContext() const { return ImPlotContext; }
        
    protected:

        ImGuiContext* Context = nullptr;
        ImPlotContext* ImPlotContext = nullptr;

        // Persistent ring keyed by render-thread frame index; each slot pools reused ImDrawList
        // copies, so after warm-up SnapUsingSwap is allocation-free unless buffers grow.
        FImDrawDataSnapshot Snapshots[FRAMES_IN_FLIGHT];

        // Per-slot producer/consumer counters (independent of the world FrameRing) so the game thread
        // blocks rather than stomp a snapshot the render thread is still recording from.
        TAtomic<uint64> SnapshotProduced[FRAMES_IN_FLIGHT] = {};
        TAtomic<uint64> SnapshotConsumed[FRAMES_IN_FLIGHT] = {};
        std::mutex SnapshotSlotMutex;
        std::condition_variable SnapshotSlotCV;

        void WaitForSnapshotSlot(uint8 Slot, uint64 Target);
    };
}
