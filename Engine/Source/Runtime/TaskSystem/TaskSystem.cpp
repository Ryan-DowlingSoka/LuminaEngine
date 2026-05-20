#include "pch.h"
#include "TaskSystem.h"

#include "Core/Threading/Thread.h"
#include "Log/Log.h"
#include "Memory/Memory.h"

namespace Lumina
{
    RUNTIME_API FTaskSystem* GTaskSystem = nullptr;

    namespace
    {
        void OnStartThread(uint32 threadNum)
        {
            LUMINA_PROFILE_SCOPE();
            
            FString ThreadName = "Background Worker: " + eastl::to_string(threadNum);
            Threading::SetThreadName(ThreadName.c_str());
        
            Memory::InitializeThreadHeap();
        }

        void OnStopThread(uint32_t threadNum)
        {
            LUMINA_PROFILE_SCOPE();
            
            Memory::ShutdownThreadHeap();
        }

        void ObWaitForTaskCompleteStart(uint32_t threadNum)
        {
            const char* TaskStartTxt = "Wait For Task Complete";
            TracyMessage(TaskStartTxt, strlen(TaskStartTxt));
        }

        void* CustomAllocFunc(size_t alignment, size_t size, void* userData_, const char* file_, int line_)
        {
            return Memory::Malloc(size, alignment);
        }

        void CustomFreeFunc(void* ptr, size_t size, void* userData_, const char* file_, int line_)
        {
            Memory::Free(ptr);
        }
    }
    
    namespace Task
    {
        void Initialize()
        {
            GTaskSystem = Memory::New<FTaskSystem>();

            GTaskSystem->NumWorkers                             = Threading::GetNumThreads() - 2;
        
            enki::TaskSchedulerConfig config;
            config.numTaskThreadsToCreate                       = GTaskSystem->NumWorkers;
            config.numExternalTaskThreads                       = 3;
            config.customAllocator.alloc                        = CustomAllocFunc;
            config.customAllocator.free                         = CustomFreeFunc;
            config.profilerCallbacks.threadStart                = OnStartThread;
            config.profilerCallbacks.waitForTaskCompleteStart   = ObWaitForTaskCompleteStart;
            config.profilerCallbacks.threadStop                 = OnStopThread;

            GTaskSystem->Scheduler.Initialize(config);
        }

        void Shutdown()
        {
            GTaskSystem->Scheduler.WaitforAllAndShutdown();
            Memory::Delete(GTaskSystem);
            GTaskSystem = nullptr;
        }
    }

    FTaskHandle FTaskSystem::ScheduleLambda(uint32 Num, uint32 MinRange, TaskSetFunction&& Function, ETaskPriority Priority)
    {
        if (Num == 0)
        {
            LOG_WARN("Task Size of [0] passed to task system.");
            return nullptr;
        }
        
        FTaskHandle TaskHandle = MakeShared<FTaskCompletion>();
        FLambdaTask* Task = Memory::New<FLambdaTask>(TaskHandle, Priority, Num, std::max(1u, MinRange), Move(Function));
        ScheduleTask(Task);
        
        return TaskHandle;
    }

    void FTaskSystem::ScheduleTask(ITaskSet* pTask)
    {
        LUMINA_PROFILE_SECTION("Tasks::ScheduleTask");
        Scheduler.AddTaskSetToPipe(pTask);
    }

    void FTaskSystem::ScheduleTask(IPinnedTask* pTask)
    {
        LUMINA_PROFILE_SECTION("Tasks::ScheduleTask");
        Scheduler.AddPinnedTask(pTask);
    }

    void FTaskSystem::WaitForTask(const ITaskSet* pTask, ETaskPriority Priority)
    {
        LUMINA_PROFILE_SECTION("Tasks::WaitForTask");
        Scheduler.WaitforTask(pTask, (enki::TaskPriority)Priority);
    }

    void FTaskSystem::WaitForTask(const IPinnedTask* pTask)
    {
        LUMINA_PROFILE_SECTION("Tasks::WaitForTask");
        Scheduler.WaitforTask(pTask);
    }

    void FTaskSystem::WaitForAll()
    {
        LUMINA_PROFILE_SCOPE();
        Scheduler.WaitforAll(); 
    }

    FTaskHandle Task::AsyncTask(uint32 Num, uint32 MinRange, TaskSetFunction&& Function, ETaskPriority Priority)
    {
        return GTaskSystem->ScheduleLambda(Num, MinRange, Move(Function), Priority);
    }
}
