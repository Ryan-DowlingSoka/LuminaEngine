#include "pch.h"
#include "JobScheduler.h"

#include "Core/Threading/Thread.h"
#include "Core/Threading/Atomic.h"
#include "Core/Threading/Fiber.h"
#include "Memory/Memory.h"
#include "Memory/MemoryConcurrentQueue.h"
#include "Platform/Process/PlatformProcess.h"
#include "Containers/Array.h"
#include "Core/LuminaMacros.h"
#include "Core/Profiler/Profile.h"
#include "Log/Log.h"

#if USING(WITH_EDITOR)
#include "JobProfiler.h"
#endif

#include <intrin.h>
#include <condition_variable>
#include <cstdio>

// Fiber scheduler with assist-wait fallback for external threads. One worker thread per core drains
// lock-free MPMC job queues; each job runs on a pooled user-mode fiber. A worker WaitForCounter parks
// the running fiber and switches the worker to other runnable work, resuming the fiber later (possibly
// on a different worker). External threads (main/render/physics) are not on fibers, they assist-wait,
// running queued jobs inline until the counter is satisfied. Nested parallelism is deadlock-free: the
// awaited work runs on other fibers/workers while the waiter is parked.
namespace Lumina::Jobs
{
    namespace
    {
        FORCEINLINE void CpuPause() { _mm_pause(); }

        constexpr uint32 kCounterPoolSize   = 8192;
        constexpr uint32 kDefaultWorkFibers = 256;
        constexpr uint32 kDefaultFiberStack = 512 * 1024;

        struct FQueuedJob
        {
            FJobFunction Function = nullptr;
            void*        Argument = nullptr;
            FCounter*    Counter  = nullptr; // FCounter is incomplete here; pointer only
#if USING(WITH_EDITOR)
            const char*  Name     = nullptr; // label for the editor profiler; absent otherwise to shrink the queue element
#endif
        };

        // A pooled fiber jobs run on. Long-lived: it loops running one bound job then switching back to
        // the scheduler to be reused. Migrates between workers when a job parks and resumes elsewhere.
        struct FWorkFiber
        {
            Fibers::FFiber Handle = nullptr;
            FQueuedJob     Job{};   // bound by the scheduler immediately before switching in
#if USING(WITH_EDITOR)
            // Editor-only live state for the Task System profiler (the fiber grid / by-fiber timeline).
            uint16          Index       = 0;                 // pool index, stable
            TAtomic<uint8>  State{0};                   // EFiberState
            TAtomic<uint16> OwnerWorker{0xFFFF};        // worker last/currently running this fiber
            TAtomic<uint32> WaitCounterId{0};           // counter pool index when Parked
#endif
#if defined(TRACY_ENABLE)
            char            TracyName[20] = {};       // stable per-fiber label for Tracy's fiber zones
#endif
        };

#if defined(TRACY_ENABLE)
        thread_local bool GTracyFiberEntered = false;
        FORCEINLINE void TracyEnterFiber(FWorkFiber* F)
        {
            GTracyFiberEntered = TracyIsConnected;
            if (GTracyFiberEntered)
            {
                // Group hint clusters all work-fiber tracks together below the worker threads.
                TracyFiberEnterHint(F->TracyName, Threading::ThreadGroup_Fiber);
            }
        }
        FORCEINLINE void TracyLeaveFiber()
        {
            if (GTracyFiberEntered)
            {
                TracyFiberLeave;
                GTracyFiberEntered = false;
            }
        }
#else
        FORCEINLINE void TracyEnterFiber(void*) {}
        FORCEINLINE void TracyLeaveFiber()      {}
#endif

        // A fiber parked on a counter. Lives on the parking fiber's own stack (safe: the fiber stays
        // alive while parked), linked into FCounter::Waiters under the counter spinlock.
        struct FWaitNode
        {
            FWorkFiber* Fiber  = nullptr;
            int32       Target = 0;
            FWaitNode*  Next   = nullptr;
        };
    }

    // Public opaque type; the definition lives here. References FWaitNode from the unnamed namespace
    // above (accessible unqualified in the enclosing namespace).
    struct alignas(64) FCounter
    {
        TAtomic<int32>  Value{0};
        FCompletionFn   Completion    = nullptr; // fired once when Value reaches 0
        void*           CompletionCtx = nullptr;

        TAtomic<uint32> WaitLock{0};         // spinlock guarding Waiters
        FWaitNode*      Waiters = nullptr;   // intrusive list, guarded by WaitLock
        TAtomic<bool>   HasWaiters{false};   // seq_cst gate so the lock-free decrement can skip the lock

        uint16          PoolIndex = 0xFFFF;
        bool            bPooled   = false;
    };

    namespace
    {
        using FJobQueue   = moodycamel::ConcurrentQueue<FQueuedJob, Memory::FTrackedConcurrentQueueTraits>;
        using FIndexQueue = moodycamel::ConcurrentQueue<uint16, Memory::FTrackedConcurrentQueueTraits>;
        using FFiberQueue = moodycamel::ConcurrentQueue<FWorkFiber*, Memory::FTrackedConcurrentQueueTraits>;

        // Deferred action the scheduler fiber performs AFTER a work fiber has switched away.
        enum class EPending : uint8 { None, Park, Free, ParkFn };

