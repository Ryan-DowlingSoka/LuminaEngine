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
#include "Assets/AssetTypes/Textures/TextureRenderTarget.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Object/Cast.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Object/Package/Package.h"
#include "Audio/AudioGlobals.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Core/Engine/Engine.h"
#include "Core/Profiler/CPUProfiler.h"
#include "TaskSystem/TaskSystem.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectIterator.h"
#include "Core/Serialization/MemoryArchiver.h"
#include "Core/Serialization/ObjectArchiver.h"
#include "EASTL/sort.h"
#include "Assets/AssetTypes/EntityComponent/EntityComponentType.h"
#include "Entity/EntityUtils.h"
#include "Entity/RuntimeComponent.h"
#include "Entity/Components/CameraComponent.h"
#include "Entity/Components/DestructibleComponent.h"
#include "Entity/Components/DirtyComponent.h"
#include "Entity/Components/EditorComponent.h"
#include "Entity/Components/LifetimeComponent.h"
#include "Entity/Components/StaticMeshComponent.h"
#include "Entity/Events/ImpulseEvent.h"
#include "entity/components/entitytags.h"
#include "Entity/Components/LineBatcherComponent.h"
#include "Entity/Components/TriangleBatcherComponent.h"
#include "Entity/Components/NameComponent.h"
#include "Entity/Components/PhysicsComponent.h"
#include "Entity/Components/PostProcessComponent.h"
#include "Entity/Components/TransformComponent.h"
#include "Entity/Components/ScriptComponent.h"
#include "Entity/Components/WidgetComponent.h"
#include "Entity/Components/InputComponent.h"
#include "Input/InputContext.h"
#include "Input/InputViewport.h"
#include "Entity/Components/SingletonEntityComponent.h"
#include "Entity/Systems/SystemSingletons.h"
#include "Entity/Systems/CameraSystem.h"
#include "entity/components/tagcomponent.h"
#include "Entity/Events/WorldEvents.h"
#include "Entity/Events/LuaEventBus.h"
#include "Physics/Physics.h"
#include "Renderer/RenderThread.h"
#include "Scene/RenderScene/Forward/ForwardRenderScene.h"
#include "Scripting/Lua/Scripting.h"
#include "World/Net/NetRpc.h"
#include "World/Net/NetWorldState.h"
#include "World/Net/NetRole.h"
#include "World/Net/NetReplication.h"
#include "World/Net/ScriptRepState.h"
#include "World/Entity/Components/NetworkComponent.h"
#include "Scripting/Lua/ScriptTypeRegistry.h"
#include "Scripting/Lua/Stack.h"
#include "Scripting/Lua/VariadicArgs.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "Subsystems/WorldSettings.h"
#include "UI/RmlUiBridge.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/entity/systems/EntitySystem.h"
#include "WorldLuaSubsystem.h"

// Lets Lua bindings take a leading CWorld* the invoker fills from the calling script's thread data,
// so they bind with SetFunction and need no lua_State plumbing. Must precede RegisterLuaModule.
namespace Lumina::Lua
{
    template<>
    struct TLuaContext<Lumina::CWorld*> : eastl::true_type
    {
        static Lumina::CWorld* Get(lua_State* L)
        {
            const auto* TD = static_cast<FScriptThreadData*>(lua_getthreaddata(L));
            return TD ? TD->World : nullptr;
        }
    };
}

namespace Lumina
{
    namespace LuaBinds
    {
        using namespace entt::literals;

        static bool HasComponent_Lua(FEntityRegistry& Registry, entt::entity Entity, Lua::FRef Ref)
        {
            LUMINA_PROFILE_SECTION("Has Component [Lua]");
            entt::id_type Type = ECS::Utils::GetTypeID(Ref);
            if (entt::resolve(Type))
            {
                auto Meta = ECS::Utils::InvokeMetaFunc(Type, "has"_hs, entt::forward_as_meta(Registry), Entity);
                return Meta.cast<bool>();
            }
            FRuntimeComponentStorage* Storage = ECS::Utils::FindRuntimeStorageById(Registry, Type);
            return Storage != nullptr && Storage->contains(Entity);
        }

        static Lua::FRef GetComponent_Lua(FEntityRegistry& Registry, entt::entity Entity, Lua::FRef Ref)
        {
            LUMINA_PROFILE_SECTION("Get Component [Lua]");
            entt::id_type Type = ECS::Utils::GetTypeID(Ref);
            if (entt::resolve(Type))
            {
                auto Meta = ECS::Utils::InvokeMetaFunc(Type, "get_lua"_hs, entt::forward_as_meta(Registry), Entity, entt::forward_as_meta(Ref));
                return Meta.cast<Lua::FRef>();
            }

            // Runtime (data-authored) component: push the FProperty-backed proxy, or nil if absent.
            lua_State* L = Ref.GetState();
            if (ResolveRuntimeComponentType(Type) != nullptr)
            {
                FRuntimeComponentStorage* Storage = ECS::Utils::FindRuntimeStorageById(Registry, Type);
                if (Storage != nullptr && Storage->contains(Entity))
                {
                    PushRuntimeComponent(L, &Registry, Entity, Type);
                    return Lua::FRef(L, -1);
                }
            }
            lua_pushnil(L);
            return Lua::FRef(L, -1);
        }

