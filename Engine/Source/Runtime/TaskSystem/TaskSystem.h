#pragma once

#include "TaskScheduler.h"
#include "TaskTypes.h"
#include "Core/Threading/Thread.h"
#include "Memory/Memory.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    RUNTIME_API extern class FTaskSystem* GTaskSystem;

    namespace Task
    {
        
        void Initialize();
        void Shutdown();
        
        struct FParallelRange
        {
            uint32 Start;
            uint32 End;
            uint32 Thread;
        };
    }

    class FTaskSystem
    {
        friend struct FCompletionActionDelete;
        
    public:
        
        bool IsBusy() const { return Scheduler.GetIsShutdownRequested(); }
        uint32_t GetNumWorkers() const { return NumWorkers; }
        
        enki::TaskScheduler& GetScheduler() { return Scheduler; }

        /** Num iterations split across worker threads; ranges are unordered. Returned handle is auto-cleaned. */
        RUNTIME_API FTaskHandle ScheduleLambda(uint32 Num, uint32 MinRange, TaskSetFunction&& Function, ETaskPriority Priority = ETaskPriority::Medium);
        
        
        template<typename TFunc>
        void ParallelFor(uint32 Num, TFunc&& Func, uint32 MinRange = 0, ETaskPriority Priority = ETaskPriority::Medium)
        {
            if (Num == 0)
            {
                return;
            }
            
            struct ParallelTask : ITaskSet
            {
                ParallelTask(TFunc&& InFunc, uint32 InNum, uint32 InMinRange)
                    : ITaskSet(InNum, InMinRange)
                    , Func(std::forward<TFunc>(InFunc))
                {
                }

                void ExecuteRange(TaskSetPartition range_, uint32_t ThreadNum) override
                {
                    if constexpr (eastl::is_invocable_v<TFunc, const Task::FParallelRange&>)
                    {
                        Func(Task::FParallelRange{range_.start, range_.end, ThreadNum});
                        return;
                    }
                    
                    for (uint32 i = range_.start; i < range_.end; ++i)
                    {
                        if constexpr (eastl::is_invocable_v<TFunc, uint32>)
                        {
                            Func(i);
                        }
                        else if constexpr (eastl::is_invocable_v<TFunc, uint32, uint32>)
                        {
                            Func(i, ThreadNum);
                        }
                    }
                }

                TFunc Func;
            };

            LUMINA_PROFILE_SECTION("Tasks::ParallelFor");
            ParallelTask Task = ParallelTask(std::forward<TFunc>(Func), Num, std::min(1u, MinRange == 0 ? Num : MinRange));
            if (Num == 1)
            {
                Task.ExecuteRange(TaskSetPartition{0, 1}, Threading::GetThreadID());
                return;
            }
            
            Task.m_Priority = static_cast<enki::TaskPriority>(Priority);
            ScheduleTask(&Task);
            WaitForTask(&Task, Priority);
        }

        template<typename TIterator, typename TFunc>
        void ParallelForEach(TIterator Begin, TIterator End, TFunc&& Func, ETaskPriority Priority = ETaskPriority::Medium)
        {
            struct ParallelTask : ITaskSet
            {
                ParallelTask(TFunc&& InFunc, TIterator InBegin, TIterator InEnd)
                    : ITaskSet(static_cast<uint32>(InEnd - InBegin))
                    , Func(std::forward<TFunc>(InFunc))
                    , Begin(InBegin)
                {
                }
        
                void ExecuteRange(TaskSetPartition range_, uint32_t ThreadNum) override
                {
                    if constexpr (eastl::is_invocable_v<TFunc, TIterator, TIterator, uint32>)
                    {
                        Func(Begin + range_.start, Begin + range_.end, ThreadNum);
                        return;
                    }
                    else if constexpr (eastl::is_invocable_v<TFunc, TIterator, TIterator>)
                    {
                        Func(Begin + range_.start, Begin + range_.end);
                        return;
                    }
                    
                    for (uint32 i = range_.start; i < range_.end; ++i)
                    {
                        TIterator It = Begin + i;
                        
                        if constexpr (eastl::is_invocable_v<TFunc, decltype(*It)&, uint32>)
                        {
                            Func(*It, ThreadNum);
                        }
                        else if constexpr (eastl::is_invocable_v<TFunc, decltype(*It)&>)
                        {
                            Func(*It);
                        }
                        else if constexpr (eastl::is_invocable_v<TFunc, TIterator, uint32>)
                        {
                            Func(It, ThreadNum);
                        }
                        else if constexpr (eastl::is_invocable_v<TFunc, TIterator>)
                        {
                            Func(It);
                        }
                    }
                }
        
                TFunc Func;
                TIterator Begin;
            };
        
            uint32 Num = static_cast<uint32>(End - Begin);
            
            LUMINA_PROFILE_SECTION("Tasks::ParallelForEach");
            ParallelTask Task = ParallelTask(std::forward<TFunc>(Func), Begin, End);
            
            if (Num <= 1)
            {
                Task.ExecuteRange(TaskSetPartition{0, Num}, Threading::GetThreadID());
                return;
            }
            
            Task.m_Priority = static_cast<enki::TaskPriority>(Priority);
            ScheduleTask(&Task);
            WaitForTask(&Task, Priority);
        }
        
        RUNTIME_API void ScheduleTask(ITaskSet* pTask);

        RUNTIME_API void ScheduleTask(IPinnedTask* pTask);

        RUNTIME_API void WaitForTask(const ITaskSet* pTask, ETaskPriority Priority = ETaskPriority::Low);
        
        RUNTIME_API void WaitForTask(const IPinnedTask* pTask);
        
        RUNTIME_API void WaitForAll();
    

        enki::TaskScheduler                 Scheduler;
        uint32                              NumWorkers = 0;
    };
    
    namespace Task
    {
        RUNTIME_API FTaskHandle AsyncTask(uint32 Num, uint32 MinRange, TaskSetFunction&& Function, ETaskPriority Priority = ETaskPriority::Medium);

        template<typename TFunc>
        void ParallelFor(uint32 Num, TFunc&& Func, uint32 MinRange = 0, ETaskPriority Priority = ETaskPriority::Medium)
        {
            GTaskSystem->ParallelFor(Num, std::forward<TFunc>(Func), MinRange, Priority);
        }

        template<typename TIterator, typename TFunc>
        requires(eastl::is_same_v<typename eastl::iterator_traits<TIterator>::iterator_category, eastl::random_access_iterator_tag> ||
            std::is_same_v<typename std::iterator_traits<TIterator>::iterator_category, std::random_access_iterator_tag>)
        void ParallelForEach(TIterator Begin, TIterator End, TFunc&& Func, ETaskPriority Priority = ETaskPriority::Medium)
        {
            GTaskSystem->ParallelForEach(Begin, End, std::forward<TFunc>(Func), Priority);
        }
    }
}
