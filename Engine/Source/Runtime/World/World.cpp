#include "pch.h"
#include "World.h"
#include <utility>
#include "lua.h"
#include "WorldManager.h"
#include "Audio/AudioGlobals.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Core/Engine/Engine.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectIterator.h"
#include "Core/Serialization/MemoryArchiver.h"
#include "Core/Serialization/ObjectArchiver.h"
#include "EASTL/sort.h"
#include "Entity/EntityUtils.h"
#include "Entity/Components/DirtyComponent.h"
#include "Entity/Components/EditorComponent.h"
#include "entity/components/entitytags.h"
#include "Entity/Components/LineBatcherComponent.h"
#include "Entity/Components/NameComponent.h"
#include "Entity/Components/PhysicsComponent.h"
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
    }
    
    
    CWorld::CWorld()
        : SingletonEntity(entt::null)
        , SystemContext(this)
        , LineBatcherComponent(nullptr)
    {
    }

    void CWorld::RegisterLuaModule(Lua::FRef& GlobalRef)
    {
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
            .Register();
        
        GlobalRef.NewClass<entt::runtime_view>("RuntimeView")
            .AddFunction<&entt::runtime_view::contains>("Contains")
            .AddFunction<&LuaBinds::ForEachInRuntimeView_Lua>("Each")
            .AddFunction<&LuaBinds::RuntimeViewSizeHint_Lua>("SizeHint")
            .AddFunction<&LuaBinds::RuntimeViewGetEntities_Lua>("GetEntities")
            .Register();
        
        GlobalRef.NewClass<FEntityRegistry>("FEntityRegistry")
            .AddFunction<&FEntityRegistry::valid>("Valid")
            .AddFunction<&FEntityRegistry::orphan>("Orphan")
            .AddFunction<&FEntityRegistry::compact<>>("Compact")
            .AddFunction<&LuaBinds::EntityCount_Lua>("EntityCount")
            .AddFunction<&LuaBinds::RemoveComponent_Lua>("Remove")
            .AddFunction<&LuaBinds::CreateEntity_Lua>("Create")
            .AddFunction<&LuaBinds::DestroyEntity_Lua>("Destroy")
            .AddFunction<&LuaBinds::HasComponent_Lua>("Has")
            .AddFunction<&LuaBinds::EmplaceComponent_Lua>("Emplace")
            .AddFunction<&LuaBinds::ForEachEntity_Lua>("ForEachEntity")
            .AddFunction<&LuaBinds::GetComponent_Lua>("Get")
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
        //...
    }
    
    void CWorld::InitializeWorld(EWorldType InWorldType)
    {
        using namespace entt::literals;
        
        WorldType = InWorldType;
        GWorldManager->AddWorld(this);
        
        EntityRegistry.swap(RegistryPending);
        RegistryPending = {};

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
        GAudioContext->StopSounds();
        
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

        RegistryPending.clear<>();
        EntityRegistry.clear<>();
        PhysicsScene.reset();
        DestroyRenderer();
        
        FCoreDelegates::PostWorldUnload.Broadcast();
        
        GWorldManager->RemoveWorld(this);
        
        Lua::FScriptingContext::Get().RunGC();
    }
    
    void CWorld::Update(const FUpdateContext& Context)
    {
        LUMINA_PROFILE_SCOPE();

        const EUpdateStage Stage = Context.GetUpdateStage();
        
        if (Stage == EUpdateStage::FrameStart)
        {
            DeltaTime = Context.GetDeltaTime();
            TimeSinceCreation += DeltaTime;
        }
        
        if (bPaused && Stage != EUpdateStage::Paused || (!bPaused && Stage == EUpdateStage::Paused))
        {
            return;
        }
        
        if (Stage == EUpdateStage::DuringPhysics)
        {
            PhysicsScene->Update(Context.GetDeltaTime());
        }

        SystemContext.DeltaTime     = DeltaTime;
        SystemContext.Time          = TimeSinceCreation;
        SystemContext.UpdateStage   = Stage;
        
        SingletonDispatcher.update<FScriptComponentPendingReady>();

        if (Stage == EUpdateStage::FrameStart)
        {
            LuaEventBus.ProcessDeferred();
        }

        TickSystems(SystemContext);
    }

    void CWorld::Render(FRenderGraph& RenderGraph) const
    {
        LUMINA_PROFILE_SCOPE();

        SCameraComponent* CameraComponent = GetActiveCamera();
        const FViewVolume& ViewVolume = CameraComponent ? CameraComponent->GetViewVolume() : FViewVolume();
        
        RenderScene->RenderView(RenderGraph, ViewVolume);
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
            ScriptComponent->ReadyFunc(ScriptComponent->Script->Reference);
        }
    }

    void CWorld::InitializeScriptEntities()
    {
        auto ScriptView = EntityRegistry.view<SScriptComponent>(entt::exclude<SDisabledTag>);
        ScriptView.each([&](entt::entity Entity, SScriptComponent& Component)
        {
            OnScriptComponentCreated(Entity, Component, false); 
        });
        
        auto RootView = EntityRegistry.view<SScriptComponent>(entt::exclude<FRelationshipComponent, SDisabledTag>);
        RootView.each([&](entt::entity Entity, SScriptComponent& ScriptComponent)
        {
            if (ScriptComponent.Script == nullptr)
            {
                return;
            }
            
            if ((WorldType == EWorldType::Editor) == ScriptComponent.bRunInEditor)
            {
                if (ScriptComponent.ReadyFunc.IsValid())
                {
                    ScriptComponent.ReadyFunc(ScriptComponent.Script->Reference);
                }
            }
        });
        
        auto RelationshipView = EntityRegistry.view<SScriptComponent, FRelationshipComponent>(entt::exclude<SDisabledTag>);
        RelationshipView.each([&](entt::entity Entity, SScriptComponent& Script, const FRelationshipComponent& Relationship)
        {
            if (Relationship.Parent == entt::null)
            {
                ECS::Utils::ForEachDescendantReverse(EntityRegistry, Entity, [&](entt::entity Descendant)
                {
                    if (SScriptComponent* ScriptComp = EntityRegistry.try_get<SScriptComponent>(Descendant))
                    {
                        if (ScriptComp->Script == nullptr)
                        {
                            return;
                        }
                        
                        if ((WorldType == EWorldType::Editor) == ScriptComp->bRunInEditor)
                        {
                            if (ScriptComp->ReadyFunc.IsValid())
                            {
                                ScriptComp->ReadyFunc(ScriptComp->Script->Reference);
                            }
                        }
                    }
                });
                
                if (Script.Script == nullptr)
                {
                    return;
                }
                
                if ((WorldType == EWorldType::Editor) == Script.bRunInEditor)
                {
                    if (Script.ReadyFunc.IsValid())
                    {
                        Script.ReadyFunc(Script.Script->Reference);
                    }
                }
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
    
    void CWorld::DuplicateEntity(entt::entity& To, entt::entity From, TFunctionRef<bool(entt::type_info)> Callback)
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

                if (ID == entt::type_hash<FRelationshipComponent>::value())
                {
                    continue;
                }

                if (Storage.contains(Source) && !Storage.contains(NewEntity))
                {
                    Storage.push(NewEntity, Storage.value(Source));
                }
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
        return EntityRegistry.get<SDefaultWorldSettings>(SingletonEntity);
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

    void CWorld::OnScriptComponentCreated(entt::entity Entity, SScriptComponent& ScriptComponent, bool bRunReady)
    {
        ScriptComponent.Entity = Entity;
        ScriptComponent.World  = this;
        
        if (ScriptComponent.ScriptPath.Path.empty())
        {
            return;
        }
        
        ScriptComponent.Script = Lua::FScriptingContext::Get().LoadUniqueScriptPath(ScriptComponent.ScriptPath.Path);
        if (ScriptComponent.Script == nullptr)
        {
            return;
        }
        
        ScriptComponent.Script->Environment.Set("World", this);
        ScriptComponent.Script->Environment.Set("Registry", &EntityRegistry);
        ScriptComponent.Script->Environment.Set("Physics", PhysicsScene.get());
        ScriptComponent.Script->Environment.Set("Events", &LuaEventBus);
        
        ScriptComponent.Script->Reference.Set("Entity", Entity);
        ScriptComponent.Script->Reference.Set("Transform", &EntityRegistry.get<STransformComponent>(Entity));
        ScriptComponent.Script->Reference.Set("Name", EntityRegistry.get<SNameComponent>(Entity).Name);
        
        ScriptComponent.ScriptMetaTable = ScriptComponent.Script->Reference["__meta"];
        ScriptComponent.AttachFunc      = ScriptComponent.Script->Reference["OnAttach"];
        ScriptComponent.ReadyFunc       = ScriptComponent.Script->Reference["OnReady"];
        ScriptComponent.UpdateFunc      = ScriptComponent.Script->Reference["Update"];
        ScriptComponent.DetachFunc      = ScriptComponent.Script->Reference["OnDetach"];
        
        if (ScriptComponent.ScriptMetaTable.IsValid())
        {
            Lua::FRef RunInEditor = ScriptComponent.ScriptMetaTable.GetField("bRunInEditor");
            if (RunInEditor.IsValid())
            {
                ScriptComponent.bRunInEditor = RunInEditor.GetOr(false);
            }
            
            Lua::FRef TickRate = ScriptComponent.ScriptMetaTable.GetField("TickRate");
            if (TickRate.IsValid())
            {
                ScriptComponent.TickRate = TickRate.GetOr(0.0f);
            }
        }
        
        if ((WorldType == EWorldType::Editor) == ScriptComponent.bRunInEditor)
        {
            if (ScriptComponent.AttachFunc.IsValid())
            {
                ScriptComponent.AttachFunc(ScriptComponent.Script->Reference);
            }
            
            if (bRunReady)
            {
                SingletonDispatcher.enqueue<FScriptComponentPendingReady>(Entity);
            }
        }
    }

    void CWorld::OnScriptComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        // Auto-remove all event bus subscriptions owned by this entity.
        LuaEventBus.UnsubscribeEntity(Entity);

        SScriptComponent& ScriptComponent = Registry.get<SScriptComponent>(Entity);
        if (ScriptComponent.Script == nullptr || !ScriptComponent.DetachFunc.IsValid())
        {
            return;
        }

        if ((WorldType == EWorldType::Editor) == ScriptComponent.bRunInEditor)
        {
            ScriptComponent.DetachFunc(ScriptComponent.Script->Reference);
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
        LineBatcherComponent->DrawLine(Start, End, Color, Thickness, bDepthTest, Duration);
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
