#include "pch.h"
#include "GPUProfiler.h"

#include "Core/Console/ConsoleVariable.h"
#include "Renderer/CommandList.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RHIGlobals.h"

namespace Lumina
{
    static TConsoleVar<bool> CVarGPUProfilingEnabled(
        "r.GPUProfiler.Enabled",
        false,
        "Enable GPU timestamp collection and the in-engine GPU profiler panel. Off by default to avoid the per-scope cost.");

    void FGPUProfileFrame::Reset()
    {
        Scopes.clear();
        ScopeStack.clear();
        QueryCursor = 0;
        StatsQueryCursor = 0;
        FrameNumber = 0;
        TotalTimeMs = 0.0f;
        TotalStats = {};
        State = EGPUFrameState::Idle;
    }

    FGPUProfiler& FGPUProfiler::Get()
    {
        static FGPUProfiler Instance;
        return Instance;
    }

    void FGPUProfiler::Shutdown()
    {
        for (FGPUProfileFrame& Frame : Frames)
        {
            Frame.Reset();
            Frame.QueryPool.clear();
            Frame.PipelineStatsPool.clear();
        }
        FrameTimeHistory.clear();
        LatestResolvedSlot = -1;
        RecordingSlot = 0;
        FrameCounter = 0;
    }

    bool FGPUProfiler::IsEnabled() const
    {
        return CVarGPUProfilingEnabled.GetValue() && Frames[RecordingSlot].State == EGPUFrameState::Recording;
    }

    void FGPUProfiler::BeginFrame()
    {
        const bool bEnabled = CVarGPUProfilingEnabled.GetValue();

        // Try to resolve every other slot, they may have completed since last visit.
        for (uint32 i = 0; i < MaxFramesInFlight; ++i)
        {
            if (i == RecordingSlot)
            {
                continue;
            }
            FGPUProfileFrame& Frame = Frames[i];
            if (Frame.State == EGPUFrameState::Submitted)
            {
                TryResolveFrame(Frame);
            }
        }

        FGPUProfileFrame& Slot = Frames[RecordingSlot];

        // If the slot we're about to write into is still pending GPU completion, force-resolve it.
        // GetTimerQueryTime spin-waits, but that only happens when we've already wrapped the ring.
        if (Slot.State == EGPUFrameState::Submitted)
        {
            TryResolveFrame(Slot);
        }

        Slot.Reset();

        if (!bEnabled)
        {
            // Leave Slot.State = Idle so IsEnabled() returns false this frame.
            return;
        }

        Slot.State = EGPUFrameState::Recording;
        Slot.FrameNumber = ++FrameCounter;
    }

    void FGPUProfiler::EndFrame()
    {
        FGPUProfileFrame& Slot = Frames[RecordingSlot];

        if (Slot.State == EGPUFrameState::Recording)
        {
            // Any unmatched scopes should not happen, but guard against it.
            Slot.ScopeStack.clear();
            Slot.State = EGPUFrameState::Submitted;
        }

        RecordingSlot = (RecordingSlot + 1) % MaxFramesInFlight;
    }

    void FGPUProfiler::BeginScope(ICommandList* CmdList, const char* Name, const FColor& Color)
    {
        if (!IsEnabled() || CmdList == nullptr)
        {
            return;
        }

        FGPUProfileFrame& Frame = Frames[RecordingSlot];

        if (Frame.Scopes.size() >= MaxScopesPerFrame)
        {
            return;
        }

        // Ensure we have a query for this scope.
        if (Frame.QueryCursor >= Frame.QueryPool.size())
        {
            FRHITimerQueryRef NewQuery = GRenderContext->CreateTimerQuery();
            if (!NewQuery.IsValid())
            {
                return;
            }
            Frame.QueryPool.push_back(NewQuery);
        }

        const int32 QueryIdx = (int32)Frame.QueryCursor++;
        ITimerQuery* Query = Frame.QueryPool[QueryIdx].GetReference();

        // Clear any leftover state from this query's previous use.
        GRenderContext->ResetTimerQuery(Query);

        // Pipeline-statistics queries share scope boundaries with timers.
        // Top-level scopes only, nested stats queries are illegal in Vulkan
        // (a stats query pool is single-active-per-command-buffer) and
        // flattening overlapping ranges isn't meaningful anyway.
        const bool bWantStats = Frame.ScopeStack.empty();
        int32 StatsIdx = -1;
        IPipelineStatsQuery* StatsQuery = nullptr;
        if (bWantStats)
        {
            if (Frame.StatsQueryCursor >= Frame.PipelineStatsPool.size())
            {
                FRHIPipelineStatsQueryRef NewStats = GRenderContext->CreatePipelineStatsQuery();
                if (NewStats.IsValid())
                {
                    Frame.PipelineStatsPool.push_back(NewStats);
                }
            }

            if (Frame.StatsQueryCursor < Frame.PipelineStatsPool.size())
            {
                StatsIdx = (int32)Frame.StatsQueryCursor++;
                StatsQuery = Frame.PipelineStatsPool[StatsIdx].GetReference();
                GRenderContext->ResetPipelineStatsQuery(StatsQuery);
            }
        }

        const int32 ParentIndex = Frame.ScopeStack.empty() ? -1 : Frame.ScopeStack.back();
        const int32 ScopeIndex  = (int32)Frame.Scopes.size();

        FGPUProfileScope& Scope = Frame.Scopes.push_back();
        Scope.Name            = Name ? Name : "<unnamed>";
        Scope.Color           = Color;
        Scope.ParentIndex     = ParentIndex;
        Scope.Depth           = ParentIndex >= 0 ? Frame.Scopes[ParentIndex].Depth + 1 : 0;
        Scope.QueryIndex      = QueryIdx;
        Scope.StatsQueryIndex = StatsIdx;

        Frame.ScopeStack.push_back(ScopeIndex);

        CmdList->AddMarker(Name ? Name : "<unnamed>", Color);
        CmdList->BeginTimerQuery(Query);
        if (StatsQuery != nullptr)
        {
            CmdList->BeginPipelineStatsQuery(StatsQuery);
        }
    }

