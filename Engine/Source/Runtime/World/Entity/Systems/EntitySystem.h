#pragma once

#include "SystemContext.h"
#include "SystemAccess.h"
#include "Core/Engine/Engine.h"
#include "Core/Engine/EngineMetaContext.h"
#include "Scripting/Lua/ScriptTypes.h"
#include "World/Entity/Traits.h"


namespace Lumina
{
    
    #define ENTITY_SYSTEM( ... )\
    FUpdatePriorityList PriorityList = FUpdatePriorityList(__VA_ARGS__);

    namespace Meta
    {
        template<typename TSystem>
        concept HasStartup = requires(TSystem Sys, const FSystemContext& Context)
        {
            { Sys.Startup(Context) } noexcept -> std::same_as<void>;
        };
        
        template<typename TSystem>
        concept HasUpdate = requires(TSystem Sys, const FSystemContext& Context)
        {
            { Sys.Update(Context) } noexcept -> std::same_as<void>;
        };
        
        template<typename TSystem>
        concept HasTeardown = requires(TSystem Sys, const FSystemContext& Context)
        {
            { Sys.Teardown(Context) } noexcept -> std::same_as<void>;
        };
    
        template<typename TSystem>
        concept IsSystem = HasStartup<TSystem> || HasUpdate<TSystem> || HasTeardown<TSystem>;

        // Opt-in: a system declares `static inline FSystemAccess Access = ...;` to allow concurrent
        // execution. Without it the system is treated as exclusive (serial).
        template<typename TSystem>
        concept HasAccess = requires { { TSystem::Access } -> std::convertible_to<const FSystemAccess&>; };

        template<typename TSystem>
        void RegisterECSSystem()
        {
            using namespace entt::literals;
            auto Meta = entt::meta_factory<TSystem>(GetEngineMetaContext())
                .type(TSystem::StaticStruct()->GetName().c_str())
                .traits(ECS::ETraits::System)
                .template data<&TSystem::PriorityList, entt::as_is_t>("PriorityList"_hs);

            if constexpr (HasAccess<TSystem>)
            {
                Meta.template data<&TSystem::Access, entt::as_is_t>("Access"_hs);
            }
        
            if constexpr (HasStartup<TSystem>)
            {
                Meta.template func<&TSystem::Startup>("Startup"_hs);
            }
            
            if constexpr (HasUpdate<TSystem>)
            {
                Meta.template func<&TSystem::Update>("Update"_hs);
            }
        
            if constexpr (HasTeardown<TSystem>)
            {
                Meta.template func<&TSystem::Teardown>("Teardown"_hs);
            }
        }
    }
    
    struct FEntitySystemWrapper
    {
        friend class CWorld;
        
        const FUpdatePriorityList& GetUpdatePriorityList() const;
        FSystemAccess GetSystemAccess() const;
        void Startup(const FSystemContext& SystemContext) const noexcept;
        void Update(const FSystemContext& SystemContext) const noexcept;
        void Teardown(const FSystemContext& SystemContext) const noexcept;
        uint64 GetHash() const noexcept;

    private:
        entt::meta_type Underlying;
        entt::meta_any  Instance;
    };
    
    struct FEntityScriptSystem
    {
        friend class CWorld;
        
        FUpdatePriorityList GetUpdatePriorityList() const;
        FSystemAccess GetSystemAccess() const;   // always exclusive, scripts touch the Lua VM
        void Startup(const FSystemContext& SystemContext) const noexcept;
        void Update(const FSystemContext& SystemContext) const noexcept;
        void Teardown(const FSystemContext& SystemContext) const noexcept;
        uint64 GetHash() const noexcept;

    private:

        TWeakPtr<Lua::FScript> WeakScript;
    };
}
