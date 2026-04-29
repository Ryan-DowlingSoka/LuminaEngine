#include "pch.h"
#include "LuaDebugger.h"

#include "Log/Log.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/ScriptTypes.h"

#include "lua.h"
#include "lualib.h"

namespace Lumina::Lua
{
    static FLuaDebugger GDebugger;

    FLuaDebugger& FLuaDebugger::Get()
    {
        return GDebugger;
    }

    namespace
    {
        // lua_callbacks holds free function pointers, no userdata channel.
        // Forward through a translation unit local that knows where the
        // singleton lives.
        void DebugBreakTrampoline(lua_State* L, lua_Debug* ar)
        {
            FLuaDebugger::Get().HandleDebugBreak(L, ar);
        }

        void DebugStepTrampoline(lua_State* L, lua_Debug* ar)
        {
            FLuaDebugger::Get().HandleDebugStep(L, ar);
        }

        // Stringify whatever's at -1 on Thread for the debugger UI. Pops the
        // value. Tries to be informative without invoking __tostring metamethods
        // (which could re-enter Lua mid-pause and cause problems).
        FString StringifyTopValue(lua_State* Thread)
        {
            FString Result;
            const int Type = lua_type(Thread, -1);
            switch (Type)
            {
            case LUA_TNIL:
                Result = "nil";
                break;
            case LUA_TBOOLEAN:
                Result = lua_toboolean(Thread, -1) ? "true" : "false";
                break;
            case LUA_TNUMBER:
            {
                char Buf[64];
                std::snprintf(Buf, sizeof(Buf), "%.14g", lua_tonumber(Thread, -1));
                Result = Buf;
                break;
            }
            case LUA_TSTRING:
            {
                size_t Len = 0;
                const char* Str = lua_tolstring(Thread, -1, &Len);
                Result.assign("\"");
                Result.append(Str ? Str : "", Len);
                Result.append("\"");
                break;
            }
            case LUA_TTABLE:
            {
                char Buf[64];
                std::snprintf(Buf, sizeof(Buf), "table: %p", lua_topointer(Thread, -1));
                Result = Buf;
                break;
            }
            case LUA_TFUNCTION:
            {
                char Buf[64];
                std::snprintf(Buf, sizeof(Buf), "function: %p", lua_topointer(Thread, -1));
                Result = Buf;
                break;
            }
            case LUA_TUSERDATA:
            case LUA_TLIGHTUSERDATA:
            {
                char Buf[64];
                std::snprintf(Buf, sizeof(Buf), "userdata: %p", lua_topointer(Thread, -1));
                Result = Buf;
                break;
            }
            case LUA_TVECTOR:
            {
                const float* V = lua_tovector(Thread, -1);
                if (V)
                {
                    char Buf[96];
                    std::snprintf(Buf, sizeof(Buf), "vec3(%.4g, %.4g, %.4g)", V[0], V[1], V[2]);
                    Result = Buf;
                }
                else
                {
                    Result = "vector(?)";
                }
                break;
            }
            default:
                Result = lua_typename(Thread, Type);
                break;
            }
            lua_pop(Thread, 1);
            return Result;
        }

        const char* TypeNameForTop(lua_State* Thread)
        {
            return lua_typename(Thread, lua_type(Thread, -1));
        }
    }

    void FLuaDebugger::Initialize(lua_State* InMainState)
    {
        MainState = InMainState;

        lua_Callbacks* Callbacks = lua_callbacks(MainState);
        Callbacks->debugbreak    = &DebugBreakTrampoline;
        Callbacks->debugstep     = &DebugStepTrampoline;
    }

    void FLuaDebugger::Shutdown()
    {
        if (MainState != nullptr)
        {
            lua_Callbacks* Callbacks = lua_callbacks(MainState);
            Callbacks->debugbreak = nullptr;
            Callbacks->debugstep  = nullptr;

            if (PausedThreadRef >= 0)
            {
                lua_unref(MainState, PausedThreadRef);
                PausedThreadRef = -1;
            }
        }

        Status = EDebuggerStatus::Running;
        StepMode = EStepMode::None;
        PausedThread = nullptr;
        PausedSource.clear();
        PausedLine = -1;
        CallStack.clear();
        Breakpoints.clear();
        PendingResume = EResumeRequest::None;
        SkipNextBreakThread = nullptr;
        SkipNextBreakSource.clear();
        SkipNextBreakLine = -1;
        StepLastSource.clear();
        StepLastLine = -1;
        MainState = nullptr;
    }

