#pragma once

#include "Platform/GenericPlatform.h"

// Lumina job system — a fiber scheduler with counter-based dependencies.
//
// One worker thread per core drains lock-free MPMC job queues (one per priority). Each job runs on a
// pooled user-mode fiber. When a job waits on a counter it does NOT block the worker: the fiber is
// parked and the worker switches to other runnable work; the fiber is resumed later — possibly on a
// different worker (fibers migrate freely). That keeps every core productive, makes nested parallelism
// deadlock-free, and unlike a blocking wait the waiting stack is suspended rather than held.
//
// Non-worker ("external") threads — main/render/physics — are not on fibers. A WaitForCounter from an
// external thread keeps the old assist-wait behavior: it runs queued jobs inline until satisfied.
//
// This is the low-level core. The task-facing API (Task::ParallelFor / FTaskGraph / etc.)
// is layered on top in TaskSystem / TaskGraph.
namespace Lumina::Jobs
{
    enum class EJobPriority : uint8
    {
        High   = 0,
        Normal = 1,
        Low    = 2,
    };

    // Opaque, pooled. A counter is "done" when its value reaches the waited-for target (default 0).
    struct FCounter;

    using FJobFunction  = void (*)(void* Arg, uint32 WorkerIndex);
    using FCompletionFn = void (*)(void* Ctx, uint32 WorkerIndex);

    struct FJobDecl
    {
        FJobFunction Function = nullptr;
        void*        Argument = nullptr;
    };

    struct FConfig
    {
        uint32 NumWorkerThreads   = 0; // 0 => hardware_concurrency() - 1
        uint32 NumExternalThreads = 8; // reserved thread slots for non-worker threads (main/render/physics/...)
        uint32 NumWorkFibers      = 0; // pooled fibers jobs run on; 0 => default (kDefaultWorkFibers)
        uint32 FiberStackSize     = 0; // per-fiber reserved stack in bytes; 0 => default (kDefaultFiberStack)
    };

    RUNTIME_API void Initialize(const FConfig& Config);
    RUNTIME_API void Shutdown();
    RUNTIME_API bool IsInitialized();

    // Background worker thread count.
    RUNTIME_API uint32 GetNumWorkers();
    // Total addressable thread slots (workers + external). Array-sizing bound for per-thread data.
    RUNTIME_API uint32 GetNumThreadSlots();
    // Dense slot in [0, GetNumThreadSlots()) for the OS thread currently running the caller. Stable per
    // OS thread, but a job's slot is only valid until its first WaitForCounter: a parked fiber may resume
    // on a different worker, so re-read this after any wait instead of caching the value across it.
    RUNTIME_API uint32 GetWorkerIndex();
    // True when the caller is a scheduler worker thread (so WaitForCounter yields instead of blocking).
    RUNTIME_API bool   IsWorkerThread();

    // Claim/release a thread slot for a non-worker thread (render, physics, ...).
    RUNTIME_API uint32 RegisterExternalThread();
    RUNTIME_API void   UnregisterExternalThread();

    // Counters. AllocCounter hands back a fresh counter set to InitialValue.
    RUNTIME_API FCounter* AllocCounter(int32 InitialValue = 0);
    RUNTIME_API void      FreeCounter(FCounter* Counter);
    RUNTIME_API int32     GetCounterValue(const FCounter* Counter);
    // One-shot callback fired (on a worker) the moment the counter reaches 0. May free the counter.
    RUNTIME_API void      SetCounterCompletion(FCounter* Counter, FCompletionFn Fn, void* Ctx);

    // Submit jobs. The counter is incremented by Count up-front; each job decrements it on completion.
    RUNTIME_API void RunJobs(const FJobDecl* Jobs, uint32 Count, EJobPriority Priority, FCounter* Counter);
    RUNTIME_API void RunJob(FJobFunction Fn, void* Arg, EJobPriority Priority, FCounter* Counter);

    // Manually decrement a counter (not tied to a job). Fires waiters/completion at zero. Used for
    // graph fan-in where a node's completion signals a shared counter.
    RUNTIME_API void DecrementCounter(FCounter* Counter, int32 By = 1);

    // Wait until Counter <= Value. On a worker the calling fiber parks and the worker runs other work;
    // on an external thread it assist-waits (runs queued jobs inline) until satisfied.
    RUNTIME_API void WaitForCounter(FCounter* Counter, int32 Value = 0);

    // Block the calling thread until every job submitted so far has completed.
    RUNTIME_API void WaitForAll();
}
