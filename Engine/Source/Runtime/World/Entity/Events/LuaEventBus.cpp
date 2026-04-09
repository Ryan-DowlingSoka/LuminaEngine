#include "pch.h"
#include "LuaEventBus.h"
#include "Log/Log.h"

namespace Lumina
{
    void FLuaEventBus::Subscribe(FStringView EventName, Lua::FRef Callback)
    {
        if (!Callback.IsInvokable())
        {
            LOG_WARN("[EventBus] Subscribe('{}') received a non-callable value — ignored.", EventName);
            return;
        }

        Subscriptions[FName(EventName)].push_back({ entt::null, eastl::move(Callback) });
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

        Subscriptions[FName(EventName)].push_back({ Owner, eastl::move(Callback) });
    }

    void FLuaEventBus::Unsubscribe(FStringView EventName, const Lua::FRef& Callback)
    {
        auto It = Subscriptions.find(FName(EventName));
        if (It == Subscriptions.end())
        {
            return;
        }

        TVector<FListener>& Listeners = It->second;
        for (auto ListenerIt = Listeners.begin(); ListenerIt != Listeners.end(); )
        {
            if (ListenerIt->Callback == Callback)
            {
                ListenerIt = Listeners.erase(ListenerIt);
            }
            else
            {
                ++ListenerIt;
            }
        }
    }

    void FLuaEventBus::Dispatch(FStringView EventName, const Lua::FRef& Payload)
    {
        auto It = Subscriptions.find(FName(EventName));
        if (It == Subscriptions.end())
        {
            return;
        }
        
        TVector<FListener> Snapshot = It->second;
        DispatchListeners(Snapshot, Payload);
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
        Subscriptions.erase(FName(EventName));
    }

    int32 FLuaEventBus::GetSubscriberCount(FStringView EventName) const
    {
        auto It = Subscriptions.find(FName(EventName));
        return It != Subscriptions.end() ? (int32)It->second.size() : 0;
    }

    void FLuaEventBus::UnsubscribeEntity(entt::entity Entity)
    {
        for (auto& [Name, Listeners] : Subscriptions)
        {
            for (auto It = Listeners.begin(); It != Listeners.end(); )
            {
                if (It->Owner == Entity)
                {
                    It = Listeners.erase(It);
                }
                else
                {
                    ++It;
                }
            }
        }
    }

    void FLuaEventBus::Clear()
    {
        Subscriptions.clear();
        DeferredQueue.clear();
    }

    void FLuaEventBus::DispatchListeners(TVector<FListener>& Listeners, const Lua::FRef& Payload)
    {
        for (FListener& Listener : Listeners)
        {
            if (Listener.Callback.IsInvokable())
            {
                Listener.Callback(Payload);
            }
        }
    }
}
