#include "pch.h"
#include "LuaSystem.h"
#include "Core/Profiler/CPUProfiler.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "World/World.h"


namespace Lumina
{
    FEntityRegistry& FLuaSystemContext::GetRegistry() const
    {
        return World->GetEntityRegistry();
    }

    namespace
    {
        bool ParseStage(FStringView Name, EUpdateStage& Out)
        {
            for (uint8 i = 0; i < (uint8)EUpdateStage::Max; ++i)
            {
                if (Name == FStringView(GUpdateStageNames[i]))
                {
                    Out = (EUpdateStage)i;
                    return true;
                }
            }
            return false;
        }

        EUpdatePriority ParsePriority(FStringView Name)
        {
            if (Name == FStringView("Highest")) return EUpdatePriority::Highest;
            if (Name == FStringView("High"))    return EUpdatePriority::High;
            if (Name == FStringView("Medium"))  return EUpdatePriority::Medium;
            if (Name == FStringView("Low"))     return EUpdatePriority::Low;
            return EUpdatePriority::Default;
        }

        // Reads stage/priority from the module table. Supports either a single { Stage, Priority } pair or a
        // { Stages = { StageName = PriorityName, ... } } map. Returns an all-disabled list when none is found.
        FUpdatePriorityList ResolvePriorities(Lua::FRef& Table)
        {
            FUpdatePriorityList List;

            Lua::FRef Stages = Table.GetField("Stages");
            if (Stages.IsValid() && Stages.IsTable())
            {
                bool bAny = false;
                for (auto&& [Key, Value] : Stages)
                {
                    TOptional<FString> StageName = Key.As<FString>();
                    if (!StageName)
                    {
                        continue;
                    }

                    EUpdateStage Stage;
                    if (!ParseStage(FStringView(StageName->c_str()), Stage))
                    {
                        continue;
                    }

                    EUpdatePriority Priority = EUpdatePriority::Default;
                    if (TOptional<FString> PriorityName = Value.As<FString>())
                    {
                        Priority = ParsePriority(FStringView(PriorityName->c_str()));
                    }

                    List.SetStagePriority(FUpdateStagePriority(Stage, Priority));
                    bAny = true;
                }

                if (bAny)
                {
                    return List;
                }
            }

            if (TOptional<FString> StageName = Table.Get<FString>("Stage"))
            {
                EUpdateStage Stage;
                if (ParseStage(FStringView(StageName->c_str()), Stage))
                {
                    EUpdatePriority Priority = EUpdatePriority::Default;
                    if (TOptional<FString> PriorityName = Table.Get<FString>("Priority"))
                    {
                        Priority = ParsePriority(FStringView(PriorityName->c_str()));
                    }
                    List.SetStagePriority(FUpdateStagePriority(Stage, Priority));
                }
            }

            return List;
        }

        FName DeriveNameFromPath(FStringView Path)
        {
            size_t Slash = Path.find_last_of('/');
            size_t Start = (Slash == FStringView::npos) ? 0 : Slash + 1;
            size_t Dot   = Path.find_last_of('.');
            size_t End   = (Dot == FStringView::npos || Dot < Start) ? Path.size() : Dot;

            FString Stem(Path.data() + Start, End - Start);
            return FName(Stem.c_str());
        }

        // Resolves the module table's hooks into the instance and stamps stage/priority metadata. Shared by
        // initial load and hot reload. Returns false if the script has no callable Update.
        bool BindInstance(FScriptSystemInstance& Instance, const TSharedPtr<Lua::FScript>& Script)
        {
            Instance.Script     = Script;
            Instance.Table      = Script->Reference;
            Instance.StartupFn  = Instance.Table.GetField("Startup");
            Instance.UpdateFn   = Instance.Table.GetField("Update");
            Instance.TeardownFn = Instance.Table.GetField("Teardown");

            if (!Instance.UpdateFn.IsValid() || !Instance.UpdateFn.IsInvokable())
            {
                return false;
            }

            Instance.Priorities = ResolvePriorities(Instance.Table);
            if (Instance.Priorities.AreAllStagesDisabled())
            {
                // No stage declared -> default to PrePhysics so the system still ticks.
                Instance.Priorities.SetStagePriority(FUpdateStagePriority(EUpdateStage::PrePhysics, EUpdatePriority::Default));
            }

            // Pin a single ctx userdata wrapping this instance's LuaCtx (pointer-stable); rebind Ctx per tick.
            if (lua_State* L = Script->Reference.GetState())
            {
                Lua::TStack<FLuaSystemContext*>::Push(L, &Instance.LuaCtx);
                Instance.CtxRef = Lua::FRef(L, -1);
                lua_pop(L, 1);
            }

            return true;
        }

