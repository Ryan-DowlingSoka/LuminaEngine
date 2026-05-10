#include "pch.h"
#include "LuaEventBus.h"
#include "Log/Log.h"

namespace Lumina
{
    void FLuaEventBus::Subscribe(FStringView EventName, Lua::FRef Callback)
    {
        if (!Callback.IsInvokable())
        {
            LOG_WARN("[EventBus] Subscribe('{}') received a non-callable value - ignored.", EventName);
            return;
        }

        const FName Name(EventName);
        if (DispatchDepth > 0)
        {
            PendingMutations.push_back({ FPendingMutation::EKind::Subscribe, Name, entt::null, eastl::move(Callback) });
            return;
        }
        ApplySubscribe(Name, entt::null, eastl::move(Callback));
    }

    void FLuaEventBus::SubscribeEntity(entt::entity Owner, FStringView EventName, Lua::FRef Callback)
    {
        if (Owner == entt::null)
        {
            LOG_WARN("[EventBus] SubscribeEntity('{}') called with null entity - ignored.", EventName);
            return;
        }

        if (!Callback.IsInvokable())
        {
            LOG_WARN("[EventBus] SubscribeEntity('{}') received a non-callable value - ignored.", EventName);
            return;
        }

        const FName Name(EventName);
        if (DispatchDepth > 0)
        {
            PendingMutations.push_back({ FPendingMutation::EKind::Subscribe, Name, Owner, eastl::move(Callback) });
            return;
        }
        ApplySubscribe(Name, Owner, eastl::move(Callback));
    }

    void FLuaEventBus::Unsubscribe(FStringView EventName, const Lua::FRef& Callback)
    {
        const FName Name(EventName);
        if (DispatchDepth > 0)
        {
            // Tombstone matching listeners so the in-flight iteration skips them; the
            // outermost dispatch reaps them when it unwinds.
            auto It = Subscriptions.find(Name);
            if (It != Subscriptions.end())
            {
                for (FListener& Listener : It->second)
                {
                    if (!Listener.bDead && Listener.Callback == Callback)
                    {
                        Listener.bDead = true;
                    }
                }
            }
            PendingMutations.push_back({ FPendingMutation::EKind::Unsubscribe, Name, entt::null, Callback });
            return;
        }
        ApplyUnsubscribe(Name, Callback);
    }

    void FLuaEventBus::Dispatch(FStringView EventName, const Lua::FRef& Payload)
    {
        auto It = Subscriptions.find(FName(EventName));
        if (It == Subscriptions.end())
        {
            return;
        }

        FDispatchScope Scope(this);
        TVector<FListener>& Listeners = It->second;

        // Snapshot size before iteration so listeners added mid-dispatch don't fire on
        // this pass. Index-based iteration is safe because mutations are deferred while
        // DispatchDepth > 0.
        const size_t Count = Listeners.size();
        for (size_t i = 0; i < Count; ++i)
        {
            FListener& Listener = Listeners[i];
            if (Listener.bDead)
            {
                continue;
            }
            if (Listener.Callback.IsInvokable())
            {
                Listener.Callback(Payload);
            }
        }
    }

    void FLuaEventBus::DispatchToEntity(entt::entity Target, FStringView EventName, const Lua::FRef& Payload)
    {
        if (Target == entt::null)
        {
            return;
        }

        auto It = Subscriptions.find(FName(EventName));
        if (It == Subscriptions.end())
        {
            return;
        }

        FDispatchScope Scope(this);
        TVector<FListener>& Listeners = It->second;
        const size_t Count = Listeners.size();
        for (size_t i = 0; i < Count; ++i)
        {
            FListener& Listener = Listeners[i];
            if (Listener.bDead || Listener.Owner != Target)
            {
                continue;
            }
            if (Listener.Callback.IsInvokable())
            {
                Listener.Callback(Payload);
            }
        }
    }

    void FLuaEventBus::DispatchDeferred(FStringView EventName, Lua::FRef Payload)
    {
        DeferredQueue.push_back({ FName(EventName), eastl::move(Payload) });
    }

    void FLuaEventBus::ProcessDeferred()
    {
        TVector<FDeferredEvent> ToProcess = eastl::move(DeferredQueue);
        DeferredQueue.clear();

        for (FDeferredEvent& Event : ToProcess)
        {
            Dispatch(Event.EventName.c_str(), Event.Payload);
        }
    }

