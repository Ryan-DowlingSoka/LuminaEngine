#pragma once

#include "TaskSystem.h"
#include "TaskTypes.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Memory/SmartPtr.h"
#include "Memory/Allocators/Allocator.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    /**
     * Lightweight task graph for fan-out / dependency / fan-in patterns.
     *
     * Build the graph with Add() and AddParallelFor(), wire ordering with AddDependency(),
     * then call Dispatch() to start execution and Wait() to block until everything finishes.
     *
     * Dependent nodes are queued automatically by the underlying scheduler the moment
     * their parents complete, no worker threads are blocked on intermediate joins.
     *
     * Typical usage:
     *
     *     FTaskGraph Graph;
     *     auto A = Graph.AddParallelFor(NumA, 64, [&](const Task::FParallelRange& R){ ... });
     *     auto B = Graph.AddParallelFor(NumB, 32, [&](const Task::FParallelRange& R){ ... });
     *     auto C = Graph.Add([&]{ MergeResults(); });
     *     Graph.AddDependency(C, A);
     *     Graph.AddDependency(C, B);
     *     Graph.Dispatch();
     *     Graph.Wait();
     */
    class FTaskGraph
    {
    public:

        using FOneShotFunc      = TMoveOnlyFunction<void()>;
        using FParallelForFunc  = TMoveOnlyFunction<void(const Task::FParallelRange&)>;

        struct FNodeHandle
        {
            uint32 Index = ~0u;
            bool IsValid() const { return Index != ~0u; }
        };

        RUNTIME_API FTaskGraph();
        RUNTIME_API ~FTaskGraph();

        FTaskGraph(const FTaskGraph&) = delete;
        FTaskGraph& operator=(const FTaskGraph&) = delete;
        FTaskGraph(FTaskGraph&&) = delete;
        FTaskGraph& operator=(FTaskGraph&&) = delete;

        /** Adds a single-shot task that runs Func once on a worker thread. */
        RUNTIME_API FNodeHandle Add(FOneShotFunc Func, ETaskPriority Priority = ETaskPriority::Medium);

        /**
         * Adds a parallel-for task. The scheduler distributes [0, Count) across worker threads
         * and invokes Func once per partition. Count == 0 produces a no-op node that still
         * allows dependents to fire.
         */
        RUNTIME_API FNodeHandle AddParallelFor(uint32 Count, uint32 MinRange, FParallelForFunc Func, ETaskPriority Priority = ETaskPriority::Medium);

        /** Declares Node may not start until Dependency has finished. Must be called before Dispatch(). */
        RUNTIME_API void AddDependency(FNodeHandle Node, FNodeHandle Dependency);

        /** Schedules every root node. Each non-root node is queued automatically when its dependencies complete. */
        RUNTIME_API void Dispatch();

        /** Blocks the calling thread until every node in the graph has completed. Helps the scheduler in the meantime. */
        RUNTIME_API void Wait();

    private:

        struct FNode;

        FLinearAllocator                        Allocator;
        TVector<FNode*>                         Nodes;
        TVector<eastl::pair<uint32, uint32>>    Edges;
        bool                                    bDispatched = false;
    };
}
