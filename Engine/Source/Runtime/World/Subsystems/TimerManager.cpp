#include "pch.h"
#include "TimerManager.h"

#include <algorithm>

#include "Log/Log.h"
#include "Scripting/Lua/Class.h"

namespace Lumina
{
    FTimerManager::~FTimerManager()
    {
        Clear();
    }

    void FTimerManager::RegisterLuaModule(Lua::FRef& GlobalRef)
    {
        GlobalRef.NewClass<FTimerManager>("TimerManager")
            .AddFunction<&FTimerManager::Delay_Lua>("Delay")
            .AddFunction<&FTimerManager::SetTimer_Lua>("SetTimer")
            .AddFunction<&FTimerManager::SetEntityTimer_Lua>("SetEntityTimer")
            .AddFunction<&FTimerManager::ClearTimer_Lua>("ClearTimer")
            .AddFunction<&FTimerManager::IsTimerActive_Lua>("IsTimerActive")
            .AddFunction<&FTimerManager::GetTimerRemaining_Lua>("GetTimerRemaining")
            .AddFunction<&FTimerManager::PauseTimer_Lua>("PauseTimer")
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

        // Snapshot timers that existed at tick start so callbacks that schedule
        // new timers don't cause those new timers to fire this same frame.
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

            // Swap-out the callback references so re-entrant SetTimer/ClearTimer
            // on this same slot (from within the callback) is well-defined.
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
}