    void FLuaEventBus::ClearEvent(FStringView EventName)
    {
        const FName Name(EventName);
        if (DispatchDepth > 0)
        {
            auto It = Subscriptions.find(Name);
            if (It != Subscriptions.end())
            {
                for (FListener& Listener : It->second)
                {
                    Listener.bDead = true;
                }
            }
            PendingMutations.push_back({ FPendingMutation::EKind::ClearEvent, Name, entt::null, Lua::FRef() });
            return;
        }
        ApplyClearEvent(Name);
    }

    int32 FLuaEventBus::GetSubscriberCount(FStringView EventName) const
    {
        auto It = Subscriptions.find(FName(EventName));
        if (It == Subscriptions.end())
        {
            return 0;
        }
        int32 Count = 0;
        for (const FListener& Listener : It->second)
        {
            if (!Listener.bDead)
            {
                ++Count;
            }
        }
        return Count;
    }

    void FLuaEventBus::UnsubscribeEntity(entt::entity Entity)
    {
        if (DispatchDepth > 0)
        {
            for (auto& [Name, Listeners] : Subscriptions)
            {
                for (FListener& Listener : Listeners)
                {
                    if (Listener.Owner == Entity)
                    {
                        Listener.bDead = true;
                    }
                }
            }
            PendingMutations.push_back({ FPendingMutation::EKind::UnsubscribeEntity, FName(), Entity, Lua::FRef() });
            return;
        }
        ApplyUnsubscribeEntity(Entity);
    }

    void FLuaEventBus::Clear()
    {
        Subscriptions.clear();
        DeferredQueue.clear();
        PendingMutations.clear();
    }

    void FLuaEventBus::ApplySubscribe(FName EventName, entt::entity Owner, Lua::FRef Callback)
    {
        Subscriptions[EventName].push_back({ Owner, eastl::move(Callback), false });
    }

    void FLuaEventBus::ApplyUnsubscribe(FName EventName, const Lua::FRef& Callback)
    {
        auto It = Subscriptions.find(EventName);
        if (It == Subscriptions.end())
        {
            return;
        }
        TVector<FListener>& Listeners = It->second;
        Listeners.erase(
            eastl::remove_if(Listeners.begin(), Listeners.end(),
                [&](const FListener& L) { return L.Callback == Callback; }),
            Listeners.end());
    }

    void FLuaEventBus::ApplyUnsubscribeEntity(entt::entity Entity)
    {
        for (auto& [Name, Listeners] : Subscriptions)
        {
            Listeners.erase(
                eastl::remove_if(Listeners.begin(), Listeners.end(),
                    [&](const FListener& L) { return L.Owner == Entity; }),
                Listeners.end());
        }
    }

    void FLuaEventBus::ApplyClearEvent(FName EventName)
    {
        Subscriptions.erase(EventName);
    }

    void FLuaEventBus::DrainPendingMutations()
    {
        // Reap tombstones first so subsequent re-subscribes don't see ghost slots.
        for (auto It = Subscriptions.begin(); It != Subscriptions.end(); )
        {
            TVector<FListener>& Listeners = It->second;
            Listeners.erase(
                eastl::remove_if(Listeners.begin(), Listeners.end(),
                    [](const FListener& L) { return L.bDead; }),
                Listeners.end());
            if (Listeners.empty())
            {
                It = Subscriptions.erase(It);
            }
            else
            {
                ++It;
            }
        }

        if (PendingMutations.empty())
        {
            return;
        }

        TVector<FPendingMutation> ToApply = eastl::move(PendingMutations);
        PendingMutations.clear();
        for (FPendingMutation& Mutation : ToApply)
        {
            switch (Mutation.Kind)
            {
            case FPendingMutation::EKind::Subscribe:
                ApplySubscribe(Mutation.EventName, Mutation.Entity, eastl::move(Mutation.Callback));
                break;
            case FPendingMutation::EKind::Unsubscribe:
                ApplyUnsubscribe(Mutation.EventName, Mutation.Callback);
                break;
            case FPendingMutation::EKind::UnsubscribeEntity:
                ApplyUnsubscribeEntity(Mutation.Entity);
                break;
            case FPendingMutation::EKind::ClearEvent:
                ApplyClearEvent(Mutation.EventName);
                break;
            }
        }
    }
}
