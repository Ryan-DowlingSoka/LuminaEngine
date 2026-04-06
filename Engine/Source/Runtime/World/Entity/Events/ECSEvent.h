#pragma once

#include "entt/entt.hpp"
#include "Core/Engine/Engine.h"
#include "World/Entity/Traits.h"
#include "World/Entity/Registry/EntityRegistry.h"


namespace Lumina::Meta
{
    class CStruct;

    template<typename TEvent>
    CStruct* GetStructType()
    {
        return TEvent::StaticStruct();
    }
    
    template<typename TEvent>
    void DispatchEvent_Lua(FEntityRegistry& Registry, Lua::FRef Ref)
    {
        if (Ref.Is<TEvent>())
        {
            Registry.ctx().get<entt::dispatcher&>().trigger(Ref.Get<TEvent>());
        }
    }
    
    template<typename TEvent>
    void EnqueueEvent_Lua(FEntityRegistry& Registry, Lua::FRef Ref)
    {
        if (Ref.Is<TEvent>())
        {
            Registry.ctx().get<entt::dispatcher&>().enqueue(Ref.Get<TEvent>());
        }
    }
    
    template<typename TEvent>
    void RegisterECSEvent()
    {
        using namespace entt::literals;
        auto Meta = entt::meta_factory<TEvent>(GEngine->GetEngineMetaContext())
            .type(TEvent::StaticStruct()->GetName().c_str())
            .traits(ECS::ETraits::Event)
            .template func<&DispatchEvent_Lua<TEvent>>("dispatch_lua"_hs)
            .template func<&EnqueueEvent_Lua<TEvent>>("enqueue_lua"_hs);
    }
}
