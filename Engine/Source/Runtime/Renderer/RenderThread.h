#pragma once
#include "Containers/Function.h"
#include "Containers/Array.h"
#include "Core/Threading/Atomic.h"
#include "Core/Threading/Thread.h"
#include "Memory/SmartPtr.h"
#include "ModuleAPI.h"

#include <condition_variable>


namespace Lumina::Jobs { struct FCounter; }

namespace Lumina
{
    // Owns the strict-FIFO queue of game-thread-enqueued render commands. As of the fiber migration
    // (Phase 2) there is NO dedicated render OS thread: commands drain on a single auto-arming pool job
    // (the "render drain"), so only one drain runs at a time, FIFO + single-threaded recording/submit
    // are preserved, but the work rides a pool worker instead of a 31st thread. When the system is down
    // (before Start, after Stop) Enqueue runs inline on the caller.
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

        bool IsRunning() const { return bRunning.load(Atomic::MemoryOrderAcquire); }

        // True when the caller is executing inside the render drain (i.e. inside a render command).
        // Replaces the old OS-thread-id Threading::IsRenderThread() check, which is meaningless now
        // that the drain rides a pool worker. The serial drain does not migrate, so a thread_local is
        // valid; revisit if render-side recording ever yields to the scheduler (Phase 4).
        static bool IsInRenderStage();

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

        void RunCommand(const char* DebugName, FCommand& Cmd);
        void ArmDrain();
        void DrainLoop();
        static void DrainEntry(void* Arg, uint32 WorkerIndex);

        struct FQueuedCommand
        {
            const char* DebugName;
            FCommand    Cmd;
        };

        TAtomic<bool>               bRunning = false;

        FMutex                      QueueMutex;
        TVector<FQueuedCommand>     PendingCommands;
        TAtomic<bool>               bDrainActive = false;     // exactly one drain job at a time
        Jobs::FCounter*             DrainCounter = nullptr;   // tracks the in-flight drain job (for Stop)

        FMutex                      IdleMutex;                // Flush waits here on CommandsCompleted
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