    void FLuaDebugger::SetBreakpoint(FStringView Path, int LineZeroBased, bool bEnabled)
    {
        const FString Key(Path);
        if (bEnabled)
        {
            Breakpoints[Key].insert(LineZeroBased);
        }
        else
        {
            auto Itr = Breakpoints.find(Key);
            if (Itr != Breakpoints.end())
            {
                Itr->second.erase(LineZeroBased);
                if (Itr->second.empty())
                {
                    Breakpoints.erase(Itr);
                }
            }
        }

        // Apply to any currently-loaded scripts at this path so the change
        // takes effect without a save / reload cycle.
        if (MainState == nullptr)
        {
            return;
        }
        for (const TSharedPtr<FScript>& Script : FScriptingContext::Get().GetAllRegisteredScripts())
        {
            if (Script && FStringView(Script->Path.c_str(), Script->Path.size()) == Path)
            {
                InstallBreakpointForScript(*Script, LineZeroBased, bEnabled);
            }
        }
    }

    void FLuaDebugger::ClearBreakpointsFor(FStringView Path)
    {
        const FString Key(Path);
        auto Itr = Breakpoints.find(Key);
        if (Itr == Breakpoints.end())
        {
            return;
        }

        THashSet<int> ToClear = Itr->second;
        Breakpoints.erase(Itr);

        for (const TSharedPtr<FScript>& Script : FScriptingContext::Get().GetAllRegisteredScripts())
        {
            if (!Script || FStringView(Script->Path.c_str(), Script->Path.size()) != Path)
            {
                continue;
            }
            for (int Line : ToClear)
            {
                InstallBreakpointForScript(*Script, Line, false);
            }
        }
    }

    bool FLuaDebugger::HasBreakpoint(FStringView Path, int LineZeroBased) const
    {
        const FString Key(Path);
        auto Itr = Breakpoints.find(Key);
        if (Itr == Breakpoints.end())
        {
            return false;
        }
        return Itr->second.find(LineZeroBased) != Itr->second.end();
    }

    TVector<int> FLuaDebugger::GetBreakpointLines(FStringView Path) const
    {
        TVector<int> Out;
        const FString Key(Path);
        auto Itr = Breakpoints.find(Key);
        if (Itr == Breakpoints.end())
        {
            return Out;
        }
        Out.reserve(Itr->second.size());
        for (int Line : Itr->second)
        {
            Out.push_back(Line);
        }
        return Out;
    }

    void FLuaDebugger::OnScriptLoaded(FScript& Script)
    {
        const FString Key(Script.Path.c_str(), Script.Path.size());
        auto Itr = Breakpoints.find(Key);
        if (Itr == Breakpoints.end())
        {
            return;
        }
        InstallBreakpointsForScript(Script, Itr->second);
    }

    void FLuaDebugger::InstallBreakpointsForScript(FScript& Script, const THashSet<int>& LinesZeroBased)
    {
        for (int Line : LinesZeroBased)
        {
            InstallBreakpointForScript(Script, Line, true);
        }
    }

    void FLuaDebugger::InstallBreakpointForScript(FScript& Script, int LineZeroBased, bool bEnabled)
    {
        if (!Script.MainFunction.IsValid())
        {
            return;
        }

        // Push the captured module closure, ask Luau to (un)set a breakpoint
        // on that line. lua_breakpoint walks child protos recursively, so a
        // breakpoint inside any nested closure within the module is handled.
        lua_State* L = Script.MainFunction.GetState();
        Script.MainFunction.Push();
        const int FuncIndex = lua_gettop(L);

        // Luau line numbering is 1-based; editor-side ours is 0-based.
        lua_breakpoint(L, FuncIndex, LineZeroBased + 1, bEnabled ? 1 : 0);

        lua_pop(L, 1);
    }

