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

    // Compile -> Record (parallel per batch) -> Submit (per-queue with cross-queue waits).
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

        void Compile();
        void Record();
        void Submit();

        static ECommandQueue DeriveQueue(ERGPassFlags PassFlags, const FRGPassDescriptor* Descriptor);


        FLinearAllocator                GraphAllocator;
        TVector<FRenderGraphPass*>      Passes;
        // Tracked so destructors run at graph teardown.
        TVector<FRGPassDescriptor*>     Descriptors;
        TVector<FRGBatch>               Batches;
    };
}

#include "RenderGraph.inl"
