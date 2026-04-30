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

    /** One slot in the Locals/Upvalues panel for a captured stack frame. */
    struct FStackVariable
    {
        FString Name;
        FString Value;     // Stringified eagerly at break time.
        FString TypeName;  // "string", "number", "table", "function", ...
    };

    /**
     * Snapshot of a single Lua stack level. /* captured eagerly at break time
     * we never read live VM state from the editor UI thread because the VM
     * is already mid-callback and reading after we resume would race.
     */
    struct FStackFrame
    {
        FString Source;       // Virtual path (== Luau chunkname for path-loaded scripts).
        FString FunctionName; // Best-effort: lua_getinfo "name", or "?" / "main".
        FString What;         // "Lua", "C", "main", "tail" from lua_Debug::what.
        int     Line = -1;
        TVector<FStackVariable> Locals;
        TVector<FStackVariable> Upvalues;
    };

    /**
     * Per-breakpoint metadata. Stored alongside the line in the Breakpoints
     * registry. The line itself living in the THashSet indicates the breakpoint
     * exists; this struct controls whether/when it actually pauses.
     */
    struct FBreakpointSettings
    {
        FString  Condition;            // Lua expression; pause only if it returns truthy. Empty = always pause.
        FString  LogMessage;           // Non-empty turns this into a log point: log + continue, never pause.
        uint32   HitCount = 0;         // Total times this breakpoint has fired this session.
        uint32   IgnoreCount = 0;      // Skip the first N hits before pausing/logging.
        bool     bEnabled = true;
    };

    /**
     * A single child of an expandable value (table key or userdata field)
     * surfaced through EnumerateChildrenInPausedFrame.
     */
    struct FChildEntry
    {
        FString Key;            // Already-formatted key, e.g. `foo` or `[1]` or `["weird key"]`.
        FString AccessSuffix;   // What to append to the parent path to address this child:
                                //   `.foo`, `[1]`, `["weird key"]`. Lets the editor build
                                //   a Lua-syntax-correct path for nested expansion / watches.
        FString Value;
        FString TypeName;
        bool    bIsExpandable = false;
    };

    /** A single entry in the recent break-points history. */
    struct FBreakHistoryEntry
    {
        FString Source;
        FString FunctionName;
        int     Line = -1;
        double  TimeSeconds = 0.0;     // Engine-time snapshot at break.
        bool    bWasLogPoint = false;
    };

    /**
     * Editor-side Luau debugger.
     *
     * Owned by FScriptingContext. Hooks into lua_callbacks for the engine's
     * single shared lua_State. Uses lua_breakpoint to set break opcodes on
     * loaded module closures; on hit, captures a stack snapshot and yields
     * the running coroutine via lua_break so the editor stays responsive
     * while the user inspects state. Resumes on the main thread when the
     * user clicks Continue / Step.
     *
     * This class is single-threaded: every entry point assumes the main
     * thread is the active one. The Lua VM itself enforces this.
     */
    class RUNTIME_API FLuaDebugger
    {
    public:

        static FLuaDebugger& Get();

        void Initialize(lua_State* InMainState);
        void Shutdown();

        // Breakpoint registry, keyed by virtual script path. Lines are 0-based
        // editor-line indices; Luau's line numbering is 1-based, so we add one
        // when calling lua_breakpoint and subtract one when reading currentline.
        void SetBreakpoint(FStringView Path, int LineZeroBased, bool bEnabled);
        void ClearBreakpointsFor(FStringView Path);
        bool HasBreakpoint(FStringView Path, int LineZeroBased) const;
        TVector<int> GetBreakpointLines(FStringView Path) const;

        // Per-breakpoint metadata. SetBreakpointCondition / LogMessage / Enabled
        // mutate the entry without removing it; if the breakpoint doesn't exist
        // they're a no-op. GetBreakpointSettings returns nullptr when absent.
        void SetBreakpointCondition(FStringView Path, int LineZeroBased, FStringView Condition);
        void SetBreakpointLogMessage(FStringView Path, int LineZeroBased, FStringView Message);
        void SetBreakpointEnabled(FStringView Path, int LineZeroBased, bool bEnabled);
        void SetBreakpointIgnoreCount(FStringView Path, int LineZeroBased, uint32 IgnoreCount);
        const FBreakpointSettings* GetBreakpointSettings(FStringView Path, int LineZeroBased) const;
        FBreakpointSettings* GetBreakpointSettingsMutable(FStringView Path, int LineZeroBased);

        // One-shot breakpoint cleared on first hit. Used by "run to cursor".
        void RunToLine(FStringView Path, int LineZeroBased);
        void ClearRunToLine();
        bool HasRunToLine() const { return RunToTargetLine >= 0; }

        // Evaluate `Expr` in the paused frame's environment. Locals + upvalues
        // are visible by name with global fallback. OutValue receives a
        // stringified result (or the error message); OutTypeName receives the
        // Lua type name ("string", "number", ..., or "error" on failure).
        // Returns false if not paused or FrameIndex is out of range.
        bool EvaluateInPausedFrame(int FrameIndex, FStringView Expr, FString& OutValue, FString& OutTypeName);

        // Like EvaluateInPausedFrame but additionally reports whether the
        // resolved value is a container the editor can expand (table or
        // userdata with an iterable metatable). Used by hover tooltips so
        // they can show "{ ... 4 fields }" instead of just `table: 0x...`.
        bool EvaluateInPausedFrameWithExpandable(int FrameIndex, FStringView Expr,
                                                  FString& OutValue, FString& OutTypeName,
                                                  bool& bOutExpandable);

        // Enumerates the immediate children of the value reached by Expr
        // (which is itself evaluated in the paused frame's env). Tables list
        // their keys; userdata lists the keys of their metatable's __index
        // table (when present). Capped at MaxChildren to bound memory and
        // tooltip rendering cost. Children with non-string/non-number keys
        // are skipped.
        bool EnumerateChildrenInPausedFrame(int FrameIndex, FStringView Expr,
                                             TVector<FChildEntry>& Out,
                                             int MaxChildren = 256);

        // Recent break-points (most recent first). Used by the editor's
        // "Recent breaks" panel. Capped at 32 entries to bound memory.
        const TVector<FBreakHistoryEntry>& GetBreakHistory() const { return BreakHistory; }
        void ClearBreakHistory() { BreakHistory.clear(); }

        // Called by FScriptingContext::LoadUniqueScriptPath right after a
        // freshly compiled FScript is registered. Walks pending breakpoints
        // for the path and installs them on the new module closure so reload
        // continues to honor user-set breakpoints.
        void OnScriptLoaded(FScript& Script);

        // Pause introspection.
        EDebuggerStatus GetStatus() const { return Status; }
        bool IsPaused() const { return Status == EDebuggerStatus::Paused; }
        FStringView GetPausedSource() const { return FStringView(PausedSource.c_str(), PausedSource.size()); }
        int GetPausedLineZeroBased() const { return PausedLine; }
        const TVector<FStackFrame>& GetCallStack() const { return CallStack; }

        // User controls. Each schedules a resume on the next Tick().
        void RequestContinue();
        void RequestStepInto();
        void RequestStepOver();
        void RequestStepOut();
        void RequestStop();

        // Pumped from the editor main loop after each frame's deferred-action
        // pass. Resumes the paused thread when the user has issued a control.
        void Tick();

        // Filthy hack alert: lua_callbacks invokes a free function pointer,
        // not a member, so the callback wrappers need access to the singleton.
        // These are public for that reason but should be treated as private.
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

        // State captured at resume time so HandleDebugStep can decide whether
        // we have actually progressed to a new source line. Without these we
        // would break on every VM instruction, which is multiple times per
        // editor line, totally unusable as a step experience.
        int                                         StepBaseDepth = 0;
        int                                         StepLastLine = -1;
        FString                                     StepLastSource;

        // Luau's LOP_BREAK handler bails out of luau_execute when lua_break
        // is called inside debugbreak, and crucially does NOT advance the
        // program counter past the LOP_BREAK opcode. On resume the same
        // LOP_BREAK fires again and re-enters debugbreak, looping forever
        // on the same line.
        //
        // Workaround: remember the (thread, source, line) we just paused on
        // and skip exactly one debugbreak on that combination after resume.
        // The skipped callback returns without lua_break, so the VM falls
        // through to VM_CONTINUE(op) and runs the original instruction.
        lua_State*                                  SkipNextBreakThread = nullptr;
        FString                                     SkipNextBreakSource;
        int                                         SkipNextBreakLine = -1;

        // Set inside the debugbreak/step callback. The thread is anchored
        // via PausedThreadRef so the GC doesn't reclaim it before we resume.
        lua_State*                                  PausedThread = nullptr;
        int                                         PausedThreadRef = -1;

        FString                                     PausedSource;
        int                                         PausedLine = -1;

        TVector<FStackFrame>                        CallStack;

        // Path -> set of 0-based line indices.
        THashMap<FString, THashSet<int>>            Breakpoints;

        // Path -> (line -> settings). Sparse: only lines that need non-default
        // settings (condition / log message / disabled / hit count) get an entry.
        THashMap<FString, THashMap<int, FBreakpointSettings>> BreakpointSettings;

        // One-shot breakpoint state. RunToLine arms it; the first matching
        // break-or-step on this path/line clears it before pausing.
        FString                                     RunToTargetSource;
        int                                         RunToTargetLine = -1;

        TVector<FBreakHistoryEntry>                 BreakHistory;

        EResumeRequest                              PendingResume = EResumeRequest::None;

        // Internal: filter the breakpoint hit. Returns true if execution
        // should pause; false to continue running (disabled / condition-false /
        // log-point / ignore-count). Logs and stamps history as a side effect.
        bool ProcessBreakpointHit(lua_State* L, lua_Debug* ar, FStringView Source, int LineZeroBased);

        // Internal: compile + run `return (Expr)` in the paused frame's env
        // and leave the result on top of MainState's stack. On failure returns
        // false with the error message in OutError and the stack untouched
        // beyond StackBefore.
        bool PushFrameEvalResult(int FrameIndex, FStringView Expr, FString& OutError);

        // Internal: append a break entry, capping at 32. Most recent at front.
        void RecordBreakHistory(FStringView Source, FStringView FunctionName, int Line, bool bLogPoint);
    };
}
