#pragma once

#include "TaskScheduler.h"
#include "Containers/Function.h"
#include "Core/Templates/LuminaTemplate.h"
#include "Core/Threading/Atomic.h"
#include "Memory/SmartPtr.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    using ITaskSet =            enki::ITaskSet;
    using IPinnedTask =         enki::IPinnedTask;
    using ICompletableTask =    enki::ICompletable;
    using TaskSetPartition =    enki::TaskSetPartition;
    using TaskFunction =        enki::TaskSetFunction;
    
    struct FTaskCompletion
    {
        FTaskCompletion() = default;
        
        TAtomic<bool> bCompleted{false};
    
        bool IsCompleted() const { return bCompleted.load(std::memory_order_acquire); }
        void Wait() const
        {
            std::atomic_wait(&bCompleted, false);
        }
    };

    using FTaskHandle = TSharedPtr<FTaskCompletion>;

    enum class ETaskPriority : uint8
    {
        High   = 0,
        Medium = 1,
        Low    = 2,
    };
    
    struct FCompletionActionDelete : enki::ICompletable
    {
        enki::Dependency    Dependency;
    
        void OnDependenciesComplete(enki::TaskScheduler* pTaskScheduler_, uint32_t threadNum_ ) override;
    };
    
    
    typedef TMoveOnlyFunction<void(uint32 Start, uint32 End, uint32 Thread)> TaskSetFunction;
    class FLambdaTask : public ITaskSet
    {
    public:
        
        FLambdaTask(const FLambdaTask&) = delete;
        FLambdaTask& operator=(const FLambdaTask&) = delete;
        ~FLambdaTask() override = default;

        FLambdaTask(const FTaskHandle& InHandle, ETaskPriority Priority, uint32 SetSize, uint32 MinRange, TaskSetFunction&& TaskFunctor)
        {
            TaskHandle = InHandle;
            m_Priority = static_cast<enki::TaskPriority>(Priority);
            m_SetSize = SetSize;
            m_MinRange = MinRange;
            Function = Move(TaskFunctor);
            TaskRecycle.SetDependency(TaskRecycle.Dependency, this);
        }
        
        
        void ExecuteRange(TaskSetPartition range_, uint32_t threadnum_) override
        {
            LUMINA_PROFILE_SECTION("Tasks::LambdaTask");

            Function(range_.start, range_.end, threadnum_);
        }
        
        TWeakPtr<FTaskCompletion>   TaskHandle;
        TaskSetFunction             Function;
        FCompletionActionDelete     TaskRecycle;
    };
    
}
