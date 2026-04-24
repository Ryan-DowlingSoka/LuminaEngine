#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Math/Color.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{
    class CWorld;
    enum class EWorldType : uint8;
    enum class ENetMode : uint8;

    struct RUNTIME_API FCPUProfileScope
    {
        FFixedString    Name;
        FColor          Color       = FColor::White;
        int32           ParentIndex = -1;
        int32           Depth       = 0;
        double          StartMs     = 0.0;
        double          EndMs       = 0.0;

        double DurationMs() const { return EndMs - StartMs; }
    };

    struct RUNTIME_API FCPUProfileFrame
    {
        TVector<FCPUProfileScope>   Scopes;
        TVector<int32>              ScopeStack;
        uint64                      FrameNumber     = 0;
        double                      FrameStartMs    = 0.0;
        double                      TotalTimeMs     = 0.0;

        void Reset();
    };

    enum class ECPUTargetKind : uint8
    {
        Custom,
        World,
    };

    // One profiler "track". One world == one target, so client / server / editor
    // worlds accumulate independently and can be inspected side-by-side.
    struct RUNTIME_API FCPUProfileTarget
    {
        FFixedString        Name;
        void*               Key                 = nullptr;
        ECPUTargetKind      Kind                = ECPUTargetKind::Custom;

        // World metadata for world targets; lets the editor panel badge
        // net-role and PIE without chasing a possibly-destroyed CWorld*.
        EWorldType          WorldType           = EWorldType{};
        ENetMode            NetMode             = ENetMode{};
        bool                bPIE                = false;

        FCPUProfileFrame    Current;
        FCPUProfileFrame    Latest;
        TVector<float>      FrameTimeHistory;
        uint64              LastActiveFrame     = 0;
        bool                bHasResolvedFrame   = false;
    };

    class RUNTIME_API FCPUProfiler
    {
    public:

        static constexpr uint32 FrameHistorySize       = 240;
        static constexpr uint64 StaleTargetFrameCount  = 120;

        static FCPUProfiler& Get();

        FCPUProfiler() = default;
        ~FCPUProfiler() = default;

        FCPUProfiler(const FCPUProfiler&) = delete;
        FCPUProfiler& operator = (const FCPUProfiler&) = delete;

        bool IsEnabled() const;

        // Engine frame boundary: rolls targets Current -> Latest and starts a new Current.
        void BeginFrame();
        void EndFrame();

        // Activation. Scopes emitted between Push/Pop route to the pushed target.
        // Safe to call with profiling disabled; becomes a cheap no-op.
        void PushTarget(void* Key, const char* Name, ECPUTargetKind Kind);
        void PushWorldTarget(CWorld* World);
        void PopTarget();

        // Called by FCPUProfileScopeRAII. No-op when no active target or disabled.
        void BeginScope(const char* Name, const FColor& Color);
        void EndScope();

        // Editor accessors.
        const TVector<TUniquePtr<FCPUProfileTarget>>& GetTargets() const { return Targets; }
        const FCPUProfileTarget* FindTargetByKey(void* Key) const;
        uint64 GetFrameCounter() const { return FrameCounter; }

    private:

        FCPUProfileTarget* FindOrCreate(void* Key, const char* Name, ECPUTargetKind Kind);
        FCPUProfileTarget* ActiveTarget() const;
        void FinalizeFrame(FCPUProfileTarget& Target);

        TVector<TUniquePtr<FCPUProfileTarget>>  Targets;
        TVector<FCPUProfileTarget*>             TargetStack;
        uint64                                  FrameCounter    = 0;
    };

    struct RUNTIME_API FCPUProfileScopeRAII
    {
        bool bActive;

        FCPUProfileScopeRAII(const char* Name, const FColor& Color);
        ~FCPUProfileScopeRAII();

        FCPUProfileScopeRAII(const FCPUProfileScopeRAII&) = delete;
        FCPUProfileScopeRAII& operator = (const FCPUProfileScopeRAII&) = delete;
    };
}

#define LUMINA_CPU_JOIN_INNER(A, B) A##B
#define LUMINA_CPU_JOIN(A, B) LUMINA_CPU_JOIN_INNER(A, B)

#define CPU_PROFILE_SCOPE(Name) \
    ::Lumina::FCPUProfileScopeRAII LUMINA_CPU_JOIN(CpuScope_, __LINE__)((Name), ::Lumina::FColor::White)

#define CPU_PROFILE_SCOPE_COLOR(Name, Color) \
    ::Lumina::FCPUProfileScopeRAII LUMINA_CPU_JOIN(CpuScope_, __LINE__)((Name), (Color))
