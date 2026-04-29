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
     * Snapshot of a single Lua stack level. Captured eagerly at break time —
     * we never read live VM state from the editor UI thread because the VM
     * is already mid-callback and reading after we resume would race.
     */
    struct FStackFrame
    {
        FString Source;       // Virtual path (== Luau chunkname for path-loaded scripts).
        FString FunctionName; // Best-effort: lua_getinfo "name", or "?" / "main".
        FString What;         // "Lua", "C", "main", "tail" — from lua_Debug::what.
        int     Line = -1;
        TVector<FStackVariable> Locals;
        TVector<FStackVariable> Upvalues;
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
        // editor line — totally unusable as a step experience.
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

        // Path → set of 0-based line indices.
        THashMap<FString, THashSet<int>>            Breakpoints;

        EResumeRequest                              PendingResume = EResumeRequest::None;
    };
}
