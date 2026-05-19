#pragma once
#include "Containers/Function.h"
#include "Core/Threading/Atomic.h"
#include "Core/Threading/Thread.h"
#include "Memory/SmartPtr.h"
#include "ModuleAPI.h"

#include <condition_variable>


namespace Lumina
{
    class RUNTIME_API FPhysicsThread
    {
    public:

        using FCommand = TMoveOnlyFunction<void()>;

        FPhysicsThread();
        ~FPhysicsThread();
        LE_NO_COPYMOVE(FPhysicsThread);

        void Start();
        void Stop();

        bool IsRunning() const { return bWorkerRunning.load(Atomic::MemoryOrderAcquire); }

        // DebugName must outlive the call (string literal).
        void Enqueue(const char* DebugName, FCommand&& Cmd);
        void EnqueueAndWait(const char* DebugName, FCommand&& Cmd);
        void Flush();

        uint64 EnqueuedCount() const { return CommandsEnqueued.load(Atomic::MemoryOrderAcquire); }
        uint64 CompletedCount() const { return CommandsCompleted.load(Atomic::MemoryOrderAcquire); }

        void WaitForCounter(uint64 Target);

        static FPhysicsThread& Get();

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

    RUNTIME_API extern FPhysicsThread* GPhysicsThread;


    class RUNTIME_API FPhysicsCommandFence
    {
    public:

        FPhysicsCommandFence() = default;
        LE_NO_COPYMOVE(FPhysicsCommandFence);

        void BeginFence();
        void Wait();
        bool IsComplete() const;

    private:

        uint64 TargetCounter = 0;
    };
}
