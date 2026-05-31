#include "pch.h"
#include "JobScheduler.h"

#include "Core/Threading/Thread.h"
#include "Core/Threading/Atomic.h"
#include "Core/Threading/Fiber.h"
#include "Memory/Memory.h"
#include "Memory/MemoryConcurrentQueue.h"
#include "Containers/Array.h"
#include "Log/Log.h"

#include <intrin.h>
#include <condition_variable>
#include <chrono>
#include <cstdio>

// Fiber scheduler with assist-wait fallback for external threads. One worker thread per core drains
// lock-free MPMC job queues; each job runs on a pooled user-mode fiber. A worker WaitForCounter parks
// the running fiber and switches the worker to other runnable work, resuming the fiber later (possibly
// on a different worker). External threads (main/render/physics) are not on fibers — they assist-wait,
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
        };

        // A pooled fiber jobs run on. Long-lived: it loops running one bound job then switching back to
        // the scheduler to be reused. Migrates between workers when a job parks and resumes elsewhere.
        struct FWorkFiber
        {
            Fibers::FFiber Handle = nullptr;
            FQueuedJob     Job{};   // bound by the scheduler immediately before switching in
        };

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

        // Deferred action the scheduler fiber performs AFTER a work fiber has switched away — the work
        // fiber is only ever made resumable (parked or freed) here, once its context is fully saved.
        enum class EPending : uint8 { None, Park, Free };

        struct FPendingSwitch
        {
            EPending    Action  = EPending::None;
            FWorkFiber* Fiber   = nullptr; // the work fiber we just switched away from
            FCounter*   Counter = nullptr; // park target
            FWaitNode*  Node    = nullptr; // park node (on the parking fiber's stack)
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

        struct FScheduler
        {
            uint32 NumWorkers     = 0;
            uint32 NumExternal    = 0;
            uint32 NumThreadSlots = 0;
            uint32 NumWorkFibers  = 0;
            uint32 FiberStackSize = 0;

            TVector<FThread> WorkerThreads;

            FJobQueue      JobQueues[3];
            // Per-thread-slot moodycamel consumer tokens (declared AFTER the queues so they are
            // destroyed first). Each slot maps to one thread, so a token is never used concurrently.
            TVector<moodycamel::ConsumerToken> ConsTokens[3];
            TAtomic<int64> AvailJobs{0};   // queued, not yet popped (idle-wake hint)
            TAtomic<int64> InFlight{0};    // submitted, not yet completed (WaitForAll)

            FWorkFiber*    WorkFibers = nullptr; // pool storage
            FFiberQueue    FreeFibers;           // idle, ready to be bound to a job
            FFiberQueue    ReadyFibers;          // parked fibers whose counter is now satisfied
            TAtomic<int64> ReadyCount{0};        // ReadyFibers size hint (idle-wake)

            FCounter*       CounterPool = nullptr;
            FIndexQueue     FreeCounters;

            TAtomic<uint32> NextExternalSlot{0};
            TAtomic<bool>   bShutdown{false};

            FMutex                  IdleMutex;
            std::condition_variable IdleCV;
            TAtomic<int32>          IdleWorkers{0};
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
            if (G->IdleWorkers.load(std::memory_order_relaxed) > 0)
            {
                if (All) G->IdleCV.notify_all();
                else     G->IdleCV.notify_one();
            }
        }

        bool PopJob(FQueuedJob& Out, uint32 Slot)
        {
            for (int P = 0; P < 3; ++P)
            {
                if (G->JobQueues[P].try_dequeue(G->ConsTokens[P][Slot], Out))
                {
                    G->AvailJobs.fetch_sub(1, std::memory_order_relaxed);
                    return true;
                }
            }
            return false;
        }

        void PushReady(FWorkFiber* Fiber)
        {
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

        // Release any waiters satisfied by a decrement and fire the one-shot completion at zero. Pure
        // queue ops + callback — never switches fibers, so it is safe to call from an external thread.
        // NewValue is the post-decrement counter value.
        void ReleaseCounter(FCounter* Counter, int32 NewValue, uint32 WorkerIndex)
        {
            // Fast path: nothing reached zero and no fiber is parked. The HasWaiters load is seq_cst so
            // it forms a single total order with the parker (which stores HasWaiters then re-reads Value
            // under the lock) — that closes the StoreLoad window the only reorder x86 permits.
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
                G->FreeFibers.enqueue(P.Fiber);
                return;

            case EPending::Park:
                {
                    FCounter* C = P.Counter;
                    LockCounter(C);
                    if (C->Value.load(std::memory_order_seq_cst) <= P.Node->Target)
                    {
                        // Satisfied between the fiber's fast-path check and now — resume immediately.
                        UnlockCounter(C);
                        PushReady(P.Fiber);
                    }
                    else
                    {
                        C->HasWaiters.store(true, std::memory_order_seq_cst);
                        P.Node->Next = C->Waiters;
                        C->Waiters   = P.Node;
                        UnlockCounter(C);
                    }
                    return;
                }
            }
        }

        void WaitForWork()
        {
            for (int Spin = 0; Spin < 512; ++Spin)
            {
                if (HasWork()) return;
                CpuPause();
            }

            std::unique_lock<FMutex> Lock(G->IdleMutex);
            G->IdleWorkers.fetch_add(1, std::memory_order_relaxed);
            G->IdleCV.wait_for(Lock, std::chrono::milliseconds(1), [] { return HasWork(); });
            G->IdleWorkers.fetch_sub(1, std::memory_order_relaxed);
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
                    Fibers::Switch(Ready->Handle);
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
                        Fibers::Switch(Free->Handle);
                        StarveSpins = 0;
                        continue;
                    }
                    G->FreeFibers.enqueue(Free);
                }
                else if (G->AvailJobs.load(std::memory_order_relaxed) > 0)
                {
                    // Jobs pending but every fiber is parked: only forward progress is resuming a ready
                    // fiber. Surface persistent starvation rather than hanging silently.
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
            Threading::SetThreadName(Name);
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
            G->ConsTokens[P].reserve(G->NumThreadSlots);
            for (uint32 S = 0; S < G->NumThreadSlots; ++S)
            {
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
            F->Handle = Fibers::Create(G->FiberStackSize, &FiberMain, F);
            G->FreeFibers.enqueue(F);
        }

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
        {
            std::unique_lock<FMutex> Lock(G->IdleMutex);
            G->IdleCV.notify_all();
        }

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
        // Defensively clear wait state — a recycled pooled counter must not carry a stale list/flag.
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

        if (Counter != nullptr)
        {
            Counter->Value.fetch_add(static_cast<int32>(Count), std::memory_order_acq_rel);
        }
        G->InFlight.fetch_add(static_cast<int64>(Count), std::memory_order_acq_rel);

        // Bulk-enqueue: building items into a stack batch and pushing them with one bulk op amortizes
        // moodycamel's per-item block/atomic bookkeeping over the whole batch.
        FJobQueue& Queue = G->JobQueues[static_cast<int>(Priority)];
        constexpr uint32 kBatch = 256;
        FQueuedJob Batch[kBatch];
        for (uint32 Base = 0; Base < Count; Base += kBatch)
        {
            const uint32 N = (Count - Base) < kBatch ? (Count - Base) : kBatch;
            for (uint32 i = 0; i < N; ++i)
            {
                Batch[i] = FQueuedJob{ Jobs[Base + i].Function, Jobs[Base + i].Argument, Counter };
            }
            Queue.enqueue_bulk(Batch, N);
        }
        G->AvailJobs.fetch_add(static_cast<int64>(Count), std::memory_order_relaxed);

        WakeWorkers(Count > 1);
    }

    void RunJob(FJobFunction Fn, void* Arg, EJobPriority Priority, FCounter* Counter)
    {
        FJobDecl Decl{ Fn, Arg };
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

            TLS.Pending = FPendingSwitch{ EPending::Park, TLS.CurrentFiber, Counter, &Node };
            Fibers::Switch(TLS.SchedulerFiber);
            // Resumed here once satisfied — possibly on a different worker thread.
            return;
        }

        // External thread (not on a fiber): assist-wait — run queued jobs inline until satisfied.
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
}
