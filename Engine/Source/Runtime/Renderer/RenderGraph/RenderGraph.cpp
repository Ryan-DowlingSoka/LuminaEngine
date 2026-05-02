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
    // Sized for ~50 passes/frame (~1.5KB each + descriptors). Reset on graph destruction.
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
        // Linear allocator doesn't run dtors; do it manually so captured refs release.
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

        // One CL per batch keeps state tracking consistent within the batch; batches are
        // independent (different queues / explicit Async), so they record in parallel.
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

        // Q must wait on R when R submitted a batch after Q's last submit on this queue.
        TArray<int32, (uint32)ECommandQueue::Num> LastSubmittedBatchIdx;
        for (uint32 i = 0; i < LastSubmittedBatchIdx.size(); ++i)
        {
            LastSubmittedBatchIdx[i] = -1;
        }

        for (uint32 BatchIdx = 0; BatchIdx < (uint32)Batches.size(); ++BatchIdx)
        {
            FRGBatch& Batch = Batches[BatchIdx];
            const uint32 BatchQueueIdx = (uint32)Batch.Queue;

            // Async batches skip cross-queue waits per the ERGExecutionFlags::Async contract.
            if (!Batch.bIsAsync)
            {
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