    void FLuaDebugger::HandleDebugBreak(lua_State* L, lua_Debug* ar)
    {
        // After a resume, the first debugbreak on the same (thread, source,
        // line) we paused at is the LOP_BREAK we never advanced past — see
        // SkipNextBreak* commentary. Eat it so the VM's VM_CONTINUE(op) can
        // run the original instruction and move the PC forward.
        if (SkipNextBreakThread == L && SkipNextBreakLine >= 0)
        {
            lua_Debug Here;
            if (lua_getinfo(L, 0, "sl", &Here))
            {
                const int Line = Here.currentline > 0 ? Here.currentline - 1 : -1;
                const FStringView Src = Here.source ? FStringView(Here.source) : FStringView();
                if (Line == SkipNextBreakLine && Src == FStringView(SkipNextBreakSource.c_str(), SkipNextBreakSource.size()))
                {
                    SkipNextBreakThread = nullptr;
                    SkipNextBreakSource.clear();
                    SkipNextBreakLine = -1;
                    return; // no lua_break, VM will execute original op
                }
            }
            // Different line/source, the resume already moved past the
            // line we were skipping, so disarm the skip and treat this as
            // a normal break.
            SkipNextBreakThread = nullptr;
            SkipNextBreakSource.clear();
            SkipNextBreakLine = -1;
        }

        EnterPause(L, ar);
    }

    void FLuaDebugger::HandleDebugStep(lua_State* L, lua_Debug* ar)
    {
        if (StepMode == EStepMode::None)
        {
            return;
        }

        // Single-step fires per VM instruction, that's many fires per editor
        // line. Pull the current frame's source/line and only consider a break
        // when we have actually progressed past the line we resumed from.
        lua_Debug TopFrame;
        if (!lua_getinfo(L, 0, "sl", &TopFrame))
        {
            return;
        }

        const int Depth          = lua_stackdepth(L);
        const int CurrentLine    = TopFrame.currentline > 0 ? TopFrame.currentline - 1 : -1;
        const FStringView Source = TopFrame.source ? FStringView(TopFrame.source) : FStringView();
        const FStringView LastSource(StepLastSource.c_str(), StepLastSource.size());

        // Reaching a new source-line position means EITHER the line number
        // changed within the same chunk OR we've crossed into a different
        // chunk (e.g. stepped into another script). Either is a line-tick.
        const bool bOnNewLine = (CurrentLine != StepLastLine) || (Source != LastSource);

        bool bShouldBreak = false;
        switch (StepMode)
        {
        case EStepMode::Into:
            // First instruction of any new line at any depth.
            bShouldBreak = bOnNewLine;
            break;

        case EStepMode::Over:
            if (Depth < StepBaseDepth)
            {
                // Returned out of the call we were stepping over.
                bShouldBreak = true;
            }
            else if (Depth == StepBaseDepth && bOnNewLine)
            {
                // Same depth, moved to a new line, the over completed.
                bShouldBreak = true;
            }
            // Otherwise (Depth > base, or same depth same line) keep running.
            break;

        case EStepMode::Out:
            // Break only after we've returned at least one frame.
            bShouldBreak = (Depth < StepBaseDepth);
            break;

        default:
            return;
        }

        if (!bShouldBreak)
        {
            return;
        }

        // Disarm the per-thread single-step flag before pausing; the next
        // user-issued step request will re-arm it. Leaving it on means the
        // VM keeps invoking us after resume even with mode None.
        lua_singlestep(L, 0);
        StepMode = EStepMode::None;
        EnterPause(L, ar);
    }

