#include "pch.h"
#include "PhysicsThread.h"

#include "Core/Assertions/Assert.h"
#include "Core/Profiler/Profile.h"
#include "Core/Templates/LuminaTemplate.h"
#include "TaskSystem/TaskSystem.h"


namespace Lumina
{
    RUNTIME_API FPhysicsThread* GPhysicsThread = nullptr;

    FPhysicsThread& FPhysicsThread::Get()
    {
        DEBUG_ASSERT(GPhysicsThread != nullptr);
        return *GPhysicsThread;
    }

    FPhysicsThread::FPhysicsThread()
    {
        PendingCommands.reserve(64);
    }

    FPhysicsThread::~FPhysicsThread()
    {
        Stop();
    }

    void FPhysicsThread::Start()
    {
        if (bWorkerRunning.load(Atomic::MemoryOrderAcquire))
        {
            return;
        }

        bExitRequested.store(false, Atomic::MemoryOrderRelease);
        bWorkerRunning.store(true, Atomic::MemoryOrderRelease);

        Worker = std::thread([this]() { WorkerMain(); });
    }

    void FPhysicsThread::Stop()
    {
        if (!bWorkerRunning.load(Atomic::MemoryOrderAcquire))
        {
            return;
        }

        Flush();

        {
            std::unique_lock Lock(QueueMutex);
            bExitRequested.store(true, Atomic::MemoryOrderRelease);
            QueueCV.notify_all();
        }

        if (Worker.joinable())
        {
            Worker.join();
        }

        bWorkerRunning.store(false, Atomic::MemoryOrderRelease);
        Threading::SetPhysicsThread(std::thread::id{});
    }

    void FPhysicsThread::Enqueue(const char* DebugName, FCommand&& Cmd)
    {
        if (!bWorkerRunning.load(Atomic::MemoryOrderAcquire))
        {
            CommandsEnqueued.fetch_add(1, Atomic::MemoryOrderAcqRel);
            RunCommand(DebugName, Cmd);
            CommandsCompleted.fetch_add(1, Atomic::MemoryOrderAcqRel);
            return;
        }

        {
            std::unique_lock Lock(QueueMutex);
            PendingCommands.push_back({ DebugName, Move(Cmd) });
            CommandsEnqueued.fetch_add(1, Atomic::MemoryOrderAcqRel);
        }
        QueueCV.notify_one();
    }

    void FPhysicsThread::EnqueueAndWait(const char* DebugName, FCommand&& Cmd)
    {
        if (!bWorkerRunning.load(Atomic::MemoryOrderAcquire) || Threading::IsPhysicsThread())
        {
            CommandsEnqueued.fetch_add(1, Atomic::MemoryOrderAcqRel);
            RunCommand(DebugName, Cmd);
            CommandsCompleted.fetch_add(1, Atomic::MemoryOrderAcqRel);
            return;
        }

        FPhysicsCommandFence Fence;
        Enqueue(DebugName, Move(Cmd));
        Fence.BeginFence();
        Fence.Wait();
    }

    void FPhysicsThread::Flush()
    {
        if (!bWorkerRunning.load(Atomic::MemoryOrderAcquire))
        {
            return;
        }
        if (Threading::IsPhysicsThread())
        {
            return;
        }

        LUMINA_PROFILE_SCOPE();

        WaitForCounter(CommandsEnqueued.load(Atomic::MemoryOrderAcquire));
    }

    void FPhysicsThread::WaitForCounter(uint64 Target)
    {
        if (!bWorkerRunning.load(Atomic::MemoryOrderAcquire) || Threading::IsPhysicsThread())
        {
            return;
        }

        std::unique_lock Lock(IdleMutex);
        IdleCV.wait(Lock, [this, Target]()
        {
            return CommandsCompleted.load(Atomic::MemoryOrderAcquire) >= Target;
        });
    }

    void FPhysicsThread::WorkerMain()
    {
        Threading::SetPhysicsThread(std::this_thread::get_id());
        Threading::SetThreadName("Lumina Physics");
        Threading::InitializeThreadHeap();

        if (GTaskSystem != nullptr)
        {
            GTaskSystem->GetScheduler().RegisterExternalTaskThread();
        }

        TVector<FQueuedCommand> Batch;
        Batch.reserve(64);

        while (true)
        {
            {
                std::unique_lock Lock(QueueMutex);
                QueueCV.wait(Lock, [this]()
                {
                    return !PendingCommands.empty() || bExitRequested.load(Atomic::MemoryOrderAcquire);
                });

                if (bExitRequested.load(Atomic::MemoryOrderAcquire) && PendingCommands.empty())
                {
                    break;
                }

                Batch.swap(PendingCommands);
            }

            for (FQueuedCommand& Q : Batch)
            {
                RunCommand(Q.DebugName, Q.Cmd);
                CommandsCompleted.fetch_add(1, Atomic::MemoryOrderAcqRel);
            }
            Batch.clear();

            {
                std::unique_lock Lock(IdleMutex);
                IdleCV.notify_all();
            }
        }

        if (GTaskSystem != nullptr)
        {
            GTaskSystem->GetScheduler().DeRegisterExternalTaskThread();
        }

        Threading::ShutdownThreadHeap();
    }

    void FPhysicsThread::RunCommand(const char* DebugName, FCommand& Cmd)
    {
        LUMINA_PROFILE_SCOPE_COLORED(tracy::Color::DarkOliveGreen);
        LUMINA_PROFILE_TAG(DebugName);
        Cmd();
    }


    void FPhysicsCommandFence::BeginFence()
    {
        TargetCounter = GPhysicsThread != nullptr
            ? GPhysicsThread->EnqueuedCount()
            : 0;
    }

    void FPhysicsCommandFence::Wait()
    {
        if (TargetCounter == 0 || GPhysicsThread == nullptr)
        {
            return;
        }

        LUMINA_PROFILE_SCOPE();
        GPhysicsThread->WaitForCounter(TargetCounter);
    }

    bool FPhysicsCommandFence::IsComplete() const
    {
        if (TargetCounter == 0 || GPhysicsThread == nullptr)
        {
            return true;
        }
        return GPhysicsThread->CompletedCount() >= TargetCounter;
    }
}
