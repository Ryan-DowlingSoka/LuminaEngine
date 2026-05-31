#include "pch.h"
#include "TaskGraph.h"
#include "Scheduler/JobScheduler.h"
#include "Core/Threading/Atomic.h"

namespace Lumina
{
    namespace
    {
        constexpr uint32 kGraphMaxChunks = 256;

        uint32 ComputeGraphChunkCount(uint32 Count, uint32 MinRange)
        {
            const uint32 Grain = MinRange == 0 ? 1u : MinRange;
            uint32 NumChunks = (Count + Grain - 1) / Grain;

            uint32 MaxChunks = Jobs::GetNumWorkers() * 4u;
            if (MaxChunks == 0)              MaxChunks = 1;
            if (MaxChunks > kGraphMaxChunks) MaxChunks = kGraphMaxChunks;

            if (NumChunks > MaxChunks) NumChunks = MaxChunks;
            if (NumChunks == 0)        NumChunks = 1;
            return NumChunks;
        }
    }

    struct FTaskGraph::FNode
    {
        void*                           Callable       = nullptr;
        FInvokeOneShot                  InvokeOneShot  = nullptr;
        FInvokeParallel                 InvokeParallel = nullptr;
        FDestroyCallable                Destroy        = nullptr;
        bool                            bIsParallelFor = false;
        uint32                          SetSize        = 1;
        uint32                          MinRange       = 1;
        ETaskPriority                   Priority       = ETaskPriority::Medium;

        FTaskGraph*                     Graph          = nullptr;
        uint32                          Index          = 0;

        // Dispatch state.
        TAtomic<int32>                  PendingDeps{0};
        TFrameVector<uint32>            Dependents;
        Jobs::FCounter*                 Counter        = nullptr;

        // Pre-built (at Dispatch) so worker-side scheduling never touches the arena.
        struct FChunk
        {
            FNode* Node;
            uint32 Start;
            uint32 End;
        };
        FChunk*                         Chunks    = nullptr;
        Jobs::FJobDecl*                 Decls     = nullptr;
        uint32                          NumChunks = 0;

