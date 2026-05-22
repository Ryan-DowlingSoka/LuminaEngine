#pragma once

#include "TaskSystem.h"
#include "TaskTypes.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Memory/SmartPtr.h"
#include "Memory/Allocators/Allocator.h"
#include "Platform/GenericPlatform.h"
#include <new>
#include <utility>
#include <type_traits>

namespace Lumina
{
    /** Task graph for fan-out / dependency / fan-in patterns; dependents queue automatically when parents complete. */
    class FTaskGraph
    {
    public:

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

        // The callable is placement-constructed in the graph's arena (reused via Reset),
        // so adding a node never heap-allocates -- even for large lambda captures that would
        // blow std::move_only_function's small-buffer optimization.
        template<typename F>
        FNodeHandle Add(F&& Func, ETaskPriority Priority = ETaskPriority::Medium)
        {
            using TFn = std::decay_t<F>;
            void* Mem = Allocator.Allocate(sizeof(TFn), alignof(TFn));
            ::new (Mem) TFn(std::forward<F>(Func));
            return AddOneShotNode(Mem,
                [](void* P) { (*static_cast<TFn*>(P))(); },
                [](void* P) { static_cast<TFn*>(P)->~TFn(); },
                Priority);
        }

        /** Count == 0 produces a no-op node that still fires dependents. */
        template<typename F>
        FNodeHandle AddParallelFor(uint32 Count, uint32 MinRange, F&& Func, ETaskPriority Priority = ETaskPriority::Medium)
        {
            if (Count == 0)
            {
                return AddParallelForNode(0, MinRange, nullptr, nullptr, nullptr, Priority);
            }

            using TFn = std::decay_t<F>;
            void* Mem = Allocator.Allocate(sizeof(TFn), alignof(TFn));
            ::new (Mem) TFn(std::forward<F>(Func));
            return AddParallelForNode(Count, MinRange, Mem,
                [](void* P, const Task::FParallelRange& R) { (*static_cast<TFn*>(P))(R); },
                [](void* P) { static_cast<TFn*>(P)->~TFn(); },
                Priority);
        }

        /** Must be called before Dispatch(). */
        RUNTIME_API void AddDependency(FNodeHandle Node, FNodeHandle Dependency);

        RUNTIME_API void Dispatch();
        RUNTIME_API void Wait();

        /** Wipe the graph for reuse, keeping allocator block + container capacity (no per-frame
            heap churn). Waits for any in-flight dispatch first. Call before re-adding nodes. */
        RUNTIME_API void Reset();

    private:

        struct FNode;

        using FInvokeOneShot   = void(*)(void*);
        using FInvokeParallel  = void(*)(void*, const Task::FParallelRange&);
        using FDestroyCallable = void(*)(void*);

        // Non-template node builders the templated Add/AddParallelFor forward to once the
        // closure has been placed in the arena and type-erased into raw thunks.
        RUNTIME_API FNodeHandle AddOneShotNode(void* Callable, FInvokeOneShot Invoke, FDestroyCallable Destroy, ETaskPriority Priority);
        RUNTIME_API FNodeHandle AddParallelForNode(uint32 Count, uint32 MinRange, void* Callable, FInvokeParallel Invoke, FDestroyCallable Destroy, ETaskPriority Priority);

        // Block allocator so FNodes and their (arena-backed) Deps are reused via Reset()
        // rather than reallocated every frame; grows if a graph ever needs more.
        FBlockLinearAllocator                   Allocator;
        TVector<FNode*>                         Nodes;
        TVector<eastl::pair<uint32, uint32>>    Edges;
        // Dispatch scratch, kept across frames so it reuses capacity.
        TVector<uint32>                         DepCount;
        TVector<uint32>                         DepCursor;
        bool                                    bDispatched = false;
    };
}
