#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Math/Color.h"
#include "Core/Threading/Atomic.h"
#include "Memory/SmartPtr.h"
#include "Renderer/RenderResource.h"

namespace Lumina
{
    class ICommandList;
}

namespace Lumina
{
    struct RUNTIME_API FGPUProfileScope
    {
        FFixedString        Name;
        FColor              Color          = FColor::White;
        int32               ParentIndex    = -1;
        int32               Depth          = 0;
        int32               QueryIndex     = -1;
        int32               StatsQueryIndex= -1;
        float               ResolvedTimeMs = 0.0f;
        FPipelineStats      ResolvedStats;
        // Barriers attributed to this scope (the innermost open scope when the
        // RHI committed them). Set during recording; survives resolve.
        uint32              NumBufferBarriers = 0;
        uint32              NumImageBarriers  = 0;
    };

    enum class EGPUFrameState : uint8
    {
        Idle,
        Recording,
        Submitted,
        Resolved,
    };

    // Why a barrier was committed. Lets the profiler separate genuine per-pass
    // transitions from the end-of-frame keep-initial-state restores (which fire
    // with no scope open and are otherwise invisible).
    enum class EGPUBarrierPhase : uint8
    {
        Pass,                   // emitted by a draw/dispatch's Reads/Writes declaration
        RestoreInitialState,    // end-of-list KeepInitialStates() restore
        Copy,                   // copy/blit/resolve transition
        Clear,                  // clear transition
        Other,
    };

    // One committed image- or buffer-memory barrier, captured for inspection.
    // Strings are pre-formatted at capture time so the UI does no work.
    struct RUNTIME_API FGPUBarrierRecord
    {
        FFixedString        ResourceName;
        FFixedString        Before;
        FFixedString        After;
        int32               ScopeIndex      = -1;   // innermost open scope at commit, -1 = unscoped
        uint16              Mip             = 0;
        uint16              ArraySlice      = 0;
        uint16              NumMips         = 1;
        uint16              NumArraySlices  = 1;
        EGPUBarrierPhase    Phase           = EGPUBarrierPhase::Pass;
        bool                bImage          = false;
        bool                bEntireResource = true;
        bool                bRedundant      = false; // before == after (pure execution/UAV barrier)
    };

    struct RUNTIME_API FGPUProfileFrame
    {
        TVector<FGPUProfileScope>               Scopes;
        TVector<int32>                          ScopeStack;
        TVector<FRHITimerQueryRef>              QueryPool;
        TVector<FRHIPipelineStatsQueryRef>      PipelineStatsPool;
        uint32                                  QueryCursor         = 0;
        uint32                                  StatsQueryCursor    = 0;
        uint64                                  FrameNumber         = 0;
        float                                   TotalTimeMs         = 0.0f;
        FPipelineStats                          TotalStats;
        // CPU-recorded barrier counts for the frame, split so buffer UAV barriers
        // can be watched independently from image layout transitions.
        uint32                                  NumBufferBarriers   = 0;
        uint32                                  NumImageBarriers    = 0;
        // Per-barrier detail, captured only while the profiler is enabled. Bounded
        // by MaxBarrierRecordsPerFrame; NumDroppedBarriers tracks overflow.
        TVector<FGPUBarrierRecord>              Barriers;
        uint32                                  NumDroppedBarriers  = 0;
        EGPUFrameState                          State               = EGPUFrameState::Idle;

        void Reset();
    };

    class RUNTIME_API FGPUProfiler
    {
    public:

        static constexpr uint32 MaxFramesInFlight        = 4;
        static constexpr uint32 MaxScopesPerFrame        = 128;
        static constexpr uint32 FrameHistorySize         = 240;
        static constexpr uint32 MaxBarrierRecordsPerFrame= 1024;

        static FGPUProfiler& Get();

        FGPUProfiler() = default;
        ~FGPUProfiler() = default;

        FGPUProfiler(const FGPUProfiler&) = delete;
        FGPUProfiler& operator = (const FGPUProfiler&) = delete;

        // Call before render context shuts down.
        void Shutdown();

        // True only when CVar is enabled AND a frame is actively recording.
        bool IsEnabled() const;

        void BeginFrame();
        void EndFrame();

        void BeginScope(ICommandList* CmdList, const char* Name, const FColor& Color = FColor::White);
        void EndScope(ICommandList* CmdList);

        // Accumulate barriers emitted this frame; called from RHI barrier commit on whatever thread
        // records the list (atomic). EndFrame snapshots + resets these into the recording frame.
        void AddBarriers(uint32 NumBuffer, uint32 NumImage);

        // Capture per-barrier detail for one commit. All records in a single commit share the
        // innermost open scope, resolved here under the lock. No-op unless the profiler is enabled.
        // Cheap to call with Count == 0.
        void AddBarrierRecords(const FGPUBarrierRecord* Records, uint32 Count);

        const FGPUProfileFrame* GetLatestResolvedFrame() const;

        const TVector<float>& GetFrameTimeHistory() const { return FrameTimeHistory; }

        uint32                  GetRecordingSlot() const            { return RecordingSlot; }
        int32                   GetLatestResolvedSlot() const       { return LatestResolvedSlot; }
        const FGPUProfileFrame& GetSlot(uint32 Index) const         { return Frames[Index]; }
        uint64                  GetFrameCounter() const             { return FrameCounter; }

    private:

        void TryResolveFrame(FGPUProfileFrame& Frame);

        FMutex              Mutex;
        FGPUProfileFrame    Frames[MaxFramesInFlight];
        uint32              RecordingSlot          = 0;
        int32               LatestResolvedSlot     = -1;
        uint64              FrameCounter           = 0;

        // Frame-in-progress barrier accumulators; drained in EndFrame.
        TAtomic<uint32>     PendingBufferBarriers  = 0;
        TAtomic<uint32>     PendingImageBarriers   = 0;

        TVector<float>      FrameTimeHistory;
    };

    // Zero-cost when CVar is disabled.
    struct RUNTIME_API FGPUProfileScopeRAII
    {
        ICommandList*   CmdList;
        bool            bActive;

        FGPUProfileScopeRAII(ICommandList* InCmdList, const char* Name, const FColor& Color);
        ~FGPUProfileScopeRAII();

        FGPUProfileScopeRAII(const FGPUProfileScopeRAII&) = delete;
        FGPUProfileScopeRAII& operator = (const FGPUProfileScopeRAII&) = delete;
    };
}

#define LUMINA_GPU_JOIN_INNER(A, B) A##B
#define LUMINA_GPU_JOIN(A, B) LUMINA_GPU_JOIN_INNER(A, B)

#define GPU_PROFILE_SCOPE(CmdList, Name) \
    ::Lumina::FGPUProfileScopeRAII LUMINA_GPU_JOIN(GpuScope_, __LINE__)((CmdList), (Name), ::Lumina::FColor::White)

#define GPU_PROFILE_SCOPE_COLOR(CmdList, Name, Color) \
    ::Lumina::FGPUProfileScopeRAII LUMINA_GPU_JOIN(GpuScope_, __LINE__)((CmdList), (Name), (Color))