        static size_t RemoveComponent_Lua(FEntityRegistry& Registry, entt::entity Entity, Lua::FRef Ref)
        {
            LUMINA_PROFILE_SECTION("Remove Component [Lua]");
            entt::id_type Type = ECS::Utils::GetTypeID(Ref);
            if (entt::resolve(Type))
            {
                auto Meta = ECS::Utils::InvokeMetaFunc(Type, "remove"_hs, entt::forward_as_meta(Registry), Entity);
                return Meta.cast<size_t>();
            }
            if (CEntityComponentType* RuntimeType = ResolveRuntimeComponentType(Type))
            {
                return ECS::Utils::RemoveRuntimeComponent(Registry, Entity, RuntimeType) ? 1u : 0u;
            }
            return 0;
        }

        static Lua::FRef EmplaceComponent_Lua(FEntityRegistry& Registry, entt::entity Entity, Lua::FRef Ref)
        {
            LUMINA_PROFILE_SECTION("Emplace Component [Lua]");
            entt::id_type Type = ECS::Utils::GetTypeID(Ref);
            if (entt::resolve(Type))
            {
                auto Meta = ECS::Utils::InvokeMetaFunc(Type, "emplace_lua"_hs, entt::forward_as_meta(Registry), Entity, entt::forward_as_meta(Ref));
                return Meta.cast<Lua::FRef>();
            }

            lua_State* L = Ref.GetState();
            if (CEntityComponentType* RuntimeType = ResolveRuntimeComponentType(Type))
            {
                ECS::Utils::AddRuntimeComponent(Registry, Entity, RuntimeType);
                PushRuntimeComponent(L, &Registry, Entity, Type);
                return Lua::FRef(L, -1);
            }
            lua_pushnil(L);
            return Lua::FRef(L, -1);
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

        //~ Lua system context (ctx) -- handed to script systems; mirrors the C++ FSystemContext over the live
        //  registry. Component/query helpers delegate to the shared bindings above.

        static double Ctx_DeltaTime(FLuaSystemContext& Ctx) { return Ctx.DeltaTime(); }
        static double Ctx_Time(FLuaSystemContext& Ctx)      { return Ctx.Time(); }
        static FEntityRegistry* Ctx_Registry(FLuaSystemContext& Ctx) { return &Ctx.GetRegistry(); }

        static Lua::FRef Ctx_Get(FLuaSystemContext& Ctx, entt::entity Entity, Lua::FRef Comp)     { return GetComponent_Lua(Ctx.GetRegistry(), Entity, Comp); }
        static bool      Ctx_Has(FLuaSystemContext& Ctx, entt::entity Entity, Lua::FRef Comp)     { return HasComponent_Lua(Ctx.GetRegistry(), Entity, Comp); }
        static Lua::FRef Ctx_Emplace(FLuaSystemContext& Ctx, entt::entity Entity, Lua::FRef Comp) { return EmplaceComponent_Lua(Ctx.GetRegistry(), Entity, Comp); }
        static size_t    Ctx_Remove(FLuaSystemContext& Ctx, entt::entity Entity, Lua::FRef Comp)  { return RemoveComponent_Lua(Ctx.GetRegistry(), Entity, Comp); }
        static entt::entity Ctx_Create(FLuaSystemContext& Ctx)                        { return Ctx.GetRegistry().create(); }
        static uint32       Ctx_Destroy(FLuaSystemContext& Ctx, entt::entity Entity)  { return Ctx.GetRegistry().destroy(Entity); }
        static entt::runtime_view Ctx_View(FLuaSystemContext& Ctx, Lua::FVariadicArgs Args) { return RuntimeView_Lua(Ctx.GetRegistry(), Args); }

        static void Ctx_Each(FLuaSystemContext& Ctx, Lua::FRef Comps, Lua::FRef Func)
        {
            LUMINA_PROFILE_SCOPE();

            FEntityRegistry& Registry = Ctx.GetRegistry();
            entt::runtime_view View;
            for (auto&& [Key, Value] : Comps)
            {
                entt::id_type Type = ECS::Utils::GetTypeID(Value);
                entt::meta_type Meta = entt::resolve(Type);
                if (Meta)
                {
                    if (entt::basic_sparse_set<>* Storage = Registry.storage(Meta.info().hash()))
                    {
                        View.iterate(*Storage);
                    }
                }
                else if (entt::basic_sparse_set<>* Storage = Registry.storage(Type))
                {
                    View.iterate(*Storage);
                }
            }

            View.each([&](entt::entity Entity)
            {
                Func(Entity);
            });
        }
        
        static void World_Each(CWorld* World, Lua::FRef Comps, Lua::FRef Func)
        {
            LUMINA_PROFILE_SCOPE();

            if (World == nullptr)
            {
                return;
            }
            
            FEntityRegistry& Registry = World->GetEntityRegistry();
            entt::runtime_view View;
            for (auto&& [Key, Value] : Comps)
            {
                entt::id_type Type = ECS::Utils::GetTypeID(Value);
                if (entt::meta_type Meta = entt::resolve(Type))
                {
                    if (entt::basic_sparse_set<>* Storage = Registry.storage(Meta.info().hash()))
                    {
                        View.iterate(*Storage);
                    }
                }
                else if (entt::basic_sparse_set<>* Storage = Registry.storage(Type))
                {
                    View.iterate(*Storage);
                }
            }

            View.each([&](entt::entity Entity)
            {
                Func(Entity);
            });
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

        // Bindings below take a leading CWorld* (injected from thread data) and trailing optional args;
        // they bind via FRef::SetFunction

        static void Camera_SetActive(CWorld* World, entt::entity Entity, TOptional<float> Blend, TOptional<int> Ease)
        {
            if (World == nullptr)
            {
                return;
            }
            const auto Fn = static_cast<ECameraBlendFunction>(static_cast<uint8>(Ease.value_or(static_cast<int>(ECameraBlendFunction::EaseInOut))));
            World->SetActiveCamera(Entity, Blend.value_or(0.0f), Fn);
        }

        static entt::entity Camera_GetActive(CWorld* World)
        {
            return World ? World->GetActiveCameraEntity() : entt::null;
        }

        static entt::entity World_FindByName(CWorld* World, FName Name)
        {
            return World ? World->GetEntityByName(Name) : entt::null;
        }
        
        static entt::entity World_FindByTag (CWorld* World, FName Tag)
        {
            return World ? World->GetEntityByTag(Tag)   : entt::null;
        }
        
        static int64 World_GetNumEntities(CWorld* World)
        {
            return World ? (int64)World->GetNumEntities() : 0;
        }
        
        static double World_GetDeltaTime(CWorld* World)
        {
            return World ? World->GetWorldDeltaTime() : 0.0;
        }
        
        static double  World_GetTimeSinceCreation(CWorld* World)
        {
            return World ? World->GetTimeSinceWorldCreation() : 0.0;
        }

        static entt::entity World_SpawnPrefab(CWorld* World, FName Path)
        {
            return World ? World->SpawnPrefab(Path) : entt::null;
        }
        
        static entt::entity World_SpawnPrefabAt(CWorld* World, FName Path, FVector3 Loc)
        {
            return World ? World->SpawnPrefabAt(Path, FTransform(Loc)) : entt::null;
        }
        
        static entt::entity World_Duplicate(CWorld* World, entt::entity Entity)
        {
            entt::entity Duplicate = entt::null;
            if (World)
            {
                World->DuplicateEntity(Duplicate, Entity, nullptr);
            }
            
            return Duplicate;
        }
        
        static void World_BeginPhysicsBatch(CWorld* World) 
        {
            if (World && World->GetPhysicsScene())
            {
                World->GetPhysicsScene()->BeginBodyBatch();
            }
        }
        
        static void World_EndPhysicsBatch(CWorld* World)
        {
            if (World && World->GetPhysicsScene())
            {
                World->GetPhysicsScene()->EndBodyBatch();
            }
        }
        
        static void World_Destroy(CWorld* World, entt::entity Entity)             
        { 
            if (World)
            {
                ECS::Utils::DestroyEntity(World->GetEntityRegistry(), Entity);
            }
        }

        // Entity authoring through World.* (mutate ANY entity id; self:* stays for this-entity). Components
        // added here replicate automatically if the entity has a Network component (the net layer observes the ECS).
        // Routes through ConstructEntity so every entity gets the required Name + Transform (like the editor).
        static entt::entity World_CreateEntity(CWorld* World, TOptional<FName> Name)
        {
            return World ? World->ConstructEntity(Name.value_or(NAME_None)) : entt::null;
        }
        static Lua::FRef World_AddComponent(CWorld* World, entt::entity Entity, Lua::FRef Type)
        {
            return World ? EmplaceComponent_Lua(World->GetEntityRegistry(), Entity, Type) : Lua::FRef{};
        }
        static void World_RemoveComponent(CWorld* World, entt::entity Entity, Lua::FRef Type)
        {
            if (World)
            {
                RemoveComponent_Lua(World->GetEntityRegistry(), Entity, Type);
            }
        }

        // Parent Child to Parent in the scene graph (preserves Child's world transform; set its local
        // transform afterward for an offset). Pass a null Parent to detach to the root.
        static void World_AttachEntity(CWorld* World, entt::entity Child, entt::entity Parent)
        {
            if (World)
            {
                ECS::Utils::ReparentEntity(World->GetEntityRegistry(), Child, Parent);
            }
        }

        // Local-space location setter (relative to the parent), unlike World_SetLocation which is world-space.
        static void World_SetLocalLocation(CWorld* World, entt::entity Entity, FVector3 Location)
        {
            if (World)
            {
                ECS::Utils::SetEntityLocation(World->GetEntityRegistry(), Entity, Location);
            }
        }


        static void World_AddScript(CWorld* World, entt::entity Entity, FStringView Path)
        {
            if (World == nullptr)
            {
                return;
            }
            World->SetEntityScript(Entity, Path);
        }
        
        static int World_SubsystemIndex(lua_State* L)
        {
            const char* Key = lua_tostring(L, 2);
            CWorld* World = Key != nullptr ? Lua::TLuaContext<CWorld*>::Get(L) : nullptr;
            if (World != nullptr)
            {
                if (FWorldLuaSubsystemPush Push = FWorldLuaSubsystemRegistry::Get().Find(Key))
                {
                    Push(L, World);
                    return 1;
                }
            }

            lua_pushnil(L);
            return 1;
        }
        static bool         World_IsValid(CWorld* World, entt::entity Entity)             { return World && ECS::Utils::IsEntityValid(World->GetEntityRegistry(), Entity); }

        // Removes tombstone holes left in component pools. Reorders elements, invalidating cached
        // component pointers -- call only at a quiet point (after a batch of Destroys), never mid-iteration.
        static void         World_Compact(CWorld* World)                                 { if (World) World->GetEntityRegistry().compact(); }

        static FVector3     World_GetLocation(CWorld* World, entt::entity Entity)         { return World ? ECS::Utils::GetEntityLocation(World->GetEntityRegistry(), Entity) : FVector3{}; }
        static FQuat        World_GetRotation(CWorld* World, entt::entity Entity)         { return World ? ECS::Utils::GetEntityRotation(World->GetEntityRegistry(), Entity) : FQuat{}; }
        static FVector3     World_GetScale(CWorld* World, entt::entity Entity)            { return World ? ECS::Utils::GetEntityScale(World->GetEntityRegistry(), Entity)    : FVector3{1.0f}; }

        static FVector3     World_Translate(CWorld* World, entt::entity Entity, FVector3 Delta) { return World ? ECS::Utils::TranslateEntity(World->GetEntityRegistry(), Entity, Delta) : FVector3{}; }

        // World-space setter: rebuilds local from the parent chain so it stays correct under
        // reparenting (unlike ECS.SetEntityLocation, which writes local space directly).
        static void World_SetLocation(CWorld* World, entt::entity Entity, FVector3 Location)
        {
            if (World == nullptr)
            {
                return;
            }
            FEntityRegistry& Registry = World->GetEntityRegistry();
            FTransform WorldTransform;
            WorldTransform.Location = Location;
            WorldTransform.Rotation = ECS::Utils::GetEntityRotation(Registry, Entity);
            WorldTransform.Scale    = ECS::Utils::GetEntityScale(Registry, Entity);
            ECS::Utils::SetEntityWorldTransform(Registry, Entity, WorldTransform);
        }

        static void World_SetRotationFromEuler(CWorld* World, entt::entity Entity, FVector3 Rotation)   
        { 
            if (World)
            {
                ECS::Utils::SetEntityRotation(World->GetEntityRegistry(), Entity, FQuat(Math::Radians(Rotation)));
            }
        }
        static void World_SetRotation(CWorld* World, entt::entity Entity, FQuat Rotation)   
        { 
            if (World)
            {
                ECS::Utils::SetEntityRotation(World->GetEntityRegistry(), Entity, Rotation);
            }
        }
        static void World_SetScale(CWorld* World, entt::entity Entity, FVector3 Scale)
        {
            if (World)
            {
                ECS::Utils::SetEntityScale(World->GetEntityRegistry(), Entity, Scale);
            }
        }

        // Batched homing toward Target by Step (clamped): crosses the Lua boundary and takes the transform
        // mutex ONCE for the whole list. Roots skip chain resolve (world == local), the 10k-loop hot path.
        static void World_MoveToward(CWorld* World, TVector<entt::entity> Entities, FVector3 Target, float Step)
        {
            if (World == nullptr || Step <= 0.0f)
            {
                return;
            }

            FEntityRegistry& Registry = World->GetEntityRegistry();
            auto& Storage = Registry.storage<STransformComponent>();

            FRecursiveScopeLock Lock(ECS::Utils::GetTransformResolveMutex());

            for (entt::entity Entity : Entities)
            {
                if (!Storage.contains(Entity))
                {
                    continue;
                }

                STransformComponent& Transform = Storage.get(Entity);

                const FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Entity);
                const bool bRooted = Rel == nullptr || Rel->Parent == entt::null || !Registry.valid(Rel->Parent);

                if (bRooted)
                {
                    // World == local: LocalTransform.Location is authoritative and never stale.
                    const FVector3 ToTarget = Target - Transform.LocalTransform.Location;
                    const float Distance = Math::Length(ToTarget);
                    if (Distance < 0.01f)
                    {
                        continue;
                    }

                    Transform.LocalTransform.Location += (ToTarget / Distance) * Math::Min(Step, Distance);
                    Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
                }
                else
                {
                    // Parented (rare for bulk spawns): resolve the chain for a correct world read,
                    // then route the world-space move back through the parent-relative converter.
                    ECS::Utils::ResolveTransformChain(Registry, Entity);
                    const FVector3 ToTarget = Target - Transform.WorldTransform.Location;
                    const float Distance = Math::Length(ToTarget);
                    if (Distance < 0.01f)
                    {
                        continue;
                    }

                    FTransform WorldTransform = Transform.WorldTransform;
                    WorldTransform.Location += (ToTarget / Distance) * Math::Min(Step, Distance);
                    ECS::Utils::SetEntityWorldTransform(Registry, Entity, WorldTransform);
                }
            }
        }

        // Cross-entity component access: reads components off ANY entity id (not just `self`).
        // Same get/has semantics as self:GetComponent.
        static Lua::FRef World_GetComponent(CWorld* World, entt::entity Entity, Lua::FRef Ref)
        {
            if (World == nullptr)
            {
                lua_pushnil(Ref.GetState());
                return Lua::FRef(Ref.GetState(), -1);
            }
            return GetComponent_Lua(World->GetEntityRegistry(), Entity, std::move(Ref));
        }

        static bool World_HasComponent(CWorld* World, entt::entity Entity, Lua::FRef Ref)
        {
            return World != nullptr && HasComponent_Lua(World->GetEntityRegistry(), Entity, std::move(Ref));
        }

        static Lua::FRef World_GetScript(CWorld* World, entt::entity Entity)
        {
            if (World == nullptr) return {};
            return GetScriptTable_Lua(World->GetEntityRegistry(), Entity);
        }

        static bool World_Fracture(CWorld* World, entt::entity Entity)
        {
            return World != nullptr && Entity != entt::null && World->FractureEntity(Entity, World->GetEntityLocation(Entity), 0.0f);
        }

        static bool World_FractureAt(CWorld* World, entt::entity Entity, float X, float Y, float Z, TOptional<float> Strength)
        {
            return World != nullptr && Entity != entt::null && World->FractureEntity(Entity, FVector3(X, Y, Z), Strength.value_or(0.0f));
        }

        static void RenderTarget_Paint(CWorld* World, CTextureRenderTarget* RT, float U, float V, float Radius,
            TOptional<float> R, TOptional<float> G, TOptional<float> B, TOptional<float> A,
            TOptional<float> Strength, TOptional<float> Hardness)
        {
            if (World == nullptr || RT == nullptr)
            {
                return;
            }
            World->PaintRenderTarget(RT, FVector2(U, V), Radius,
                                     FVector4(R.value_or(1.0f), G.value_or(0.0f), B.value_or(0.0f), A.value_or(1.0f)),
                                     Strength.value_or(1.0f), Hardness.value_or(1.0f), nullptr);
        }

        static void RenderTarget_Clear(CWorld* World, CTextureRenderTarget* RT, TOptional<float> R, TOptional<float> G, TOptional<float> B, TOptional<float> A)
        {
            if (World == nullptr || RT == nullptr)
            {
                return;
            }
            World->ClearRenderTarget(RT, FVector4(R.value_or(0.0f), G.value_or(0.0f), B.value_or(0.0f), A.value_or(0.0f)));
        }
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
        
        // Physics scene userdata. Methods are entity-based (they resolve the body internally), so
        // it reads identically whether reached as self.Physics, World.Physics, or for another
        // entity's id. Commands (AddForce/...) apply on the next physics step; getters return the
        // latched physics snapshot. Overloaded getters are static_cast to pick the entity version.
        using PScene = Physics::IPhysicsScene;
        GlobalRef.NewClass<PScene>("PhysicsScene")
            .AddFunction<&PScene::CastRay>("RayCast")
                .AddComment("Casts a ray into the physics scene")
            .AddFunction<&PScene::CastSphere>("SphereCast")
                .AddComment("Casts a sphere into the physics scene")
            .AddFunction<&PScene::GetEntityBodyID>("GetBodyID")
                .AddComment("Jolt body id backing the entity, or 0 if it has no rigid body.")
            .AddFunction<&PScene::ActivateBody>("ActivateBody")
                .AddComment("Wake a sleeping body so it simulates again.")
            .AddFunction<&PScene::DeactivateBody>("DeactivateBody")
                .AddComment("Put a body to sleep until something disturbs it.")
            .AddFunction<&PScene::AddForce>("AddForce")
                .AddComment("Apply a world-space force (N) to the entity's body for one physics step.")
            .AddFunction<&PScene::AddImpulse>("AddImpulse")
                .AddComment("Apply an instantaneous world-space impulse (kg*m/s).")
            .AddFunction<&PScene::AddTorque>("AddTorque")
                .AddComment("Apply a world-space torque (N*m) for one physics step.")
            .AddFunction<&PScene::AddAngularImpulse>("AddAngularImpulse")
                .AddComment("Apply an instantaneous angular impulse (kg*m^2/s).")
            .AddFunction<&PScene::AddForceAtPosition>("AddForceAtPosition")
                .AddComment("Apply a force at a world-space point, producing torque about the center of mass.")
            .AddFunction<&PScene::AddImpulseAtPosition>("AddImpulseAtPosition")
                .AddComment("Apply an impulse at a world-space point.")
            .AddFunction<&PScene::SetLinearVelocity>("SetLinearVelocity")
                .AddComment("Replace the body's linear velocity (m/s).")
            .AddFunction<&PScene::SetAngularVelocity>("SetAngularVelocity")
                .AddComment("Replace the body's angular velocity (rad/s).")
            .AddFunction<&PScene::SetGravityFactor>("SetGravityFactor")
                .AddComment("Per-body gravity multiplier (0 = float, 1 = normal).")
            .AddFunction<static_cast<FVector3(PScene::*)(entt::entity)>(&PScene::GetLinearVelocity)>("GetLinearVelocity")
                .AddComment("Linear velocity from the latched physics snapshot (frame-coherent).")
            .AddFunction<static_cast<FVector3(PScene::*)(entt::entity)>(&PScene::GetAngularVelocity)>("GetAngularVelocity")
                .AddComment("Angular velocity from the latched physics snapshot.")
            .AddFunction<static_cast<FVector3(PScene::*)(entt::entity, const FVector3&)>(&PScene::GetVelocityAtPoint)>("GetVelocityAtPoint")
                .AddComment("Velocity of a world-space point on the body (includes spin).")
            .AddFunction<static_cast<FVector3(PScene::*)(entt::entity)>(&PScene::GetCenterOfMass)>("GetCenterOfMass")
                .AddComment("World-space center of mass.")
            .AddFunction<static_cast<FVector3(PScene::*)(entt::entity)>(&PScene::GetBodyPosition)>("GetBodyPosition")
                .AddComment("Actual physics body position -- not the lagged render transform.")
            .AddFunction<static_cast<FQuat(PScene::*)(entt::entity)>(&PScene::GetBodyRotation)>("GetBodyRotation")
                .AddComment("Actual physics body rotation -- not the lagged render transform.")
            .Register();

        // World.Net query facade. One instance per world; reached as World.Net:IsServer() etc.
        GlobalRef.NewClass<FNetLuaInterface>("NetInterface")
            .AddFunction<&FNetLuaInterface::IsServer>("IsServer")
                .AddComment("True on the listen/dedicated server (the authority).")
            .AddFunction<&FNetLuaInterface::IsClient>("IsClient")
                .AddComment("True on a connected client.")
            .AddFunction<&FNetLuaInterface::IsStandalone>("IsStandalone")
                .AddComment("True when the world is not networked.")
            .AddFunction<&FNetLuaInterface::IsNetworked>("IsNetworked")
                .AddComment("True when running as a client or server.")
            .AddFunction<&FNetLuaInterface::IsConnected>("IsConnected")
                .AddComment("Server: at least one client connected. Client: link to the server established.")
            .AddFunction<&FNetLuaInterface::GetConnectedClients>("GetConnectedClients")
                .AddComment("Server-side count of currently-connected clients (0 elsewhere).")
            .AddFunction<&FNetLuaInterface::GetUniqueId>("GetUniqueId")
                .AddComment("This peer's id: 0 on the server, the server-assigned id on a client.")
            .AddFunction<&FNetLuaInterface::GetConnectionCount>("GetConnectionCount")
                .AddComment("Server-side count of connected clients (pair with GetConnectionAt).")
            .AddFunction<&FNetLuaInterface::GetConnectionAt>("GetConnectionAt")
                .AddComment("Server-side: connected client id at index [0, GetConnectionCount).")
            .AddFunction<&FNetLuaInterface::HasAuthority>("HasAuthority")
                .AddComment("True if this peer is the authority for the entity (server, or a local non-replicated entity).")
            .AddFunction<&FNetLuaInterface::IsLocallyOwned>("IsLocallyOwned")
                .AddComment("True if this peer controls the entity (its AutonomousProxy). Gate input/camera with this.")
            .AddFunction<&FNetLuaInterface::GetLocalRole>("GetLocalRole")
                .AddComment("This peer's ENetRole for the entity: None/SimulatedProxy/AutonomousProxy/Authority.")
            .AddFunction<&FNetLuaInterface::GetRemoteRole>("GetRemoteRole")
                .AddComment("The far end's ENetRole for the entity.")
            .AddFunction<&FNetLuaInterface::GetOwner>("GetOwner")
                .AddComment("Owning connection id of the entity (0 = server-owned / unowned).")
            .AddFunction<&FNetLuaInterface::SetOwner>("SetOwner")
                .AddComment("Server-only: assign the owning connection of an entity (replicates to clients).")
            .AddFunction<&FNetLuaInterface::MarkDirty>("MarkDirty")
                .AddComment("Server-only: flag an entity's replicated properties dirty (reliable resend next tick).")
            .AddFunction<&FNetLuaInterface::GetLocallyOwnedEntity>("GetLocallyOwnedEntity")
                .AddComment("The entity this peer controls (its AutonomousProxy), or null. Find 'my pawn' on a client.")
            .AddFunction<&FNetLuaInterface::Host>("Host")
                .AddComment("Host a map as a listen server: World.Net:Host(\"/Game/Maps/Foo\", 7777). Port <= 0 => 7777.")
            .AddFunction<&FNetLuaInterface::Connect>("Connect")
                .AddComment("Connect to a server: World.Net:Connect(\"127.0.0.1\", 7777). The server picks the level. Port <= 0 => 7777.")
            .Register();

        GlobalRef.NewClass<entt::runtime_view>("RuntimeView")
            .AddFunction<&entt::runtime_view::contains>("Contains")
            .AddFunction<&LuaBinds::ForEachInRuntimeView_Lua>("Each")
            .AddFunction<&LuaBinds::RuntimeViewSizeHint_Lua>("SizeHint")
            .AddFunction<&LuaBinds::RuntimeViewGetEntities_Lua>("GetEntities")
            .Register();

        // Camera.* / World.* / RenderTarget.* bind via FRef::SetFunction; the leading CWorld* is
        // injected from thread data and trailing optional args may be omitted -- no lua_State plumbing.
        Lua::FRef CameraTable = GlobalRef.NewTable("Camera");
        CameraTable.SetFunction<&LuaBinds::Camera_SetActive>("SetActive");      // SetActive(entity [, blendTime [, easeFn]])
        CameraTable.SetFunction<&LuaBinds::Camera_SetActive>("BlendTo");        // alias that reads like the cinematic call
        CameraTable.SetFunction<&LuaBinds::Camera_GetActive>("GetActive");

        // Camera.Ease.{Linear,EaseIn,EaseOut,EaseInOut} -- pass as the easeFn arg.
        Lua::FRef CameraEase = CameraTable.NewTable("Ease");
        CameraEase.Set("Linear",    static_cast<int>(ECameraBlendFunction::Linear));
        CameraEase.Set("EaseIn",    static_cast<int>(ECameraBlendFunction::EaseIn));
        CameraEase.Set("EaseOut",   static_cast<int>(ECameraBlendFunction::EaseOut));
        CameraEase.Set("EaseInOut", static_cast<int>(ECameraBlendFunction::EaseInOut));

        // Vec3(x,y,z) -> vector. A simple constructor for building movement/offset vectors in script.
        GlobalRef.SetFunction<[](float X, float Y, float Z) -> FVector3 { return FVector3(X, Y, Z); }>("Vec3");

        Lua::FRef WorldTable = GlobalRef.NewTable("World");
        WorldTable.SetFunction<&LuaBinds::World_FindByName>("FindByName");
        WorldTable.SetFunction<&LuaBinds::World_FindByTag>("FindByTag");
        WorldTable.SetFunction<&LuaBinds::World_GetNumEntities>("GetNumEntities");
        WorldTable.SetFunction<&LuaBinds::World_Each>("Each");
        WorldTable.SetFunction<&LuaBinds::World_GetDeltaTime>("GetDeltaTime");
        WorldTable.SetFunction<&LuaBinds::World_GetTimeSinceCreation>("GetTimeSinceCreation");
        WorldTable.SetFunction<&LuaBinds::World_SpawnPrefab>("SpawnPrefab");        // SpawnPrefab(path) -> entity
        WorldTable.SetFunction<&LuaBinds::World_SpawnPrefabAt>("SpawnPrefabAt");    // SpawnPrefabAt(path, location) -> entity
        WorldTable.SetFunction<&LuaBinds::World_Duplicate>("Duplicate");            // Duplicate(entity) -> entity
        WorldTable.SetFunction<&LuaBinds::World_BeginPhysicsBatch>("BeginPhysicsBatch"); // wrap a bulk spawn loop; batches body creation
        WorldTable.SetFunction<&LuaBinds::World_EndPhysicsBatch>("EndPhysicsBatch");      // must be paired with BeginPhysicsBatch
        WorldTable.SetFunction<&LuaBinds::World_Destroy>("Destroy");                // Destroy(entity)
        WorldTable.SetFunction<&LuaBinds::World_CreateEntity>("CreateEntity");      // CreateEntity() -> entity
        WorldTable.SetFunction<&LuaBinds::World_AddComponent>("AddComponent");      // AddComponent(entity, Type)
        WorldTable.SetFunction<&LuaBinds::World_AddScript>("AddScript");            // AddScript(entity, "/path") -- replicated
        WorldTable.SetFunction<&LuaBinds::World_RemoveComponent>("RemoveComponent"); // RemoveComponent(entity, Type)
        WorldTable.SetFunction<&LuaBinds::World_AttachEntity>("AttachEntity");      // AttachEntity(child, parent) -- scene-graph parent
        WorldTable.SetFunction<&LuaBinds::World_SetLocalLocation>("SetLocalLocation"); // SetLocalLocation(entity, vector) (parent-relative)
        WorldTable.SetFunction<&LuaBinds::World_IsValid>("IsValid");                // IsValid(entity) -> bool
        WorldTable.SetFunction<&LuaBinds::World_Compact>("Compact");                // Compact() -- reclaim tombstones; invalidates cached component ptrs
        WorldTable.SetFunction<&LuaBinds::World_GetLocation>("GetLocation");        // GetLocation(entity) -> vector (world)
        WorldTable.SetFunction<&LuaBinds::World_SetLocation>("SetLocation");        // SetLocation(entity, vector) (world)
        WorldTable.SetFunction<&LuaBinds::World_Translate>("Translate");            // Translate(entity, delta) -> vector
        WorldTable.SetFunction<&LuaBinds::World_MoveToward>("MoveToward");          // MoveToward(entities, target, step) -- batched, one boundary crossing
        WorldTable.SetFunction<&LuaBinds::World_GetRotation>("GetRotation");        // GetRotation(entity) -> quat
        WorldTable.SetFunction<&LuaBinds::World_SetRotationFromEuler>("SetRotationFromEuler");        // GetRotation(entity) -> quat
        WorldTable.SetFunction<&LuaBinds::World_SetRotation>("SetRotation");        // SetRotation(entity, quat)
        WorldTable.SetFunction<&LuaBinds::World_GetScale>("GetScale");              // GetScale(entity) -> vector
        WorldTable.SetFunction<&LuaBinds::World_SetScale>("SetScale");              // SetScale(entity, vector)
        WorldTable.SetFunction<&LuaBinds::World_GetComponent>("GetComponent");      // GetComponent(entity, Type)
        WorldTable.SetFunction<&LuaBinds::World_HasComponent>("HasComponent");      // HasComponent(entity, Type)
        WorldTable.SetFunction<&LuaBinds::World_GetScript>("GetScript");            // GetScript(entity) -> script table or nil
        WorldTable.SetFunction<&LuaBinds::World_Fracture>("Fracture");          // Fracture(entity)
        WorldTable.SetFunction<&LuaBinds::World_FractureAt>("FractureAt");      // FractureAt(entity, x, y, z [, strength])

        // World.Debug namespace. Reached as World.Debug:DrawLine(...) (colon -- it's a userdata facade,
        // like World.Net). Trailing args (thickness, depth-test, duration) are optional; Dev/Debug only.
        GlobalRef.NewClass<FWorldDebugInterface>("WorldDebug")
            .AddFunction<&FWorldDebugInterface::DrawText>("DrawText")
                .AddComment("Screen-space debug text for this frame: DrawText(text [, color]).")
            .AddFunction<&FWorldDebugInterface::DrawLine>("DrawLine")
                .AddComment("DrawLine(start, finish, color [, thickness, depthTest, duration]).")
            .AddFunction<&FWorldDebugInterface::DrawBox>("DrawBox")
                .AddComment("DrawBox(center, halfExtents, rotation, color [, thickness, depthTest, duration]).")
            .AddFunction<&FWorldDebugInterface::DrawSphere>("DrawSphere")
                .AddComment("DrawSphere(center, radius, color [, thickness, depthTest, duration]).")
            .AddFunction<&FWorldDebugInterface::DrawCapsule>("DrawCapsule")
                .AddComment("DrawCapsule(start, finish, radius, color [, thickness, depthTest, duration]).")
            .AddFunction<&FWorldDebugInterface::DrawCone>("DrawCone")
                .AddComment("DrawCone(apex, direction, angleRadians, length, color [, thickness, depthTest, duration]).")
            .AddFunction<&FWorldDebugInterface::DrawArrow>("DrawArrow")
                .AddComment("DrawArrow(start, direction, length, color [, thickness, depthTest, duration]).")
            .Register();

        // Engine-provided World.<Subsystem> namespaces. Each is a per-world C++ facade reached through the
        // shared World table's metatable (see World_SubsystemIndex); the LuauType feeds the editor snippet
        // below. Game modules can add their own with RegisterWorldLuaSubsystem during their Lua bootstrap.
        RegisterWorldLuaSubsystem("Physics", "PhysicsScene",
            [](lua_State* L, CWorld* World) { Lua::TStack<Physics::IPhysicsScene*>::Push(L, World->GetPhysicsScene()); });
        RegisterWorldLuaSubsystem("Net", "NetInterface",
            [](lua_State* L, CWorld* World) { Lua::TStack<FNetLuaInterface*>::Push(L, World->GetNetInterface()); });
        RegisterWorldLuaSubsystem("Debug", "WorldDebug",
            [](lua_State* L, CWorld* World) { Lua::TStack<FWorldDebugInterface*>::Push(L, World->GetDebugInterface()); });

        // World.<Subsystem> namespaces resolve per script through this metatable so the single shared
        // World table is correct across every world. See World_SubsystemIndex.
        {
            lua_State* L = GlobalRef.GetState();
            WorldTable.Push();
            lua_newtable(L);
            lua_pushcfunction(L, &LuaBinds::World_SubsystemIndex, "World.__index");
            lua_rawsetfield(L, -2, "__index");
            lua_setmetatable(L, -2);
            lua_pop(L, 1);
        }

#if WITH_EDITOR
        // Editor type for the World table. Subsystem fields are generated from the registry (whatever
        // RegisterWorldLuaSubsystem declared a LuauType for), so a game-defined subsystem is typed
        // without editing this snippet. `[string]: any` keeps the not-yet-typed flat World.* helpers
        // (FindByName, SpawnPrefab, ...) error-free.
        FString WorldSnippet = "declare World: {\n";
        for (const FWorldLuaSubsystemRegistry::FEntry& Entry : FWorldLuaSubsystemRegistry::Get().GetEntries())
        {
            if (Entry.LuauType != nullptr)
            {
                WorldSnippet += "    ";
                WorldSnippet += Entry.Name.c_str();
                WorldSnippet += ": ";
                WorldSnippet += Entry.LuauType;
                WorldSnippet += ",\n";
            }
        }
        WorldSnippet +=
            "    [string]: any,\n"
            "}\n";
        Lua::FScriptTypeRegistry::Get().RegisterSnippet("World", WorldSnippet);
#endif

        // RenderTarget.Paint(target, u, v, radius [, r, g, b, a [, strength [, hardness]]])
        // target = an Asset.Hard render-target handle; UV 0..1, radius relative to longer side, color defaults to opaque red.
        Lua::FRef RenderTargetTable = GlobalRef.NewTable("RenderTarget");
        RenderTargetTable.SetFunction<&LuaBinds::RenderTarget_Paint>("Paint");
        RenderTargetTable.SetFunction<&LuaBinds::RenderTarget_Clear>("Clear");

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

        // The `ctx` handed to Lua script systems. Mirrors the C++ FSystemContext: time, entity iteration and
        // component access over the live registry. World.<Subsystem> (Physics/Net/Debug) resolve globally.
        GlobalRef.NewClass<FLuaSystemContext>("SystemContext")
            .AddFunction<&LuaBinds::Ctx_DeltaTime>("DeltaTime")
            .AddFunction<&LuaBinds::Ctx_Time>("Time")
            .AddFunction<&LuaBinds::Ctx_Registry>("Registry")
            .AddFunction<&LuaBinds::Ctx_Get>("Get")
            .AddFunction<&LuaBinds::Ctx_Has>("Has")
            .AddFunction<&LuaBinds::Ctx_Emplace>("Emplace")
            .AddFunction<&LuaBinds::Ctx_Remove>("Remove")
            .AddFunction<&LuaBinds::Ctx_Create>("Create")
            .AddFunction<&LuaBinds::Ctx_Destroy>("Destroy")
            .AddFunction<&LuaBinds::Ctx_View>("View")
            .AddFunction<&LuaBinds::Ctx_Each>("Each")
            .Register();
    }
}