        struct FPendingSwitch
        {
            EPending    Action  = EPending::None;
            FWorkFiber* Fiber   = nullptr; // the work fiber we just switched away from
            FCounter*   Counter = nullptr; // park target (Park)
            FWaitNode*  Node    = nullptr; // park node, on the parking fiber's stack (Park)
            FParkFn     ParkFn  = nullptr; // publish callback (ParkFn)
            void*       ParkCtx = nullptr; // callback context (ParkFn)
        };

        struct FThreadState
        {
            uint32         WorkerIndex    = ~0u;
            bool           bIsWorker      = false;
            Fibers::FFiber SchedulerFiber = nullptr; // this worker's scheduler fiber
            FWorkFiber*    CurrentFiber   = nullptr; // work fiber currently switched in on this worker
            FPendingSwitch Pending;
        };
        thread_local FThreadState TLS;

        // Eventcount: lets an idle worker block until work appears.
        struct FEventCount
        {
            static constexpr uint64 kWaiterMask = 0xFFFFFFFFull;
            static constexpr uint64 kEpochInc   = 0x100000000ull;
            static constexpr uint64 kWaiterInc  = 1ull;

            TAtomic<uint64>         State{0};
            FMutex                  Mutex;
            std::condition_variable CV;

            // Register intent to wait and capture the current epoch. Must be followed by exactly one of
            // CancelWait (condition turned out satisfied) or Wait (block on the captured epoch).
            uint32 PrepareWait()
            {
                const uint64 Prev = State.fetch_add(kWaiterInc, std::memory_order_acq_rel);
                return static_cast<uint32>(Prev >> 32);
            }

            void CancelWait()
            {
                State.fetch_sub(kWaiterInc, std::memory_order_acq_rel);
            }

            void Wait(uint32 Key)
            {
                {
                    std::unique_lock<FMutex> Lock(Mutex);
                    CV.wait(Lock, [&] { return static_cast<uint32>(State.load(std::memory_order_acquire) >> 32) != Key; });
                }
                State.fetch_sub(kWaiterInc, std::memory_order_acq_rel);
            }

            void Notify(bool All)
            {
                const uint64 Prev = State.fetch_add(kEpochInc, std::memory_order_acq_rel);
                if ((Prev & kWaiterMask) != 0)
                {
                    // Hold the lock across notify so we never signal in the gap between a waiter's
                    // predicate check and its block (the std CV lost-wakeup guard).
                    std::scoped_lock Lock(Mutex);
                    if (All)
                    {
                        CV.notify_all();
                    }
                    else
                    {
                        CV.notify_one();
                    }
                }
            }
        };

        struct FScheduler
        {
            uint32 NumWorkers     = 0;
            uint32 NumExternal    = 0;
            uint32 NumThreadSlots = 0;
            uint32 NumWorkFibers  = 0;
            uint32 FiberStackSize = 0;

            TVector<FThread> WorkerThreads;

            FJobQueue      JobQueues[3];
            TVector<moodycamel::ProducerToken> ProdTokens[3];
            TVector<moodycamel::ConsumerToken> ConsTokens[3];
            alignas(64) TAtomic<int64> AvailJobs{0};   // queued, not yet popped (idle-wake hint)
            alignas(64) TAtomic<int64> InFlight{0};    // submitted, not yet completed (WaitForAll)

            FWorkFiber*    WorkFibers = nullptr; // pool storage
            FFiberQueue    FreeFibers;           // idle, ready to be bound to a job
            FFiberQueue    ReadyFibers;          // parked fibers whose counter is now satisfied
            alignas(64) TAtomic<int64> ReadyCount{0};  // ReadyFibers size hint (idle-wake)

            FCounter*       CounterPool = nullptr;
            FIndexQueue     FreeCounters;

            TAtomic<uint32> NextExternalSlot{0};
            TAtomic<bool>   bShutdown{false};

            FEventCount     WorkSignal; // idle workers park here; bumped on submit / fiber-ready / shutdown

            // Live count of threads inside PopJob's shared-queue dequeue. Sampled by the editor advisor
            // (only while profiling) to gauge contention on the global queue, the thing per-worker
            // deques / work-stealing would relieve. Own cache line: it must not false-share the hot
            // counters above, especially since the diagnostic itself reads/writes it from many workers.
            alignas(64) TAtomic<int32> PoppersInFlight{0};

            // Bit per worker (index < 64) set while that worker is blocked idle. The advisor reads it when
            // a fiber becomes ready: if the fiber's owner worker is idle, resume affinity could put it back
            // on its warm core for free. Editor advisor only; written only when a worker actually blocks.
            alignas(64) TAtomic<uint64> IdleWorkerMask{0};

#if USING(WITH_EDITOR)
            // Per-worker OS-core occupancy for the editor's CPU view: which logical core a worker last
            // dispatched a job on, and whether it is running one right now. Sampled at fiber dispatch.
            struct FWorkerCoreSample { TAtomic<uint32> Core{0}; TAtomic<uint8> Busy{0}; };
            FWorkerCoreSample* WorkerCores = nullptr; // [NumWorkers]
#endif
        };

        FScheduler* G = nullptr;

        bool HasWork()
        {
            return G->AvailJobs.load(std::memory_order_relaxed) > 0
                || G->ReadyCount.load(std::memory_order_relaxed) > 0
                || G->bShutdown.load(std::memory_order_acquire);
        }

        void WakeWorkers(bool All)
        {
            G->WorkSignal.Notify(All);
        }

