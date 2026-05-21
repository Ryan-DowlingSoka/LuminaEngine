#include "pch.h"
#include "TaskGraph.h"

namespace Lumina
{
    struct FTaskGraph::FNode : enki::ITaskSet
    {
        FOneShotFunc                OneShot;
        FParallelForFunc            ParallelForFunc;
        TVector<enki::Dependency>   Deps;
        bool                        bIsParallelFor = false;

        void ExecuteRange(enki::TaskSetPartition Range, uint32_t ThreadNum) override
        {
            if (bIsParallelFor)
            {
                if (ParallelForFunc)
                {
                    ParallelForFunc(Task::FParallelRange{Range.start, Range.end, ThreadNum});
                }
            }
            else
            {
                if (OneShot)
                {
                    OneShot();
                }
            }
        }
    };

    FTaskGraph::FTaskGraph()
        :Allocator(1024llu * 4)
    {}

    FTaskGraph::~FTaskGraph()
    {
        if (bDispatched)
        {
            Wait();
        }

        // FNodes are placement-constructed in the linear allocator, which frees its
        // raw block without running destructors. Destroy each node explicitly so its
        // Deps vector (sized in Dispatch) and captured-function storage are freed
        // instead of leaking every time a graph goes out of scope.
        for (FNode* Node : Nodes)
        {
            Node->~FNode();
        }
    }

    FTaskGraph::FNodeHandle FTaskGraph::Add(FOneShotFunc Func, ETaskPriority Priority)
    {
        auto* Node              = Allocator.TAlloc<FNode>();
        Node->OneShot           = Move(Func);
        Node->bIsParallelFor    = false;
        Node->m_SetSize         = 1;
        Node->m_MinRange        = 1;
        Node->m_Priority        = static_cast<enki::TaskPriority>(Priority);

        FNodeHandle Handle{ static_cast<uint32>(Nodes.size()) };
        Nodes.push_back(Node);
        return Handle;
    }

    FTaskGraph::FNodeHandle FTaskGraph::AddParallelFor(uint32 Count, uint32 MinRange, FParallelForFunc Func, ETaskPriority Priority)
    {
        auto* Node              = Allocator.TAlloc<FNode>();
        Node->bIsParallelFor    = true;
        Node->m_Priority        = static_cast<enki::TaskPriority>(Priority);

        if (Count == 0)
        {
            // Empty work; node still needed so dependents fire.
            Node->m_SetSize     = 1;
            Node->m_MinRange    = 1;
        }
        else
        {
            Node->ParallelForFunc   = Move(Func);
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
        TVector<uint32> DepCount(NumNodes, 0u);
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

        TVector<uint32> DepCursor(NumNodes, 0u);
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
