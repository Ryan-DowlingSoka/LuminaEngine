#include "pch.h"
#include "RenderGraph.h"
#include "RenderGraphDescriptor.h"
#include "RenderGraphPass.h"
#include "Core/Engine/Engine.h"
#include "Platform/Process/PlatformProcess.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RHIGlobals.h"
#include "TaskSystem/TaskSystem.h"


namespace Lumina
{
    // Linear allocator backing for the render graph. Sized to comfortably hold a full frame's
    // worth of passes (~50 passes * ~1.5KB each + their descriptors). Reset happens implicitly
    // when the graph goes out of scope at frame end.
    static constexpr size_t GRenderGraphAllocatorSize = 256u * 1024u;


    FRenderGraph::FRenderGraph()
        : GraphAllocator(GRenderGraphAllocatorSize)
    {
        Passes.reserve(64);
        Descriptors.reserve(64);
        Batches.reserve(16);
    }


    FRenderGraph::~FRenderGraph()
    {
        // The linear allocator only frees its backing memory; it does not invoke destructors.
        // We explicitly destruct passes and descriptors so captured lambdas, command list refs,
        // and resource-access vectors release their resources.
        for (FRenderGraphPass* Pass : Passes)
        {
            Pass->~FRenderGraphPass();
        }

        for (FRGPassDescriptor* Descriptor : Descriptors)
        {
            Descriptor->~FRGPassDescriptor();
        }
    }


    FRGPassDescriptor* FRenderGraph::AllocDescriptor()
    {
        FRGPassDescriptor* Descriptor = GraphAllocator.TAlloc<FRGPassDescriptor>();
        Descriptors.push_back(Descriptor);
        return Descriptor;
    }


    ECommandQueue FRenderGraph::DeriveQueue(ERGPassFlags PassFlags, const FRGPassDescriptor* Descriptor)
    {
        // Routing is opt-in per pass via the ExecutionFlags on the descriptor.
        // Without the async flag, compute/transfer work stays on the graphics queue to keep
        // resource sharing safe (Vulkan images are currently VK_SHARING_MODE_EXCLUSIVE).
        if (Descriptor != nullptr)
        {
            if (EnumHasAnyFlags(PassFlags, ERGPassFlags::Compute) && Descriptor->HasAnyFlag(ERGExecutionFlags::AsyncCompute))
            {
                return ECommandQueue::Compute;
            }

            if (EnumHasAnyFlags(PassFlags, ERGPassFlags::Transfer) && Descriptor->HasAnyFlag(ERGExecutionFlags::AsyncTransfer))
            {
                return ECommandQueue::Transfer;
            }
        }

        return ECommandQueue::Graphics;
    }


    void FRenderGraph::Compile()
    {
        LUMINA_PROFILE_SCOPE();

        Batches.clear();

        const uint32 NumPasses = (uint32)Passes.size();
        for (uint32 i = 0; i < NumPasses; ++i)
        {
            FRenderGraphPass* Pass = Passes[i];

            Pass->PassIndex = i;
            Pass->Queue     = DeriveQueue(Pass->PassFlags, Pass->Descriptor);

            const bool bIsAsync = Pass->Descriptor != nullptr
                && Pass->Descriptor->HasAnyFlag(ERGExecutionFlags::Async);

            // A batch is a run of passes that share a single command list. We must start a new
            // batch when the queue changes (different CL type) or when the pass is flagged Async
            // (contract: runs on its own CL, no shared state tracking). Async passes never
            // absorb following passes either — they are always singleton batches.
            const bool bStartNewBatch = Batches.empty()
                || Batches.back().Queue != Pass->Queue
                || Batches.back().bIsAsync
                || bIsAsync;

            if (bStartNewBatch)
            {
                FRGBatch NewBatch;
                NewBatch.Queue = Pass->Queue;
                NewBatch.bIsAsync = bIsAsync;
                NewBatch.Passes.reserve(8);
                Batches.push_back(std::move(NewBatch));
            }

            Pass->BatchIndex = (uint32)Batches.size() - 1;
            Batches.back().Passes.push_back(Pass);
        }
    }


    void FRenderGraph::Record()
    {
        LUMINA_PROFILE_SCOPE();

        const uint32 NumBatches = (uint32)Batches.size();
        if (NumBatches == 0)
        {
            return;
        }

        // One command list per batch. All passes in the batch record to the same CL, so state
        // tracking (image layouts, barrier source stages) composes correctly across them — the
        // same guarantee the old single-CL RenderGraph provided.
        //
        // Parallelism: batches are independent (different queues, or explicit Async), so we can
        // still record them concurrently on task-system workers. This gives async compute and
        // similar multi-queue workloads a real CPU win, without sacrificing correctness within
        // a batch.
        Task::ParallelFor(NumBatches, [this](uint32 BatchIdx)
        {
            LUMINA_PROFILE_SECTION("RenderGraph::RecordBatch");

            FRGBatch& Batch = Batches[BatchIdx];

            Batch.CommandList = GRenderContext->CreateCommandList(FCommandListInfo::As(Batch.Queue));
            Batch.CommandList->Open();

            for (FRenderGraphPass* Pass : Batch.Passes)
            {
                Batch.CommandList->AddMarker(Pass->Event.Get(), FColor::MakeRandom());
                Pass->Execute(*Batch.CommandList);
                Batch.CommandList->PopMarker();
            }

            Batch.CommandList->Close();
        });
    }


    void FRenderGraph::Submit()
    {
        LUMINA_PROFILE_SCOPE();

        if (Batches.empty())
        {
            return;
        }

        // Track which batch index was the last one submitted on each queue. A batch on queue Q
        // needs to wait on queue R when R has submitted a batch strictly after Q's last submit.
        TArray<int32, (uint32)ECommandQueue::Num> LastSubmittedBatchIdx;
        for (uint32 i = 0; i < LastSubmittedBatchIdx.size(); ++i)
        {
            LastSubmittedBatchIdx[i] = -1;
        }

        for (uint32 BatchIdx = 0; BatchIdx < (uint32)Batches.size(); ++BatchIdx)
        {
            FRGBatch& Batch = Batches[BatchIdx];
            const uint32 BatchQueueIdx = (uint32)Batch.Queue;

            // Async batches opt out of cross-queue waits per the ERGExecutionFlags::Async
            // contract — the caller has promised the pass has no dependencies on prior work.
            if (!Batch.bIsAsync)
            {
                // Any other queue that submitted after our last submission on this queue must be
                // waited on for cross-queue memory visibility.
                const int32 OurLast = LastSubmittedBatchIdx[BatchQueueIdx];
                for (uint32 OtherQ = 0; OtherQ < (uint32)ECommandQueue::Num; ++OtherQ)
                {
                    if (OtherQ == BatchQueueIdx)
                    {
                        continue;
                    }

                    if (LastSubmittedBatchIdx[OtherQ] > OurLast)
                    {
                        GRenderContext->AddCommandQueueWait(Batch.Queue, (ECommandQueue)OtherQ);
                    }
                }
            }

            ICommandList* CL = Batch.CommandList.GetReference();
            GRenderContext->ExecuteCommandLists(&CL, 1, Batch.Queue);

            LastSubmittedBatchIdx[BatchQueueIdx] = (int32)BatchIdx;
        }
    }


    void FRenderGraph::Execute()
    {
        LUMINA_PROFILE_SCOPE();

        Compile();
        Record();
        Submit();
    }
}