        bool PopJob(FQueuedJob& Out, uint32 Slot)
        {
#if USING(WITH_EDITOR)
            // Sample how many threads are dequeuing the shared queue at once (contention proxy for the
            // advisor). Latch the enabled flag so the inc/dec pair stays balanced across a mid-call toggle.
            const bool Diag = FJobProfiler::Get().IsEnabled();
            if (Diag)
            {
                const uint32 Conc = static_cast<uint32>(G->PoppersInFlight.fetch_add(1, std::memory_order_relaxed)) + 1u;
                FJobProfiler::Get().NotePop(Conc);
            }
#endif
            bool bGot = false;
            for (int P = 0; P < 3; ++P)
            {
                if (G->JobQueues[P].try_dequeue(G->ConsTokens[P][Slot], Out))
                {
                    G->AvailJobs.fetch_sub(1, std::memory_order_relaxed);
                    bGot = true;
                    break;
                }
            }
#if USING(WITH_EDITOR)
            if (Diag)
            {
                G->PoppersInFlight.fetch_sub(1, std::memory_order_relaxed);
            }
#endif
            return bGot;
        }

        void PushReady(FWorkFiber* Fiber)
        {
#if USING(WITH_EDITOR)
            Fiber->State.store(static_cast<uint8>(EFiberState::Ready), std::memory_order_relaxed);
            // Affinity opportunity.
            if (FJobProfiler::Get().IsEnabled())
            {
                const uint16 Owner = Fiber->OwnerWorker.load(std::memory_order_relaxed);
                if (Owner < 64 && (G->IdleWorkerMask.load(std::memory_order_relaxed) & (1ull << Owner)) != 0)
                {
                    FJobProfiler::Get().NoteAffinityOpportunity();
                }
            }
#endif
            G->ReadyFibers.enqueue(Fiber);
            G->ReadyCount.fetch_add(1, std::memory_order_relaxed);
            WakeWorkers(false);
        }

        void LockCounter(FCounter* C)
        {
            uint32 Expected = 0;
            while (!C->WaitLock.compare_exchange_weak(Expected, 1u, std::memory_order_acquire, std::memory_order_relaxed))
            {
                Expected = 0;
                CpuPause();
            }
        }

        void UnlockCounter(FCounter* C)
        {
            C->WaitLock.store(0u, std::memory_order_release);
        }

        // Release any waiters satisfied by a decrement and fire the one-shot completion at zero.
        void ReleaseCounter(FCounter* Counter, int32 NewValue, uint32 WorkerIndex)
        {
            // Fast path: nothing reached zero and no fiber is parked.
            if (NewValue > 0 && !Counter->HasWaiters.load(std::memory_order_seq_cst))
            {
                return;
            }

            FCompletionFn Completion = nullptr;
            void*         Ctx        = nullptr;
            FWaitNode*    Woken      = nullptr;

            LockCounter(Counter);
            {
                FWaitNode** Link = &Counter->Waiters;
                while (*Link != nullptr)
                {
                    FWaitNode* N = *Link;
                    if (Counter->Value.load(std::memory_order_seq_cst) <= N->Target)
                    {
                        *Link   = N->Next; // unlink
                        N->Next = Woken;
                        Woken   = N;
                    }
                    else
                    {
                        Link = &N->Next;
                    }
                }
                if (Counter->Waiters == nullptr)
                {
                    Counter->HasWaiters.store(false, std::memory_order_seq_cst);
                }
            }
            if (NewValue <= 0 && Counter->Completion != nullptr)
            {
                Completion          = Counter->Completion;
                Ctx                 = Counter->CompletionCtx;
                Counter->Completion = nullptr; // one-shot
            }
            UnlockCounter(Counter);

            for (FWaitNode* N = Woken; N != nullptr; )
            {
                FWaitNode* Next = N->Next;
                PushReady(N->Fiber);
                N = Next;
            }

            if (Completion != nullptr)
            {
                Completion(Ctx, WorkerIndex); // owns the counter's lifetime
            }
        }

        void OnJobComplete(FCounter* Counter, uint32 WorkerIndex)
        {
            if (Counter == nullptr)
            {
                G->InFlight.fetch_sub(1, std::memory_order_acq_rel);
                return;
            }

            const int32 NewValue = Counter->Value.fetch_sub(1, std::memory_order_seq_cst) - 1;
            G->InFlight.fetch_sub(1, std::memory_order_acq_rel);
            ReleaseCounter(Counter, NewValue, WorkerIndex);
        }

#if USING(WITH_EDITOR)
        // Stamp the OS core this worker is dispatching on / mark it (un)busy for the editor CPU view.
        void NoteWorkerCore(uint32 Worker, bool Busy)
        {
            if (G->WorkerCores == nullptr || Worker >= G->NumWorkers)
            {
                return;
            }
            if (Busy)
            {
                G->WorkerCores[Worker].Core.store(Platform::GetCurrentCoreNumber(), std::memory_order_relaxed);
            }
            G->WorkerCores[Worker].Busy.store(Busy ? 1u : 0u, std::memory_order_relaxed);
        }

