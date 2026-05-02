#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"

#include <EASTL/set.h>

struct lua_State;
struct lua_Debug;

namespace Lumina::Lua
{
    struct FScript;

    enum class EDebuggerStatus : uint8
    {
        Running,
        Paused,
    };

    enum class EStepMode : uint8
    {
        None,
        Into,
        Over,
        Out,
    };

    struct FStackVariable
    {
        FString Name;
        FString Value;     // Stringified eagerly at break.
        FString TypeName;
    };

    // Snapshot captured at break: live VM state is unsafe to read post-resume.
    struct FStackFrame
    {
        FString Source;
        FString FunctionName;
        FString What;
        int     Line = -1;
        TVector<FStackVariable> Locals;
        TVector<FStackVariable> Upvalues;
    };

    // Per-breakpoint metadata; sparse, lines without metadata still break unconditionally.
    struct FBreakpointSettings
    {
        FString  Condition;            // Empty = always pause.
        FString  LogMessage;           // Non-empty = log point (log + continue, never pause).
        uint32   HitCount = 0;
        uint32   IgnoreCount = 0;
        bool     bEnabled = true;
    };

    struct FChildEntry
    {
        FString Key;            // Formatted, e.g. `foo`, `[1]`, `["weird key"]`.
        FString AccessSuffix;   // Append to parent path to address this child.
        FString Value;
        FString TypeName;
        bool    bIsExpandable = false;
    };

    struct FBreakHistoryEntry
    {
        FString Source;
        FString FunctionName;
        int     Line = -1;
        double  TimeSeconds = 0.0;
        bool    bWasLogPoint = false;
    };

    // Editor-side Luau debugger. Hooks lua_callbacks; on hit, yields via lua_break
    // and captures a snapshot. Single-threaded; main-thread only.
    class RUNTIME_API FLuaDebugger
    {
    public:

        static FLuaDebugger& Get();

        void Initialize(lua_State* InMainState);
        void Shutdown();

        // Lines are 0-based; Luau is 1-based, so +/- 1 at the boundary.
        void SetBreakpoint(FStringView Path, int LineZeroBased, bool bEnabled);
        void ClearBreakpointsFor(FStringView Path);
        bool HasBreakpoint(FStringView Path, int LineZeroBased) const;
        TVector<int> GetBreakpointLines(FStringView Path) const;

        // No-op if the breakpoint doesn't exist; Get returns nullptr when absent.
        void SetBreakpointCondition(FStringView Path, int LineZeroBased, FStringView Condition);
        void SetBreakpointLogMessage(FStringView Path, int LineZeroBased, FStringView Message);
        void SetBreakpointEnabled(FStringView Path, int LineZeroBased, bool bEnabled);
        void SetBreakpointIgnoreCount(FStringView Path, int LineZeroBased, uint32 IgnoreCount);
        const FBreakpointSettings* GetBreakpointSettings(FStringView Path, int LineZeroBased) const;
        FBreakpointSettings* GetBreakpointSettingsMutable(FStringView Path, int LineZeroBased);

        // One-shot run-to-cursor breakpoint.
        void RunToLine(FStringView Path, int LineZeroBased);
        void ClearRunToLine();
        bool HasRunToLine() const { return RunToTargetLine >= 0; }

        // Evaluate Expr in the paused frame env (locals + upvalues + globals fallback).
        bool EvaluateInPausedFrame(int FrameIndex, FStringView Expr, FString& OutValue, FString& OutTypeName);

        // Adds bOutExpandable for tooltips ("{ ... 4 fields }" vs `table: 0x...`).
        bool EvaluateInPausedFrameWithExpandable(int FrameIndex, FStringView Expr,
                                                  FString& OutValue, FString& OutTypeName,
                                                  bool& bOutExpandable);

        // Lists immediate children. Skips non-string/non-number keys.
        bool EnumerateChildrenInPausedFrame(int FrameIndex, FStringView Expr,
                                             TVector<FChildEntry>& Out,
                                             int MaxChildren = 256);

        // Most recent first; capped at 32.
        const TVector<FBreakHistoryEntry>& GetBreakHistory() const { return BreakHistory; }
        void ClearBreakHistory() { BreakHistory.clear(); }

        // Called after a script is registered to install pending breakpoints on the new closure.
        void OnScriptLoaded(FScript& Script);

        EDebuggerStatus GetStatus() const { return Status; }
        bool IsPaused() const { return Status == EDebuggerStatus::Paused; }
        FStringView GetPausedSource() const { return FStringView(PausedSource.c_str(), PausedSource.size()); }
        int GetPausedLineZeroBased() const { return PausedLine; }
        const TVector<FStackFrame>& GetCallStack() const { return CallStack; }

        // Each schedules a resume on the next Tick().
        void RequestContinue();
        void RequestStepInto();
        void RequestStepOver();
        void RequestStepOut();
        void RequestStop();

        void Tick();

        // Public only because lua_callbacks needs free-function trampolines; treat as private.
        void HandleDebugBreak(lua_State* L, lua_Debug* ar);
        void HandleDebugStep(lua_State* L, lua_Debug* ar);

    private:

        void EnterPause(lua_State* L, lua_Debug* ar);
        void CaptureCallStack(lua_State* L);
        void InstallBreakpointsForScript(FScript& Script, const THashSet<int>& LinesZeroBased);
        void InstallBreakpointForScript(FScript& Script, int LineZeroBased, bool bEnabled);

        enum class EResumeRequest : uint8
        {
            None,
            Continue,
            StepInto,
            StepOver,
            StepOut,
            Stop,
        };

        lua_State*                                  MainState = nullptr;
        EDebuggerStatus                             Status = EDebuggerStatus::Running;
        EStepMode                                   StepMode = EStepMode::None;

        // Captured at resume so HandleDebugStep can detect line progression.
        int                                         StepBaseDepth = 0;
        int                                         StepLastLine = -1;
        FString                                     StepLastSource;

        // LOP_BREAK doesn't advance the PC; without this skip, resume re-fires the same break.
        // Skip exactly one debugbreak on this (thread, source, line) tuple after resume.
        lua_State*                                  SkipNextBreakThread = nullptr;
        FString                                     SkipNextBreakSource;
        int                                         SkipNextBreakLine = -1;

        // Anchored via PausedThreadRef so GC doesn't reclaim before resume.
        lua_State*                                  PausedThread = nullptr;
        int                                         PausedThreadRef = -1;

        FString                                     PausedSource;
        int                                         PausedLine = -1;

        TVector<FStackFrame>                        CallStack;

        // Path -> set of 0-based line indices.
        THashMap<FString, THashSet<int>>            Breakpoints;

        // Sparse: only lines with non-default settings have an entry.
        THashMap<FString, THashMap<int, FBreakpointSettings>> BreakpointSettings;

        FString                                     RunToTargetSource;
        int                                         RunToTargetLine = -1;

        TVector<FBreakHistoryEntry>                 BreakHistory;

        EResumeRequest                              PendingResume = EResumeRequest::None;

        // Returns true to pause; false to keep running (disabled/condition-false/log-point/ignored).
        bool ProcessBreakpointHit(lua_State* L, lua_Debug* ar, FStringView Source, int LineZeroBased);

        // Compiles + runs `return (Expr)` in the frame env; leaves result on MainState top.
        bool PushFrameEvalResult(int FrameIndex, FStringView Expr, FString& OutError);

        void RecordBreakHistory(FStringView Source, FStringView FunctionName, int Line, bool bLogPoint);
    };
}
