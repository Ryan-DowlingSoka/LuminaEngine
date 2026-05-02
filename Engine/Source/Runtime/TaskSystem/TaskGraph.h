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
    /** Task graph for fan-out / dependency / fan-in patterns; dependents queue automatically when parents complete. */
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

        RUNTIME_API FNodeHandle Add(FOneShotFunc Func, ETaskPriority Priority = ETaskPriority::Medium);

        /** Count == 0 produces a no-op node that still fires dependents. */
        RUNTIME_API FNodeHandle AddParallelFor(uint32 Count, uint32 MinRange, FParallelForFunc Func, ETaskPriority Priority = ETaskPriority::Medium);

        /** Must be called before Dispatch(). */
        RUNTIME_API void AddDependency(FNodeHandle Node, FNodeHandle Dependency);

        RUNTIME_API void Dispatch();
        RUNTIME_API void Wait();

    private:

        struct FNode;

        FLinearAllocator                        Allocator;
        TVector<FNode*>                         Nodes;
        TVector<eastl::pair<uint32, uint32>>    Edges;
        bool                                    bDispatched = false;
    };
}