        // Editor profiler glue. Fiber-state stores are unconditional (so the live grid works even when
        // span recording is off); span/event recording self-gates on FJobProfiler::IsEnabled().
        void ProfBind(FWorkFiber* F, uint32 Worker)   // fresh job bound → Running
        {
            F->State.store(static_cast<uint8>(EFiberState::Running), std::memory_order_relaxed);
            F->OwnerWorker.store(static_cast<uint16>(Worker), std::memory_order_relaxed);
            NoteWorkerCore(Worker, true);
            FJobProfiler& P = FJobProfiler::Get();
            if (P.IsEnabled())
            {
                P.SliceBegin(Worker, F->Index, F->Job.Name, FJobProfiler::NowMs());
            }
        }
        void ProfResume(FWorkFiber* F, uint32 Worker) // parked fiber resumed → Running (counts migration)
        {
            const bool Migrated = F->OwnerWorker.load(std::memory_order_relaxed) != static_cast<uint16>(Worker);
            FJobProfiler& P = FJobProfiler::Get();
            if (P.IsEnabled())
            {
                P.NoteResume(Migrated);
            }
            F->State.store(static_cast<uint8>(EFiberState::Running), std::memory_order_relaxed);
            F->OwnerWorker.store(static_cast<uint16>(Worker), std::memory_order_relaxed);
            NoteWorkerCore(Worker, true);
            if (P.IsEnabled())
            {
                P.SliceBegin(Worker, F->Index, F->Job.Name, FJobProfiler::NowMs());
            }
        }
        void ProfEnd(uint32 Worker, bool Parked)      // the fiber that just switched back stopped running
        {
            NoteWorkerCore(Worker, false);
            FJobProfiler& P = FJobProfiler::Get();
            if (P.IsEnabled())
            {
                P.SliceEnd(Worker, Parked, FJobProfiler::NowMs());
            }
        }
        void ProfSubmit(uint32 Count, bool ByWorker)  // jobs entering the queue, tagged by origin thread
        {
            FJobProfiler& P = FJobProfiler::Get();
            if (P.IsEnabled())
            {
                P.NoteSubmit(Count, ByWorker);
            }
        }
        void ProfStarvation()                         // a fresh fiber-pool starvation episode
        {
            FJobProfiler& P = FJobProfiler::Get();
            if (P.IsEnabled())
            {
                P.NoteStarvation();
            }
        }
#endif

        // Runs on the scheduler fiber after a work fiber switched back to it. Publishes the just-switched
        // fiber as parked (linked into its counter) or free. This is the ONLY place a work fiber becomes
        // resumable, guaranteeing its register/stack state is fully saved before anyone can switch it in.
        void ProcessPending()
        {
            FPendingSwitch P = TLS.Pending;
            TLS.Pending = FPendingSwitch{};

            switch (P.Action)
            {
            case EPending::None:
                return;

            case EPending::Free:
#if USING(WITH_EDITOR)
                ProfEnd(TLS.WorkerIndex, false);
                P.Fiber->State.store(static_cast<uint8>(EFiberState::Free), std::memory_order_relaxed);
#endif
                G->FreeFibers.enqueue(P.Fiber);
                return;

            case EPending::Park:
                {
#if USING(WITH_EDITOR)
                    ProfEnd(TLS.WorkerIndex, true);
#endif
                    FCounter* C = P.Counter;
                    LockCounter(C);
                    if (C->Value.load(std::memory_order_seq_cst) <= P.Node->Target)
                    {
                        // Satisfied between the fiber's fast-path check and now, resume immediately.
                        UnlockCounter(C);
                        PushReady(P.Fiber);
                    }
                    else
                    {
                        C->HasWaiters.store(true, std::memory_order_seq_cst);
                        P.Node->Next = C->Waiters;
                        C->Waiters   = P.Node;
                        UnlockCounter(C);
#if USING(WITH_EDITOR)
                        P.Fiber->State.store(static_cast<uint8>(EFiberState::Parked), std::memory_order_relaxed);
                        P.Fiber->WaitCounterId.store(C->PoolIndex, std::memory_order_relaxed);
#endif
                    }
                    return;
                }

            case EPending::ParkFn:
                {
#if USING(WITH_EDITOR)
                    ProfEnd(TLS.WorkerIndex, true);
#endif
                    // The callback links the fiber into its wait queue under that queue's own lock and
                    // returns whether it actually parked. If it declined (condition already satisfied),
                    // the fiber is immediately runnable again.
                    const bool Parked = P.ParkFn(P.ParkCtx, FFiberHandle{ P.Fiber });
                    if (!Parked)
                    {
                        PushReady(P.Fiber);
                    }
#if USING(WITH_EDITOR)
                    else
                    {
                        P.Fiber->State.store(static_cast<uint8>(EFiberState::Parked), std::memory_order_relaxed);
                        P.Fiber->WaitCounterId.store(0, std::memory_order_relaxed);
                    }
#endif
                    return;
                }
            }
        }

        void WaitForWork()
        {
            for (int Spin = 0; Spin < 512; ++Spin)
            {
                if (HasWork())
                {
                    return;
                }
                CpuPause();
            }

            // Capture the epoch, then re-check. If work (or shutdown) appeared after the spin, the
            // epoch may already differ, but the eventcount only signals "something changed", not the
            // queue contents, so re-check HasWork() explicitly and bail without blocking if satisfied.
            const uint32 Key = G->WorkSignal.PrepareWait();
            if (HasWork())
            {
                G->WorkSignal.CancelWait();
                return;
            }

#if USING(WITH_EDITOR)
            FJobProfiler::Get().IdleBegin(TLS.WorkerIndex, FJobProfiler::NowMs());
            if (TLS.WorkerIndex < 64)
            {
                G->IdleWorkerMask.fetch_or(1ull << TLS.WorkerIndex, std::memory_order_relaxed);
            }
#endif
            G->WorkSignal.Wait(Key);
#if USING(WITH_EDITOR)
            if (TLS.WorkerIndex < 64)
            {
                G->IdleWorkerMask.fetch_and(~(1ull << TLS.WorkerIndex), std::memory_order_relaxed);
            }
            FJobProfiler::Get().IdleEnd(TLS.WorkerIndex, FJobProfiler::NowMs());
#endif
        }