        void DispatchHook(FScriptSystemInstance* Instance, const Lua::FRef& Hook, const FSystemContext& Context, bool bPassDelta)
        {
            if (Instance == nullptr || !Instance->Script || !Hook.IsValid())
            {
                return;
            }

            if (Lua::FLuaDebugger::Get().IsPaused())
            {
                return;
            }

            Instance->Script->ThreadData.World  = Instance->World;
            Instance->Script->ThreadData.Entity = entt::null;
            Instance->Script->PublishThreadContext();

            Instance->LuaCtx.World = Instance->World;
            Instance->LuaCtx.Ctx   = const_cast<FSystemContext*>(&Context);

            if (bPassDelta)
            {
                Hook.Call(Instance->CtxRef, (float)Context.GetDeltaTime());
            }
            else
            {
                Hook.Call(Instance->CtxRef);
            }
        }
    }

    void ScriptSystem_Startup(void* Self, const FSystemContext& Context) noexcept
    {
        if (auto* Instance = static_cast<FScriptSystemInstance*>(Self))
        {
            DispatchHook(Instance, Instance->StartupFn, Context, false);
        }
    }

    void ScriptSystem_Update(void* Self, const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        if (auto* Instance = static_cast<FScriptSystemInstance*>(Self))
        {
            DispatchHook(Instance, Instance->UpdateFn, Context, true);
        }
    }

    void ScriptSystem_Teardown(void* Self, const FSystemContext& Context) noexcept
    {
        if (auto* Instance = static_cast<FScriptSystemInstance*>(Self))
        {
            DispatchHook(Instance, Instance->TeardownFn, Context, false);
        }
    }

    TUniquePtr<FScriptSystemInstance> LoadScriptSystem(CWorld* World, FStringView Path)
    {
        TSharedPtr<Lua::FScript> Script = Lua::FScriptingContext::Get().LoadUniqueScriptPath(Path);
        if (!Script || !Script->Reference.IsValid() || !Script->Reference.IsTable())
        {
            LOG_WARN("Script system '{}' did not return a table; ignored.", Path);
            return nullptr;
        }

        TUniquePtr<FScriptSystemInstance> Instance = MakeUnique<FScriptSystemInstance>();
        Instance->World = World;
        Instance->Path.assign(Path.data(), Path.size());
        Instance->LuaCtx.World = World;

        if (!BindInstance(*Instance, Script))
        {
            LOG_WARN("Script system '{}' has no callable Update; ignored.", Path);
            return nullptr;
        }

        if (TOptional<FString> ExplicitName = Instance->Table.Get<FString>("Name"); ExplicitName && !ExplicitName->empty())
        {
            Instance->Name = FName(ExplicitName->c_str());
        }
        else
        {
            Instance->Name = DeriveNameFromPath(Path);
        }

        return Instance;
    }

    bool ReloadScriptSystem(FScriptSystemInstance& Instance)
    {
        TSharedPtr<Lua::FScript> Script = Lua::FScriptingContext::Get().LoadUniqueScriptPath(FStringView(Instance.Path.c_str()));
        if (!Script || !Script->Reference.IsValid() || !Script->Reference.IsTable())
        {
            LOG_WARN("Script system '{}' reload failed (no table returned); keeping previous binding.", Instance.Path);
            return false;
        }

        return BindInstance(Instance, Script);
    }
}