        static void RunChunk(void* Arg, uint32 Worker)
        {
            FChunk* C = static_cast<FChunk*>(Arg);
            FNode*  N = C->Node;
            if (N->bIsParallelFor)
            {
                if (N->InvokeParallel)
                {
                    N->InvokeParallel(N->Callable, Task::FParallelRange{ C->Start, C->End, Worker });
                }
            }
            else if (N->InvokeOneShot)
            {
                N->InvokeOneShot(N->Callable);
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
        for (FNode* Node : Nodes)
        {
            Node->~FNode();
        }
    }

    void FTaskGraph::Reset()
    {
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
        Node->Dependents.set_allocator(FFrameArenaAllocator(&Allocator, "TaskGraphDeps"));
        Node->Callable          = Callable;
        Node->InvokeOneShot     = Invoke;
        Node->Destroy           = Destroy;
        Node->bIsParallelFor    = false;
        Node->SetSize           = 1;
        Node->MinRange          = 1;
        Node->Priority          = Priority;

        FNodeHandle Handle{ static_cast<uint32>(Nodes.size()) };
        Nodes.push_back(Node);
        return Handle;
    }

    FTaskGraph::FNodeHandle FTaskGraph::AddParallelForNode(uint32 Count, uint32 MinRange, void* Callable, FInvokeParallel Invoke, FDestroyCallable Destroy, ETaskPriority Priority)
    {
        auto* Node              = Allocator.TAlloc<FNode>();
        Node->Dependents.set_allocator(FFrameArenaAllocator(&Allocator, "TaskGraphDeps"));
        Node->bIsParallelFor    = true;
        Node->Priority          = Priority;

        if (Count == 0)
        {
            // Empty work; node still needed so dependents fire. No callable was placed.
            Node->SetSize       = 0;
            Node->MinRange      = 1;
        }
        else
        {
            Node->Callable          = Callable;
            Node->InvokeParallel    = Invoke;
            Node->Destroy           = Destroy;
            Node->SetSize           = Count;
            Node->MinRange          = std::max(1u, MinRange);
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

    void FTaskGraph::ScheduleNode(FNode* Node)
    {
        Node->Counter = Jobs::AllocCounter(0);
        Jobs::SetCounterCompletion(Node->Counter, &FTaskGraph::OnNodeComplete, Node);
        Jobs::RunJobs(Node->Decls, Node->NumChunks, ToJobPriority(Node->Priority), Node->Counter);
    }

    void FTaskGraph::OnNodeComplete(void* NodeCtx, uint32 /*Worker*/)
    {
        FNode*      Node  = static_cast<FNode*>(NodeCtx);
        FTaskGraph* Graph = Node->Graph;

        Jobs::FreeCounter(Node->Counter);
        Node->Counter = nullptr;

        for (uint32 DepIndex : Node->Dependents)
        {
            FNode* Dependent = Graph->Nodes[DepIndex];
            if (Dependent->PendingDeps.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0)
            {
                ScheduleNode(Dependent);
            }
        }

        Jobs::DecrementCounter(Graph->GraphCounter, 1);
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

        // Reset per-node dispatch state and build the in-degree / dependents graph.
        for (uint32 i = 0; i < NumNodes; ++i)
        {
            FNode* Node = Nodes[i];
            Node->Index = i;
            Node->Graph = this;
            Node->PendingDeps.store(0, std::memory_order_relaxed);
            Node->Dependents.clear();
        }
        for (const auto& Edge : Edges)
        {
            // Edge = (child, parent): child depends on parent.
            Nodes[Edge.first]->PendingDeps.fetch_add(1, std::memory_order_relaxed);
            Nodes[Edge.second]->Dependents.push_back(Edge.first);
        }

        // Pre-chunk every node here (single-threaded), so worker-side ScheduleNode never allocates.
        for (uint32 i = 0; i < NumNodes; ++i)
        {
            FNode* Node = Nodes[i];
            const uint32 Count = Node->bIsParallelFor ? Node->SetSize : 1u;

            uint32 ChunkCount;
            if (Count <= 1 || (Node->bIsParallelFor && Node->InvokeParallel == nullptr))
            {
                ChunkCount = 1;
            }
            else
            {
                ChunkCount = ComputeGraphChunkCount(Count, Node->MinRange);
            }

            Node->NumChunks = ChunkCount;
            Node->Chunks = static_cast<FNode::FChunk*>(Allocator.Allocate(sizeof(FNode::FChunk) * ChunkCount, alignof(FNode::FChunk)));
            Node->Decls  = static_cast<Jobs::FJobDecl*>(Allocator.Allocate(sizeof(Jobs::FJobDecl) * ChunkCount, alignof(Jobs::FJobDecl)));

            const uint32 Base = ChunkCount ? Count / ChunkCount : 0;
            const uint32 Rem  = ChunkCount ? Count % ChunkCount : 0;
            uint32 Start = 0;
            for (uint32 c = 0; c < ChunkCount; ++c)
            {
                const uint32 Len = Base + (c < Rem ? 1u : 0u);
                Node->Chunks[c] = FNode::FChunk{ Node, Start, Start + Len };
                Node->Decls[c]  = Jobs::FJobDecl{ &FNode::RunChunk, &Node->Chunks[c] };
                Start += Len;
            }
        }

        // Capture the root set NOW, while PendingDeps is stable (no node has been scheduled yet, so no
        // worker can be decrementing in-degrees). Scheduling a root below may complete it and drive a
        // dependent to zero on a worker thread; that worker — not this loop — owns scheduling it.
        DispatchRoots.clear();
        for (uint32 i = 0; i < NumNodes; ++i)
        {
            if (Nodes[i]->PendingDeps.load(std::memory_order_relaxed) == 0)
            {
                DispatchRoots.push_back(i);
            }
        }

        GraphCounter = Jobs::AllocCounter(static_cast<int32>(NumNodes));

        for (uint32 RootIndex : DispatchRoots)
        {
            ScheduleNode(Nodes[RootIndex]);
        }
    }

    void FTaskGraph::Wait()
    {
        if (!bDispatched)
        {
            return;
        }

        LUMINA_PROFILE_SECTION("FTaskGraph::Wait");

        if (GraphCounter != nullptr)
        {
            Jobs::WaitForCounter(GraphCounter, 0);
            Jobs::FreeCounter(GraphCounter);
            GraphCounter = nullptr;
        }

        bDispatched = false;
    }
}
