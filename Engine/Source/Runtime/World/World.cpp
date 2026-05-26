#include "pch.h"
#include "World.h"
#include <cmath>
#include <utility>
#include "lua.h"
#include "WorldManager.h"
#include "WorldContext.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/GeometryCollection/GeometryCollection.h"
#include "Assets/AssetTypes/Material/MaterialInterface.h"
#include "Assets/AssetTypes/Prefabs/Prefab.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Package/Package.h"
#include "Audio/AudioGlobals.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Core/Engine/Engine.h"
#include "Core/Profiler/CPUProfiler.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectIterator.h"
#include "Core/Serialization/MemoryArchiver.h"
#include "Core/Serialization/ObjectArchiver.h"
#include "EASTL/sort.h"
#include "Entity/EntityUtils.h"
#include "Entity/Components/CameraComponent.h"
#include "Entity/Components/DestructibleComponent.h"
#include "Entity/Components/DirtyComponent.h"
#include "Entity/Components/EditorComponent.h"
#include "Entity/Components/LifetimeComponent.h"
#include "Entity/Components/StaticMeshComponent.h"
#include "Entity/Events/ImpulseEvent.h"
#include "entity/components/entitytags.h"
#include "Entity/Components/LineBatcherComponent.h"
#include "Entity/Components/NameComponent.h"
#include "Entity/Components/PhysicsComponent.h"
#include "Entity/Components/PostProcessComponent.h"
#include "Entity/Components/TransformComponent.h"
#include "Entity/Components/ScriptComponent.h"
#include "Entity/Components/WidgetComponent.h"
#include "Entity/Components/SingletonEntityComponent.h"
#include "Entity/Systems/SystemSingletons.h"
#include "entity/components/tagcomponent.h"
#include "Entity/Events/WorldEvents.h"
#include "Entity/Events/LuaEventBus.h"
#include "Physics/Physics.h"
#include "Renderer/RenderThread.h"
#include "Scene/RenderScene/Forward/ForwardRenderScene.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/VariadicArgs.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "Subsystems/FCameraManager.h"
#include "Subsystems/WorldSettings.h"
#include "UI/RmlUiBridge.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/entity/systems/EntitySystem.h"

namespace Lumina
{
    namespace LuaBinds
    {
        using namespace entt::literals;

        static bool HasComponent_Lua(FEntityRegistry& Registry, entt::entity Entity, Lua::FRef Ref)
        {
            LUMINA_PROFILE_SECTION("Has Component [Lua]");
            entt::id_type Type = ECS::Utils::GetTypeID(eastl::move(Ref));
            auto Meta = ECS::Utils::InvokeMetaFunc(Type, "has"_hs, entt::forward_as_meta(Registry), Entity);
            return Meta.cast<bool>();
        }
        
        static Lua::FRef GetComponent_Lua(FEntityRegistry& Registry, entt::entity Entity, Lua::FRef Ref)
        {
            LUMINA_PROFILE_SECTION("Get Component [Lua]");
            entt::id_type Type = ECS::Utils::GetTypeID(Ref);
            auto Meta = ECS::Utils::InvokeMetaFunc(Type, "get_lua"_hs, entt::forward_as_meta(Registry), Entity, entt::forward_as_meta(Ref));
            return Meta.cast<Lua::FRef>();
        }
        
        static size_t RemoveComponent_Lua(FEntityRegistry& Registry, entt::entity Entity, Lua::FRef Ref)
        {
            LUMINA_PROFILE_SECTION("Remove Component [Lua]");
            entt::id_type Type = ECS::Utils::GetTypeID(Ref);
            auto Meta = ECS::Utils::InvokeMetaFunc(Type, "remove"_hs, entt::forward_as_meta(Registry), Entity);
            return Meta.cast<size_t>();
        }
        
        static Lua::FRef EmplaceComponent_Lua(FEntityRegistry& Registry, entt::entity Entity, Lua::FRef Ref)
        {
            LUMINA_PROFILE_SECTION("Emplace Component [Lua]");
            entt::id_type Type = ECS::Utils::GetTypeID(Ref);
            auto Meta = ECS::Utils::InvokeMetaFunc(Type, "emplace_lua"_hs, entt::forward_as_meta(Registry), Entity, entt::forward_as_meta(Ref));
            return Meta.cast<Lua::FRef>();
        }
        
        static void ForEachEntity_Lua(FEntityRegistry& Registry, Lua::FRef Ref)
        {
            auto View = Registry.view<entt::entity>();
            View.each([&](entt::entity Entity)
            {
               Ref(Entity); 
            });
        }
        
        static bool IsEntityNull_Lua(entt::entity Entity)
        {
            return Entity == entt::null;
        }
        
        static entt::runtime_view RuntimeView_Lua(FEntityRegistry& Registry, Lua::FVariadicArgs Args)
        {
            LUMINA_PROFILE_SCOPE();

            entt::runtime_view RuntimeView;

            for (int i = 0; i < Args.Count(); ++i)
            {
                entt::id_type Type = ECS::Utils::GetTypeID(Args.Get<Lua::FRef>(i));
                
                entt::meta_type Meta = entt::resolve(Type);
                if (!Meta)
                {
                    if (entt::basic_sparse_set<>* Storage = Registry.storage(Type))
                    {
                        RuntimeView.iterate(*Storage);
                    }
                    else if (Args.Is<FStringView>(i))
                    {
                        entt::hashed_string Hash(Args.Get<FStringView>(i).data());
                        if (entt::basic_sparse_set<>* Set = Registry.storage(Hash))
                        {
                            RuntimeView.iterate(*Set);
                        }
                    }
                }
                else if (entt::basic_sparse_set<>* Storage = Registry.storage(Meta.info().hash()))
                {
                    RuntimeView.iterate(*Storage);
                }
            }

            return RuntimeView;
        }
        
        static uint64 EntityCount_Lua(FEntityRegistry& Registry)
        {
            return Registry.view<entt::entity>()->size();
        }
        
        static entt::entity CreateEntity_Lua(FEntityRegistry& Registry)
        {
            return Registry.create();
        }
        
        static uint32 DestroyEntity_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            return Registry.destroy(Entity);
        }
        
        static void DispatchEvent_Lua(FEntityRegistry& Registry, Lua::FRef Ref)
        {
            LUMINA_PROFILE_SECTION("Dispatch Event [Lua]");
            entt::id_type Type = ECS::Utils::GetTypeID(Ref);
            auto Meta = ECS::Utils::InvokeMetaFunc(Type, "dispatch_lua"_hs, entt::forward_as_meta(Registry), entt::forward_as_meta(Ref));
        }
        
        static void EnqueueEvent_Lua(FEntityRegistry& Registry, Lua::FRef Ref)
        {
            LUMINA_PROFILE_SECTION("Enqueue Event [Lua]");
            entt::id_type Type = ECS::Utils::GetTypeID(Ref);
            auto Meta = ECS::Utils::InvokeMetaFunc(Type, "enqueue_lua"_hs, entt::forward_as_meta(Registry), entt::forward_as_meta(Ref));
        }
        
        
        static void ForEachInRuntimeView_Lua(entt::runtime_view& View, Lua::FRef Func)
        {
            LUMINA_PROFILE_SCOPE();

            View.each([&](entt::entity Entity)
            {
                Func(Entity);
            });
        }
        
        static TVector<entt::entity> RuntimeViewGetEntities_Lua(entt::runtime_view& View)
        {
            LUMINA_PROFILE_SCOPE();
            
            TVector<entt::entity> Entities;
            Entities.reserve(View.size_hint());
            View.each([&](entt::entity Entity)
            {
                Entities.emplace_back(Entity);
            });
            
            return Entities;
        }
        
        static size_t RuntimeViewSizeHint_Lua(entt::runtime_view& View)
        {
            LUMINA_PROFILE_SCOPE();

            return View.size_hint();
        }
        
