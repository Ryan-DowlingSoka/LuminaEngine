#include "pch.h"
#include "EntityMessageBus.h"
#include "World/World.h"
#include "World/Entity/Components/ScriptComponent.h"

namespace Lumina
{
    void FEntityMessageBus::Send(entt::entity Target, FStringView MessageName, Lua::FRef Payload)
    {
        if (World == nullptr || Target == entt::null)
        {
            return;
        }

        FEntityRegistry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(Target))
        {
            return;
        }

        SScriptComponent* ScriptComponent = Registry.try_get<SScriptComponent>(Target);
        if (ScriptComponent == nullptr || ScriptComponent->Script == nullptr)
        {
            return;
        }

        auto It = ScriptComponent->MessageHandlers.find(FName(MessageName));
        if (It == ScriptComponent->MessageHandlers.end())
        {
            return;
        }

        Lua::FRef& Handler = It->second;
        if (!Handler.IsInvokable())
        {
            return;
        }

        // Match the lifecycle invocation convention: pass self as the first arg so
        // `function self:OnDamage(payload)` (Luau colon syntax) binds correctly.
        Handler.InvokeAsCoroutine(ScriptComponent->Script->Reference, Payload);
    }

    void FEntityMessageBus::SendDeferred(entt::entity Target, FStringView MessageName, Lua::FRef Payload)
    {
        DeferredQueue.push_back({ Target, FName(MessageName), eastl::move(Payload) });
    }

    void FEntityMessageBus::ProcessDeferred()
    {
        // Move the queue locally first -- a handler invoked during dispatch may
        // call SendDeferred again for next frame; we don't want to drain that here.
        TVector<FDeferredMessage> ToProcess = eastl::move(DeferredQueue);
        DeferredQueue.clear();

        for (FDeferredMessage& Message : ToProcess)
        {
            Send(Message.Target, Message.Name.c_str(), Message.Payload);
        }
    }

    void FEntityMessageBus::Clear()
    {
        DeferredQueue.clear();
    }
}
