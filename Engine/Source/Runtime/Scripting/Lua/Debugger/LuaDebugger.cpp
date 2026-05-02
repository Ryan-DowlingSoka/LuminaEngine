#include "pch.h"
#include "LuaDebugger.h"

#include "Log/Log.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/ScriptTypes.h"

#include "lua.h"
#include "lualib.h"
#include "luacode.h"

namespace Lumina::Lua
{
    static FLuaDebugger GDebugger;

    FLuaDebugger& FLuaDebugger::Get()
    {
        return GDebugger;
    }

    namespace
    {
        // lua_callbacks takes free function pointers, no userdata; trampoline through the singleton.
        void DebugBreakTrampoline(lua_State* L, lua_Debug* ar)
        {
            FLuaDebugger::Get().HandleDebugBreak(L, ar);
        }

        void DebugStepTrampoline(lua_State* L, lua_Debug* ar)
        {
            FLuaDebugger::Get().HandleDebugStep(L, ar);
        }

        // Stringify -1 and pop. Avoids __tostring to prevent reentry mid-pause.
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
        BreakpointSettings.clear();
        BreakHistory.clear();
        RunToTargetSource.clear();
        RunToTargetLine = -1;
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

            auto SettingsIt = BreakpointSettings.find(Key);
            if (SettingsIt != BreakpointSettings.end())
            {
                SettingsIt->second.erase(LineZeroBased);
                if (SettingsIt->second.empty())
                {
                    BreakpointSettings.erase(SettingsIt);
                }
            }
        }

        // Apply to live scripts at this path so changes take effect without reload.
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
        BreakpointSettings.erase(Key);

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

        // lua_breakpoint walks child protos recursively for nested closures.
        lua_State* L = Script.MainFunction.GetState();
        Script.MainFunction.Push();
        const int FuncIndex = lua_gettop(L);

        // Luau is 1-based, editor is 0-based.
        lua_breakpoint(L, FuncIndex, LineZeroBased + 1, bEnabled ? 1 : 0);

