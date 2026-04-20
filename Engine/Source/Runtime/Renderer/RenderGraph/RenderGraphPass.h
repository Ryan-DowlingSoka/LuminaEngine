#pragma once
#include "RenderGraphEvent.h"
#include "RenderGraphTypes.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/RHIFwd.h"


namespace Lumina
{
    class FRGPassDescriptor;
    enum class ERGPassFlags : uint8;
    enum class ERGExecutionFlags : uint8;
}

namespace Lumina
{
    /**
     * Base class for a render graph pass.
     *
     * A pass holds an executor that will be invoked with a command list during Execute().
     * The render graph compiles all passes into batches, records them in parallel, and
     * submits them per-queue with cross-queue synchronization inserted automatically.
     */
    class RUNTIME_API alignas(64) FRenderGraphPass
    {
        friend class FRenderGraph;

    public:

        FRenderGraphPass(FRGEvent&& InEvent, ERGPassFlags InFlags, const FRGPassDescriptor* InDescriptor)
            : Event(InEvent)
            , PassFlags(InFlags)
            , Descriptor(InDescriptor)
        {}

        virtual ~FRenderGraphPass() = default;

        virtual void Execute(ICommandList& CommandList) = 0;

        ECommandQueue                   GetQueue() const { return Queue; }
        ERGPassFlags                    GetPassFlags() const { return PassFlags; }
        const FRGPassDescriptor*        GetDescriptor() const { return Descriptor; }
        const FRGEvent&                 GetEvent() const { return Event; }

    protected:

        FRGEvent                        Event;
        ERGPassFlags                    PassFlags = ERGPassFlags::None;

        // Compiled state (filled during FRenderGraph::Compile).
        ECommandQueue                   Queue = ECommandQueue(0);
        uint32                          PassIndex = 0;
        uint32                          BatchIndex = ~0u;

        const FRGPassDescriptor*        Descriptor = nullptr;
    };

    /**
     * Concrete pass holding a user-provided executor lambda.
     * The executor is invoked with the pass's command list during Execute().
     */
    template <typename ExecutorType>
    requires(sizeof(ExecutorType) <= 1024)
    class TRGPass : public FRenderGraphPass
    {
    public:

        TRGPass(FRGEvent&& InEvent, ERGPassFlags InFlags, const FRGPassDescriptor* InParams, ExecutorType&& Executor)
            : FRenderGraphPass(std::move(InEvent), InFlags, InParams)
            , ExecutionLambda(eastl::forward<ExecutorType>(Executor))
        {}

        void Execute(ICommandList& InCommandList) override
        {
            eastl::invoke(ExecutionLambda, InCommandList);
        }

    private:

        ExecutorType ExecutionLambda;
    };
}
