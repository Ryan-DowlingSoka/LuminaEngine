#include "pch.h"
#include "World.h"
#include <utility>
#include "lua.h"
#include "WorldManager.h"
#include "WorldContext.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
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
#include "Entity/Components/DirtyComponent.h"
#include "Entity/Components/EditorComponent.h"
#include "entity/components/entitytags.h"
#include "Entity/Components/LineBatcherComponent.h"
#include "Entity/Components/NameComponent.h"
#include "Entity/Components/PhysicsComponent.h"
#include "Entity/Components/PostProcessComponent.h"
#include "Entity/Components/TransformComponent.h"
#include "Entity/Components/ScriptComponent.h"
#include "Entity/Components/SingletonEntityComponent.h"
#include "entity/components/tagcomponent.h"
#include "Entity/Events/WorldEvents.h"
#include "Entity/Events/LuaEventBus.h"
#include "Physics/Physics.h"
#include "Scene/RenderScene/Forward/ForwardRenderScene.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/VariadicArgs.h"
#include "Subsystems/FCameraManager.h"
#include "Subsystems/WorldSettings.h"
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

        // Per-thread world resolver. Every entity script thread publishes its
        // owning CWorld via lua_setthreaddata in SetupScriptComponent, so any
        // C function bound into the global table can reach back into the
        // correct world without taking it as an explicit argument.
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
        
        // Camera/World tables: thin wrappers that resolve their target world
        // from the per-thread script context. Raw cfunctions because the
        // typed binding wrappers don't expose lua_State* to the body.
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
        
        PhysicsScene    = Physics::GetPhysicsContext()->CreatePhysicsScene(this);
        CameraManager   = MakeUnique<FCameraManager>(this);

        EntityRegistry.ctx().emplace<Physics::IPhysicsScene*>(PhysicsScene.get());
        EntityRegistry.ctx().emplace<FCameraManager*>(CameraManager.get());
        EntityRegistry.ctx().emplace<FSystemContext&>(SystemContext);
        EntityRegistry.ctx().emplace<CWorld*>(this);
        EntityRegistry.ctx().emplace<FLuaEventBus*>(&LuaEventBus);

        CreateRenderer();
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
        SystemContext.EventSink     <FSwitchActiveCameraEvent>()    .connect<&ThisClass::OnChangeCameraEvent>(this);
        SystemContext.EventSink     <FScriptComponentPendingReady>().connect<&ThisClass::OnScriptComponentPendingReady>(this);

        ScriptReloadedHandle = Lua::FScriptingContext::Get().OnScriptLoaded.AddMember(this, &ThisClass::OnScriptSourceReloaded);

        // Build the world-scoped DrawInterface table once. Per-entity setup just
        // assigns this ref into the script's environment instead of allocating
        // a new table + C closure for every SScriptComponent.
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

        if (Stage == EUpdateStage::DuringPhysics)
        {
            CPU_PROFILE_SCOPE_COLOR("Physics", FColor(0.20f, 0.75f, 0.90f));
            PhysicsScene->Update(DeltaTime);
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

    void CWorld::Render(ICommandList& CmdList) const
    {
        LUMINA_PROFILE_SCOPE();

        entt::entity CameraEntity = GetActiveCameraEntity();
        if (EntityRegistry.valid(CameraEntity))
        {
            const STransformComponent& CameraTransform = EntityRegistry.get<STransformComponent>(CameraEntity);
            (void)CameraTransform.GetWorldMatrix();

            // Bake view matrix from the freshest camera transform. Doing it here
            // instead of in SCameraSystem::Update means follow-cameras written in
            // PostPhysics scripts land in the same frame's render — otherwise the
            // view matrix lags by one frame and anything tracked stutters.
            const glm::quat CameraRotation = CameraTransform.GetWorldRotation();
            const SCameraComponent& Camera = EntityRegistry.get<SCameraComponent>(CameraEntity);
            const_cast<SCameraComponent&>(Camera).SetView(
                CameraTransform.GetWorldLocation(),
                CameraRotation * glm::vec3(0.0f, 0.0f, 1.0f),
                CameraRotation * glm::vec3(0.0f, 1.0f, 0.0f));

            // Resolve any SPostProcessComponent volumes the camera is inside
            // (or that have bInfiniteExtent) into a final blended settings
            // block. The camera's own PostProcess is the base; volumes blend
            // on top in priority order.
            const glm::vec3 CameraWorldPos = EntityRegistry.get<STransformComponent>(CameraEntity).GetWorldLocation();
            SPostProcessSettings ResolvedPostProcess = Camera.PostProcess;

            struct FVolumeContribution { float Weight; const SPostProcessSettings* Settings; int32 Priority; };
            TVector<FVolumeContribution> Contributions;

            auto VolumeView = EntityRegistry.view<const SPostProcessComponent, const STransformComponent>(entt::exclude<SDisabledTag>);
            for (entt::entity VolEntity : VolumeView)
            {
                const SPostProcessComponent& Volume = VolumeView.get<const SPostProcessComponent>(VolEntity);
                if (!Volume.bEnabled || Volume.BlendWeight <= 0.0f)
                {
                    continue;
                }

                float Weight = Volume.BlendWeight;

                if (!Volume.bInfiniteExtent)
                {
                    // Transform the camera into the volume's local space and
                    // measure signed distance to the box. Negative == inside,
                    // positive == outside; values inside [0, BlendDistance]
                    // fall off linearly so designers get a smooth on-ramp.
                    const STransformComponent& VolXform = VolumeView.get<const STransformComponent>(VolEntity);
                    const glm::mat4 InvWorld = glm::inverse(VolXform.GetWorldMatrix());
                    const glm::vec3 LocalCam = glm::vec3(InvWorld * glm::vec4(CameraWorldPos, 1.0f));
                    const glm::vec3 D = glm::abs(LocalCam) - Volume.BoxExtent;
                    const float Outside = glm::max(D.x, glm::max(D.y, D.z));

                    if (Outside > Volume.BlendDistance)
                    {
                        continue;
                    }
                    if (Outside > 0.0f && Volume.BlendDistance > 0.0001f)
                    {
                        Weight *= 1.0f - (Outside / Volume.BlendDistance);
                    }
                }

                if (Weight > 0.0f)
                {
                    Contributions.push_back({Weight, &Volume.Settings, Volume.Priority});
                }
            }

            // Lower priority first so higher priority volumes blend last
            // (their weight wins ties at full strength).
            eastl::sort(Contributions.begin(), Contributions.end(),
                [](const FVolumeContribution& A, const FVolumeContribution& B)
                {
                    return A.Priority < B.Priority;
                });

            for (const FVolumeContribution& Contribution : Contributions)
            {
                BlendPostProcessSettings(ResolvedPostProcess, *Contribution.Settings, Contribution.Weight);
            }

            // Gather the active post-process material chain. The camera's
            // own list runs first; each contributing volume appends its
            // materials in priority order so the visible chain is
            // camera-then-overrides, matching the grading blend order.
            TVector<CMaterialInterface*> PostProcessMaterials;
            for (const TObjectPtr<CMaterialInterface>& M : Camera.PostProcessMaterials)
            {
                if (M.IsValid())
                {
                    PostProcessMaterials.push_back(M.Get());
                }
            }
            // Mirror the contributions sort -- volumes whose box test passed
            // above are listed there in the priority order their materials
            // should run in. We track the entity instead of a settings
            // pointer so the lookup back to the SPostProcessComponent is
            // structural rather than relying on offsetof on a REFLECT struct.
            struct FMaterialVolumeRef { entt::entity Entity; int32 Priority; };
            TVector<FMaterialVolumeRef> MaterialVolumes;
            for (entt::entity VolEntity : VolumeView)
            {
                const SPostProcessComponent& Volume = VolumeView.get<const SPostProcessComponent>(VolEntity);
                if (!Volume.bEnabled || Volume.PostProcessMaterials.empty())
                {
                    continue;
                }
                if (!Volume.bInfiniteExtent)
                {
                    const STransformComponent& VolXform = VolumeView.get<const STransformComponent>(VolEntity);
                    const glm::mat4 InvWorld = glm::inverse(VolXform.GetWorldMatrix());
                    const glm::vec3 LocalCam = glm::vec3(InvWorld * glm::vec4(CameraWorldPos, 1.0f));
                    const glm::vec3 D = glm::abs(LocalCam) - Volume.BoxExtent;
                    const float Outside = glm::max(D.x, glm::max(D.y, D.z));
                    if (Outside > Volume.BlendDistance)
                    {
                        continue;
                    }
                }
                MaterialVolumes.push_back({VolEntity, Volume.Priority});
            }
            eastl::sort(MaterialVolumes.begin(), MaterialVolumes.end(),
                [](const FMaterialVolumeRef& A, const FMaterialVolumeRef& B)
                {
                    return A.Priority < B.Priority;
                });
            for (const FMaterialVolumeRef& Ref : MaterialVolumes)
            {
                const SPostProcessComponent& Volume = VolumeView.get<const SPostProcessComponent>(Ref.Entity);
                for (const TObjectPtr<CMaterialInterface>& M : Volume.PostProcessMaterials)
                {
                    if (M.IsValid())
                    {
                        PostProcessMaterials.push_back(M.Get());
                    }
                }
            }
            RenderScene->SetActivePostProcessMaterials(PostProcessMaterials);

            RenderScene->RenderView(CmdList, Camera.GetViewVolume(), &ResolvedPostProcess);

            return;
        }

        RenderScene->SetActivePostProcessMaterials({});
        RenderScene->RenderView(CmdList, FViewVolume{}, nullptr);
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
            ScriptComponent->ReadyFunc.InvokeAsCoroutine(ScriptComponent->Script->Reference);
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
            if ((WorldType == EWorldType::Editor) != Component->bRunInEditor)
            {
                return;
            }
            const Lua::FRef& HookRef = Component->*Hook;
            if (HookRef.IsValid())
            {
                HookRef.InvokeAsCoroutine(Component->Script->Reference);
            }
        };

        auto InvokeAttach = [&](entt::entity Entity) { Visit(Entity, &SScriptComponent::AttachFunc); };
        auto InvokeReady  = [&](entt::entity Entity) { Visit(Entity, &SScriptComponent::ReadyFunc);  };

        // Walk every relationship root once. The traversal walks all descendants
        // regardless of whether they carry a script. Visit is a no-op for the
        // scriptless ones, but a non-script root may still have script descendants.
        auto RelationshipRoots = EntityRegistry.view<FRelationshipComponent>(entt::exclude<SDisabledTag>);

        // Phase 2: top-down OnAttach.
        RelationshipRoots.each([&](entt::entity Entity, const FRelationshipComponent& Relationship)
        {
            if (Relationship.Parent != entt::null)
            {
                return;
            }
            InvokeAttach(Entity);
            ECS::Utils::ForEachDescendant(EntityRegistry, Entity, InvokeAttach);
        });
        
        // Script entities with no relationship component at all are roots with no
        // children, they fall outside the relationship view, so handle them here.
        ScriptView.each([&](entt::entity Entity, SScriptComponent&)
        {
            if (!EntityRegistry.all_of<FRelationshipComponent>(Entity))
            {
                InvokeAttach(Entity);
            }
        });

        // Phase 3: bottom-up OnReady.
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
                return PriorityA > PriorityB;
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

        auto DuplicateRecursive = [&](auto& Self, entt::entity Source, entt::entity NewParent) -> entt::entity
        {
            entt::entity NewEntity = EntityRegistry.create();

            for (auto&& [ID, Storage] : EntityRegistry.storage())
            {
                if (!Callback(Storage.info()))
                {
                    continue;
                }

                // Scripts can't be bit-copied: their Lua refs point at the source's thread, the shared
                // FScript would be shared, and self.Entity/self.Transform would resolve to the source.
                // Re-emplace below with just the editable fields so on_construct re-runs SetupScriptComponent.
                if (ID == entt::type_hash<FRelationshipComponent>::value()
                    || ID == entt::type_hash<SScriptComponent>::value())
                {
                    continue;
                }

                if (Storage.contains(Source) && !Storage.contains(NewEntity))
                {
                    Storage.push(NewEntity, Storage.value(Source));
                }
            }

            // Bit-copying STransformComponent carries the source's self-references; rebind so MarkDirty
            // and ResolveIfDirty operate on the duplicate, not the original.
            if (STransformComponent* NewTransform = EntityRegistry.try_get<STransformComponent>(NewEntity))
            {
                NewTransform->Bind(EntityRegistry, NewEntity);
                EntityRegistry.emplace_or_replace<FNeedsTransformUpdate>(NewEntity);
            }

            // Re-copy SRigidBodyComponent explicitly. The storage iteration above skips it
            // when a default rigid body has already been emplaced by a collider on_construct
            // hook (entt fires emplace_or_replace<SRigidBodyComponent> when SBoxCollider/etc.
            // are pushed onto the duplicate), which clobbers BodyType/Mass/CollisionProfile
            // back to defaults. Force the source's data through, then invalidate the body id
            // so the physics scene allocates a fresh Jolt body.
            if (const SRigidBodyComponent* SourceBody = EntityRegistry.try_get<SRigidBodyComponent>(Source))
            {
                SRigidBodyComponent& NewBody = EntityRegistry.emplace_or_replace<SRigidBodyComponent>(NewEntity, *SourceBody);
                NewBody.BodyID = 0xFFFFFFFF;
                EntityRegistry.emplace_or_replace<FNeedsPhysicsBodyUpdate>(NewEntity);
            }

            // Emplace SScriptComponent with the editable fields only; on_construct fires the canonical
            // attach flow which loads a unique FScript and binds fresh Lua refs to the new entity.
            if (const SScriptComponent* SourceScript = EntityRegistry.try_get<SScriptComponent>(Source))
            {
                SScriptComponent NewScript;
                NewScript.ScriptPath        = SourceScript->ScriptPath;
                NewScript.PropertyOverrides = SourceScript->PropertyOverrides;
                NewScript.UpdateStage       = SourceScript->UpdateStage;
                NewScript.TickRate          = SourceScript->TickRate;
                NewScript.bRunInEditor      = SourceScript->bRunInEditor;
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
                CreateRenderer();       
            }
            else
            {
                DestroyRenderer();
            }
        }
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

        // Publish per-thread context so yield-aware Lua APIs (e.g. TimerManager:Wait)
        // can scope themselves to this entity. The address is stable for the script's
        // lifetime since FScript is held by shared_ptr.
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

        // Physics hooks: cache only if the script defined them. The base EntityScript
        // table doesn't ship no-op fallbacks for these (unlike OnUpdate etc.), so the
        // FRef stays invalid for scripts that don't care, and the physics dispatch
        // skips them via IsInvokable().
        ScriptComponent.ContactBeginFunc    = ScriptComponent.Script->Reference["OnContactBegin"];
        ScriptComponent.ContactEndFunc      = ScriptComponent.Script->Reference["OnContactEnd"];
        ScriptComponent.OverlapBeginFunc    = ScriptComponent.Script->Reference["OnOverlapBegin"];
        ScriptComponent.OverlapEndFunc      = ScriptComponent.Script->Reference["OnOverlapEnd"];
        
        // Sync per-instance overrides with the current schema, then apply them
        // by mutating the Exports table the script references.
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
            auto MaybeRunInEditor = ScriptComponent.ScriptMetaTable.Get<bool>("bRunInEditor");
            ScriptComponent.bRunInEditor = MaybeRunInEditor ? MaybeRunInEditor.value() : false;
            
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
    }

    void CWorld::OnScriptComponentCreated(entt::entity Entity, SScriptComponent& ScriptComponent, bool bRunReady)
    {
        SetupScriptComponent(Entity, ScriptComponent);

        if (ScriptComponent.Script == nullptr)
        {
            return;
        }

        if ((WorldType == EWorldType::Editor) != ScriptComponent.bRunInEditor)
        {
            return;
        }

        if (ScriptComponent.AttachFunc.IsValid())
        {
            ScriptComponent.AttachFunc.InvokeAsCoroutine(ScriptComponent.Script->Reference);
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

        if ((WorldType == EWorldType::Editor) == ScriptComponent.bRunInEditor)
        {
            ScriptComponent.DetachFunc.InvokeAsCoroutine(ScriptComponent.Script->Reference);
        }
    }

    void CWorld::ReloadScriptForComponent(entt::entity Entity, SScriptComponent& ScriptComponent)
    {
        // Mirror the destroy path so any state owned by the old script instance
        // gets cleared: per-entity event bus subscriptions, pending timers, the
        // OnDetach lifecycle hook. Cached FRefs (AttachFunc/UpdateFunc/etc.)
        // and MessageHandlers all point into the soon-to-be-discarded FScript
        // table; reset them before LoadUniqueScriptPath returns a new instance.
        if (ScriptComponent.Script != nullptr && ScriptComponent.DetachFunc.IsValid())
        {
            if ((WorldType == EWorldType::Editor) == ScriptComponent.bRunInEditor)
            {
                ScriptComponent.DetachFunc.InvokeAsCoroutine(ScriptComponent.Script->Reference);
            }
        }

        LuaEventBus.UnsubscribeEntity(Entity);
        TimerManager.ClearTimersForEntity(Entity);

        ScriptComponent.Script.reset();
        ScriptComponent.AttachFunc          = {};
        ScriptComponent.ReadyFunc           = {};
        ScriptComponent.UpdateFunc          = {};
        ScriptComponent.DetachFunc          = {};
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
        // FScriptingContext fires this from ProcessDeferredActions on the main
        // thread, so we can mutate registry-resident components directly.
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
        // EnqueueLine is thread-safe; the queue is drained once per render
        // extraction tick. Routing every caller through it removes the
        // foot-gun of accidentally calling DrawLine from a worker.
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