        // The scheduler fiber's loop. Prefers resuming parked fibers (drains in-flight work and frees
        // fibers) over binding fresh jobs.
        void SchedulerLoop()
        {
            const uint32 Slot = TLS.WorkerIndex;
            uint32 StarveSpins = 0;

            while (true)
            {
                ProcessPending();

                if (G->bShutdown.load(std::memory_order_acquire))
                {
                    break;
                }

                FWorkFiber* Ready = nullptr;
                if (G->ReadyFibers.try_dequeue(Ready))
                {
                    G->ReadyCount.fetch_sub(1, std::memory_order_relaxed);
                    TLS.CurrentFiber = Ready;
#if USING(WITH_EDITOR)
                    ProfResume(Ready, Slot);
#endif
                    TracyEnterFiber(Ready);
                    Fibers::Switch(Ready->Handle);
                    TracyLeaveFiber();
                    StarveSpins = 0;
                    continue;
                }

                // Claim a free fiber first, then a job, so a job is never popped without somewhere to run
                // it (avoids re-queue churn). Put the fiber back if there is no job.
                FWorkFiber* Free = nullptr;
                if (G->FreeFibers.try_dequeue(Free))
                {
                    FQueuedJob Job;
                    if (PopJob(Job, Slot))
                    {
                        Free->Job        = Job;
                        TLS.CurrentFiber = Free;
#if USING(WITH_EDITOR)
                        ProfBind(Free, Slot);
#endif
                        TracyEnterFiber(Free);
                        Fibers::Switch(Free->Handle);
                        TracyLeaveFiber();
                        StarveSpins = 0;
                        continue;
                    }
                    G->FreeFibers.enqueue(Free);
                }
                else if (G->AvailJobs.load(std::memory_order_relaxed) > 0)
                {
                    // Jobs pending but every fiber is parked: only forward progress is resuming a ready
                    // fiber. Surface persistent starvation rather than hanging silently.
#if USING(WITH_EDITOR)
                    if (StarveSpins == 0)
                    {
                        ProfStarvation(); // count distinct episodes, not every spin
                    }
#endif
                    if (((++StarveSpins) & 0xFFFFF) == 0)
                    {
                        LOG_WARN("Job system: fiber pool starved ({} fibers all in use, jobs pending).", G->NumWorkFibers);
                    }
                    CpuPause();
                    continue;
                }

                WaitForWork();
            }
        }

        // Entry for every pooled work fiber. Loops forever: run the bound job, switch back to be reused.
        void FiberMain(void* /*Arg*/)
        {
            while (true)
            {
                FWorkFiber* Self = TLS.CurrentFiber; // set by the scheduler before switching in
                FQueuedJob  Job  = Self->Job;

                Job.Function(Job.Argument, TLS.WorkerIndex);
                OnJobComplete(Job.Counter, TLS.WorkerIndex);

                TLS.Pending = FPendingSwitch{ EPending::Free, Self, nullptr, nullptr };
                Fibers::Switch(TLS.SchedulerFiber);
                // Resumes here when the scheduler binds a new job to this fiber and switches back in.
            }
        }

        void WorkerThreadMain(uint32 WorkerIndex)
        {
            TLS.WorkerIndex = WorkerIndex;
            TLS.bIsWorker   = true;

            char Name[32];
            (void)snprintf(Name, sizeof(Name), "Lumina Worker %u", WorkerIndex);
            Threading::SetThreadName(Name, Threading::ThreadGroup_Worker);
            Threading::SetThreadPerformanceHint();
            Memory::InitializeThreadHeap();

            TLS.SchedulerFiber = Fibers::ThreadToFiber();

            SchedulerLoop();

            Fibers::FiberToThread();
            Memory::ShutdownThreadHeap();
        }
    }

