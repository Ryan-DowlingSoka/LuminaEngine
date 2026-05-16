#include "pch.h"
#include "RenderThread.h"

#include "Core/Assertions/Assert.h"
#include "Core/Profiler/Profile.h"
#include "Core/Templates/LuminaTemplate.h"
#include "TaskSystem/TaskSystem.h"


namespace Lumina
{
    RUNTIME_API FRenderThread* GRenderThread = nullptr;

    FRenderThread& FRenderThread::Get()
    {
        DEBUG_ASSERT(GRenderThread != nullptr);
        return *GRenderThread;
    }

    FRenderThread::FRenderThread()
    {
        PendingCommands.reserve(256);
    }

    FRenderThread::~FRenderThread()
    {
        Stop();
    }

    void FRenderThread::Start()
    {
        if (bWorkerRunning.load(Atomic::MemoryOrderAcquire))
        {
            return;
        }

        bExitRequested.store(false, Atomic::MemoryOrderRelease);
        bWorkerRunning.store(true, Atomic::MemoryOrderRelease);

        Worker = std::thread([this]() { WorkerMain(); });
    }

    void FRenderThread::Stop()
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
        Threading::SetRenderThread(std::thread::id{});
    }

    void FRenderThread::Enqueue(const char* DebugName, FCommand&& Cmd)
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

    void FRenderThread::EnqueueAndWait(const char* DebugName, FCommand&& Cmd)
    {
        if (!bWorkerRunning.load(Atomic::MemoryOrderAcquire) || Threading::IsRenderThread())
        {
            CommandsEnqueued.fetch_add(1, Atomic::MemoryOrderAcqRel);
            RunCommand(DebugName, Cmd);
            CommandsCompleted.fetch_add(1, Atomic::MemoryOrderAcqRel);
            return;
        }

        FRenderCommandFence Fence;
        Enqueue(DebugName, Move(Cmd));
        Fence.BeginFence();
        Fence.Wait();
    }

    void FRenderThread::Flush()
    {
        if (!bWorkerRunning.load(Atomic::MemoryOrderAcquire))
        {
            return;
        }
        if (Threading::IsRenderThread())
        {
            return;
        }

        LUMINA_PROFILE_SCOPE();

        WaitForCounter(CommandsEnqueued.load(Atomic::MemoryOrderAcquire));
    }

    void FRenderThread::WaitForCounter(uint64 Target)
    {
        if (!bWorkerRunning.load(Atomic::MemoryOrderAcquire) || Threading::IsRenderThread())
        {
            return;
        }

        std::unique_lock Lock(IdleMutex);
        IdleCV.wait(Lock, [this, Target]()
        {
            return CommandsCompleted.load(Atomic::MemoryOrderAcquire) >= Target;
        });
    }

    void FRenderThread::WorkerMain()
    {
        Threading::SetRenderThread(std::this_thread::get_id());
        Threading::SetThreadName("Lumina Render");
        Threading::InitializeThreadHeap();

        // Claim our own enki external-thread slot so render-thread paths that
        // schedule tasks (resource destructors via FVulkanMemoryAllocator,
        // shader compile callbacks, etc.) don't share slot 0 with the main thread.
        if (GTaskSystem != nullptr)
        {
            GTaskSystem->GetScheduler().RegisterExternalTaskThread();
        }

        TVector<FQueuedCommand> Batch;
        Batch.reserve(256);

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

    void FRenderThread::RunCommand(const char* DebugName, FCommand& Cmd)
    {
        LUMINA_PROFILE_SCOPE_COLORED(tracy::Color::SteelBlue);
        LUMINA_PROFILE_TAG(DebugName);
        Cmd();
    }


    void FRenderCommandFence::BeginFence()
    {
        TargetCounter = GRenderThread != nullptr
            ? GRenderThread->EnqueuedCount()
            : 0;
    }

    void FRenderCommandFence::Wait()
    {
        if (TargetCounter == 0 || GRenderThread == nullptr)
        {
            return;
        }

        LUMINA_PROFILE_SCOPE();
        GRenderThread->WaitForCounter(TargetCounter);
    }

    bool FRenderCommandFence::IsComplete() const
    {
        if (TargetCounter == 0 || GRenderThread == nullptr)
        {
            return true;
        }
        return GRenderThread->CompletedCount() >= TargetCounter;
    }
}
