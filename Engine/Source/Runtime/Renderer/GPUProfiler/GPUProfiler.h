#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Math/Color.h"
#include "Memory/SmartPtr.h"
#include "Renderer/RenderResource.h"

namespace Lumina
{
    class ICommandList;
}

namespace Lumina
{
    struct RUNTIME_API FGPUProfileScope
    {
        FFixedString        Name;
        FColor              Color          = FColor::White;
        int32               ParentIndex    = -1;
        int32               Depth          = 0;
        int32               QueryIndex     = -1;    // Index into the owning frame's QueryPool
        int32               StatsQueryIndex= -1;    // Index into the owning frame's PipelineStatsPool; -1 if unused
        float               ResolvedTimeMs = 0.0f;
        FPipelineStats      ResolvedStats;
    };

    enum class EGPUFrameState : uint8
    {
        Idle,
        Recording,
        Submitted,
        Resolved,
    };

    struct RUNTIME_API FGPUProfileFrame
    {
        TVector<FGPUProfileScope>               Scopes;
        TVector<int32>                          ScopeStack;
        TVector<FRHITimerQueryRef>              QueryPool;
        TVector<FRHIPipelineStatsQueryRef>      PipelineStatsPool;
        uint32                                  QueryCursor         = 0;
        uint32                                  StatsQueryCursor    = 0;
        uint64                                  FrameNumber         = 0;
        float                                   TotalTimeMs         = 0.0f;
        FPipelineStats                          TotalStats;
        EGPUFrameState                          State               = EGPUFrameState::Idle;

        void Reset();
    };

    class RUNTIME_API FGPUProfiler
    {
    public:

        static constexpr uint32 MaxFramesInFlight    = 4;
        static constexpr uint32 MaxScopesPerFrame    = 128;
        static constexpr uint32 FrameHistorySize     = 240;

        static FGPUProfiler& Get();

        FGPUProfiler() = default;
        ~FGPUProfiler() = default;

        FGPUProfiler(const FGPUProfiler&) = delete;
        FGPUProfiler& operator = (const FGPUProfiler&) = delete;

        /** Release GPU resources, call before render context shuts down. */
        void Shutdown();

        /** Returns true when profiling is CVar-enabled AND a frame is actively recording. */
        bool IsEnabled() const;

        /** Called from RenderManager at the start of each frame. */
        void BeginFrame();

        /** Called from RenderManager after the primary command list has been submitted. */
        void EndFrame();

        /** Push a new GPU scope onto the active frame's stack. Records a begin-timestamp on CmdList. */
        void BeginScope(ICommandList* CmdList, const char* Name, const FColor& Color = FColor::White);

        /** Pop the active GPU scope. Records an end-timestamp on CmdList. */
        void EndScope(ICommandList* CmdList);

        /** Most recently resolved frame, or nullptr if none is ready. Used by the editor panel. */
        const FGPUProfileFrame* GetLatestResolvedFrame() const;

        /** Rolling window of total GPU frame times in ms for UI graphs. */
        const TVector<float>& GetFrameTimeHistory() const { return FrameTimeHistory; }

        // Diagnostic accessors, used by the editor debug panel to inspect ring-buffer state.
        uint32                  GetRecordingSlot() const            { return RecordingSlot; }
        int32                   GetLatestResolvedSlot() const       { return LatestResolvedSlot; }
        const FGPUProfileFrame& GetSlot(uint32 Index) const         { return Frames[Index]; }
        uint64                  GetFrameCounter() const             { return FrameCounter; }

    private:

        void TryResolveFrame(FGPUProfileFrame& Frame);

        FMutex              Mutex;
        FGPUProfileFrame    Frames[MaxFramesInFlight];
        uint32              RecordingSlot          = 0;
        int32               LatestResolvedSlot     = -1;
        uint64              FrameCounter           = 0;

        TVector<float>      FrameTimeHistory;
    };

    /** RAII helper emitted by the GPU_PROFILE_SCOPE macro. Zero-cost when CVar is disabled. */
    struct RUNTIME_API FGPUProfileScopeRAII
    {
        ICommandList*   CmdList;
        bool            bActive;

        FGPUProfileScopeRAII(ICommandList* InCmdList, const char* Name, const FColor& Color);
        ~FGPUProfileScopeRAII();

        FGPUProfileScopeRAII(const FGPUProfileScopeRAII&) = delete;
        FGPUProfileScopeRAII& operator = (const FGPUProfileScopeRAII&) = delete;
    };
}

#define LUMINA_GPU_JOIN_INNER(A, B) A##B
#define LUMINA_GPU_JOIN(A, B) LUMINA_GPU_JOIN_INNER(A, B)

#define GPU_PROFILE_SCOPE(CmdList, Name) \
    ::Lumina::FGPUProfileScopeRAII LUMINA_GPU_JOIN(GpuScope_, __LINE__)((CmdList), (Name), ::Lumina::FColor::White)

#define GPU_PROFILE_SCOPE_COLOR(CmdList, Name, Color) \
    ::Lumina::FGPUProfileScopeRAII LUMINA_GPU_JOIN(GpuScope_, __LINE__)((CmdList), (Name), (Color))