    void Initialize(const FConfig& Config)
    {
        ASSERT(G == nullptr);

        G = Memory::New<FScheduler>();

        const uint32 Hardware = Threading::GetNumThreads();
        G->NumWorkers     = Config.NumWorkerThreads != 0 ? Config.NumWorkerThreads : (Hardware > 2 ? Hardware - 1 : 1);
        G->NumExternal    = Config.NumExternalThreads != 0 ? Config.NumExternalThreads : 8;
        G->NumThreadSlots = G->NumWorkers + G->NumExternal;
        G->NumWorkFibers  = Config.NumWorkFibers != 0 ? Config.NumWorkFibers : kDefaultWorkFibers;
        G->FiberStackSize = Config.FiberStackSize != 0 ? Config.FiberStackSize : kDefaultFiberStack;

        // One consumer token per queue per thread slot. Each slot is owned by a single thread, so a
        // token is only ever touched by that thread.
        for (int P = 0; P < 3; ++P)
        {
            G->ProdTokens[P].reserve(G->NumThreadSlots);
            G->ConsTokens[P].reserve(G->NumThreadSlots);
            for (uint32 S = 0; S < G->NumThreadSlots; ++S)
            {
                G->ProdTokens[P].emplace_back(G->JobQueues[P]);
                G->ConsTokens[P].emplace_back(G->JobQueues[P]);
            }
        }

        G->CounterPool = static_cast<FCounter*>(Memory::Malloc(sizeof(FCounter) * kCounterPoolSize, alignof(FCounter)));
        for (uint32 i = 0; i < kCounterPoolSize; ++i)
        {
            FCounter* C = new (&G->CounterPool[i]) FCounter();
            C->bPooled   = true;
            C->PoolIndex = static_cast<uint16>(i);
            G->FreeCounters.enqueue(static_cast<uint16>(i));
        }

        // Build the work-fiber pool BEFORE starting workers so FreeFibers is populated when they spin up.
        G->WorkFibers = static_cast<FWorkFiber*>(Memory::Malloc(sizeof(FWorkFiber) * G->NumWorkFibers, alignof(FWorkFiber)));
        for (uint32 i = 0; i < G->NumWorkFibers; ++i)
        {
            FWorkFiber* F = new (&G->WorkFibers[i]) FWorkFiber();
#if USING(WITH_EDITOR)
            F->Index = static_cast<uint16>(i);
#endif
#if defined(TRACY_ENABLE)
            (void)snprintf(F->TracyName, sizeof(F->TracyName), "Job Fiber %u", i);
#endif
            F->Handle = Fibers::Create(G->FiberStackSize, &FiberMain, F);
            G->FreeFibers.enqueue(F);
        }

#if USING(WITH_EDITOR)
        G->WorkerCores = static_cast<FScheduler::FWorkerCoreSample*>(
            Memory::Malloc(sizeof(FScheduler::FWorkerCoreSample) * G->NumWorkers, alignof(FScheduler::FWorkerCoreSample)));
        for (uint32 i = 0; i < G->NumWorkers; ++i)
        {
            new (&G->WorkerCores[i]) FScheduler::FWorkerCoreSample();
        }
#endif

        G->WorkerThreads.reserve(G->NumWorkers);
        for (uint32 i = 0; i < G->NumWorkers; ++i)
        {
            G->WorkerThreads.emplace_back(WorkerThreadMain, i);
        }

        LOG_DISPLAY("Job system online: {} workers, {} thread slots, {} fibers ({}KB stacks).",
            G->NumWorkers, G->NumThreadSlots, G->NumWorkFibers, G->FiberStackSize / 1024);
    }

    void Shutdown()
    {
        if (G == nullptr)
        {
            return;
        }

        G->bShutdown.store(true, std::memory_order_release);
        G->WorkSignal.Notify(true);

        for (FThread& Thread : G->WorkerThreads)
        {
            if (Thread.joinable())
            {
                Thread.join();
            }
        }

        // Workers have joined, so no thread has any work fiber switched in. Safe to delete them here.
        for (uint32 i = 0; i < G->NumWorkFibers; ++i)
        {
            Fibers::Destroy(G->WorkFibers[i].Handle);
            G->WorkFibers[i].~FWorkFiber();
        }
        void* FiberMem = G->WorkFibers;
        Memory::Free(FiberMem);

#if USING(WITH_EDITOR)
        if (G->WorkerCores != nullptr)
        {
            for (uint32 i = 0; i < G->NumWorkers; ++i)
            {
                G->WorkerCores[i].~FWorkerCoreSample();
            }
            Memory::Free(G->WorkerCores);
        }
#endif

        void* PoolMem = G->CounterPool;
        Memory::Free(PoolMem);

        Memory::Delete(G);
        G = nullptr;
    }

    bool IsInitialized() { return G != nullptr; }

    uint32 GetNumWorkers()     { return G ? G->NumWorkers     : 0; }
    uint32 GetNumThreadSlots() { return G ? G->NumThreadSlots : 1; }
    bool   IsWorkerThread()    { return TLS.bIsWorker; }

    uint32 GetWorkerIndex()
    {
        if (TLS.WorkerIndex != ~0u)
        {
            return TLS.WorkerIndex;
        }
        // Stray thread running a job inline: lazily claim an external slot.
        uint32 Slot = G->NextExternalSlot.fetch_add(1, std::memory_order_relaxed);
        if (Slot >= G->NumExternal)
        {
            Slot = G->NumExternal - 1;
        }
        TLS.WorkerIndex = G->NumWorkers + Slot;
        return TLS.WorkerIndex;
    }

    uint32 RegisterExternalThread()
    {
        uint32 Slot = G->NextExternalSlot.fetch_add(1, std::memory_order_relaxed);
        if (Slot >= G->NumExternal)
        {
            Slot = G->NumExternal - 1;
        }
        TLS.WorkerIndex = G->NumWorkers + Slot;
        TLS.bIsWorker   = false;
        return TLS.WorkerIndex;
    }

    void UnregisterExternalThread()
    {
        TLS.WorkerIndex = ~0u;
    }

    FCounter* AllocCounter(int32 InitialValue)
    {
        FCounter* Counter;
        uint16 Index;
        if (G->FreeCounters.try_dequeue(Index))
        {
            Counter = &G->CounterPool[Index];
        }
        else
        {
            Counter = Memory::New<FCounter>();
            Counter->bPooled   = false;
            Counter->PoolIndex = 0xFFFF;
        }

        Counter->Value.store(InitialValue, std::memory_order_relaxed);
        Counter->Completion    = nullptr;
        Counter->CompletionCtx = nullptr;
        Counter->Waiters = nullptr;
        Counter->WaitLock.store(0u, std::memory_order_relaxed);
        Counter->HasWaiters.store(false, std::memory_order_relaxed);
        return Counter;
    }

