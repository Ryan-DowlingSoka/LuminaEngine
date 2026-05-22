#include "pch.h"
#include "TaskGraph.h"

namespace Lumina
{
    struct FTaskGraph::FNode : enki::ITaskSet
    {
        void*                           Callable       = nullptr;
        FInvokeOneShot                  InvokeOneShot  = nullptr;
        FInvokeParallel                 InvokeParallel = nullptr;
        FDestroyCallable                Destroy        = nullptr;
        TFrameVector<enki::Dependency>  Deps;
        bool                            bIsParallelFor = false;

        void ExecuteRange(enki::TaskSetPartition Range, uint32_t ThreadNum) override
        {
            if (bIsParallelFor)
            {
                if (InvokeParallel)
                {
                    InvokeParallel(Callable, Task::FParallelRange{Range.start, Range.end, ThreadNum});
                }
            }
            else
            {
                if (InvokeOneShot)
                {
                    InvokeOneShot(Callable);
                }
            }
        }

        ~FNode()
        {
            // Closure lives in the arena: run its destructor, but never free (arena owns the bytes).
            if (Callable && Destroy)
            {
                Destroy(Callable);
            }
        }
    };

    FTaskGraph::FTaskGraph()
        : Allocator(16llu * 1024)
    {}

    FTaskGraph::~FTaskGraph()
    {
        if (bDispatched)
        {
            Wait();
        }

        // FNodes are placement-constructed in the block allocator, which frees its raw
        // blocks without running destructors. Destroy each node explicitly so its Deps
        // (enki::Dependency teardown) and captured-function storage run.
        for (FNode* Node : Nodes)
        {
            Node->~FNode();
        }
    }

    void FTaskGraph::Reset()
    {
        // Tasks must be complete before tearing down nodes/deps (enki Dependency teardown
        // asserts completion). Destroy nodes BEFORE rewinding the arena their Deps live in.
        if (bDispatched)
        {
            Wait();
        }
        for (FNode* Node : Nodes)
        {
            Node->~FNode();
        }
        Nodes.clear();          // keep capacity
        Edges.clear();          // keep capacity
        Allocator.Reset();      // rewind blocks, keep them
        bDispatched = false;
    }

    FTaskGraph::FNodeHandle FTaskGraph::AddOneShotNode(void* Callable, FInvokeOneShot Invoke, FDestroyCallable Destroy, ETaskPriority Priority)
    {
        auto* Node              = Allocator.TAlloc<FNode>();
        Node->Deps.set_allocator(FFrameArenaAllocator(&Allocator, "TaskGraphDeps"));
        Node->Callable          = Callable;
        Node->InvokeOneShot     = Invoke;
        Node->Destroy           = Destroy;
        Node->bIsParallelFor    = false;
        Node->m_SetSize         = 1;
        Node->m_MinRange        = 1;
        Node->m_Priority        = static_cast<enki::TaskPriority>(Priority);

        FNodeHandle Handle{ static_cast<uint32>(Nodes.size()) };
        Nodes.push_back(Node);
        return Handle;
    }

    FTaskGraph::FNodeHandle FTaskGraph::AddParallelForNode(uint32 Count, uint32 MinRange, void* Callable, FInvokeParallel Invoke, FDestroyCallable Destroy, ETaskPriority Priority)
    {
        auto* Node              = Allocator.TAlloc<FNode>();
        Node->Deps.set_allocator(FFrameArenaAllocator(&Allocator, "TaskGraphDeps"));
        Node->bIsParallelFor    = true;
        Node->m_Priority        = static_cast<enki::TaskPriority>(Priority);

        if (Count == 0)
        {
            // Empty work; node still needed so dependents fire. No callable was placed.
            Node->m_SetSize     = 1;
            Node->m_MinRange    = 1;
        }
        else
        {
            Node->Callable          = Callable;
            Node->InvokeParallel    = Invoke;
            Node->Destroy           = Destroy;
            Node->m_SetSize         = Count;
            Node->m_MinRange        = std::max(1u, MinRange);
        }

        FNodeHandle Handle{ static_cast<uint32>(Nodes.size()) };
        Nodes.push_back(Node);
        return Handle;
    }

    void FTaskGraph::AddDependency(FNodeHandle Node, FNodeHandle Dependency)
    {
        ASSERT(!bDispatched);
        ASSERT(Node.IsValid() && Dependency.IsValid());
        ASSERT(Node.Index < Nodes.size() && Dependency.Index < Nodes.size());
        Edges.emplace_back(Node.Index, Dependency.Index);
    }

    void FTaskGraph::Dispatch()
    {
        ASSERT(!bDispatched);
        ASSERT(GTaskSystem != nullptr);
        bDispatched = true;

        const uint32 NumNodes = static_cast<uint32>(Nodes.size());
        if (NumNodes == 0)
        {
            return;
        }

        // Size each node's Deps exactly so enki::Dependency addresses stay stable for SetDependency().
        // DepCount/DepCursor are persistent members; assign() reuses their capacity.
        DepCount.assign(NumNodes, 0u);
        for (const auto& Edge : Edges)
        {
            DepCount[Edge.first]++;
        }

        for (uint32 i = 0; i < NumNodes; ++i)
        {
            if (DepCount[i] > 0)
            {
                Nodes[i]->Deps.resize(DepCount[i]);
            }
        }

        DepCursor.assign(NumNodes, 0u);
        for (const auto& Edge : Edges)
        {
            FNode* Child  = Nodes[Edge.first];
            FNode* Parent = Nodes[Edge.second];
            const uint32 Slot = DepCursor[Edge.first]++;
            Child->SetDependency(Child->Deps[Slot], Parent);
        }

        // Only schedule roots; dependents queue automatically when parents complete.
        Task::ParallelFor(NumNodes, [&](uint32 Index)
        {
            if (DepCount[Index] == 0)
            {
                GTaskSystem->ScheduleTask(Nodes[Index]);
            }
        });
    }

    void FTaskGraph::Wait()
    {
        if (!bDispatched)
        {
            return;
        }

        LUMINA_PROFILE_SECTION("FTaskGraph::Wait");

        // Walk every node so the caller assists workers until the graph drains.
        for (auto& Node : Nodes)
        {
            GTaskSystem->GetScheduler().WaitforTask(Node);
        }

        bDispatched = false;
    }
}
