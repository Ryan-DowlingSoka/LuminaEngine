#pragma once
#include "RenderGraphContext.h"
#include "RenderGraphEvent.h"
#include "RenderGraphTypes.h"
#include "RenderGraphDescriptor.h"
#include "Containers/Array.h"
#include "Memory/Allocators/Allocator.h"


namespace Lumina
{
    class FRGPassDescriptor;
    struct FBindingLayoutDesc;
    class FRHIBindingSet;
    class FRenderGraphPass;
}

namespace Lumina
{
    namespace Concept
    {
        template <typename ExecutorType>
        concept TExecutor = (sizeof(ExecutorType) <= 1024) && eastl::is_invocable_v<ExecutorType, ICommandList&>;
    }

    /**
     * FRenderGraph
     *
     * Build-and-execute style render graph.
     *
     * Usage:
     *     FRenderGraph Graph;
     *     FRGPassDescriptor* D = Graph.AllocDescriptor();
     *     D->Read(SomeImage);
     *     D->Write(SomeOtherImage);
     *     Graph.AddPass(RG_Compute, "MyPass", D, [&](ICommandList& CL) { ... });
     *     ...
     *     Graph.Execute();
     *
     * Execution pipeline:
     *   1. Compile()  - Derives per-pass queue, builds batches of consecutive same-queue
     *                   passes. Optionally uses declared resource accesses for dependency
     *                   analysis and reordering opportunities.
     *   2. Record()   - Records every batch onto its own command list in parallel using
     *                   the task system. Within a batch, all passes share a single CL so
     *                   the state tracker carries resource state across them.
     *   3. Submit()   - Submits command lists in batch order per queue, inserting timeline
     *                   semaphore waits at cross-queue boundaries via AddCommandQueueWait.
     *
     * Queue routing:
     *   - RG_Raster                      -> Graphics queue
     *   - RG_Compute + AsyncCompute      -> Compute queue (concurrent with graphics)
     *   - RG_Compute                     -> Graphics queue (in-order with graphics work)
     *   - RG_Transfer + AsyncTransfer    -> Transfer queue
     *   - RG_Transfer                    -> Graphics queue
     */
    class RUNTIME_API FRenderGraph
    {
    public:
        FRenderGraph();
        ~FRenderGraph();

        LE_NO_COPYMOVE(FRenderGraph);

        template <Concept::TExecutor ExecutorType>
        FRGPassHandle AddPass(ERGPassFlags PassFlags, FStringView EventName, const FRGPassDescriptor* Parameters, ExecutorType&& Executor);

        FRGPassDescriptor* AllocDescriptor();

        void Execute();

        template<typename T, typename... TArgs>
        T* Alloc(TArgs&&... Args)
        {
            return GraphAllocator.TAlloc<T>(Forward<TArgs>(Args)...);
        }

    private:

        /** Analyze all passes and build batches for submission. */
        void Compile();

        /** Record every pass onto its own command list in parallel. */
        void Record();

        /** Submit per-queue batches with cross-queue semaphore waits. */
        void Submit();


        /** Derive the command queue to use for this pass from its flags. */
        static ECommandQueue DeriveQueue(ERGPassFlags PassFlags, const FRGPassDescriptor* Descriptor);


        FLinearAllocator                GraphAllocator;

        /** All passes in declaration order. */
        TVector<FRenderGraphPass*>      Passes;

        /** Descriptors issued via AllocDescriptor. Tracked so destructors run at graph teardown. */
        TVector<FRGPassDescriptor*>     Descriptors;

        /** Compiled batches — runs of consecutive same-queue passes. */
        TVector<FRGBatch>               Batches;
    };
}

#include "RenderGraph.inl"