    void FreeCounter(FCounter* Counter)
    {
        if (Counter == nullptr)
        {
            return;
        }
        if (Counter->bPooled)
        {
            G->FreeCounters.enqueue(Counter->PoolIndex);
        }
        else
        {
            Memory::Delete(Counter);
        }
    }

    int32 GetCounterValue(const FCounter* Counter)
    {
        return Counter ? Counter->Value.load(std::memory_order_acquire) : 0;
    }

    void SetCounterCompletion(FCounter* Counter, FCompletionFn Fn, void* Ctx)
    {
        Counter->Completion    = Fn;
        Counter->CompletionCtx = Ctx;
    }

    void RunJobs(const FJobDecl* Jobs, uint32 Count, EJobPriority Priority, FCounter* Counter)
    {
        if (Count == 0)
        {
            return;
        }
        
        LUMINA_PROFILE_SCOPE();

#if USING(WITH_EDITOR)
        ProfSubmit(Count, TLS.bIsWorker); // tag fork-join (worker) vs externally-fed work for the advisor
#endif

        if (Counter != nullptr)
        {
            Counter->Value.fetch_add(static_cast<int32>(Count), std::memory_order_acq_rel);
        }
        G->InFlight.fetch_add(static_cast<int64>(Count), std::memory_order_acq_rel);

        const int Prio  = static_cast<int>(Priority);
        FJobQueue& Queue = G->JobQueues[Prio];
        // Producer token for this thread's slot, one slot per thread, same invariant the consumer
        // tokens rely on, so the token is never touched concurrently.
        moodycamel::ProducerToken& Tok = G->ProdTokens[Prio][GetWorkerIndex()];

        if (Count == 1)
        {
            // Single-job fast path
            FQueuedJob Job;
            Job.Function = Jobs[0].Function;
            Job.Argument = Jobs[0].Argument;
            Job.Counter  = Counter;
#if USING(WITH_EDITOR)
            Job.Name     = Jobs[0].Name;
#endif
            Queue.enqueue(Tok, Job);
        }
        else
        {
            // Bulk-enqueue: building items into a stack batch and pushing them with one bulk op amortizes
            // moodycamel's per-item block/atomic bookkeeping over the whole batch.
            constexpr uint32 kBatch = 256;
            FQueuedJob Batch[kBatch];
            for (uint32 Base = 0; Base < Count; Base += kBatch)
            {
                const uint32 N = (Count - Base) < kBatch ? (Count - Base) : kBatch;
                for (uint32 i = 0; i < N; ++i)
                {
                    Batch[i].Function = Jobs[Base + i].Function;
                    Batch[i].Argument = Jobs[Base + i].Argument;
                    Batch[i].Counter  = Counter;
#if USING(WITH_EDITOR)
                    Batch[i].Name     = Jobs[Base + i].Name;
#endif
                }
                Queue.enqueue_bulk(Tok, Batch, N);
            }
        }
        G->AvailJobs.fetch_add(static_cast<int64>(Count), std::memory_order_relaxed);

        WakeWorkers(Count > 1);
    }

    void RunJob(FJobFunction Fn, void* Arg, EJobPriority Priority, FCounter* Counter, const char* Name)
    {
        FJobDecl Decl{ Fn, Arg, Name };
        RunJobs(&Decl, 1, Priority, Counter);
    }

    void DecrementCounter(FCounter* Counter, int32 By)
    {
        if (Counter == nullptr)
        {
            return;
        }
        const int32 NewValue = Counter->Value.fetch_sub(By, std::memory_order_seq_cst) - By;
        ReleaseCounter(Counter, NewValue, GetWorkerIndex());
    }

    void WaitForCounter(FCounter* Counter, int32 Value)
    {
        if (Counter == nullptr)
        {
            return;
        }

        if (Counter->Value.load(std::memory_order_acquire) <= Value)
        {
            return; // fast path
        }

        if (TLS.bIsWorker)
        {
            // Park and yield to the scheduler, which links us into the counter (see
            // ProcessPending). The releasing decrement touches nothing of a waited (no-completion)
            // counter once we are spliced out, so the caller may reclaim it the instant this returns.
            FWaitNode Node;
            Node.Fiber  = TLS.CurrentFiber;
            Node.Target = Value;
            Node.Next   = nullptr;

#if USING(WITH_EDITOR)
            { FJobProfiler& Prof = FJobProfiler::Get(); if (Prof.IsEnabled())
                {
                    Prof.NotePark();
                }
            }
#endif
            TLS.Pending = FPendingSwitch{ EPending::Park, TLS.CurrentFiber, Counter, &Node };
            Fibers::Switch(TLS.SchedulerFiber);
            // Resumed here once satisfied, possibly on a different worker thread.
            return;
        }

        // External thread (not on a fiber): assist-wait, run queued jobs inline until satisfied.
        const uint32 Slot = GetWorkerIndex();
        uint32 IdleSpins = 0;
        while (Counter->Value.load(std::memory_order_acquire) > Value)
        {
            FQueuedJob Job;
            if (PopJob(Job, Slot))
            {
                Job.Function(Job.Argument, Slot);
                OnJobComplete(Job.Counter, Slot);
                IdleSpins = 0;
            }
            else if (++IdleSpins < 256)
            {
                CpuPause();
            }
            else
            {
                Threading::ThreadYield();
                IdleSpins = 0;
            }
        }
    }

