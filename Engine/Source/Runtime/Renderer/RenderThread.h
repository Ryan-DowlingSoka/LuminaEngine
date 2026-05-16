#pragma once
#include "Containers/Function.h"
#include "Core/Threading/Atomic.h"
#include "Core/Threading/Thread.h"
#include "Memory/SmartPtr.h"
#include "ModuleAPI.h"

#include <condition_variable>


namespace Lumina
{
    // Render-thread worker. Owns the queue of game-thread-enqueued commands.
    // When the worker is down (boot CVar disabled, before Start, after Stop)
    // Enqueue runs inline on the caller -- callers never need to special-case.
    class RUNTIME_API FRenderThread
    {
    public:

        // Move-only so render commands can capture TUniquePtr snapshots.
        using FCommand = TMoveOnlyFunction<void()>;

        FRenderThread();
        ~FRenderThread();
        LE_NO_COPYMOVE(FRenderThread);

        void Start();
        void Stop();

        bool IsRunning() const { return bWorkerRunning.load(Atomic::MemoryOrderAcquire); }

        // DebugName must be a string literal (lifetime not extended).
        void Enqueue(const char* DebugName, FCommand&& Cmd);

        // Push, then block until done. Use for game-thread paths that need the
        // command's GPU effect before proceeding (WaitIdle, screenshot, etc.).
        void EnqueueAndWait(const char* DebugName, FCommand&& Cmd);

        // Block the caller until the queue drains. Equivalent to FlushRenderingCommands.
        void Flush();

        uint64 EnqueuedCount() const { return CommandsEnqueued.load(Atomic::MemoryOrderAcquire); }
        uint64 CompletedCount() const { return CommandsCompleted.load(Atomic::MemoryOrderAcquire); }

        void WaitForCounter(uint64 Target);

        static FRenderThread& Get();

    private:

        void WorkerMain();

        void RunCommand(const char* DebugName, FCommand& Cmd);

        struct FQueuedCommand
        {
            const char* DebugName;
            FCommand    Cmd;
        };

        std::thread                 Worker;
        TAtomic<bool>               bWorkerRunning = false;
        TAtomic<bool>               bExitRequested = false;

        FMutex                      QueueMutex;
        std::condition_variable     QueueCV;
        TVector<FQueuedCommand>     PendingCommands;

        FMutex                      IdleMutex;
        std::condition_variable     IdleCV;

        TAtomic<uint64>             CommandsEnqueued = 0;
        TAtomic<uint64>             CommandsCompleted = 0;
    };

    RUNTIME_API extern FRenderThread* GRenderThread;


    // Reusable game-thread sync primitive. BeginFence captures the current
    // queue counter; Wait blocks until the render thread reaches it.
    class RUNTIME_API FRenderCommandFence
    {
    public:

        FRenderCommandFence() = default;
        LE_NO_COPYMOVE(FRenderCommandFence);

        void BeginFence();
        void Wait();
        bool IsComplete() const;

    private:

        uint64 TargetCounter = 0;
    };


    // Backing for ENQUEUE_RENDER_COMMAND. Lets call sites read as
    //     ENQUEUE_RENDER_COMMAND(Name)([captures](){ ... });
    class FRenderCommandEnqueuer
    {
    public:

        explicit FRenderCommandEnqueuer(const char* InName) : DebugName(InName) {}

        template <typename Lambda>
        void operator()(Lambda&& L) const
        {
            FRenderThread::Get().Enqueue(DebugName, FRenderThread::FCommand(std::forward<Lambda>(L)));
        }

    private:

        const char* DebugName;
    };

    inline FRenderCommandEnqueuer MakeRenderCommandEnqueuer(const char* Name)
    {
        return FRenderCommandEnqueuer(Name);
    }

    // Game-thread only. Reserved for level travel, screenshot, resource resize, shutdown.
    inline void FlushRenderingCommands()
    {
        if (GRenderThread)
        {
            GRenderThread->Flush();
        }
    }
}


#define ENQUEUE_RENDER_COMMAND(TypeName) \
    ::Lumina::MakeRenderCommandEnqueuer(#TypeName)
