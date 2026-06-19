#pragma once

#include <atomic>
#include <mutex>

#include "Platform/GenericPlatform.h"
#include "Containers/String.h"
#include "Containers/Array.h"

namespace Lumina
{
    // One gameplay scope aggregated over a frame (a C# script, a system, or a user Profiler.Sample scope).
    struct RUNTIME_API FGameplayProfileEntry
    {
        FFixedString Name;
        uint64       Hash        = 0;
        uint32       Calls       = 0;
        double       InclusiveMs = 0.0;   // time including children
        double       ExclusiveMs = 0.0;   // self time (children subtracted)
    };

    // A frame's aggregated gameplay scopes.
    struct RUNTIME_API FGameplayProfileFrame
    {
        TVector<FGameplayProfileEntry> Entries;
        double TotalMs     = 0.0;          // total measured gameplay time (== sum of exclusive)
        uint64 FrameNumber = 0;
    };

    // One ECS system execution within a frame: the time span it ran and the worker thread slot it ran on, so
    // the editor can lay systems out per-thread (the "slotted in threads" timeline). Captured for native AND
    // C# systems uniformly at the scheduler dispatch site (CWorld::TickSystems).
    struct RUNTIME_API FSystemSpan
    {
        FFixedString Name;
        double       StartMs    = 0.0;
        double       EndMs      = 0.0;
        uint16       Worker     = 0;       // Jobs::GetWorkerIndex() slot the Update ran on
        uint8        Stage      = 0;       // EUpdateStage
        uint8        Batch      = 0;       // index of the parallel batch within the stage
        bool         bExclusive = false;   // ran alone (no declared access), vs in a parallel batch
    };

    // A frame's worth of system spans plus the frame's wall-clock window (for the timeline x-axis).
    struct RUNTIME_API FSystemSpanFrame
    {
        TVector<FSystemSpan> Spans;
        double FrameStartMs = 0.0;
        double FrameEndMs   = 0.0;
        uint64 FrameNumber  = 0;
    };

    // A lightweight CPU profiler for GAMEPLAY work: per-frame, name-aggregated scope timings with call counts
    // and inclusive/exclusive ms, plus rolling history for sparklines. Drives the editor "Gameplay Profiler"
    // tool and the C# Profiler API. Near-zero cost when disabled (one atomic check).
    //
    // BeginScope/EndScope are CONCURRENCY-SAFE so parallel C# EntitySystems (ticked across job-system workers)
    // can profile: the open-scope stack is thread-local (each scope opens and closes on the same thread/fiber),
    // and the shared per-frame aggregation is mutex-guarded. BeginFrame/EndFrame stay game-thread, called at the
    // frame boundary when no worker scope is open.
    class RUNTIME_API FGameplayProfiler
    {
    public:

        static constexpr uint32 HistorySize = 180;

        static FGameplayProfiler& Get();

        bool IsEnabled() const { return bEnabled.load(std::memory_order_relaxed); }
        void SetEnabled(bool bInEnabled);

        // Roll Current -> Latest at the game-thread frame boundary. Cheap no-ops when disabled.
        void BeginFrame();
        void EndFrame();

        void BeginScope(FStringView Name);
        void EndScope();

        // Records one system execution span (thread-safe; called from worker threads during TickSystems).
        // Cheap no-op when disabled.
        void RecordSystemSpan(FStringView Name, uint8 Stage, uint8 Batch, bool bExclusive, double StartMs, double EndMs, uint16 Worker);

        // Current wall-clock in ms on the profiler's clock (so callers can time spans on the same base).
        static double NowMilliseconds();

        const FGameplayProfileFrame& GetLatest() const { return Latest; }
        const FSystemSpanFrame& GetLatestSystemSpans() const { return SystemLatest; }
        const TVector<float>& GetFrameTotalHistory() const { return FrameTotalHistory; }
        const TVector<float>* GetEntryHistory(uint64 Hash) const;

    private:

        std::atomic<bool>                       bEnabled { false };
        std::mutex                              Mutex;            // guards Current/IndexOf/Latest/histories
        THashMap<uint64, int32>                 IndexOf;          // hash -> index into Current.Entries
        FGameplayProfileFrame                   Current;
        FGameplayProfileFrame                   Latest;
        TVector<float>                          FrameTotalHistory;
        THashMap<uint64, TVector<float>>        EntryHistory;     // hash -> rolling inclusive-ms ring
        uint64                                  FrameCounter = 0;

        std::mutex                              SpanMutex;        // guards SystemCurrent.Spans (worker pushes)
        FSystemSpanFrame                        SystemCurrent;
        FSystemSpanFrame                        SystemLatest;
    };

    // RAII scope for native gameplay code: GAMEPLAY_PROFILE_SCOPE("Name"). No-op when the profiler is off.
    struct RUNTIME_API FGameplayProfileScopeRAII
    {
        bool bActive;

        explicit FGameplayProfileScopeRAII(FStringView Name);
        ~FGameplayProfileScopeRAII();

        FGameplayProfileScopeRAII(const FGameplayProfileScopeRAII&) = delete;
        FGameplayProfileScopeRAII& operator=(const FGameplayProfileScopeRAII&) = delete;
    };
}

#define LUMINA_GP_JOIN_INNER(A, B) A##B
#define LUMINA_GP_JOIN(A, B) LUMINA_GP_JOIN_INNER(A, B)

#define GAMEPLAY_PROFILE_SCOPE(Name) \
    ::Lumina::FGameplayProfileScopeRAII LUMINA_GP_JOIN(GpScope_, __LINE__)(::Lumina::FStringView(Name))
