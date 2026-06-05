#include "pch.h"
#include "TimerManager.h"

#include <algorithm>

#include "lua.h"
#include "lualib.h"
#include "Log/Log.h"
#include "Memory/SmartPtr.h"
#include "Scripting/Lua/Async.h"
#include "Scripting/Lua/Class.h"
#include "Scripting/Lua/ScriptTypes.h"
#include "Scripting/Lua/Stack.h"

namespace Lumina
{
    FTimerManager::~FTimerManager()
    {
        Clear();
    }

    void FTimerManager::RegisterLuaModule(Lua::FRef& GlobalRef)
    {
        // Name must not collide with "TimerManager"; Luau caches GETIMPORT and would shadow the per-script userdata.
        GlobalRef.NewClass<FTimerManager>("FTimerManager")
            .AddFunction<&FTimerManager::Delay_Lua>("Delay")
            .AddFunction<&FTimerManager::SetTimer_Lua>("SetTimer")
            .AddFunction<&FTimerManager::SetEntityTimer_Lua>("SetEntityTimer")
            .AddFunction<&FTimerManager::ClearTimer_Lua>("ClearTimer")
            .AddFunction<&FTimerManager::IsTimerActive_Lua>("IsTimerActive")
            .AddFunction<&FTimerManager::GetTimerRemaining_Lua>("GetTimerRemaining")
            .AddFunction<&FTimerManager::PauseTimer_Lua>("PauseTimer")
            .AddRawFunction("Wait", &FTimerManager::Wait_Lua)
            .Register();
    }

    FTimerHandle FTimerManager::SetTimer(float Rate, FTimerCallback Callback, bool bLoop, float FirstDelay)
    {
        FTimerHandle Out;
        Out.Handle = CreateTimer(Rate, bLoop, FirstDelay, entt::null, eastl::move(Callback), Lua::FRef());
        return Out;
    }

    FTimerHandle FTimerManager::SetTimerForEntity(entt::entity Owner, float Rate, FTimerCallback Callback, bool bLoop, float FirstDelay)
    {
        FTimerHandle Out;
        Out.Handle = CreateTimer(Rate, bLoop, FirstDelay, Owner, eastl::move(Callback), Lua::FRef());
        return Out;
    }

    void FTimerManager::ClearTimer(FTimerHandle& Handle)
    {
        if (!Handle.IsValid() || !Registry.valid(Handle.Handle))
        {
            Handle.Invalidate();
            return;
        }

        if (bTicking)
        {
            Registry.get<FTimer>(Handle.Handle).bPendingDestroy = true;
        }
        else
        {
            Registry.destroy(Handle.Handle);
        }
        Handle.Invalidate();
    }

    void FTimerManager::ClearTimersForEntity(entt::entity Owner)
    {
        if (Owner == entt::null)
        {
            return;
        }

        auto View = Registry.view<FTimer>();
        for (entt::entity Entity : View)
        {
            FTimer& Timer = View.get<FTimer>(Entity);
            if (Timer.Owner == Owner)
            {
                if (bTicking)
                {
                    Timer.bPendingDestroy = true;
                }
                else
                {
                    Registry.destroy(Entity);
                }
            }
        }
    }

    void FTimerManager::Clear()
    {
        Registry.clear<>();
    }

    bool FTimerManager::IsTimerActive(FTimerHandle Handle) const
    {
        if (!Handle.IsValid() || !Registry.valid(Handle.Handle))
        {
            return false;
        }

        const FTimer& Timer = Registry.get<FTimer>(Handle.Handle);
        return !Timer.bPendingDestroy;
    }

    bool FTimerManager::IsTimerPaused(FTimerHandle Handle) const
    {
        if (!Handle.IsValid() || !Registry.valid(Handle.Handle))
        {
            return false;
        }
        return Registry.get<FTimer>(Handle.Handle).bPaused;
    }

    float FTimerManager::GetTimerRate(FTimerHandle Handle) const
    {
        if (!Handle.IsValid() || !Registry.valid(Handle.Handle))
        {
            return 0.0f;
        }
        return Registry.get<FTimer>(Handle.Handle).Rate;
    }

    float FTimerManager::GetTimerRemaining(FTimerHandle Handle) const
    {
        if (!Handle.IsValid() || !Registry.valid(Handle.Handle))
        {
            return 0.0f;
        }
        return Registry.get<FTimer>(Handle.Handle).Remaining;
    }

    float FTimerManager::GetTimerElapsed(FTimerHandle Handle) const
    {
        if (!Handle.IsValid() || !Registry.valid(Handle.Handle))
        {
            return 0.0f;
        }
        const FTimer& Timer = Registry.get<FTimer>(Handle.Handle);
        return Timer.Rate - Timer.Remaining;
    }

    void FTimerManager::SetTimerPaused(FTimerHandle Handle, bool bPause)
    {
        if (!Handle.IsValid() || !Registry.valid(Handle.Handle))
        {
            return;
        }
        Registry.get<FTimer>(Handle.Handle).bPaused = bPause;
    }

