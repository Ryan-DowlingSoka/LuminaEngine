#pragma once
#include "RenderGraphContext.h"
#include "RenderGraphEvent.h"
#include "RenderGraphTypes.h"
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
    
    template<Concept::TExecutor ExecutorType>
    struct RGParallelPassSpec
    {
        RGParallelPassSpec(ERGPassFlags InPassFlags, FRGEvent&& InEvent, const FRGPassDescriptor* InParameters, ExecutorType&& InExecutor)
            : PassFlags(InPassFlags)
            , Event(Forward<FRGEvent>(InEvent))
            , Descriptor(InParameters)
            , Executor(Forward<ExecutorType>(InExecutor))
        {}
        
        ERGPassFlags PassFlags;
        FRGEvent Event;
        const FRGPassDescriptor* Descriptor;
        ExecutorType Executor;
    };

    
    class RUNTIME_API FRenderGraph
    {
    public:
        FRenderGraph();
        ~FRenderGraph() = default;
        
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
        
        
        FLinearAllocator                GraphAllocator;
        TVector<TVector<FRGPassHandle>> PassGroups;
    };
}

#include "RenderGraph.inl"