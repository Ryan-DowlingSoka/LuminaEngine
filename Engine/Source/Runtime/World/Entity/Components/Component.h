#pragma once

#include "Core/Engine/Engine.h"
#include "Core/Object/Class.h"
#include "Core/Serialization/Archiver.h"
#include "Scripting/Lua/Reference.h"
#include "Scripting/Lua/Stack.h"
#include "Traits/ComponentTraits.h"
#include "World/Entity/Traits.h"

namespace Lumina
{
    namespace Meta
    {
        template<typename TComponent>
        bool HasComponent(entt::registry& Registry, entt::entity Entity)
        {
            return Registry.any_of<TComponent>(Entity);
        }

        template<typename TComponent>
        auto RemoveComponent(entt::registry& Registry, entt::entity Entity)
        {
            return Registry.remove<TComponent>(Entity);
        }

        template<typename TComponent>
        void ClearComponent(entt::registry& Registry)
        {
            Registry.clear<TComponent>();
        }

        template<typename TComponent>
        decltype(auto) EmplaceComponent(entt::registry& Registry, entt::entity Entity, const entt::meta_any& Any)
        {
            if constexpr (eastl::is_empty_v<TComponent>)
            {
                Registry.emplace<TComponent>(Entity);
            }
            else
            {
                return Registry.emplace_or_replace<TComponent>(Entity, Any ? Any.cast<const TComponent&>() : TComponent{});
            }
        }
        
        template<typename TComponent>
        TComponent& PatchComponent(entt::registry& Registry, entt::entity Entity, const entt::meta_any& Any)
        {
            return Registry.patch<TComponent>(Entity, [&](TComponent& Type)
            {
                Type = Any.cast<const TComponent&>();
            });
        }

        template<typename TComponent>
        TComponent& GetComponent(entt::registry& Registry, entt::entity Entity)
        {
            return Registry.get<TComponent>(Entity);
        }
        
        template<typename TComponent>
        Lua::FRef GetComponent_Lua(entt::registry& Registry, entt::entity Entity, const Lua::FRef& Ref)
        {
            TComponent* Component = Registry.try_get<TComponent>(Entity);
            if (Component == nullptr)
            {
                lua_pushnil(Ref.GetState());
                return Lua::FRef(Ref.GetState(), -1);
            }
            Lua::TStack<TComponent*>::Push(Ref.GetState(), Component);
            return Lua::FRef(Ref.GetState(), -1);
        }
        
        template<typename TComponent>
        Lua::FRef EmplaceComponent_Lua(entt::registry& Registry, entt::entity Entity, const Lua::FRef& Ref)
        {
            if constexpr (eastl::is_empty_v<TComponent>)
            {
                Registry.emplace<TComponent>(Entity);
                return Lua::FNil{};
            }
            else
            {
                TComponent* New = nullptr;
                if (Ref.Is<TComponent>())
                {
                    New = &Registry.emplace_or_replace<TComponent>(Entity, Ref.As<TComponent>().value());
                }
                else
                {
                    New = &Registry.emplace_or_replace<TComponent>(Entity);
                }
                
                Lua::TStack<TComponent*>::Push(Ref.GetState(), New);
                return Lua::FRef(Ref.GetState(), -1);
            }
        }

        template<typename TComponent>
        void Serialize(FArchive& Ar, entt::meta_any& Any)
        {
            CStruct* Struct = TComponent::StaticStruct();
            TComponent& Instance = Any.cast<TComponent&>();
            Struct->SerializeTaggedProperties(Ar, &Instance);
        }
        
        template<typename TComponent>
        CStruct* GetStructType()
        {
            return TComponent::StaticStruct();
        }
        
        template<typename TComponent>
        void RegisterComponentMeta()
        {
            using namespace entt::literals;
            auto Meta = entt::meta_factory<TComponent>(GEngine->GetEngineMetaContext())
                .type(TComponent::StaticStruct()->GetName().c_str())
                .traits(ECS::ETraits::Component)
                .template func<&GetStructType<TComponent>>("static_struct"_hs);

            Meta
            .template func<&RemoveComponent<TComponent>>("remove"_hs)
            .template func<&ClearComponent<TComponent>>("clear"_hs)
            .template func<&EmplaceComponent<TComponent>>("emplace"_hs)
            .template func<&EmplaceComponent_Lua<TComponent>>("emplace_lua"_hs)
            .template func<&HasComponent<TComponent>>("has"_hs);
            
            if constexpr (!eastl::is_empty_v<TComponent>)
            {
                Meta.template func<&GetComponent<TComponent>>("get"_hs);
                Meta.template func<&GetComponent_Lua<TComponent>>("get_lua"_hs);
                Meta.template func<&PatchComponent<TComponent>>("patch"_hs);
                Meta.template func<&Serialize<TComponent>>("serialize"_hs);
            }
        }
    }
}