    void FLuaDebugger::EnterPause(lua_State* L, lua_Debug* /*ar*/)
    {
        // Already paused? Shouldn't happen, debug callbacks don't reentrantly
        // fire on the same thread. Bail to be safe.
        if (Status == EDebuggerStatus::Paused)
        {
            return;
        }

        CaptureCallStack(L);

        // Pin the broken thread so the GC can't reap it before resume.
        // Refs are global to the lua_State family, getting the ref via the
        // main state is fine.
        lua_pushthread(L);
        PausedThreadRef = lua_ref(L, -1);
        lua_pop(L, 1);
        PausedThread = L;

        // Pull source/line from the top frame for the editor "now paused at"
        // overlay. CaptureCallStack populates these from lua_Debug.
        if (!CallStack.empty())
        {
            PausedSource = CallStack[0].Source;
            PausedLine   = CallStack[0].Line;
        }

        Status = EDebuggerStatus::Paused;

        // Tell the VM to unwind this resume. lua_break flags the thread; when
        // the callback returns the VM bails out of the current execution and
        // lua_resume returns LUA_BREAK. The host (FRef::InvokeAsCoroutine)
        // treats LUA_BREAK like LUA_YIELD, the thread stays alive, anchored
        // by PausedThreadRef, until the user resumes it from Tick().
        lua_break(L);
    }

    void FLuaDebugger::CaptureCallStack(lua_State* L)
    {
        CallStack.clear();

        const int Depth = lua_stackdepth(L);
        for (int Level = 0; Level < Depth; ++Level)
        {
            lua_Debug ar;
            // "snlf", source, line, name, info-flags. Skip "u" (upvalues) and
            // "L" (active lines) because we don't need them in the snapshot.
            if (!lua_getinfo(L, Level, "snlf", &ar))
            {
                break;
            }

            FStackFrame Frame;
            if (ar.source)
            {
                Frame.Source.assign(ar.source);
            }
            if (ar.what)
            {
                Frame.What.assign(ar.what);
            }
            if (ar.name)
            {
                Frame.FunctionName.assign(ar.name);
            }
            else
            {
                Frame.FunctionName = (Frame.What == "main") ? "<main>" : "?";
            }
            Frame.Line = ar.currentline > 0 ? ar.currentline - 1 : -1;

            // Locals: lua_getlocal pushes the value and returns the name.
            // Indices are 1-based and walk up until null.
            for (int I = 1; ; ++I)
            {
                const char* Name = lua_getlocal(L, Level, I);
                if (Name == nullptr)
                {
                    break;
                }
                FStackVariable Var;
                Var.Name.assign(Name);
                Var.TypeName.assign(TypeNameForTop(L));
                Var.Value = StringifyTopValue(L);  // pops
                Frame.Locals.emplace_back(eastl::move(Var));
            }

            // Upvalues: lua_getinfo with 'f' pushed the function onto the stack
            // (our request string includes 'f'). Walk its upvalues, then pop it.
            // Without 'f' we couldn't inspect upvalues here — function is needed.
            if (lua_isfunction(L, -1))
            {
                const int FuncIdx = lua_gettop(L);
                for (int I = 1; ; ++I)
                {
                    const char* Name = lua_getupvalue(L, FuncIdx, I);
                    if (Name == nullptr)
                    {
                        break;
                    }
                    FStackVariable Var;
                    Var.Name.assign(Name);
                    Var.TypeName.assign(TypeNameForTop(L));
                    Var.Value = StringifyTopValue(L);
                    Frame.Upvalues.emplace_back(eastl::move(Var));
                }
                lua_pop(L, 1);  // pop the function pushed by getinfo("f")
            }

            CallStack.emplace_back(eastl::move(Frame));
        }
    }

    void FLuaDebugger::RequestContinue()  { PendingResume = EResumeRequest::Continue; }
    void FLuaDebugger::RequestStepInto()  { PendingResume = EResumeRequest::StepInto; }
    void FLuaDebugger::RequestStepOver()  { PendingResume = EResumeRequest::StepOver; }
    void FLuaDebugger::RequestStepOut()   { PendingResume = EResumeRequest::StepOut;  }
    void FLuaDebugger::RequestStop()      { PendingResume = EResumeRequest::Stop;     }