    void FTimerManager::Tick(float DeltaTime)
    {
        LUMINA_PROFILE_SCOPE();

        if (DeltaTime <= 0.0f)
        {
            return;
        }

        TVector<entt::entity> ToTick;
        {
            auto View = Registry.view<FTimer>();
            ToTick.reserve(View.size());
            for (entt::entity Entity : View)
            {
                ToTick.push_back(Entity);
            }
        }

        bTicking = true;

        for (entt::entity Entity : ToTick)
        {
            if (!Registry.valid(Entity))
            {
                continue;
            }

            FTimer& Timer = Registry.get<FTimer>(Entity);
            if (Timer.bPaused || Timer.bPendingDestroy)
            {
                continue;
            }

            Timer.Remaining -= DeltaTime;
            if (Timer.Remaining > 0.0f)
            {
                continue;
            }

            // Swap-out callbacks so re-entrant SetTimer/ClearTimer from within the callback is well-defined.
            FTimerCallback NativeCallback = eastl::move(Timer.NativeCallback);
            Lua::FRef      LuaCallback    = eastl::move(Timer.LuaCallback);
            const bool     bLoop          = Timer.bLoop;
            const float    Rate           = Timer.Rate;

            if (!bLoop)
            {
                Timer.bPendingDestroy = true;
            }

            if (NativeCallback)
            {
                NativeCallback();
            }
            else if (LuaCallback.IsInvokable())
            {
                LuaCallback();
            }

            if (bLoop && Registry.valid(Entity))
            {
                FTimer& Live = Registry.get<FTimer>(Entity);
                if (!Live.bPendingDestroy)
                {
                    Live.NativeCallback = eastl::move(NativeCallback);
                    Live.LuaCallback    = eastl::move(LuaCallback);
                    Live.Remaining     += Rate;
                    if (Live.Remaining <= 0.0f)
                    {
                        Live.Remaining = Rate;
                    }
                }
            }
        }

        bTicking = false;

        auto DestroyView = Registry.view<FTimer>();
        for (entt::entity Entity : DestroyView)
        {
            if (DestroyView.get<FTimer>(Entity).bPendingDestroy)
            {
                Registry.destroy(Entity);
            }
        }
    }

    entt::entity FTimerManager::CreateTimer(float Rate, bool bLoop, float FirstDelay, entt::entity Owner, FTimerCallback NativeCallback, Lua::FRef LuaCallback)
    {
        Rate = std::max(Rate, 0.0f);

        const bool bHasNative = static_cast<bool>(NativeCallback);
        const bool bHasLua    = LuaCallback.IsInvokable();
        if (!bHasNative && !bHasLua)
        {
            LOG_WARN("[TimerManager] SetTimer called with no invokable callback - ignored.");
            return entt::null;
        }

        entt::entity Entity = Registry.create();
        FTimer& Timer = Registry.emplace<FTimer>(Entity);
        Timer.Rate           = Rate;
        Timer.Remaining      = (FirstDelay >= 0.0f) ? FirstDelay : Rate;
        Timer.bLoop          = bLoop;
        Timer.Owner          = Owner;
        Timer.NativeCallback = eastl::move(NativeCallback);
        Timer.LuaCallback    = eastl::move(LuaCallback);
        return Entity;
    }

    entt::entity FTimerManager::SetTimer_Lua(float Rate, Lua::FRef Callback, bool bLoop)
    {
        return CreateTimer(Rate, bLoop, -1.0f, entt::null, {}, eastl::move(Callback));
    }

    entt::entity FTimerManager::SetEntityTimer_Lua(entt::entity Owner, float Rate, Lua::FRef Callback, bool bLoop)
    {
        return CreateTimer(Rate, bLoop, -1.0f, Owner, {}, eastl::move(Callback));
    }

    entt::entity FTimerManager::Delay_Lua(float Delay, Lua::FRef Callback)
    {
        return CreateTimer(Delay, false, -1.0f, entt::null, {}, eastl::move(Callback));
    }

    void FTimerManager::ClearTimer_Lua(entt::entity Handle)
    {
        FTimerHandle Wrapped{ Handle };
        ClearTimer(Wrapped);
    }

    bool FTimerManager::IsTimerActive_Lua(entt::entity Handle) const
    {
        return IsTimerActive(FTimerHandle{ Handle });
    }

    float FTimerManager::GetTimerRemaining_Lua(entt::entity Handle) const
    {
        return GetTimerRemaining(FTimerHandle{ Handle });
    }

    void FTimerManager::PauseTimer_Lua(entt::entity Handle, bool bPause)
    {
        SetTimerPaused(FTimerHandle{ Handle }, bPause);
    }

    int FTimerManager::WaitImpl(lua_State* L, float Seconds, FTimerManager* TimerManager)
    {
        if (TimerManager == nullptr)
        {
            luaL_errorL(L, "Wait: no timer manager for this script's world");
        }

        // Pins the coroutine and captures its owner; null if L isn't on a yieldable coroutine.
        TSharedPtr<Lua::FYieldToken> Token = Lua::BeginYield(L);
        if (!Token)
        {
            luaL_errorL(L, "Wait can only be called from a coroutine "
                           "(use Task.Spawn, or call it from a script lifecycle/event hook).");
        }

        // Timer is tied to the owner entity, so ClearTimersForEntity cancels the wait on entity death:
        // the timer (and its captured token) is dropped, releasing the pin without ever resuming.
        TimerManager->CreateTimer(Seconds, /*bLoop=*/false, /*FirstDelay=*/-1.0f, Token->Owner,
            [Token]() { Lua::ResumeYield(Token); },
            Lua::FRef());

        return lua_yield(L, 0);
    }

    int FTimerManager::Wait_Lua(lua_State* L)
    {
        // __namecall stack: [self][seconds]
        FTimerManager* Self = Lua::TStack<FTimerManager*>::Get(L, 1);
        if (Self == nullptr)
        {
            luaL_errorL(L, "TimerManager:Wait called with invalid self");
        }

        if (!lua_isnumber(L, 2))
        {
            luaL_typeerrorL(L, 2, "number");
        }

        return WaitImpl(L, static_cast<float>(lua_tonumber(L, 2)), Self);
    }
}
