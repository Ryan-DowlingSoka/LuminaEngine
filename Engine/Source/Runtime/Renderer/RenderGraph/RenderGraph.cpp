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
    FRenderGraph::FRenderGraph()
        : GraphAllocator(1024llu * 10llu)
    {
        PassGroups.reserve(24);
    }

    FRGPassDescriptor* FRenderGraph::AllocDescriptor()
    {
        return GraphAllocator.TAlloc<FRGPassDescriptor>();
    }

    void FRenderGraph::Execute()
    {
        LUMINA_PROFILE_SCOPE();
        
        TFixedVector<FTaskHandle, 1> TaskHandles;
        TFixedVector<ICommandList*, 1> AllCommandLists;
        
		FMutex CommandListMutex;

        FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
        AllCommandLists.push_back(CommandList);
        CommandList->Open();
        
        for (TVector<FRenderGraphPass*>& Passes : PassGroups)
        {
            for (FRenderGraphPass* Pass : Passes)
            {
                // The user has promised us this pass can now run at any time without issues, so we dispatch it and keep going.
                if (Pass->GetDescriptor()->HasAnyFlag(ERGExecutionFlags::Async))
                {
                    auto Task = Task::AsyncTask(1, 1, [&CommandListMutex, &AllCommandLists, Pass](uint32, uint32, uint32)
                    {
                        FRHICommandListRef LocalCommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
                    
                        {
                            FScopeLock Lock(CommandListMutex);
                            AllCommandLists.emplace_back(LocalCommandList);
                        }
            
                        LocalCommandList->Open();
                        LocalCommandList->AddMarker(Pass->GetEvent().Get(), FColor::MakeRandom());
                        Pass->Execute(*LocalCommandList);
                        LocalCommandList->PopMarker();
                        LocalCommandList->Close();
                    });
                    
                    TaskHandles.emplace_back(Task);
                }
                else // Run the pass serially.
                {
                    CommandList->AddMarker(Pass->GetEvent().Get(), FColor::MakeRandom());
                    Pass->Execute(*CommandList);
                    CommandList->PopMarker();
                }
            }
        }
        
        CommandList->Close();

        for (const FTaskHandle& Task : TaskHandles)
        {
            Task->Wait();
        }
        
        GRenderContext->ExecuteCommandLists(AllCommandLists.data(), (uint32)AllCommandLists.size(), ECommandQueue::Graphics);
    }
}