    void FLuaDebugger::Tick()
    {
        if (Status != EDebuggerStatus::Paused || PendingResume == EResumeRequest::None)
        {
            return;
        }

        EResumeRequest Req = PendingResume;
        PendingResume = EResumeRequest::None;

        if (PausedThread == nullptr)
        {
            // Defensive: pause state without a thread is malformed.
            Status = EDebuggerStatus::Running;
            return;
        }

        lua_State* Thread = PausedThread;

        if (Req == EResumeRequest::Stop)
        {
            // Wipe the thread's call frames + stack so it's done. Calling
            // lua_error here would crash — we're outside any C frame that
            // has a setjmp anchor for the longjmp to land on. lua_resetthread
            // is the clean equivalent: thread becomes dead, the original
            // Update invocation already returned with Yielded, and once we
            // drop our anchor the thread is GC-eligible.
            lua_resetthread(Thread);

            const int OldRef = PausedThreadRef;
            PausedThreadRef = -1;
            PausedThread = nullptr;
            Status = EDebuggerStatus::Running;
            StepMode = EStepMode::None;
            StepLastLine = -1;
            StepLastSource.clear();
            SkipNextBreakThread = nullptr;
            SkipNextBreakSource.clear();
            SkipNextBreakLine = -1;
            PausedSource.clear();
            PausedLine = -1;
            CallStack.clear();

            if (OldRef >= 0)
            {
                lua_unref(MainState, OldRef);
            }
            return;
        }

        // Arm the post-resume skip for the line we're paused on. Without it
        // LOP_BREAK at this PC would re-fire debugbreak the moment we resume
        // (the VM bailed out before VM_CONTINUE(op) on the way in, so the PC
        // is still parked on LOP_BREAK).
        SkipNextBreakThread = Thread;
        SkipNextBreakSource.assign(PausedSource.c_str(), PausedSource.size());
        SkipNextBreakLine = PausedLine;

        // Configure single-step for step requests. Out/Over need the depth +
        // line we paused at; capture both up front so HandleDebugStep can
        // tell when we've moved off the originating line vs just emitting
        // another instruction on it.
        StepBaseDepth = lua_stackdepth(Thread);
        StepLastSource.assign(PausedSource.c_str(), PausedSource.size());
        StepLastLine = PausedLine;

        switch (Req)
        {
        case EResumeRequest::StepInto:
            StepMode = EStepMode::Into;
            lua_singlestep(Thread, 1);
            break;
        case EResumeRequest::StepOver:
            StepMode = EStepMode::Over;
            lua_singlestep(Thread, 1);
            break;
        case EResumeRequest::StepOut:
            StepMode = EStepMode::Out;
            lua_singlestep(Thread, 1);
            break;
        case EResumeRequest::Continue:
        default:
            StepMode = EStepMode::None;
            lua_singlestep(Thread, 0);
            break;
        }

        // Clear the pause snapshot before the resume — captured state is now
        // about to be invalid, and a fresh break will refill it.
        const int OldRef = PausedThreadRef;
        PausedThreadRef = -1;
        PausedThread = nullptr;
        Status = EDebuggerStatus::Running;
        // Keep CallStack until the next break replaces it; UI looks empty
        // mid-resume otherwise. We'll clear it on next EnterPause.

        const int ResumeStatus = lua_resume(Thread, MainState, 0);

        // After resume:
        //  - LUA_OK / coroutine end: thread is finished, drop our anchor.
        //  - LUA_BREAK / LUA_YIELD: HandleDebugBreak (or another yield site)
        //    has taken its own anchor; drop ours.
        //  - Error: log; drop our anchor.
        if (ResumeStatus != LUA_OK && ResumeStatus != LUA_YIELD && ResumeStatus != LUA_BREAK)
        {
            const char* Err = lua_tostring(Thread, -1);
            LOG_ERROR("[LuaDebugger] resume error: {}", Err ? Err : "<unknown>");
        }

        // If the resume completed without re-pausing, the skip arming we set
        // is stale — a future, unrelated break would otherwise eat itself.
        if (Status != EDebuggerStatus::Paused)
        {
            SkipNextBreakThread = nullptr;
            SkipNextBreakSource.clear();
            SkipNextBreakLine = -1;
        }

        if (OldRef >= 0)
        {
            lua_unref(MainState, OldRef);
        }
    }
}