        lua_pop(L, 1);
    }

    void FLuaDebugger::HandleDebugBreak(lua_State* L, lua_Debug* ar)
    {
        // Eat the first debugbreak on the resumed line so VM_CONTINUE(op) runs the original op.
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
                    return;
                }
            }
            // Already past the skip line; disarm and treat as a normal break.
            SkipNextBreakThread = nullptr;
            SkipNextBreakSource.clear();
            SkipNextBreakLine = -1;
        }

        lua_Debug Here;
        FStringView Source;
        int LineZeroBased = -1;
        if (lua_getinfo(L, 0, "sl", &Here))
        {
            Source        = Here.source ? FStringView(Here.source) : FStringView();
            LineZeroBased = Here.currentline > 0 ? Here.currentline - 1 : -1;
        }

        if (!ProcessBreakpointHit(L, ar, Source, LineZeroBased))
        {
            return;
        }

        EnterPause(L, ar);
    }

    void FLuaDebugger::HandleDebugStep(lua_State* L, lua_Debug* ar)
    {
        if (StepMode == EStepMode::None)
        {
            return;
        }

        // Single-step fires per VM instruction; only break once we move past the resume line.
        lua_Debug TopFrame;
        if (!lua_getinfo(L, 0, "sl", &TopFrame))
        {
            return;
        }

        const int Depth          = lua_stackdepth(L);
        const int CurrentLine    = TopFrame.currentline > 0 ? TopFrame.currentline - 1 : -1;
        const FStringView Source = TopFrame.source ? FStringView(TopFrame.source) : FStringView();
        const FStringView LastSource(StepLastSource.c_str(), StepLastSource.size());

        const bool bOnNewLine = (CurrentLine != StepLastLine) || (Source != LastSource);

        bool bShouldBreak = false;
        switch (StepMode)
        {
        case EStepMode::Into:
            bShouldBreak = bOnNewLine;
            break;

        case EStepMode::Over:
            if (Depth < StepBaseDepth)
            {
                bShouldBreak = true;
            }
            else if (Depth == StepBaseDepth && bOnNewLine)
            {
                bShouldBreak = true;
            }
            break;

        case EStepMode::Out:
            bShouldBreak = (Depth < StepBaseDepth);
            break;

        default:
            return;
        }

        if (!bShouldBreak)
        {
            return;
        }

        // Disarm single-step before pause; otherwise VM keeps calling us after resume.
        lua_singlestep(L, 0);
        StepMode = EStepMode::None;
        EnterPause(L, ar);
    }

    void FLuaDebugger::EnterPause(lua_State* L, lua_Debug* /*ar*/)
    {
        if (Status == EDebuggerStatus::Paused)
        {
            return;
        }

        CaptureCallStack(L);

        // Pin the broken thread so GC can't reap it before resume.
        lua_pushthread(L);
        PausedThreadRef = lua_ref(L, -1);
        lua_pop(L, 1);
        PausedThread = L;

        if (!CallStack.empty())
        {
            PausedSource = CallStack[0].Source;
            PausedLine   = CallStack[0].Line;
        }

        Status = EDebuggerStatus::Paused;

        // lua_break unwinds the resume; host treats LUA_BREAK like LUA_YIELD until Tick() resumes.
        lua_break(L);
    }

    void FLuaDebugger::CaptureCallStack(lua_State* L)
    {
        CallStack.clear();

        const int Depth = lua_stackdepth(L);
        for (int Level = 0; Level < Depth; ++Level)
        {
            lua_Debug ar;
            // "snlf": source, line, name, function (needed for upvalue walk).
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
                Var.Value = StringifyTopValue(L);
                Frame.Locals.emplace_back(eastl::move(Var));
            }

            // 'f' in getinfo string pushed the function; walk its upvalues then pop it.
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
                lua_pop(L, 1);
            }

            CallStack.emplace_back(eastl::move(Frame));
        }
    }

    bool FLuaDebugger::ProcessBreakpointHit(lua_State* L, lua_Debug* /*ar*/, FStringView Source, int LineZeroBased)
    {
        if (LineZeroBased < 0 || Source.empty())
        {
            return true;
        }

        const bool bRunToHit = (RunToTargetLine == LineZeroBased)
            && (FStringView(RunToTargetSource.c_str(), RunToTargetSource.size()) == Source);

        const FString Key(Source);
        FBreakpointSettings* Settings = nullptr;
        auto BpIt = BreakpointSettings.find(Key);
        if (BpIt != BreakpointSettings.end())
        {
            auto LineIt = BpIt->second.find(LineZeroBased);
            if (LineIt != BpIt->second.end())
            {
                Settings = &LineIt->second;
            }
        }

        FString FunctionName;
        {
            lua_Debug Info;
            if (lua_getinfo(L, 0, "n", &Info))
            {
                FunctionName.assign(Info.name ? Info.name : "?");
            }
        }

        if (Settings != nullptr)
        {
            Settings->HitCount++;

            if (!Settings->bEnabled)
            {
                if (bRunToHit) ClearRunToLine();
                return bRunToHit;
            }

            if (Settings->IgnoreCount > 0 && Settings->HitCount <= Settings->IgnoreCount)
            {
                if (bRunToHit) ClearRunToLine();
                return bRunToHit;
            }

            // Skip on parse error too; invalid expressions shouldn't become permanent breaks.
            if (!Settings->Condition.empty())
            {
                FString OutValue;
                FString OutType;
                const bool bOk = EvaluateInPausedFrame(0, FStringView(Settings->Condition.c_str(), Settings->Condition.size()), OutValue, OutType);
                bool bConditionTrue = false;
                if (bOk && OutType != "error")
                {
                    if (OutType == "boolean")       bConditionTrue = (OutValue == "true");
                    else if (OutType == "nil")      bConditionTrue = false;
                    else                            bConditionTrue = true;
                }
                if (!bConditionTrue)
                {
                    if (bRunToHit) ClearRunToLine();
                    return bRunToHit;
                }
            }

            // Log point: format with {expr} substitutions, do not pause.
            if (!Settings->LogMessage.empty())
            {
                eastl::string Out;
                Out.reserve(Settings->LogMessage.size() + 16);
                const char* Data = Settings->LogMessage.c_str();
                const size_t N   = Settings->LogMessage.size();
                size_t I = 0;
                while (I < N)
                {
                    if (Data[I] == '{')
                    {
                        size_t Close = I + 1;
                        while (Close < N && Data[Close] != '}') ++Close;
                        if (Close < N)
                        {
                            FString ExprStr;
                            ExprStr.assign(Data + I + 1, Close - I - 1);
                            FString EvalValue, EvalType;
                            EvaluateInPausedFrame(0, FStringView(ExprStr.c_str(), ExprStr.size()), EvalValue, EvalType);
                            Out.append(EvalValue.c_str(), EvalValue.size());
                            I = Close + 1;
                            continue;
                        }
                    }
                    Out.push_back(Data[I]);
                    ++I;
                }
                LOG_INFO("[Lua][{}:{}] {}", Source, LineZeroBased + 1, FStringView(Out.c_str(), Out.size()));

                RecordBreakHistory(Source, FStringView(FunctionName.c_str(), FunctionName.size()), LineZeroBased, true);
                if (bRunToHit) ClearRunToLine();
                return false;
            }
        }

        if (bRunToHit)
        {
            ClearRunToLine();
        }

        RecordBreakHistory(Source, FStringView(FunctionName.c_str(), FunctionName.size()), LineZeroBased, false);
        return true;
    }

    void FLuaDebugger::RecordBreakHistory(FStringView Source, FStringView FunctionName, int Line, bool bLogPoint)
    {
        FBreakHistoryEntry Entry;
        Entry.Source.assign(Source.data(), Source.size());
        Entry.FunctionName.assign(FunctionName.data(), FunctionName.size());
        Entry.Line = Line;
        Entry.TimeSeconds = 0.0;
        Entry.bWasLogPoint = bLogPoint;

        BreakHistory.insert(BreakHistory.begin(), eastl::move(Entry));
        if (BreakHistory.size() > 32)
        {
            BreakHistory.resize(32);
        }
    }

    void FLuaDebugger::SetBreakpointCondition(FStringView Path, int LineZeroBased, FStringView Condition)
    {
        if (!HasBreakpoint(Path, LineZeroBased)) return;
        const FString Key(Path);
        FBreakpointSettings& S = BreakpointSettings[Key][LineZeroBased];
        S.Condition.assign(Condition.data(), Condition.size());
    }

    void FLuaDebugger::SetBreakpointLogMessage(FStringView Path, int LineZeroBased, FStringView Message)
    {
        if (!HasBreakpoint(Path, LineZeroBased)) return;
        const FString Key(Path);
        FBreakpointSettings& S = BreakpointSettings[Key][LineZeroBased];
        S.LogMessage.assign(Message.data(), Message.size());
    }

    void FLuaDebugger::SetBreakpointEnabled(FStringView Path, int LineZeroBased, bool bEnabled)
    {
        if (!HasBreakpoint(Path, LineZeroBased)) return;
        const FString Key(Path);
        FBreakpointSettings& S = BreakpointSettings[Key][LineZeroBased];
        S.bEnabled = bEnabled;
    }

    void FLuaDebugger::SetBreakpointIgnoreCount(FStringView Path, int LineZeroBased, uint32 IgnoreCount)
    {
        if (!HasBreakpoint(Path, LineZeroBased)) return;
        const FString Key(Path);
        FBreakpointSettings& S = BreakpointSettings[Key][LineZeroBased];
        S.IgnoreCount = IgnoreCount;
    }

    const FBreakpointSettings* FLuaDebugger::GetBreakpointSettings(FStringView Path, int LineZeroBased) const
    {
        const FString Key(Path);
        auto It = BreakpointSettings.find(Key);
        if (It == BreakpointSettings.end()) return nullptr;
        auto LineIt = It->second.find(LineZeroBased);
        if (LineIt == It->second.end()) return nullptr;
        return &LineIt->second;
    }

    FBreakpointSettings* FLuaDebugger::GetBreakpointSettingsMutable(FStringView Path, int LineZeroBased)
    {
        const FString Key(Path);
        auto It = BreakpointSettings.find(Key);
        if (It == BreakpointSettings.end()) return nullptr;
        auto LineIt = It->second.find(LineZeroBased);
        if (LineIt == It->second.end()) return nullptr;
        return &LineIt->second;
    }

    void FLuaDebugger::RunToLine(FStringView Path, int LineZeroBased)
    {
        ClearRunToLine();

        RunToTargetSource.assign(Path.data(), Path.size());
        RunToTargetLine = LineZeroBased;

        // Skip install if the user already has a real breakpoint there.
        if (!HasBreakpoint(Path, LineZeroBased))
        {
            SetBreakpoint(Path, LineZeroBased, true);
        }
    }

    void FLuaDebugger::ClearRunToLine()
    {
        if (RunToTargetLine < 0)
        {
            return;
        }

        // Heuristic: no metadata means RunToLine added the bp; we own cleanup.
        const FString Key(RunToTargetSource.c_str(), RunToTargetSource.size());
        const FStringView SrcView(RunToTargetSource.c_str(), RunToTargetSource.size());

        auto SettingsIt = BreakpointSettings.find(Key);
        const bool bHasSettings = (SettingsIt != BreakpointSettings.end()
            && SettingsIt->second.find(RunToTargetLine) != SettingsIt->second.end());

        if (!bHasSettings)
        {
            SetBreakpoint(SrcView, RunToTargetLine, false);
        }

        RunToTargetSource.clear();
        RunToTargetLine = -1;
    }

    bool FLuaDebugger::PushFrameEvalResult(int FrameIndex, FStringView Expr, FString& OutError)
    {
        OutError.clear();

        if (Status != EDebuggerStatus::Paused || PausedThread == nullptr || MainState == nullptr)
        {
            OutError.assign("not paused");
            return false;
        }
        if (FrameIndex < 0 || FrameIndex >= (int)CallStack.size())
        {
            OutError.assign("frame out of range");
            return false;
        }
        if (Expr.empty())
        {
            OutError.assign("empty expression");
            return false;
        }

        // Parens guard against ambiguity for exprs starting with `function`.
        eastl::string Wrapped;
        Wrapped.reserve(Expr.size() + 12);
        Wrapped.assign("return (");
        Wrapped.append(Expr.data(), Expr.size());
        Wrapped.append(")");

        lua_CompileOptions Options{};
        Options.debugLevel = 1;
        Options.optimizationLevel = 1;

        size_t BytecodeSize = 0;
        char* Bytecode = luau_compile(Wrapped.data(), Wrapped.size(), &Options, &BytecodeSize);
        if (Bytecode == nullptr)
        {
            OutError.assign("compile error");
            return false;
        }

        lua_State* L = MainState;
        const int StackBefore = lua_gettop(L);

        const int LoadResult = luau_load(L, "=watch", Bytecode, BytecodeSize, 0);
        free(Bytecode);
        if (LoadResult != LUA_OK)
        {
            OutError.assign(lua_tostring(L, -1));
            lua_pop(L, 1);
            return false;
        }

        // env: locals + upvalues by name, with __index = globals.
        lua_newtable(L);
        const int EnvIdx = lua_gettop(L);

        for (int LocIdx = 1; ; ++LocIdx)
        {
            lua_rawcheckstack(PausedThread, 1);
            lua_rawcheckstack(L, 1);
            const char* Name = lua_getlocal(PausedThread, FrameIndex, LocIdx);
            if (Name == nullptr) break;
            lua_xmove(PausedThread, L, 1);
            lua_setfield(L, EnvIdx, Name);
        }

        {
            lua_rawcheckstack(PausedThread, 1);
            lua_Debug Info;
            if (lua_getinfo(PausedThread, FrameIndex, "f", &Info) && lua_isfunction(PausedThread, -1))
            {
                const int FnIdx = lua_gettop(PausedThread);
                for (int UpIdx = 1; ; ++UpIdx)
                {
                    lua_rawcheckstack(PausedThread, 1);
                    lua_rawcheckstack(L, 1);
                    const char* Name = lua_getupvalue(PausedThread, FnIdx, UpIdx);
                    if (Name == nullptr) break;
                    lua_xmove(PausedThread, L, 1);
                    lua_setfield(L, EnvIdx, Name);
                }
                lua_pop(PausedThread, 1);
            }
            else if (lua_gettop(PausedThread) > 0 && lua_isfunction(PausedThread, -1))
            {
                lua_pop(PausedThread, 1);
            }
        }

        lua_newtable(L);
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, EnvIdx);

        lua_pushvalue(L, EnvIdx);
        lua_remove(L, EnvIdx);
        lua_setfenv(L, -2);

        const int CallResult = lua_pcall(L, 0, 1, 0);
        if (CallResult != LUA_OK)
        {
            OutError.assign(lua_tostring(L, -1));
            lua_pop(L, 1);
            const int Now = lua_gettop(L);
            if (Now > StackBefore) lua_pop(L, Now - StackBefore);
            return false;
        }
        // Result on top of MainState; caller pops.
        return true;
    }

    namespace
    {
        // True for tables and engine-bound userdata (probes __lumina_members, falls back to __index).
        bool IsExpandableAtTop(lua_State* L)
        {
            if (lua_istable(L, -1)) return true;
            if (lua_type(L, -1) != LUA_TUSERDATA) return false;
            if (!lua_getmetatable(L, -1)) return false;

            lua_getfield(L, -1, "__lumina_members");
            if (lua_istable(L, -1))
            {
                lua_pop(L, 2);
                return true;
            }
            lua_pop(L, 1);

            lua_getfield(L, -1, "__index");
            const bool bOk = lua_istable(L, -1);
            lua_pop(L, 2);
            return bOk;
        }
    }

    bool FLuaDebugger::EvaluateInPausedFrame(int FrameIndex, FStringView Expr, FString& OutValue, FString& OutTypeName)
    {
        bool bUnused = false;
        return EvaluateInPausedFrameWithExpandable(FrameIndex, Expr, OutValue, OutTypeName, bUnused);
    }

    bool FLuaDebugger::EvaluateInPausedFrameWithExpandable(int FrameIndex, FStringView Expr,
                                                            FString& OutValue, FString& OutTypeName,
                                                            bool& bOutExpandable)
    {
        OutValue.clear();
        OutTypeName.clear();
        bOutExpandable = false;

        FString Err;
        if (!PushFrameEvalResult(FrameIndex, Expr, Err))
        {
            OutValue = Err;
            OutTypeName.assign("error");
            return false;
        }

        lua_State* L = MainState;
        OutTypeName.assign(lua_typename(L, lua_type(L, -1)));
        bOutExpandable = IsExpandableAtTop(L);
        OutValue = StringifyTopValue(L);
        return true;
    }

    bool FLuaDebugger::EnumerateChildrenInPausedFrame(int FrameIndex, FStringView Expr,
                                                      TVector<FChildEntry>& Out, int MaxChildren)
    {
        Out.clear();

        FString Err;
        if (!PushFrameEvalResult(FrameIndex, Expr, Err))
        {
            return false;
        }

        lua_State* L = MainState;
        const int ResultIdx = lua_gettop(L);

        auto AppendKv = [&](int KeyIdx, int ValIdx)
        {
            // Only string/number keys are addressable by Lua expression path.
            FChildEntry E;
            const int KeyType = lua_type(L, KeyIdx);
            char Buf[96];
            if (KeyType == LUA_TSTRING)
            {
                size_t KLen = 0;
                const char* Key = lua_tolstring(L, KeyIdx, &KLen);

                // Identifier => `.foo`, otherwise `["..."]`.
                bool bIsIdent = (KLen > 0)
                    && !(Key[0] >= '0' && Key[0] <= '9');
                for (size_t I = 0; I < KLen && bIsIdent; ++I)
                {
                    const char C = Key[I];
                    bIsIdent = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z')
                            || (C >= '0' && C <= '9') || C == '_';
                }
                if (bIsIdent)
                {
                    E.Key.assign(Key, KLen);
                    E.AccessSuffix.assign(".");
                    E.AccessSuffix.append(Key, KLen);
                }
                else
                {
                    eastl::string Quoted;
                    Quoted.reserve(KLen + 4);
                    Quoted.append("[\"");
                    for (size_t I = 0; I < KLen; ++I)
                    {
                        const char C = Key[I];
                        if (C == '"' || C == '\\') Quoted.push_back('\\');
                        Quoted.push_back(C);
                    }
                    Quoted.append("\"]");
                    E.Key.assign("[\"");
                    E.Key.append(Key, KLen);
                    E.Key.append("\"]");
                    E.AccessSuffix.assign(Quoted.c_str(), Quoted.size());
                }
            }
            else if (KeyType == LUA_TNUMBER)
            {
                std::snprintf(Buf, sizeof(Buf), "[%g]", lua_tonumber(L, KeyIdx));
                E.Key.assign(Buf);
                E.AccessSuffix.assign(Buf);
            }
            else
            {
                return false;
            }

            lua_pushvalue(L, ValIdx);
            E.TypeName.assign(lua_typename(L, lua_type(L, -1)));
            E.bIsExpandable = IsExpandableAtTop(L);
            E.Value = StringifyTopValue(L);
            Out.emplace_back(eastl::move(E));
            return true;
        };

        if (lua_istable(L, ResultIdx))
        {
            lua_pushnil(L);
            int Count = 0;
            while (Count < MaxChildren && lua_next(L, ResultIdx) != 0)
            {
                if (AppendKv(-2, -1))
                {
                    ++Count;
                }
                lua_pop(L, 1);
            }
        }
        else if (lua_type(L, ResultIdx) == LUA_TUSERDATA && lua_getmetatable(L, ResultIdx))
        {
            // Engine userdata: walk __lumina_members (name -> "property"/"method").
            lua_getfield(L, -1, "__lumina_members");
            if (lua_istable(L, -1))
            {
                const int MembersIdx = lua_gettop(L);

                // Collect names first; getter push/pop would tangle in-loop iteration.
                eastl::vector<eastl::string> Names;
                eastl::vector<bool>          IsProp;
                lua_pushnil(L);
                while (lua_next(L, MembersIdx) != 0)
                {
                    const char* Type = lua_tostring(L, -1);
                    size_t KeyLen = 0;
                    const char* KeyStr = lua_tolstring(L, -2, &KeyLen);
                    if (KeyStr && KeyLen > 0 && KeyStr[0] != '_')
                    {
                        Names.emplace_back(KeyStr, KeyLen);
                        IsProp.push_back(Type && std::strcmp(Type, "property") == 0);
                    }
                    lua_pop(L, 1);
                }

                int Count = 0;
                for (size_t I = 0; I < Names.size() && Count < MaxChildren; ++I)
                {
                    FChildEntry E;
                    E.Key.assign(Names[I].c_str(), Names[I].size());
                    E.AccessSuffix.assign(".");
                    E.AccessSuffix.append(Names[I].c_str(), Names[I].size());

                    if (IsProp[I])
                    {
                        // __index routes through GenericIndex to invoke the live getter.
                        lua_getfield(L, ResultIdx, Names[I].c_str());
                        E.TypeName.assign(lua_typename(L, lua_type(L, -1)));
                        E.bIsExpandable = IsExpandableAtTop(L);
                        E.Value = StringifyTopValue(L);
                    }
                    else
                    {
                        E.TypeName.assign("function");
                        E.Value.assign("<method>");
                        E.bIsExpandable = false;
                    }

                    Out.emplace_back(eastl::move(E));
                    ++Count;
                }
            }
            lua_pop(L, 2);
        }

        lua_pop(L, 1);
        return true;
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
            Status = EDebuggerStatus::Running;
            return;
        }

        lua_State* Thread = PausedThread;

        if (Req == EResumeRequest::Stop)
        {
            // lua_error would crash here (no setjmp anchor); lua_resetthread is the clean equivalent.
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
            ClearRunToLine();

            if (OldRef >= 0)
            {
                lua_unref(MainState, OldRef);
            }
            return;
        }

        // Arm post-resume skip; PC is still parked on LOP_BREAK and would re-fire on resume.
        SkipNextBreakThread = Thread;
        SkipNextBreakSource.assign(PausedSource.c_str(), PausedSource.size());
        SkipNextBreakLine = PausedLine;

        // Out/Over need the pause depth + line for HandleDebugStep to detect line changes.
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

        const int OldRef = PausedThreadRef;
        PausedThreadRef = -1;
        PausedThread = nullptr;
        Status = EDebuggerStatus::Running;
        // Keep CallStack until next break to avoid empty UI mid-resume.

        const int ResumeStatus = lua_resume(Thread, MainState, 0);

        // OK/break/yield: drop our anchor; another holder (or completion) covers the rest.
        if (ResumeStatus != LUA_OK && ResumeStatus != LUA_YIELD && ResumeStatus != LUA_BREAK)
        {
            const char* Err = lua_tostring(Thread, -1);
            LOG_ERROR("[LuaDebugger] resume error: {}", Err ? Err : "<unknown>");
        }

        // Resume completed without re-pausing; clear stale skip arming.
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