    void WaitForAll()
    {
        if (G == nullptr)
        {
            return;
        }
        // A worker fiber busy-spinning here would block its own scheduler from running the very jobs it
        // waits on. WaitForAll is an external-thread (main) drain point.
        ASSERT(!TLS.bIsWorker);
        while (G->InFlight.load(std::memory_order_acquire) > 0)
        {
            Threading::ThreadYield();
        }
    }

    void ParkFiber(FParkFn OnPark, void* Ctx)
    {
        // Worker fibers only. An external thread has no fiber to suspend and must assist-wait instead.
        ASSERT(TLS.bIsWorker && TLS.CurrentFiber != nullptr);

#if USING(WITH_EDITOR)
        { FJobProfiler& Prof = FJobProfiler::Get(); if (Prof.IsEnabled()) Prof.NotePark(); }
#endif
        TLS.Pending         = FPendingSwitch{};
        TLS.Pending.Action  = EPending::ParkFn;
        TLS.Pending.Fiber   = TLS.CurrentFiber;
        TLS.Pending.ParkFn  = OnPark;
        TLS.Pending.ParkCtx = Ctx;
        Fibers::Switch(TLS.SchedulerFiber);
        // Resumed here once ResumeFiber was called for us, possibly on a different worker thread.
    }

    void ResumeFiber(FFiberHandle Handle)
    {
        if (Handle.Fiber == nullptr)
        {
            return;
        }
        PushReady(static_cast<FWorkFiber*>(Handle.Fiber));
    }

    bool AssistOneJob()
    {
        if (G == nullptr)
        {
            return false;
        }
        const uint32 Slot = GetWorkerIndex();
        FQueuedJob Job;
        if (PopJob(Job, Slot))
        {
            Job.Function(Job.Argument, Slot);
            OnJobComplete(Job.Counter, Slot);
            return true;
        }
        return false;
    }

    void GetLiveStats(FJobLiveStats& Out)
    {
        Out = FJobLiveStats{};
        if (G == nullptr)
        {
            return;
        }
        Out.NumWorkers    = G->NumWorkers;
        Out.NumWorkFibers = G->NumWorkFibers;
        Out.FibersFree    = static_cast<uint32>(G->FreeFibers.size_approx());
        const int64 Ready = G->ReadyCount.load(std::memory_order_relaxed);
        Out.FibersReady   = Ready > 0 ? static_cast<uint32>(Ready) : 0;
        const uint32 NonRunning = Out.FibersFree + Out.FibersReady;
        Out.FibersInUse   = NonRunning < Out.NumWorkFibers ? Out.NumWorkFibers - NonRunning : 0;
        for (int P = 0; P < 3; ++P)
        {
            Out.QueueDepth[P] = static_cast<uint32>(G->JobQueues[P].size_approx());
        }
        Out.InFlight = G->InFlight.load(std::memory_order_relaxed);
    }

    void SnapshotFiberStates(TVector<FFiberState>& Out)
    {
        Out.clear();
#if USING(WITH_EDITOR)
        if (G == nullptr || G->WorkFibers == nullptr)
        {
            return;
        }
        Out.reserve(G->NumWorkFibers);
        for (uint32 i = 0; i < G->NumWorkFibers; ++i)
        {
            FWorkFiber& F = G->WorkFibers[i];
            FFiberState S;
            S.Index         = F.Index;
            S.State         = static_cast<EFiberState>(F.State.load(std::memory_order_relaxed));
            S.OwnerWorker   = F.OwnerWorker.load(std::memory_order_relaxed);
            S.WaitCounterId = F.WaitCounterId.load(std::memory_order_relaxed);
            S.Name          = F.Job.Name;
            Out.push_back(S);
        }
#endif
    }

    void SnapshotActiveCounters(TVector<FCounterState>& Out)
    {
        Out.clear();
#if USING(WITH_EDITOR)
        if (G == nullptr || G->CounterPool == nullptr)
        {
            return;
        }
        for (uint32 i = 0; i < kCounterPoolSize; ++i)
        {
            FCounter& C = G->CounterPool[i];
            if (!C.HasWaiters.load(std::memory_order_acquire))
            {
                continue;
            }
            uint32 Waiters = 0;
            LockCounter(&C);
            for (FWaitNode* N = C.Waiters; N != nullptr; N = N->Next)
            {
                ++Waiters;
            }
            UnlockCounter(&C);
            if (Waiters > 0)
            {
                FCounterState S;
                S.Id            = i;
                S.Value         = C.Value.load(std::memory_order_relaxed);
                S.ParkedWaiters = Waiters;
                Out.push_back(S);
            }
        }
#endif
    }

    void SnapshotWorkerCores(TVector<FWorkerCoreState>& Out)
    {
        Out.clear();
#if USING(WITH_EDITOR)
        if (G == nullptr || G->WorkerCores == nullptr)
        {
            return;
        }
        Out.reserve(G->NumWorkers);
        for (uint32 i = 0; i < G->NumWorkers; ++i)
        {
            FWorkerCoreState S;
            S.Worker = i;
            S.Core   = G->WorkerCores[i].Core.load(std::memory_order_relaxed);
            S.bBusy  = G->WorkerCores[i].Busy.load(std::memory_order_relaxed) != 0;
            Out.push_back(S);
        }
#endif
    }
}
