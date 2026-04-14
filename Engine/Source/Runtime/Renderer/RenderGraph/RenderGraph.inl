#pragma once

#include "RenderGraphPass.h"
#include "Memory/Allocators/Allocator.h"

namespace Lumina
{
    template <Concept::TExecutor ExecutorType>
    FRGPassHandle FRenderGraph::AddPass(ERGPassFlags PassFlags, FStringView EventName, const FRGPassDescriptor* Parameters, ExecutorType&& Executor)
    {
        FRGPassHandle Pass =  GraphAllocator.TAlloc<TRGPass<ExecutorType>>(FRGEvent(EventName), PassFlags, Parameters, Forward<ExecutorType>(Executor));
        PassGroups.emplace_back().push_back(Pass);

        return Pass;
    }
}