        static entt::entity DuplicateEntity_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            return ECS::Utils::DuplicateEntity(Registry, Entity);
        }

        static entt::entity GetParent_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            return ECS::Utils::GetParent(Registry, Entity);
        }

        static entt::entity GetRoot_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            return ECS::Utils::GetRootEntity(Registry, Entity);
        }

        static TVector<entt::entity> GetChildren_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            TVector<entt::entity> Children;
            ECS::Utils::CollectChildren(Registry, Entity, Children);
            return Children;
        }

        static TVector<entt::entity> GetAncestors_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            TVector<entt::entity> Ancestors;
            ECS::Utils::CollectAncestors(Registry, Entity, Ancestors);
            return Ancestors;
        }

        static TVector<entt::entity> GetDescendants_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            // CollectDescendants includes the entity itself at index 0; drop it so callers get descendants only.
            TVector<entt::entity> Descendants;
            ECS::Utils::CollectDescendants(Registry, Entity, Descendants);
            if (!Descendants.empty())
            {
                Descendants.erase(Descendants.begin());
            }
            return Descendants;
        }

        static bool IsDescendantOf_Lua(FEntityRegistry& Registry, entt::entity Entity, entt::entity Ancestor)
        {
            return ECS::Utils::IsDescendantOf(Registry, Entity, Ancestor);
        }

        static size_t GetChildCount_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            return ECS::Utils::GetChildCount(Registry, Entity);
        }

        static void Reparent_Lua(FEntityRegistry& Registry, entt::entity Child, entt::entity Parent)
        {
            ECS::Utils::ReparentEntity(Registry, Child, Parent);
        }

        static void Detach_Lua(FEntityRegistry& Registry, entt::entity Child)
        {
            ECS::Utils::RemoveFromParent(Registry, Child);
        }

        static entt::entity FindChild_Lua(FEntityRegistry& Registry, entt::entity Entity, FName Name)
        {
            return ECS::Utils::FindChildByName(Registry, Entity, Name);
        }

        static entt::entity FindDescendant_Lua(FEntityRegistry& Registry, entt::entity Entity, FName Name)
        {
            return ECS::Utils::FindDescendantByName(Registry, Entity, Name);
        }

        static TVector<entt::entity> GetSiblings_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            TVector<entt::entity> Siblings;
            ECS::Utils::CollectSiblings(Registry, Entity, Siblings);
            return Siblings;
        }

        static size_t GetSiblingIndex_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            return ECS::Utils::GetSiblingIndex(Registry, Entity);
        }

        static entt::entity GetNextSibling_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            return ECS::Utils::GetNextSibling(Registry, Entity);
        }

        static entt::entity GetPrevSibling_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            return ECS::Utils::GetPrevSibling(Registry, Entity);
        }

        static Lua::FRef GetScriptTable_Lua(FEntityRegistry& Registry, entt::entity Entity)
        {
            LUMINA_PROFILE_SECTION("Get Script Table [Lua]");
            if (!Registry.valid(Entity))
            {
                return {};
            }

            SScriptComponent* ScriptComp = Registry.try_get<SScriptComponent>(Entity);
            if (ScriptComp == nullptr || ScriptComp->Script == nullptr)
            {
                return {};
            }

            return ScriptComp->Script->Reference;
        }

        // Per-thread resolver; world set via lua_setthreaddata in SetupScriptComponent.
        static CWorld* CurrentWorld(lua_State* L)
        {
            const auto* TD = static_cast<Lua::FScriptThreadData*>(lua_getthreaddata(L));
            return TD ? TD->World : nullptr;
        }
    }
    
    
    CWorld::CWorld()
        : SingletonEntity(entt::null)
        , SystemContext(this)
        , LineBatcherComponent(nullptr)
    {
    }

    void CWorld::RegisterLuaModule(Lua::FRef& GlobalRef)
    {
        FTimerManager::RegisterLuaModule(GlobalRef);

        GlobalRef.NewClass<FLuaEventBus>("EventBus")
            .AddFunction<&FLuaEventBus::Subscribe>("Subscribe")
            .AddFunction<&FLuaEventBus::SubscribeEntity>("SubscribeEntity")
            .AddFunction<&FLuaEventBus::Unsubscribe>("Unsubscribe")
            .AddFunction<&FLuaEventBus::Dispatch>("Dispatch")
            .AddFunction<&FLuaEventBus::DispatchDeferred>("DispatchDeferred")
            .AddFunction<&FLuaEventBus::ClearEvent>("ClearEvent")
            .AddFunction<&FLuaEventBus::GetSubscriberCount>("GetSubscriberCount")
            .Register();
        
        GlobalRef.NewClass<Physics::IPhysicsScene>("PhysicsScene")
            .AddFunction<&Physics::IPhysicsScene::GetEntityBodyID>("GetEntityBodyID")
            .AddFunction<&Physics::IPhysicsScene::ActivateBody>("ActivateBody")
            .AddFunction<&Physics::IPhysicsScene::DeactivateBody>("DeactivateBody")
            .AddFunction<&Physics::IPhysicsScene::OnImpulseEvent>("ApplyImpulse")
            .AddFunction<&Physics::IPhysicsScene::OnForceEvent>("ApplyForce")
            .AddFunction<&Physics::IPhysicsScene::OnTorqueEvent>("ApplyTorque")
            .AddFunction<&Physics::IPhysicsScene::OnAngularImpulseEvent>("ApplyAngularImpulse")
            .AddFunction<&Physics::IPhysicsScene::OnSetVelocityEvent>("SetVelocity")
            .AddFunction<&Physics::IPhysicsScene::OnSetAngularVelocityEvent>("SetAngularVelocity")
            .AddFunction<&Physics::IPhysicsScene::OnAddImpulseAtPositionEvent>("AddImpulseAtPosition")
            .AddFunction<&Physics::IPhysicsScene::OnAddForceAtPositionEvent>("AddForceAtPosition")
            .AddFunction<&Physics::IPhysicsScene::OnAddImpulseAtPositionEvent>("AddImpulseAtPosition")
            .AddFunction<&Physics::IPhysicsScene::OnSetGravityFactorEvent>("SetGravityFactor")
            .AddFunction<&Physics::IPhysicsScene::GetVelocityAtPoint>("GetVelocityAtPoint")
            .AddFunction<&Physics::IPhysicsScene::GetLinearVelocity>("GetLinearVelocity")
            .AddFunction<&Physics::IPhysicsScene::GetAngularVelocity>("GetAngularVelocity")
            .AddFunction<&Physics::IPhysicsScene::GetCenterOfMass>("GetCenterOfMass")
            .AddFunction<&Physics::IPhysicsScene::GetBodyPosition>("GetBodyPosition")
            .AddFunction<&Physics::IPhysicsScene::GetBodyRotation>("GetBodyRotation")
            .Register();
        
        GlobalRef.NewClass<entt::runtime_view>("RuntimeView")
            .AddFunction<&entt::runtime_view::contains>("Contains")
            .AddFunction<&LuaBinds::ForEachInRuntimeView_Lua>("Each")
            .AddFunction<&LuaBinds::RuntimeViewSizeHint_Lua>("SizeHint")
            .AddFunction<&LuaBinds::RuntimeViewGetEntities_Lua>("GetEntities")
            .Register();
        
        // Raw cfunctions: typed binding wrappers don't expose lua_State*.
        if (lua_State* VM = Lua::FScriptingContext::Get().GetVM())
        {
            Lua::FRef CameraTable = GlobalRef.NewTable("Camera");
            CameraTable.Push();

            lua_pushcfunction(VM, +[](lua_State* L) -> int
            {
                CWorld* World = LuaBinds::CurrentWorld(L);
                const entt::entity E = lua_isnumber(L, 1)
                    ? static_cast<entt::entity>(static_cast<uint32>(lua_tointeger(L, 1)))
                    : entt::null;
                if (World) World->SetActiveCamera(E);
                return 0;
            }, "Camera.SetActive");
            lua_rawsetfield(VM, -2, "SetActive");

            lua_pushcfunction(VM, +[](lua_State* L) -> int
            {
                CWorld* World = LuaBinds::CurrentWorld(L);
                const entt::entity E = World ? World->GetActiveCameraEntity() : entt::null;
                lua_pushinteger(L, static_cast<int64_t>(static_cast<uint32>(E)));
                return 1;
            }, "Camera.GetActive");
            lua_rawsetfield(VM, -2, "GetActive");
            lua_pop(VM, 1);

            Lua::FRef WorldTable = GlobalRef.NewTable("World");
            WorldTable.Push();

            lua_pushcfunction(VM, +[](lua_State* L) -> int
            {
                CWorld* World = LuaBinds::CurrentWorld(L);
                const char* Name = luaL_checkstring(L, 1);
                const entt::entity E = World ? World->GetEntityByName(FName(Name)) : entt::null;
                lua_pushinteger(L, static_cast<int64_t>(static_cast<uint32>(E)));
                return 1;
            }, "World.FindByName");
            lua_rawsetfield(VM, -2, "FindByName");

            lua_pushcfunction(VM, +[](lua_State* L) -> int
            {
                CWorld* World = LuaBinds::CurrentWorld(L);
                const char* Tag = luaL_checkstring(L, 1);
                const entt::entity E = World ? World->GetEntityByTag(FName(Tag)) : entt::null;
                lua_pushinteger(L, static_cast<int64_t>(static_cast<uint32>(E)));
                return 1;
            }, "World.FindByTag");
            lua_rawsetfield(VM, -2, "FindByTag");

            lua_pushcfunction(VM, +[](lua_State* L) -> int
            {
                CWorld* World = LuaBinds::CurrentWorld(L);
                lua_pushinteger(L, World ? static_cast<int64_t>(World->GetNumEntities()) : 0);
                return 1;
            }, "World.GetNumEntities");
            lua_rawsetfield(VM, -2, "GetNumEntities");

            lua_pushcfunction(VM, +[](lua_State* L) -> int
            {
                CWorld* World = LuaBinds::CurrentWorld(L);
                lua_pushnumber(L, World ? World->GetWorldDeltaTime() : 0.0);
                return 1;
            }, "World.GetDeltaTime");
            lua_rawsetfield(VM, -2, "GetDeltaTime");

            lua_pushcfunction(VM, +[](lua_State* L) -> int
            {
                CWorld* World = LuaBinds::CurrentWorld(L);
                lua_pushnumber(L, World ? World->GetTimeSinceWorldCreation() : 0.0);
                return 1;
            }, "World.GetTimeSinceCreation");
            lua_rawsetfield(VM, -2, "GetTimeSinceCreation");

            // World.Fracture(entity): shatter using the component defaults, blast centered on the entity.
            lua_pushcfunction(VM, +[](lua_State* L) -> int
            {
                CWorld* World = LuaBinds::CurrentWorld(L);
                const entt::entity E = lua_isnumber(L, 1)
                    ? static_cast<entt::entity>(static_cast<uint32>(lua_tointeger(L, 1)))
                    : entt::null;
                bool bResult = false;
                if (World && E != entt::null)
                {
                    bResult = World->FractureEntity(E, World->GetEntityLocation(E), 0.0f);
                }
                lua_pushboolean(L, bResult);
                return 1;
            }, "World.Fracture");
            lua_rawsetfield(VM, -2, "Fracture");

            // World.FractureAt(entity, x, y, z [, strength]): shatter with an explicit blast origin and speed.
            lua_pushcfunction(VM, +[](lua_State* L) -> int
            {
                CWorld* World = LuaBinds::CurrentWorld(L);
                const entt::entity E = lua_isnumber(L, 1)
                    ? static_cast<entt::entity>(static_cast<uint32>(lua_tointeger(L, 1)))
                    : entt::null;
                const glm::vec3 Origin(
                    static_cast<float>(luaL_checknumber(L, 2)),
                    static_cast<float>(luaL_checknumber(L, 3)),
                    static_cast<float>(luaL_checknumber(L, 4)));
                const float Strength = static_cast<float>(luaL_optnumber(L, 5, 0.0));
                const bool bResult = World && E != entt::null && World->FractureEntity(E, Origin, Strength);
                lua_pushboolean(L, bResult);
                return 1;
            }, "World.FractureAt");
            lua_rawsetfield(VM, -2, "FractureAt");
            lua_pop(VM, 1);
        }

        GlobalRef.NewClass<FEntityRegistry>("FEntityRegistry")
            .AddFunction<&FEntityRegistry::valid>("Valid")
            .AddFunction<&FEntityRegistry::orphan>("Orphan")
            .AddFunction<&FEntityRegistry::compact<>>("Compact")
            .AddFunction<&LuaBinds::EntityCount_Lua>("EntityCount")
            .AddFunction<&LuaBinds::DuplicateEntity_Lua>("Duplicate")
            .AddFunction<&LuaBinds::RemoveComponent_Lua>("Remove")
            .AddFunction<&LuaBinds::CreateEntity_Lua>("Create")
            .AddFunction<&LuaBinds::DestroyEntity_Lua>("Destroy")
            .AddFunction<&LuaBinds::HasComponent_Lua>("Has")
            .AddFunction<&LuaBinds::EmplaceComponent_Lua>("Emplace")
            .AddFunction<&LuaBinds::ForEachEntity_Lua>("ForEachEntity")
            .AddFunction<&LuaBinds::GetComponent_Lua>("Get")
            .AddFunction<&LuaBinds::GetScriptTable_Lua>("GetScriptTable")
            .AddFunction<&LuaBinds::GetParent_Lua>("GetParent")
            .AddFunction<&LuaBinds::GetRoot_Lua>("GetRoot")
            .AddFunction<&LuaBinds::GetChildren_Lua>("GetChildren")
            .AddFunction<&LuaBinds::GetAncestors_Lua>("GetAncestors")
            .AddFunction<&LuaBinds::GetDescendants_Lua>("GetDescendants")
            .AddFunction<&LuaBinds::IsDescendantOf_Lua>("IsDescendantOf")
            .AddFunction<&LuaBinds::GetChildCount_Lua>("GetChildCount")
            .AddFunction<&LuaBinds::FindChild_Lua>("FindChild")
            .AddFunction<&LuaBinds::FindDescendant_Lua>("FindDescendant")
            .AddFunction<&LuaBinds::GetSiblings_Lua>("GetSiblings")
            .AddFunction<&LuaBinds::GetSiblingIndex_Lua>("GetSiblingIndex")
            .AddFunction<&LuaBinds::GetNextSibling_Lua>("GetNextSibling")
            .AddFunction<&LuaBinds::GetPrevSibling_Lua>("GetPrevSibling")
            .AddFunction<&LuaBinds::Reparent_Lua>("Reparent")
            .AddFunction<&LuaBinds::Detach_Lua>("Detach")
            .AddFunction<&LuaBinds::IsEntityNull_Lua>("IsNull")
            .AddFunction<&LuaBinds::RuntimeView_Lua>("RuntimeView")
            .AddFunction<&LuaBinds::DispatchEvent_Lua>("DispatchEvent")
            .AddFunction<&LuaBinds::EnqueueEvent_Lua>("EnqueueEvent")
            .Register();
    }

    void CWorld::Serialize(FArchive& Ar)
    {
        CObject::Serialize(Ar);
        
        if (Ar.IsReading())
        {
            RegistryPending.clear<>();
            ECS::Utils::SerializeRegistry(Ar, RegistryPending);
        }
        else
        {
            ECS::Utils::SerializeRegistry(Ar, EntityRegistry);
        }
    }

    void CWorld::PreLoad()
    {
    }

    void CWorld::PostLoad()
    {
    }
    
    void CWorld::InitializeWorld(EWorldType InWorldType)
    {
        using namespace entt::literals;
        
        WorldType = InWorldType;

        EntityRegistry.swap(RegistryPending);
        RegistryPending = {};

        // Re-apply prefab asset state to any prefab instances that were serialized into this world.
        // Keeps placed instances in sync with their source prefab whenever the prefab is edited.
        CPrefab::RefreshAllInstancesInWorld(this);

        EntityRegistry.ctx().emplace<entt::dispatcher&>(SingletonDispatcher);
        
        auto WorldSettingsView = EntityRegistry.view<SDefaultWorldSettings>();
        for (auto Entity : WorldSettingsView)
        {
            if (!ALERT_IF_NOT(WorldSettingsView->size() == 1, "Multiple world settings were detected in the world! {}", WorldSettingsView->size()))
            {
                EntityRegistry.clear<SDefaultWorldSettings>();
                break;
            }
            
            SingletonEntity = Entity;
            break;
        }
        
        if (!EntityRegistry.valid(SingletonEntity))
        {
            SingletonEntity = EntityRegistry.create();
            EntityRegistry.emplace<SDefaultWorldSettings>(SingletonEntity);
        }
        
        LineBatcherComponent = &EntityRegistry.emplace<FLineBatcherComponent>(SingletonEntity);
        EntityRegistry.emplace<FSingletonEntityTag>(SingletonEntity);
        EntityRegistry.emplace<FHideInSceneOutliner>(SingletonEntity);
        
        // Physics scene only for simulating worlds; Jolt reserves ~hundreds of MB up front.
        if (WorldType == EWorldType::Game || WorldType == EWorldType::Simulation)
        {
            PhysicsScene = Physics::GetPhysicsContext()->CreatePhysicsScene(this);
        }
        CameraManager   = MakeUnique<FCameraManager>(this);

        // Emplaced even when null so ctx().get<>() consumers find the key (value is null in
        // non-simulating worlds and must be null-checked).
        EntityRegistry.ctx().emplace<Physics::IPhysicsScene*>(PhysicsScene.get());
        EntityRegistry.ctx().emplace<FCameraManager*>(CameraManager.get());
        EntityRegistry.ctx().emplace<FSystemContext&>(SystemContext);
        EntityRegistry.ctx().emplace<CWorld*>(this);
        EntityRegistry.ctx().emplace<FLuaEventBus*>(&LuaEventBus);

        // System-produced singletons: SCameraSystem writes FResolvedSceneView (read in Extract);
        // SScriptSystem owns FScriptFixedUpdateState (fixed-step accumulator).
        EntityRegistry.ctx().emplace<FResolvedSceneView>();
        EntityRegistry.ctx().emplace<FScriptFixedUpdateState>();

        CreateRenderer();
        UIContext = RmlUi::CreateWorldUI(this);
        RegisterSystems();
        
        if (WorldType == EWorldType::Game || WorldType == EWorldType::Simulation)
        {
            PhysicsScene->Simulate();
        }
        
        ForEachUniqueSystem([&](const FSystemVariant& System)
        {
            eastl::visit([&](const auto& Sys) { Sys.Startup(SystemContext); }, System);
        });
        
        EntityRegistry.on_destroy   <FRelationshipComponent>()      .connect<&ThisClass::OnRelationshipComponentDestroyed>(this);
        EntityRegistry.on_construct <STransformComponent>()         .connect<&ThisClass::OnTransformComponentConstruct>(this);
        EntityRegistry.on_construct <SScriptComponent>()            .connect<&ThisClass::OnScriptComponentConstruct>(this);
        EntityRegistry.on_destroy   <SScriptComponent>()            .connect<&ThisClass::OnScriptComponentDestroyed>(this);
        // Left connected through teardown: fires at clear() to release widget RTs.
        EntityRegistry.on_destroy   <SWidgetComponent>()            .connect<&ThisClass::OnWidgetComponentDestroyed>(this);
        SystemContext.EventSink     <FSwitchActiveCameraEvent>()    .connect<&ThisClass::OnChangeCameraEvent>(this);
        SystemContext.EventSink     <FScriptComponentPendingReady>().connect<&ThisClass::OnScriptComponentPendingReady>(this);

        ScriptReloadedHandle = Lua::FScriptingContext::Get().OnScriptLoaded.AddMember(this, &ThisClass::OnScriptSourceReloaded);
        
        if (lua_State* VM = Lua::FScriptingContext::Get().GetVM())
        {
            lua_newtable(VM);
            DrawInterfaceRef = Lua::FRef(VM, -1);
            auto* DI = static_cast<IPrimitiveDrawInterface*>(this);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawLine>   ("DrawLine",    DI);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawBox>    ("DrawBox",     DI);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawSphere> ("DrawSphere",  DI);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawCapsule>("DrawCapsule", DI);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawCone>   ("DrawCone",    DI);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawArrow>  ("DrawArrow",   DI);
        }
        
        auto TransformView = EntityRegistry.view<STransformComponent>();
        TransformView.each([&](entt::entity Entity, STransformComponent& TransformComponent)
        {
            TransformComponent.Registry = &EntityRegistry;
            TransformComponent.Entity = Entity;
        });
        
        auto CameraView = EntityRegistry.view<SCameraComponent>(entt::exclude<SDisabledTag>);
        CameraView.each([&](entt::entity Entity, const SCameraComponent& Camera)
        {
           if (Camera.bAutoActivate)
           {
               SingletonDispatcher.trigger<FSwitchActiveCameraEvent>(FSwitchActiveCameraEvent{Entity});
           }
        });
        
        InitializeScriptEntities();
        
        if (WorldType == EWorldType::Simulation || WorldType == EWorldType::Game)
        {
            bPaused = false;
        }
        
        bInitializing = false;
    }
    
    void CWorld::TeardownWorld()
    {
        FlushRenderingCommands();
        GRenderContext->WaitIdle();

        RmlUi::DestroyWorldUI(this);
        UIContext.reset();

        GAudioContext->StopAllSounds();

        if (ScriptReloadedHandle.IsValid())
        {
            Lua::FScriptingContext::Get().OnScriptLoaded.Remove(ScriptReloadedHandle);
            ScriptReloadedHandle = {};
        }

        EntityRegistry.on_destroy<FRelationshipComponent>().disconnect<&ThisClass::OnRelationshipComponentDestroyed>(this);
        EntityRegistry.on_destroy<SScriptComponent>().disconnect<&ThisClass::OnScriptComponentConstruct>(this);
        
        ForEachUniqueSystem([&](const FSystemVariant& System)
        {
            eastl::visit([&](const auto& Sys) { Sys.Teardown(SystemContext); }, System);
        });
        
        if (WorldType == EWorldType::Game || WorldType == EWorldType::Simulation)
        {
            PhysicsScene->StopSimulate();
        }
        
        LuaEventBus.Clear();
        TimerManager.Clear();

        RegistryPending.clear<>();
        EntityRegistry.clear<>();
        PhysicsScene.reset();
        DestroyRenderer();
        
        FCoreDelegates::PostWorldUnload.Broadcast();

        Lua::FScriptingContext::Get().RunGC();
    }
    
    static const char* StageName(EUpdateStage Stage)
    {
        switch (Stage)
        {
        case EUpdateStage::FrameStart:    return "FrameStart";
        case EUpdateStage::PrePhysics:    return "PrePhysics";
        case EUpdateStage::DuringPhysics: return "DuringPhysics";
        case EUpdateStage::PostPhysics:   return "PostPhysics";
        case EUpdateStage::FrameEnd:      return "FrameEnd";
        case EUpdateStage::Paused:        return "Paused";
        default:                          return "Unknown";
        }
    }

    void CWorld::Update(const FUpdateContext& Context)
    {
        LUMINA_PROFILE_SCOPE();

        const EUpdateStage Stage = Context.GetUpdateStage();

        FCPUProfiler::Get().PushWorldTarget(this);
        struct FPopGuard { ~FPopGuard() { FCPUProfiler::Get().PopTarget(); } } PopGuard;

        CPU_PROFILE_SCOPE(StageName(Stage));

        if (Stage == EUpdateStage::FrameStart)
        {
            DeltaTime = Context.GetDeltaTime() * GetDefaultWorldSettings().DeltaTimeScale;
            TimeSinceCreation += DeltaTime;
        }

        if (bPaused && Stage != EUpdateStage::Paused || (!bPaused && Stage == EUpdateStage::Paused))
        {
            return;
        }

        SystemContext.DeltaTime     = DeltaTime;
        SystemContext.Time          = TimeSinceCreation;
        SystemContext.UpdateStage   = Stage;

        {
            CPU_PROFILE_SCOPE("PendingReady Dispatch");
            SingletonDispatcher.update<FScriptComponentPendingReady>();
        }

        if (Stage == EUpdateStage::FrameStart)
        {
            {
                CPU_PROFILE_SCOPE_COLOR("Lua Events (Deferred)", FColor(0.95f, 0.70f, 0.25f));
                LuaEventBus.ProcessDeferred();
            }
            {
                CPU_PROFILE_SCOPE("Timers");
                TimerManager.Tick(static_cast<float>(DeltaTime));
            }
        }

        {
            CPU_PROFILE_SCOPE("Systems");
            TickSystems(SystemContext);
        }
    }

    bool CWorld::IsScriptActiveInWorld(entt::entity Entity) const
    {
        if (WorldType == EWorldType::Editor)
        {
            return EntityRegistry.all_of<FScriptHasEditorUpdateFn>(Entity);
        }
        return true;
    }

    void CWorld::TickPhysics()
    {
        LUMINA_PROFILE_SCOPE();

        if (bPaused || PhysicsScene == nullptr)
        {
            return;
        }

        CPU_PROFILE_SCOPE_COLOR("Physics", FColor(0.20f, 0.75f, 0.90f));
        PhysicsScene->Update(DeltaTime);
    }

    void CWorld::DispatchPhysicsEvents()
    {
        if (PhysicsScene == nullptr)
        {
            return;
        }

        PhysicsScene->DispatchPendingEvents();
    }

    void CWorld::Render(ICommandList& CmdList, uint8 FrameIndex) const
    {
        LUMINA_PROFILE_SCOPE();

        RmlUi::RenderWorldWidgets(this, CmdList);

        if (RenderScene)
        {
            RenderScene->RenderView_RenderThread(CmdList, FrameIndex);
        }

        // Composite this world's UI onto its render target, right after its scene.
        RmlUi::RenderWorldUI(this, CmdList);
    }

    void CWorld::Extract()
    {
        LUMINA_PROFILE_SCOPE();

        RmlUi::TickWorldUI(this);
        RmlUi::TickWorldWidgets(this);

        // SCameraSystem resolves the active view + post-process volumes into this
        // singleton at the tail of the update; we just forward it to the renderer.
        const FResolvedSceneView& View = EntityRegistry.ctx().get<FResolvedSceneView>();

        if (View.bHasView)
        {
            RenderScene->SetActivePostProcessMaterials(View.PostProcessMaterials);
            RenderScene->Extract(View.ViewVolume, View.bHasPostProcess ? &View.PostProcess : nullptr);
            return;
        }

        RenderScene->SetActivePostProcessMaterials({});
        RenderScene->Extract(FViewVolume{}, nullptr);
    }

    void CWorld::OnScriptComponentPendingReady(const FScriptComponentPendingReady& Event)
    {
        entt::entity Entity = Event.Entity;
        
        if (!EntityRegistry.valid(Entity))
        {
            return;
        }
            
        SScriptComponent* ScriptComponent = EntityRegistry.try_get<SScriptComponent>(Entity);
        if (!ScriptComponent)
        {
            return;
        }
            
        if (ScriptComponent->ReadyFunc.IsValid())
        {
            ScriptComponent->Script->InvokeAsCoroutine(ScriptComponent->ReadyFunc, ScriptComponent->Script->Reference);
        }
    }

    void CWorld::InitializeScriptEntities()
    {
        auto ScriptView = EntityRegistry.view<SScriptComponent>(entt::exclude<SDisabledTag>);

        ScriptView.each([&](entt::entity Entity, SScriptComponent& Component)
        {
            SetupScriptComponent(Entity, Component);
        });

        auto Visit = [&](entt::entity Entity, const Lua::FRef SScriptComponent::*Hook)
        {
            if (EntityRegistry.any_of<SDisabledTag>(Entity))
            {
                return;
            }
            SScriptComponent* Component = EntityRegistry.try_get<SScriptComponent>(Entity);
            if (!Component || Component->Script == nullptr)
            {
                return;
            }
            if (!IsScriptActiveInWorld(Entity))
            {
                return;
            }
            const Lua::FRef& HookRef = Component->*Hook;
            if (HookRef.IsValid())
            {
                Component->Script->InvokeAsCoroutine(HookRef, Component->Script->Reference);
            }
        };

        auto InvokeAttach = [&](entt::entity Entity) { Visit(Entity, &SScriptComponent::AttachFunc); };
        auto InvokeReady  = [&](entt::entity Entity) { Visit(Entity, &SScriptComponent::ReadyFunc);  };

        auto RelationshipRoots = EntityRegistry.view<FRelationshipComponent>(entt::exclude<SDisabledTag>);

        RelationshipRoots.each([&](entt::entity Entity, const FRelationshipComponent& Relationship)
        {
            if (Relationship.Parent != entt::null)
            {
                return;
            }
            InvokeAttach(Entity);
            ECS::Utils::ForEachDescendant(EntityRegistry, Entity, InvokeAttach);
        });
        
        // Scriptless-root entities fall outside the relationship view; handle here.
        ScriptView.each([&](entt::entity Entity, SScriptComponent&)
        {
            if (!EntityRegistry.all_of<FRelationshipComponent>(Entity))
            {
                InvokeAttach(Entity);
            }
        });

        RelationshipRoots.each([&](entt::entity Entity, const FRelationshipComponent& Relationship)
        {
            if (Relationship.Parent != entt::null)
            {
                return;
            }
            ECS::Utils::ForEachDescendantReverse(EntityRegistry, Entity, InvokeReady);
            InvokeReady(Entity);
        });

        ScriptView.each([&](entt::entity Entity, SScriptComponent&)
        {
            if (!EntityRegistry.all_of<FRelationshipComponent>(Entity))
            {
                InvokeReady(Entity);
            }
        });
    }

    bool CWorld::RegisterSystem(const FSystemVariant& NewSystem)
    {
        const FUpdatePriorityList& PriorityList = eastl::visit([&](const auto& System) { return System.GetUpdatePriorityList(); }, NewSystem);
        
        bool StagesModified[(uint8)EUpdateStage::Max] = {};

        for (uint8 i = 0; i < (uint8)EUpdateStage::Max; ++i)
        {
            if (PriorityList.IsStageEnabled((EUpdateStage)i))
            {
                SystemUpdateList[i].emplace_back(NewSystem);
                StagesModified[i] = true;
            }
        }

        for (uint8 i = 0; i < (uint8)EUpdateStage::Max; ++i)
        {
            if (!StagesModified[i])
            {
                continue;
            }

            auto Predicate = [i](const FSystemVariant& A, const FSystemVariant& B)
            {
                const FUpdatePriorityList& PrioListA = eastl::visit([&](const auto& System) { return System.GetUpdatePriorityList(); }, A);
                const FUpdatePriorityList& PrioListB = eastl::visit([&](const auto& System) { return System.GetUpdatePriorityList(); }, B);
                const uint8 PriorityA = PrioListA.GetPriorityForStage((EUpdateStage)i);
                const uint8 PriorityB = PrioListB.GetPriorityForStage((EUpdateStage)i);
                // Lower enum value = higher priority (Highest=0 .. Low=192), so ascending
                // order runs Highest first and Low last within the stage.
                return PriorityA < PriorityB;
            };

            eastl::sort(SystemUpdateList[i].begin(), SystemUpdateList[i].end(), Predicate);
        }

        return true;
    }

    entt::entity CWorld::ConstructEntity(const FName& Name, const FTransform& Transform)
    {
        entt::entity NewEntity = GetEntityRegistry().create();
        
        FName ActualName = Name;
        if (ActualName == NAME_None)
        {
            FFixedString StringName;
            StringName.append_convert(Name + eastl::to_string(entt::to_integral(NewEntity)));
            ActualName = StringName;
        }
        
        EntityRegistry.emplace<SNameComponent>(NewEntity).Name = Name;
        EntityRegistry.emplace<STransformComponent>(NewEntity, Transform);
        
        return NewEntity;
    }
    
    bool CWorld::FractureEntity(entt::entity Entity, const glm::vec3& Origin, float Strength)
    {
        LUMINA_PROFILE_SCOPE();

        if (!EntityRegistry.valid(Entity))
        {
            return false;
        }

        SDestructibleComponent* Destructible = EntityRegistry.try_get<SDestructibleComponent>(Entity);
        if (Destructible == nullptr || Destructible->bFractured)
        {
            return false;
        }

        // Resolve the mesh to shatter: explicit fragment override, else the entity's own static mesh.
        SStaticMeshComponent* MeshComp = EntityRegistry.try_get<SStaticMeshComponent>(Entity);
        CStaticMesh* SourceMesh = Destructible->FragmentMesh.Get();
        if (SourceMesh == nullptr && MeshComp != nullptr)
        {
            SourceMesh = MeshComp->StaticMesh.Get();
        }

        if (SourceMesh == nullptr)
        {
            LOG_WARN("FractureEntity: entity {} has no mesh to fracture", entt::to_integral(Entity));
            return false;
        }

        const FTransform OwnerTransform = EntityRegistry.get<STransformComponent>(Entity).GetWorldTransform();
        const glm::vec3 OwnerScale = OwnerTransform.Scale;

        // Inherit the intact body's linear velocity so fragments keep the object's momentum.
        glm::vec3 InheritedVelocity(0.0f);
        if (PhysicsScene)
        {
            if (const SRigidBodyComponent* RB = EntityRegistry.try_get<SRigidBodyComponent>(Entity))
            {
                if (RB->BodyID != 0xFFFFFFFFu)
                {
                    InheritedVelocity = PhysicsScene->GetLinearVelocity(RB->BodyID);
                }
            }
        }

        const float LaunchSpeed = Strength > 0.0f ? Strength : Destructible->ExplosionStrength;
        const float SpinSpeed   = Destructible->SpinStrength;

        // Deterministic per-fragment jitter (good for replays / lockstep): hash the index.
        auto Hash01 = [](uint32 V) -> float
        {
            V ^= V >> 16; V *= 0x7feb352dU; V ^= V >> 15; V *= 0x846ca68bU; V ^= V >> 16;
            return static_cast<float>(V) / static_cast<float>(0xFFFFFFFFU);
        };

        // Inherited momentum + an outward blast (radial from Origin) + random spin on a fresh body.
        auto LaunchBody = [&](uint32 BodyID, const glm::vec3& WorldCenter, uint32 Seed)
        {
            if (!PhysicsScene || BodyID == 0xFFFFFFFFu)
            {
                return;
            }
            glm::vec3 Direction = WorldCenter - Origin;
            const float Distance = glm::length(Direction);
            Direction = Distance > 1e-4f
                ? Direction / Distance
                : glm::normalize(glm::vec3(Hash01(Seed) - 0.5f, Hash01(Seed + 1) + 0.25f, Hash01(Seed + 2) - 0.5f));

            const float SpeedJitter = 0.7f + 0.6f * Hash01(Seed + 3);
            const glm::vec3 LaunchVelocity = InheritedVelocity
                + Direction * (LaunchSpeed * SpeedJitter)
                + glm::vec3(0.0f, LaunchSpeed * 0.2f, 0.0f);
            PhysicsScene->OnSetVelocityEvent(SSetVelocityEvent{ BodyID, LaunchVelocity });

            if (SpinSpeed > 0.0f)
            {
                const glm::vec3 Spin(Hash01(Seed + 4) - 0.5f, Hash01(Seed + 5) - 0.5f, Hash01(Seed + 6) - 0.5f);
                PhysicsScene->OnSetAngularVelocityEvent(SSetAngularVelocityEvent{ BodyID, Spin * (2.0f * SpinSpeed) });
            }
        };

        int32 Spawned = 0;

        // Source of pieces: an assigned collection if present, else a convex Voronoi fracture
        // generated on the fly from the mesh bounds (real chunks with zero authoring).
        const FFractureData* CollectionData = nullptr;
        if (CGeometryCollection* Collection = Destructible->Collection.Get())
        {
            if (Collection->GetNumPieces() > 0)
            {
                CollectionData = &Collection->GetFractureData();
            }
        }

        TVector<FFracturePiece> GeneratedPieces;
        if (CollectionData == nullptr)
        {
            FFractureSettings Settings;
            Settings.NumPieces = Destructible->FragmentCount;
            Settings.Seed      = entt::to_integral(Entity) * 2654435761U + 1u;
            Fracture::GenerateConvexFracture(SourceMesh, Settings, GeneratedPieces);
        }

        const TVector<FFracturePiece>& Pieces = CollectionData ? CollectionData->Pieces : GeneratedPieces;

        if (!Pieces.empty())
        {
            const TVector<TObjectPtr<CMaterialInterface>>& PieceMaterials =
                (CollectionData && !Destructible->Collection->Materials.empty())
                    ? Destructible->Collection->Materials
                    : SourceMesh->Materials;

            for (const FFracturePiece& Piece : Pieces)
            {
                CStaticMesh* PieceMesh = Fracture::BuildPieceMesh(Piece, PieceMaterials, "GCPiece");
                if (PieceMesh == nullptr)
                {
                    continue;
                }

                // Pieces live in the source mesh's local space, so each fragment shares the owner's
                // world transform and reconstructs the object exactly until physics pulls it apart.
                const entt::entity Fragment = ConstructEntity("Fragment", OwnerTransform);
                EntityRegistry.emplace_or_replace<FNeedsTransformUpdate>(Fragment);

                EntityRegistry.emplace<SStaticMeshComponent>(Fragment).StaticMesh = PieceMesh;

                // Mesh colliders don't auto-add a body: emplace the collider first, then the rigid
                // body, so its on_construct builds the convex Jolt body synchronously with a valid id.
                SMeshColliderComponent& Collider = EntityRegistry.emplace<SMeshColliderComponent>(Fragment);
                Collider.Mesh    = PieceMesh;
                Collider.bConvex = true;
                EntityRegistry.emplace<SRigidBodyComponent>(Fragment);

                EntityRegistry.emplace<SLifetimeComponent>(Fragment).Lifetime = Destructible->FragmentLifetime;
                EntityRegistry.emplace<SFragmentComponent>(Fragment).Source   = entt::to_integral(Entity);

                const glm::vec3 WorldCenter = OwnerTransform.Location + OwnerTransform.Rotation * (OwnerTransform.Scale * Piece.Center);
                LaunchBody(EntityRegistry.get<SRigidBodyComponent>(Fragment).BodyID, WorldCenter, entt::to_integral(Fragment) + static_cast<uint32>(Spawned));

                ++Spawned;
            }
        }
        else
        {
            // Fallback (degenerate fracture): subdivide the bounds into a grid of textured box chunks.
            const FAABB& LocalBounds = SourceMesh->GetAABB();
            const glm::vec3 LocalExtent = glm::max(LocalBounds.GetSize(), glm::vec3(0.01f));
            const glm::vec3 LocalCenter = LocalBounds.GetCenter();
            const int32 Target = glm::clamp(Destructible->FragmentCount, 2, 512);
            const int32 Dims   = glm::max(1, static_cast<int32>(std::ceil(std::cbrt(static_cast<float>(Target)))));
            const glm::vec3 LocalCell = LocalExtent / static_cast<float>(Dims);
            const glm::vec3 FragScale = OwnerScale / static_cast<float>(Dims);
            const glm::vec3 ColliderHalf = LocalExtent * 0.5f;
            CStaticMesh* GridMesh = Destructible->FragmentMesh.Get() ? Destructible->FragmentMesh.Get() : SourceMesh;

            for (int32 zi = 0; zi < Dims && Spawned < Target; ++zi)
            for (int32 yi = 0; yi < Dims && Spawned < Target; ++yi)
            for (int32 xi = 0; xi < Dims && Spawned < Target; ++xi)
            {
                const glm::vec3 CellLocalCenter = LocalBounds.Min + (glm::vec3(xi, yi, zi) + 0.5f) * LocalCell;
                const glm::vec3 CellWorldCenter = OwnerTransform.Location + OwnerTransform.Rotation * (OwnerScale * CellLocalCenter);

                FTransform FragmentTransform;
                FragmentTransform.Location = CellWorldCenter - OwnerTransform.Rotation * (FragScale * LocalCenter);
                FragmentTransform.Rotation = OwnerTransform.Rotation;
                FragmentTransform.Scale    = FragScale;

                const entt::entity Fragment = ConstructEntity("Fragment", FragmentTransform);
                EntityRegistry.emplace_or_replace<FNeedsTransformUpdate>(Fragment);

                SStaticMeshComponent& FragmentMeshComp = EntityRegistry.emplace<SStaticMeshComponent>(Fragment);
                FragmentMeshComp.StaticMesh = GridMesh;
                if (MeshComp != nullptr)
                {
                    FragmentMeshComp.MaterialOverrides = MeshComp->MaterialOverrides;
                }

                // Box collider auto-emplaces a Dynamic rigid body, built synchronously off the physics thread.
                EntityRegistry.emplace<SBoxColliderComponent>(Fragment).HalfExtent = ColliderHalf;

                EntityRegistry.emplace<SLifetimeComponent>(Fragment).Lifetime = Destructible->FragmentLifetime;
                EntityRegistry.emplace<SFragmentComponent>(Fragment).Source   = entt::to_integral(Entity);

                LaunchBody(EntityRegistry.get<SRigidBodyComponent>(Fragment).BodyID, CellWorldCenter, entt::to_integral(Fragment) + static_cast<uint32>(Spawned));

                ++Spawned;
            }
        }

        Destructible->bFractured = true;

        // Retire the original. Strip render + physics now so it vanishes this frame, and
        // let the lifetime system reap the entity at FrameEnd -- safe even when this was
        // called from the entity's own script callback.
        if (Destructible->bDestroyOriginal)
        {
            EntityRegistry.remove<SStaticMeshComponent>(Entity);
            EntityRegistry.remove<SRigidBodyComponent>(Entity);
            EntityRegistry.remove<SBoxColliderComponent>(Entity);
            EntityRegistry.remove<SSphereColliderComponent>(Entity);
            EntityRegistry.remove<SMeshColliderComponent>(Entity);
            EntityRegistry.emplace_or_replace<SLifetimeComponent>(Entity).Lifetime = 0.01f;
        }

        return Spawned > 0;
    }

    entt::entity CWorld::SpawnPrefab(const FName& Path)
    {
        FAssetData* AssetData = FAssetRegistry::Get().GetAssetByPath(FStringView(Path.c_str()));
        if (AssetData == nullptr)
        {
            LOG_WARN("SpawnPrefab: no asset found at path '{}'", Path.c_str());
            return entt::null;
        }

        CPrefab* Prefab = Cast<CPrefab>(LoadObject<CObject>(AssetData->AssetGUID));
        if (Prefab == nullptr)
        {
            LOG_WARN("SpawnPrefab: asset '{}' is not a CPrefab", Path.c_str());
            return entt::null;
        }

        return Prefab->Instantiate(this, FTransform(), entt::null);
    }

    void CWorld::SpawnPrefabAsync(const FName& Path, const TFunction<void(entt::entity)>& Callback)
    {
        AsyncLoadObject(Path, [this, Callback, Path](CObject* Object)
        {
            CPrefab* Prefab = Cast<CPrefab>(Object);
            if (Prefab == nullptr)
            {
                LOG_WARN("SpawnPrefab: asset '{}' is not a CPrefab", Path.c_str());
                Callback(entt::null);
            }
            
            Callback(Prefab->Instantiate(this, FTransform(), entt::null));
        });
    }

    void CWorld::DuplicateEntity(entt::entity& To, entt::entity From, const TFunctionRef<bool(entt::type_info)>& Callback)
    {
        ASSERT(To != From);

        THashMap<entt::entity, entt::entity> SourceToDuplicate;

        auto DuplicateRecursive = [&](auto& Self, entt::entity Source, entt::entity NewParent) -> entt::entity
        {
            entt::entity NewEntity = EntityRegistry.create();
            SourceToDuplicate[Source] = NewEntity;

            for (auto&& [ID, Storage] : EntityRegistry.storage())
            {
                if (!Callback(Storage.info()))
                {
                    continue;
                }

                // Scripts/rigid bodies can't be bit-copied; re-emplaced below so on_construct fires fresh.
                if (ID == entt::type_hash<FRelationshipComponent>::value()
                    || ID == entt::type_hash<SScriptComponent>::value()
                    || ID == entt::type_hash<SRigidBodyComponent>::value())
                {
                    continue;
                }

                if (Storage.contains(Source) && !Storage.contains(NewEntity))
                {
                    Storage.push(NewEntity, Storage.value(Source));
                }
            }

            // Rebind: bit-copy carries source's self-references (Entity/Registry ptr).
            if (STransformComponent* NewTransform = EntityRegistry.try_get<STransformComponent>(NewEntity))
            {
                NewTransform->Bind(EntityRegistry, NewEntity);
                EntityRegistry.emplace_or_replace<FNeedsTransformUpdate>(NewEntity);
            }

            // Remove auto-emplaced default first; emplace_or_replace would fire on_update (no-op), not on_construct.
            if (const SRigidBodyComponent* SourceBody = EntityRegistry.try_get<SRigidBodyComponent>(Source))
            {
                SRigidBodyComponent NewBody = *SourceBody;
                NewBody.BodyID = 0xFFFFFFFF;

                EntityRegistry.remove<SRigidBodyComponent>(NewEntity);
                EntityRegistry.emplace<SRigidBodyComponent>(NewEntity, eastl::move(NewBody));
            }

            // Emplace editable fields only; on_construct loads a unique FScript for the duplicate.
            if (const SScriptComponent* SourceScript = EntityRegistry.try_get<SScriptComponent>(Source))
            {
                SScriptComponent NewScript;
                NewScript.ScriptPath        = SourceScript->ScriptPath;
                NewScript.PropertyOverrides = SourceScript->PropertyOverrides;
                NewScript.UpdateStage       = SourceScript->UpdateStage;
                NewScript.TickRate          = SourceScript->TickRate;
                EntityRegistry.emplace<SScriptComponent>(NewEntity, eastl::move(NewScript));
            }

            if (NewParent != entt::null)
            {
                ECS::Utils::ReparentEntity(EntityRegistry, NewEntity, NewParent);
            }
            else if (FRelationshipComponent* Rel = EntityRegistry.try_get<FRelationshipComponent>(Source))
            {
                if (Rel->Parent != entt::null)
                {
                    ECS::Utils::ReparentEntity(EntityRegistry, NewEntity, Rel->Parent);
                }
            }

            ECS::Utils::ForEachChild(EntityRegistry, Source, [&](entt::entity Child)
            {
                Self(Self, Child, NewEntity);
            });

            return NewEntity;
        };

        To = DuplicateRecursive(DuplicateRecursive, From, entt::null);

        // Fix up entity-handle properties so references between duplicated entities point at the
        // new copies; references to entities outside the duplicated set are left untouched.
        for (auto& [Source, Dup] : SourceToDuplicate)
        {
            ECS::Utils::RemapEntityReferences(EntityRegistry, Dup, SourceToDuplicate, /*bClearUnmapped*/ false);
        }
    }

    void CWorld::DestroyEntity(entt::entity Entity)
    {
        EntityRegistry.destroy(Entity);
    }

    STransformComponent& CWorld::GetEntityTransform(entt::entity Entity)
    {
        return EntityRegistry.get<STransformComponent>(Entity);
    }

    glm::vec3 CWorld::GetEntityLocation(entt::entity Entity)
    {
        return GetEntityTransform(Entity).GetWorldLocation();
    }

    void CWorld::SetEntityLocation(entt::entity Entity, glm::vec3 Location)
    {
        GetEntityTransform(Entity).SetLocation(Location);
    }

    void CWorld::SetEntityRotation(entt::entity Entity, glm::quat Rotation)
    {
        GetEntityTransform(Entity).SetRotation(Rotation);
    }

    glm::vec3 CWorld::TranslateEntity(entt::entity Entity, glm::vec3 Translation)
    {
        return GetEntityTransform(Entity).Translate(Translation);
    }

    uint32 CWorld::GetNumEntities() const
    {
        return (uint32)EntityRegistry.view<entt::entity>().size();
    }

    void CWorld::SetActiveCamera(entt::entity InEntity) const
    {
        if (!EntityRegistry.valid(InEntity))
        {
            return;
        }

        if (EntityRegistry.all_of<SCameraComponent>(InEntity))
        {
            CameraManager->SetActiveCamera(InEntity);
        }
    }

    SCameraComponent* CWorld::GetActiveCamera() const
    {
        return CameraManager->GetCameraComponent();
    }

    entt::entity CWorld::GetActiveCameraEntity() const
    {
        return CameraManager->GetActiveCameraEntity();
    }

    void CWorld::OnChangeCameraEvent(const FSwitchActiveCameraEvent& Event)
    {
        SetActiveCamera(Event.NewActiveEntity);
    }

    SDefaultWorldSettings& CWorld::GetDefaultWorldSettings()
    {
        if (!EntityRegistry.valid(SingletonEntity))
        {
            static SDefaultWorldSettings Defaults{};
            return Defaults;
        }
        
        return EntityRegistry.get_or_emplace<SDefaultWorldSettings>(SingletonEntity);
    }

    bool CWorld::EntityHasTag(entt::entity Entity, const FName& Tag)
    {
        if (auto Storage = EntityRegistry.storage(entt::hashed_string(Tag.c_str())))
        {
            return Storage->contains(Entity);
        }
        
        return false;
    }

    void CWorld::CreateRenderer()
    {
        if (!RenderScene)
        {
            RenderScene = MakeUnique<FForwardRenderScene>(this);
            RenderScene->Init();
            EntityRegistry.ctx().emplace<IRenderScene*>(RenderScene.get());
        }
    }

    void CWorld::DestroyRenderer()
    {
        if (RenderScene)
        {
            FlushRenderingCommands();

            RenderScene->Shutdown();
            RenderScene.reset();
        }
    }

    void CWorld::SetActive(bool bNewActive)
    {
        if (bActive != bNewActive)
        {
            bActive = bNewActive;

            if (bActive)
            {
                SuspendedTime = -1.0;
                CreateRenderer();
                RmlUi::SetActiveWorld(this);
            }
            else
            {
                DestroyRenderer();
            }
        }
    }

    bool CWorld::ReclaimIdleRenderer(double NowSeconds, double GraceSeconds)
    {
        if (bActive || RenderScene == nullptr)
        {
            return false;
        }

        if (SuspendedTime < 0.0)
        {
            // First frame observed idle: start the clock.
            SuspendedTime = NowSeconds;
            return false;
        }

        if (NowSeconds - SuspendedTime < GraceSeconds)
        {
            return false;
        }

        DestroyRenderer();
        return true;
    }

    ENetMode CWorld::GetNetMode() const
    {
        return OwningContext ? OwningContext->NetMode : ENetMode::Standalone;
    }

    CWorld* CWorld::DuplicateWorld(CWorld* OwningWorld)
    {
        CPackage* OuterPackage = OwningWorld->GetPackage();
        if (OuterPackage == nullptr)
        {
            return nullptr;
        }

        TVector<uint8> Data;
        FMemoryWriter Writer(Data);
        FObjectProxyArchiver WriterProxy(Writer, true);
        OwningWorld->Serialize(WriterProxy);
        
        FMemoryReader Reader(Data);
        FObjectProxyArchiver ReaderProxy(Reader, true);
        
        CWorld* PIEWorld = NewObject<CWorld>(OF_Transient);
        
        PIEWorld->PreLoad();
        PIEWorld->Serialize(ReaderProxy);
        PIEWorld->PostLoad();
        
        return PIEWorld;
    }

    const TVector<CWorld::FSystemVariant>& CWorld::GetSystemsForUpdateStage(EUpdateStage Stage)
    {
        return SystemUpdateList[static_cast<uint32>(Stage)];
    }

    void CWorld::OnRelationshipComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        Registry.on_destroy<FRelationshipComponent>().disconnect<&CWorld::OnRelationshipComponentDestroyed>(this);
        ECS::Utils::RemoveFromParent(Registry, Entity);

        TVector<entt::entity> SubTree;
    
        auto CollectRecursive = [&](auto& Self, entt::entity Current) -> void
        {
            ECS::Utils::ForEachChild(Registry, Current, [&](entt::entity Child)
            {
                Self(Self, Child);
                SubTree.push_back(Child);
            });
        };
    
        CollectRecursive(CollectRecursive, Entity);

        for (int32 i = (int32)SubTree.size() - 1; i >= 0; i--)
        {
            if (Registry.valid(SubTree[i]))
            {
                Registry.destroy(SubTree[i]);
            }
        }
        
        Registry.on_destroy<FRelationshipComponent>().connect<&CWorld::OnRelationshipComponentDestroyed>(this);
    }

    void CWorld::OnTransformComponentConstruct(entt::registry& Registry, entt::entity Entity)
    {
        STransformComponent& TransformComponent = Registry.get<STransformComponent>(Entity);
        TransformComponent.Registry = &EntityRegistry;
        TransformComponent.Entity = Entity;

        Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
    }

    void CWorld::OnWidgetComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        RmlUi::ReleaseWidget(this, Registry.get<SWidgetComponent>(Entity));
    }

    void CWorld::OnScriptComponentConstruct(entt::registry& Registry, entt::entity Entity)
    {
        SScriptComponent& ScriptComponent = Registry.get<SScriptComponent>(Entity);
        OnScriptComponentCreated(Entity, ScriptComponent, true);
    }

    void CWorld::SetupScriptComponent(entt::entity Entity, SScriptComponent& ScriptComponent)
    {
        ScriptComponent.Entity = Entity;
        ScriptComponent.World  = this;
        
        if (ScriptComponent.ScriptPath.Path.empty())
        {
            return;
        }
        
        ScriptComponent.Script = Lua::FScriptingContext::Get().LoadUniqueScriptPath(ScriptComponent.ScriptPath.Path);
        if (ScriptComponent.Script == nullptr || !ScriptComponent.Script->Reference.IsValid())
        {
            return;
        }

        // Stable ptr: FScript is held by shared_ptr for the script's lifetime.
        ScriptComponent.Script->ThreadData.Entity = Entity;
        ScriptComponent.Script->ThreadData.World  = this;
        if (lua_State* ScriptThread = ScriptComponent.Script->Reference.GetState())
        {
            lua_setthreaddata(ScriptThread, &ScriptComponent.Script->ThreadData);
        }
        
        ScriptComponent.Script->Reference.RawSet("_Registry", &EntityRegistry);
        ScriptComponent.Script->Reference.RawSet("_Physics",  PhysicsScene.get());
        ScriptComponent.Script->Reference.RawSet("_Events",   &LuaEventBus);
        ScriptComponent.Script->Reference.RawSet("_Timers",   &TimerManager);
        ScriptComponent.Script->Reference.RawSet("_Draw",     DrawInterfaceRef);

        // Per-entity fields visible to the user as `self.*`.
        ScriptComponent.Script->Reference.RawSet("World",     this);
        ScriptComponent.Script->Reference.RawSet("Entity",    Entity);
        ScriptComponent.Script->Reference.RawSet("Transform", &EntityRegistry.get<STransformComponent>(Entity));
        ScriptComponent.Script->Reference.RawSet("Name",      EntityRegistry.get<SNameComponent>(Entity).Name);
        
        ScriptComponent.ScriptMetaTable     = ScriptComponent.Script->Reference["Meta"];
        ScriptComponent.AttachFunc          = ScriptComponent.Script->Reference["OnAttach"];
        ScriptComponent.ReadyFunc           = ScriptComponent.Script->Reference["OnReady"];
        ScriptComponent.UpdateFunc          = ScriptComponent.Script->Reference["OnUpdate"];
        ScriptComponent.DetachFunc          = ScriptComponent.Script->Reference["OnDetach"];

        // No base no-op fallback (see ScriptComponent.h): invalid FRef means "not defined".
        ScriptComponent.FixedUpdateFunc     = ScriptComponent.Script->Reference["OnFixedUpdate"];
        ScriptComponent.EditorUpdateFunc    = ScriptComponent.Script->Reference["OnEditorUpdate"];

        ScriptComponent.ContactBeginFunc    = ScriptComponent.Script->Reference["OnContactBegin"];
        ScriptComponent.ContactEndFunc      = ScriptComponent.Script->Reference["OnContactEnd"];
        ScriptComponent.OverlapBeginFunc    = ScriptComponent.Script->Reference["OnOverlapBegin"];
        ScriptComponent.OverlapEndFunc      = ScriptComponent.Script->Reference["OnOverlapEnd"];
        
        if (ScriptComponent.Script->ExportsSchema.IsValid())
        {
            Lua::ReconcileOverrides(
                ScriptComponent.Script->ExportsSchema,
                ScriptComponent.Script->ExportDefaults,
                ScriptComponent.PropertyOverrides.Items);

            if (lua_State* ScriptState = ScriptComponent.Script->Reference.GetState())
            {
                ScriptComponent.Script->Reference.Push();
                lua_getfield(ScriptState, -1, "Exports");
                if (lua_istable(ScriptState, -1))
                {
                    Lua::ApplyOverridesToExportsTable(
                        ScriptState, -1,
                        ScriptComponent.Script->ExportsSchema,
                        ScriptComponent.PropertyOverrides.Items);
                }
                lua_pop(ScriptState, 2);
            }
        }
        
        if (ScriptComponent.ScriptMetaTable.IsValid())
        {
            auto MaybeTickRate = ScriptComponent.ScriptMetaTable.Get<float>("TickRate");
            ScriptComponent.TickRate = MaybeTickRate ? MaybeTickRate.value() : 0.0f;
            
            auto MaybeUpdateStage = ScriptComponent.ScriptMetaTable.Get<EUpdateStage>("UpdateStage");
            ScriptComponent.UpdateStage = MaybeUpdateStage ? MaybeUpdateStage.value() : EUpdateStage::PrePhysics;
        }
        
        if (EntityRegistry.any_of<
                FUpdateStage_FrameStart,
                FUpdateStage_PrePhysics,
                FUpdateStage_DuringPhysics,
                FUpdateStage_PostPhysics,
                FUpdateStage_FrameEnd,
                FUpdateStage_Paused>(Entity))
        {
            EntityRegistry.remove<FUpdateStage_FrameStart>(Entity);
            EntityRegistry.remove<FUpdateStage_PrePhysics>(Entity);
            EntityRegistry.remove<FUpdateStage_DuringPhysics>(Entity);
            EntityRegistry.remove<FUpdateStage_PostPhysics>(Entity);
            EntityRegistry.remove<FUpdateStage_FrameEnd>(Entity);
            EntityRegistry.remove<FUpdateStage_Paused>(Entity);
        }

        switch (ScriptComponent.UpdateStage)
        {
        case EUpdateStage::FrameStart:
            EntityRegistry.emplace_or_replace<FUpdateStage_FrameStart>(Entity);
            break;
        case EUpdateStage::PrePhysics:
            EntityRegistry.emplace_or_replace<FUpdateStage_PrePhysics>(Entity);
            break;
        case EUpdateStage::DuringPhysics:
            EntityRegistry.emplace_or_replace<FUpdateStage_DuringPhysics>(Entity);
            break;
        case EUpdateStage::PostPhysics:
            EntityRegistry.emplace_or_replace<FUpdateStage_PostPhysics>(Entity);
            break;
        case EUpdateStage::FrameEnd:
            EntityRegistry.emplace_or_replace<FUpdateStage_FrameEnd>(Entity);
            break;
        case EUpdateStage::Paused:
            EntityRegistry.emplace_or_replace<FUpdateStage_Paused>(Entity);
            break;
        case EUpdateStage::Max:
            break;
        }

        if (ScriptComponent.UpdateFunc.IsValid())
            EntityRegistry.emplace_or_replace<FScriptHasUpdateFn>(Entity);
        else
            EntityRegistry.remove<FScriptHasUpdateFn>(Entity);

        if (ScriptComponent.FixedUpdateFunc.IsValid())
            EntityRegistry.emplace_or_replace<FScriptHasFixedUpdateFn>(Entity);
        else
            EntityRegistry.remove<FScriptHasFixedUpdateFn>(Entity);

        if (ScriptComponent.EditorUpdateFunc.IsValid())
            EntityRegistry.emplace_or_replace<FScriptHasEditorUpdateFn>(Entity);
        else
            EntityRegistry.remove<FScriptHasEditorUpdateFn>(Entity);
    }

    void CWorld::OnScriptComponentCreated(entt::entity Entity, SScriptComponent& ScriptComponent, bool bRunReady)
    {
        SetupScriptComponent(Entity, ScriptComponent);

        if (ScriptComponent.Script == nullptr)
        {
            return;
        }

        if (!IsScriptActiveInWorld(Entity))
        {
            return;
        }

        if (ScriptComponent.AttachFunc.IsValid())
        {
            ScriptComponent.Script->InvokeAsCoroutine(ScriptComponent.AttachFunc, ScriptComponent.Script->Reference);
        }

        if (bRunReady)
        {
            SingletonDispatcher.enqueue<FScriptComponentPendingReady>(Entity);
        }
    }

    void CWorld::OnScriptComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        // Auto-remove all event bus subscriptions owned by this entity.
        LuaEventBus.UnsubscribeEntity(Entity);
        TimerManager.ClearTimersForEntity(Entity);

        SScriptComponent& ScriptComponent = Registry.get<SScriptComponent>(Entity);
        if (ScriptComponent.Script == nullptr || !ScriptComponent.DetachFunc.IsValid())
        {
            return;
        }

        if (IsScriptActiveInWorld(Entity))
        {
            ScriptComponent.Script->InvokeAsCoroutine(ScriptComponent.DetachFunc, ScriptComponent.Script->Reference);
        }
    }

    void CWorld::ReloadScriptForComponent(entt::entity Entity, SScriptComponent& ScriptComponent)
    {
        if (ScriptComponent.Script != nullptr && ScriptComponent.DetachFunc.IsValid())
        {
            if (IsScriptActiveInWorld(Entity))
            {
                ScriptComponent.Script->InvokeAsCoroutine(ScriptComponent.DetachFunc, ScriptComponent.Script->Reference);
            }
        }

        LuaEventBus.UnsubscribeEntity(Entity);
        TimerManager.ClearTimersForEntity(Entity);

        ScriptComponent.Script.reset();
        ScriptComponent.AttachFunc          = {};
        ScriptComponent.ReadyFunc           = {};
        ScriptComponent.UpdateFunc          = {};
        ScriptComponent.DetachFunc          = {};
        ScriptComponent.FixedUpdateFunc     = {};
        ScriptComponent.EditorUpdateFunc    = {};
        ScriptComponent.ContactBeginFunc    = {};
        ScriptComponent.ContactEndFunc      = {};
        ScriptComponent.OverlapBeginFunc    = {};
        ScriptComponent.OverlapEndFunc      = {};
        ScriptComponent.ScriptMetaTable     = {};
        ScriptComponent.MessageHandlers.clear();

        OnScriptComponentCreated(Entity, ScriptComponent, /*bRunReady*/ true);
    }

    void CWorld::OnScriptSourceReloaded(FStringView Path)
    {
        auto View = EntityRegistry.view<SScriptComponent>();
        for (entt::entity Entity : View)
        {
            SScriptComponent& Component = View.get<SScriptComponent>(Entity);
            if (FStringView(Component.ScriptPath.Path.c_str(), Component.ScriptPath.Path.size()) == Path)
            {
                ReloadScriptForComponent(Entity, Component);
            }
        }
    }

    void CWorld::RegisterSystems()
    {
        using namespace entt::literals;

        for (int i = 0; i < (int)EUpdateStage::Max; ++i)
        {
            SystemUpdateList[i].clear();
        }
        
        for (auto&& [_, Meta] : entt::resolve())
        {
            ECS::ETraits Traits = Meta.traits<ECS::ETraits>();
            if (EnumHasAnyFlags(Traits, ECS::ETraits::System))
            {
                FEntitySystemWrapper Wrapper;
                Wrapper.Underlying = Meta;
                Wrapper.Instance = Meta.construct();
                
                FSystemVariant Variant = Wrapper;
                RegisterSystem(Variant);
            }
        }
    }

    void CWorld::DrawBillboard(FRHIImage* Image, const glm::vec3& Location, float Scale)
    {
        RenderScene->DrawBillboard(Image, Location, Scale);
    }

    void CWorld::DrawLine(const glm::vec3& Start, const glm::vec3& End, const glm::vec4& Color, float Thickness, bool bDepthTest, float Duration)
    {
        if (IsSuspended())
        {
            return;
        }
        
        LineBatcherComponent->EnqueueLine(Start, End, Color, Thickness, bDepthTest, Duration);
    }

    TOptional<SRayResult> CWorld::CastRay(const SRayCastSettings& Settings)
    {
        LUMINA_PROFILE_SCOPE();
        
        if (PhysicsScene == nullptr)
        {
            return eastl::nullopt;
        }
        
        TOptional<SRayResult> Result = PhysicsScene->CastRay(Settings);
        
        if (Settings.bDrawDebug)
        {
            if (Result.has_value())
            {
                SRayResult RayResult = Result.value();
                DrawLine(Settings.Start, RayResult.Location, FColor(Settings.DebugMissColor), 3.0f, true, Settings.DebugDuration);
                
                glm::vec3 NormalEnd = RayResult.Location + RayResult.Normal * 0.5f;
                DrawLine(RayResult.Location, NormalEnd, FColor::Blue, 3.0f,true, Settings.DebugDuration);
                
                DrawBox(RayResult.Location, glm::vec3(0.05f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), FColor::Yellow, 3.0, true, Settings.DebugDuration);
                
                DrawLine(RayResult.Location, Settings.End, FColor(Settings.DebugHitColor), 3.0f, true, Settings.DebugDuration);
            }
            else
            {
                DrawLine(Settings.Start, Settings.End, FColor(Settings.DebugMissColor), 3.0f, true, Settings.DebugDuration);
            }
        }
        
        return Move(Result);
    }
    
    TVector<SRayResult> CWorld::CastSphere(const SSphereCastSettings& Settings) const
    {
        LUMINA_PROFILE_SCOPE();

        if (PhysicsScene == nullptr)
        {
            return {};
        }
        
        return PhysicsScene->CastSphere(Settings);
        
        
    }

    EUpdateStage CWorld::GetUpdateStage() const
    {
        return SystemContext.GetUpdateStage();
    }

    entt::entity CWorld::GetEntityByTag(const FName& Tag)
    {
        auto& Storage = EntityRegistry.storage<STagComponent>(entt::hashed_string(Tag.c_str()));
        if (Storage.empty())
        {
            return entt::null;
        }
        
        return *Storage.data();
    }

    entt::entity CWorld::GetEntityByName(const FName& Name)
    {
        auto View = EntityRegistry.view<SNameComponent>();
        for (entt::entity Entity : View)
        {
            SNameComponent& NameComponent = View.get<SNameComponent>(Entity);
            if (NameComponent.Name == Name)
            {
                return Entity;
            }
        }
        
        return entt::null;
    }

    entt::entity CWorld::GetFirstEntityWith(entt::id_type Type)
    {
        if (!EntityRegistry.storage(Type))
        {
            return entt::null;
        }

        auto storage = EntityRegistry.storage(Type);

        if (storage->empty())
        {
            return entt::null;
        }
        return *storage->data();
    }

    void CWorld::SetEntityTransform(entt::entity Entity, const FTransform& NewTransform)
    {
        EntityRegistry.emplace_or_replace<STransformComponent>(Entity, NewTransform);
        EntityRegistry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
    }

    TVector<entt::entity> CWorld::GetSelectedEntities() const
    {
        auto View = EntityRegistry.view<FSelectedInEditorComponent>();
        TVector<entt::entity> Selections(View.size());
        View.each([&](entt::entity Entity)
        {
           Selections.push_back(Entity); 
        });
        return Selections;
    }

    bool CWorld::IsSelected(entt::entity Entity) const
    {
        return EntityRegistry.any_of<FSelectedInEditorComponent>(Entity);
    }

    void CWorld::TickSystems(FSystemContext& Context)
    {
        auto& SystemVector = SystemUpdateList[(uint32)Context.GetUpdateStage()];
        for(FSystemVariant& SystemVariant : SystemVector)
        {
            eastl::visit([&](auto& System) { System.Update(Context); }, SystemVariant);
        }
    }
}
