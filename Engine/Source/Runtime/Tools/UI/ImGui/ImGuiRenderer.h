#pragma once

#include "imgui.h"
#include "ImDrawDataSnapshot.h"
#include "ImGuiX.h"
#include "Containers/Array.h"
#include "Core/Threading/Atomic.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RenderTypes.h"
#include "Subsystems/Subsystem.h"

#include <condition_variable>
#include <mutex>

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

        // Render thread: call after RecordFrame_RenderThread to release the
        // snapshot slot. The game thread may be blocked in BuildFrame_GameThread
        // waiting for this slot to wrap back around.
        void SignalSnapshotSlotConsumed(uint8 FrameIndex);

        // Releases persistent snapshot storage. Must be called BEFORE
        // ImGui::DestroyContext() since pooled ImDrawLists use its allocator.
        void ClearSnapshots();

        virtual void OnStartFrame(const FUpdateContext& UpdateContext) = 0;
        virtual void OnEndFrame(ICommandList& CmdList, FImDrawDataSnapshot& Snapshot) = 0;

        // Copy this frame's referenced texture refs into the snapshot so the
        // render thread keeps them alive while recording.
        virtual void FillReferencedImagesSnapshot(TVector<FRHIImageRef>& Out) = 0;

        // Game thread: create/upload/destroy all pending ImGui textures now,
        // before the snapshot is handed to the render thread. ImGui 1.92's
        // dynamic-texture backend otherwise does this lazily inside
        // RenderDrawData against shared backend state (g.PlatformIO.Textures +
        // a single backend command buffer/queue submit), which races the game
        // thread that rebuilds that list every frame. Must run under the
        // graphics-queue external-access lock.
        virtual void ProcessTextureUpdates_GameThread() {}

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

        // Per-slot producer/consumer counters so the game thread can't overwrite
        // a snapshot while the render thread is still recording from it. This
        // ring is independent of the world FrameRing -- if the render thread
        // ever falls more than FRAMES_IN_FLIGHT-1 frames behind for any reason
        // (GPU stall, no-world frame, suspended world, etc.), the game thread
        // blocks here instead of stomping a live snapshot.
        TAtomic<uint64> SnapshotProduced[FRAMES_IN_FLIGHT] = {};
        TAtomic<uint64> SnapshotConsumed[FRAMES_IN_FLIGHT] = {};
        std::mutex SnapshotSlotMutex;
        std::condition_variable SnapshotSlotCV;

        void WaitForSnapshotSlot(uint8 Slot, uint64 Target);
    };
}