    void FGPUProfiler::EndScope(ICommandList* CmdList)
    {
        if (!IsEnabled() || CmdList == nullptr)
        {
            return;
        }

        FGPUProfileFrame& Frame = Frames[RecordingSlot];
        if (Frame.ScopeStack.empty())
        {
            return;
        }

        const int32 ScopeIndex = Frame.ScopeStack.back();
        Frame.ScopeStack.pop_back();

        FGPUProfileScope& Scope = Frame.Scopes[ScopeIndex];
        if (Scope.QueryIndex >= 0 && Scope.QueryIndex < (int32)Frame.QueryPool.size())
        {
            ITimerQuery* Query = Frame.QueryPool[Scope.QueryIndex].GetReference();
            CmdList->EndTimerQuery(Query);
        }

        if (Scope.StatsQueryIndex >= 0 && Scope.StatsQueryIndex < (int32)Frame.PipelineStatsPool.size())
        {
            IPipelineStatsQuery* StatsQuery = Frame.PipelineStatsPool[Scope.StatsQueryIndex].GetReference();
            CmdList->EndPipelineStatsQuery(StatsQuery);
        }

        CmdList->PopMarker();
    }

    void FGPUProfiler::TryResolveFrame(FGPUProfileFrame& Frame)
    {
        if (Frame.State != EGPUFrameState::Submitted)
        {
            return;
        }

        // Only poll queries actually attached to a recorded scope this frame.
        // (Pool may contain stale entries from earlier frames whose bStarted is now false.)
        for (const FGPUProfileScope& Scope : Frame.Scopes)
        {
            if (Scope.QueryIndex < 0 || Scope.QueryIndex >= (int32)Frame.QueryPool.size())
            {
                continue;
            }
            ITimerQuery* Query = Frame.QueryPool[Scope.QueryIndex].GetReference();
            if (Query != nullptr && !GRenderContext->PollTimerQuery(Query))
            {
                return;
            }

            if (Scope.StatsQueryIndex >= 0 && Scope.StatsQueryIndex < (int32)Frame.PipelineStatsPool.size())
            {
                IPipelineStatsQuery* StatsQuery = Frame.PipelineStatsPool[Scope.StatsQueryIndex].GetReference();
                if (StatsQuery != nullptr && !GRenderContext->PollPipelineStatsQuery(StatsQuery))
                {
                    return;
                }
            }
        }

        float MaxTimeMs = 0.0f;
        FPipelineStats FrameStats;
        for (FGPUProfileScope& Scope : Frame.Scopes)
        {
            if (Scope.QueryIndex < 0 || Scope.QueryIndex >= (int32)Frame.QueryPool.size())
            {
                continue;
            }
            ITimerQuery* Query = Frame.QueryPool[Scope.QueryIndex].GetReference();
            const float Seconds = GRenderContext->GetTimerQueryTime(Query);
            Scope.ResolvedTimeMs = Seconds * 1000.0f;

            if (Scope.ParentIndex < 0 && Scope.ResolvedTimeMs > MaxTimeMs)
            {
                MaxTimeMs = Scope.ResolvedTimeMs;
            }

            if (Scope.StatsQueryIndex >= 0 && Scope.StatsQueryIndex < (int32)Frame.PipelineStatsPool.size())
            {
                IPipelineStatsQuery* StatsQuery = Frame.PipelineStatsPool[Scope.StatsQueryIndex].GetReference();
                Scope.ResolvedStats = GRenderContext->GetPipelineStats(StatsQuery);

                // Frame totals are the sum of top-level scopes only, nested
                // scopes would double-count (and nested scopes skip stats
                // queries anyway, so this is just belt-and-braces).
                if (Scope.ParentIndex < 0)
                {
                    FrameStats += Scope.ResolvedStats;
                }
            }
        }

        Frame.TotalTimeMs = MaxTimeMs;
        Frame.TotalStats = FrameStats;
        Frame.State = EGPUFrameState::Resolved;
        LatestResolvedSlot = (int32)(&Frame - &Frames[0]);

        if (FrameTimeHistory.size() >= FrameHistorySize)
        {
            FrameTimeHistory.erase(FrameTimeHistory.begin());
        }
        FrameTimeHistory.push_back(MaxTimeMs);
    }

    const FGPUProfileFrame* FGPUProfiler::GetLatestResolvedFrame() const
    {
        if (LatestResolvedSlot < 0)
        {
            return nullptr;
        }
        const FGPUProfileFrame& Frame = Frames[LatestResolvedSlot];
        return Frame.State == EGPUFrameState::Resolved ? &Frame : nullptr;
    }

    FGPUProfileScopeRAII::FGPUProfileScopeRAII(ICommandList* InCmdList, const char* Name, const FColor& Color)
        : CmdList(InCmdList)
        , bActive(FGPUProfiler::Get().IsEnabled())
    {
        if (bActive)
        {
            FGPUProfiler::Get().BeginScope(CmdList, Name, Color);
        }
    }

    FGPUProfileScopeRAII::~FGPUProfileScopeRAII()
    {
        if (bActive)
        {
            FGPUProfiler::Get().EndScope(CmdList);
        }
    }
}
