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
#include "World/Entity/Components/NetworkComponent.h"
#include "Scripting/Lua/ScriptTypeRegistry.h"
#include "Scripting/Lua/Stack.h"
#include "Scripting/Lua/VariadicArgs.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "Subsystems/WorldSettings.h"
#include "UI/RmlUiBridge.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/entity/systems/EntitySystem.h"

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

        // Lifecycle/transform on an ARBITRARY entity id; mirror the ECS.* utilities without the
        // registry arg, so scripts touching spawned ids (not `self`) skip self._Registry. WORLD-space.
        static entt::entity World_Duplicate(CWorld* World, entt::entity Entity)
        {
            return World ? ECS::Utils::DuplicateEntity(World->GetEntityRegistry(), Entity) : entt::null;
        }

        // Bulk-spawn helper: wrap a spawn loop in Begin/EndPhysicsBatch so the N bodies enter the
        // broadphase in one batched pass. Balance on the game thread; BodyIDs valid only after End.
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
            if (Key == nullptr) { lua_pushnil(L); return 1; }

            const auto* TD = static_cast<Lua::FScriptThreadData*>(lua_getthreaddata(L));
            CWorld* World = TD ? TD->World : nullptr;
            if (World == nullptr) { lua_pushnil(L); return 1; }

            if (strcmp(Key, "Physics") == 0)
            {
                Lua::TStack<Physics::IPhysicsScene*>::Push(L, World->GetPhysicsScene());
                return 1;
            }

            if (strcmp(Key, "Net") == 0)
            {
                Lua::TStack<FNetLuaInterface*>::Push(L, World->GetNetInterface());
                return 1;
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
            if (World == nullptr) return;
            FEntityRegistry& Registry = World->GetEntityRegistry();
            FTransform WorldTransform;
            WorldTransform.Location = Location;
            WorldTransform.Rotation = ECS::Utils::GetEntityRotation(Registry, Entity);
            WorldTransform.Scale    = ECS::Utils::GetEntityScale(Registry, Entity);
            ECS::Utils::SetEntityWorldTransform(Registry, Entity, WorldTransform);
        }

        static void World_SetRotation(CWorld* World, entt::entity Entity, FQuat Rotation)   { if (World) ECS::Utils::SetEntityRotation(World->GetEntityRegistry(), Entity, Rotation); }
        static void World_SetScale(CWorld* World, entt::entity Entity, FVector3 Scale)      { if (World) ECS::Utils::SetEntityScale(World->GetEntityRegistry(), Entity, Scale); }

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
            return GetComponent_Lua(World->GetEntityRegistry(), Entity, Ref);
        }

        static bool World_HasComponent(CWorld* World, entt::entity Entity, Lua::FRef Ref)
        {
            return World != nullptr && HasComponent_Lua(World->GetEntityRegistry(), Entity, Ref);
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

        //~ World.Debug.* -- screen-space debug text + world debug shapes. Dev/Debug only (no-op in Shipping).
        static void World_Debug_DrawText(CWorld* World, FStringView Text, TOptional<FVector4> Color)
        {
            if (World) World->DrawDebugText(FString(Text), Color.value_or(FVector4(1.0f)));
        }
        static void World_Debug_DrawLine(CWorld* World, FVector3 Start, FVector3 End, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
        {
            if (World) World->DrawLine(Start, End, Color, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }
        static void World_Debug_DrawBox(CWorld* World, FVector3 Center, FVector3 HalfExtents, FQuat Rotation, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
        {
            if (World) World->DrawBox(Center, HalfExtents, Rotation, Color, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }
        static void World_Debug_DrawSphere(CWorld* World, FVector3 Center, float Radius, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
        {
            if (World) World->DrawSphere(Center, Radius, Color, 16, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }
        static void World_Debug_DrawCapsule(CWorld* World, FVector3 Start, FVector3 End, float Radius, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
        {
            if (World) World->DrawCapsule(Start, End, Radius, Color, 16, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }
        static void World_Debug_DrawCone(CWorld* World, FVector3 Apex, FVector3 Direction, float AngleRadians, float Length, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
        {
            if (World) World->DrawCone(Apex, Direction, AngleRadians, Length, Color, 16, 4, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }
        static void World_Debug_DrawArrow(CWorld* World, FVector3 Start, FVector3 Direction, float Length, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
        {
            if (World) World->DrawArrow(Start, Direction, Length, Color, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }

        // RT is a handle from Engine.LoadObject("/Game/X"); color defaults to opaque red (blood).
        static void RenderTarget_Paint(CWorld* World, CTextureRenderTarget* RT, float U, float V, float Radius,
            TOptional<float> R, TOptional<float> G, TOptional<float> B, TOptional<float> A,
            TOptional<float> Strength, TOptional<float> Hardness)
        {
            if (World == nullptr || RT == nullptr) return;
            World->PaintRenderTarget(RT, FVector2(U, V), Radius,
                FVector4(R.value_or(1.0f), G.value_or(0.0f), B.value_or(0.0f), A.value_or(1.0f)),
                Strength.value_or(1.0f), Hardness.value_or(1.0f), nullptr);
        }

        static void RenderTarget_Clear(CWorld* World, CTextureRenderTarget* RT, TOptional<float> R, TOptional<float> G, TOptional<float> B, TOptional<float> A)
        {
            if (World == nullptr || RT == nullptr) return;
            World->ClearRenderTarget(RT, FVector4(R.value_or(0.0f), G.value_or(0.0f), B.value_or(0.0f), A.value_or(0.0f)));
        }
    }
    
    
    namespace
    {
        bool NetIsServerMode(ENetMode Mode)
        {
            return Mode == ENetMode::ListenServer || Mode == ENetMode::DedicatedServer;
        }
    }

    bool FNetLuaInterface::IsServer() const     { return World != nullptr && NetIsServerMode(World->GetNetMode()); }
    bool FNetLuaInterface::IsClient() const     { return World != nullptr && World->GetNetMode() == ENetMode::Client; }
    bool FNetLuaInterface::IsStandalone() const { return World == nullptr || World->GetNetMode() == ENetMode::Standalone; }
    bool FNetLuaInterface::IsNetworked() const  { return !IsStandalone(); }

    int32 FNetLuaInterface::GetConnectedClients() const
    {
        if (World == nullptr) { return 0; }
        FNetWorldState* State = World->GetEntityRegistry().ctx().find<FNetWorldState>();
        return State != nullptr ? State->ConnectedClients : 0;
    }

    bool FNetLuaInterface::IsConnected() const
    {
        if (World == nullptr) { return false; }
        FNetWorldState* State = World->GetEntityRegistry().ctx().find<FNetWorldState>();
        if (State == nullptr) { return false; }
        return IsServer() ? State->ConnectedClients > 0 : State->bClientConnected;
    }

    uint32 FNetLuaInterface::GetUniqueId() const
    {
        if (World == nullptr) { return ServerPeerId; }
        FNetWorldState* State = World->GetEntityRegistry().ctx().find<FNetWorldState>();
        return State != nullptr ? State->LocalPeerId : ServerPeerId;
    }

    int32 FNetLuaInterface::GetConnectionCount() const
    {
        if (World == nullptr) { return 0; }
        FNetWorldState* State = World->GetEntityRegistry().ctx().find<FNetWorldState>();
        return State != nullptr ? (int32)State->ConnectedClientIds.size() : 0;
    }

    uint32 FNetLuaInterface::GetConnectionAt(int32 Index) const
    {
        if (World == nullptr || Index < 0) { return 0; }
        FNetWorldState* State = World->GetEntityRegistry().ctx().find<FNetWorldState>();
        if (State == nullptr || Index >= (int32)State->ConnectedClientIds.size()) { return 0; }
        return State->ConnectedClientIds[(size_t)Index];
    }

    bool FNetLuaInterface::HasAuthority(entt::entity Entity) const
    {
        if (World == nullptr) { return false; }
        const SNetworkComponent* Net = World->GetEntityRegistry().try_get<SNetworkComponent>(Entity);
        // No net component at all -> authoritative by default (single-player / non-replicated).
        return Net == nullptr ? IsStandalone() || IsServer() : Net->LocalRole == ENetRole::Authority;
    }

    bool FNetLuaInterface::IsLocallyOwned(entt::entity Entity) const
    {
        if (World == nullptr) { return false; }
        const SNetworkComponent* Net = World->GetEntityRegistry().try_get<SNetworkComponent>(Entity);
        return Net != nullptr && Net->LocalRole == ENetRole::AutonomousProxy;
    }

    int32 FNetLuaInterface::GetLocalRole(entt::entity Entity) const
    {
        if (World == nullptr) { return (int32)ENetRole::None; }
        const SNetworkComponent* Net = World->GetEntityRegistry().try_get<SNetworkComponent>(Entity);
        return Net != nullptr ? (int32)Net->LocalRole : (int32)ENetRole::None;
    }

    int32 FNetLuaInterface::GetRemoteRole(entt::entity Entity) const
    {
        if (World == nullptr) { return (int32)ENetRole::None; }
        const SNetworkComponent* Net = World->GetEntityRegistry().try_get<SNetworkComponent>(Entity);
        return Net != nullptr ? (int32)Net->RemoteRole : (int32)ENetRole::None;
    }

    uint32 FNetLuaInterface::GetOwner(entt::entity Entity) const
    {
        if (World == nullptr) { return 0; }
        const SNetworkComponent* Net = World->GetEntityRegistry().try_get<SNetworkComponent>(Entity);
        return Net != nullptr ? Net->OwningConnectionId : 0u;
    }

    void FNetLuaInterface::SetOwner(entt::entity Entity, uint32 ConnectionId)
    {
        if (World == nullptr || !IsServer()) { return; } // only the authority assigns ownership
        FEntityRegistry& Registry = World->GetEntityRegistry();
        SNetworkComponent* Net = Registry.try_get<SNetworkComponent>(Entity);
        if (Net == nullptr) { return; }
        Net->OwningConnectionId = ConnectionId;
        if (FNetWorldState* State = Registry.ctx().find<FNetWorldState>())
        {
            State->bOwnershipDirty = true; // replicate the new owner to clients next tick
        }
    }

    void FNetLuaInterface::MarkDirty(entt::entity Entity)
    {
        if (World == nullptr || !IsServer()) { return; } // only the authority replicates
        FEntityRegistry& Registry = World->GetEntityRegistry();
        if (Registry.valid(Entity) && Registry.all_of<SNetworkComponent>(Entity))
        {
            Registry.emplace_or_replace<FNetDirty>(Entity);
        }
    }

    entt::entity FNetLuaInterface::GetLocallyOwnedEntity() const
    {
        if (World == nullptr) { return entt::null; }
        FEntityRegistry& Registry = World->GetEntityRegistry();
        for (entt::entity Entity : Registry.view<SNetworkComponent>())
        {
            if (Registry.get<SNetworkComponent>(Entity).LocalRole == ENetRole::AutonomousProxy)
            {
                return Entity;
            }
        }
        return entt::null;
    }

    void FNetLuaInterface::Host(FStringView Map, int32 Port) const
    {
        if (GEngine != nullptr)
        {
            GEngine->HostLevel(Map, Port > 0 ? static_cast<uint16>(Port) : 7777);
        }
    }

    void FNetLuaInterface::Connect(FStringView InHost, int32 Port) const
    {
        if (GEngine != nullptr)
        {
            GEngine->ConnectToServer(InHost, Port > 0 ? static_cast<uint16>(Port) : 7777);
        }
    }

    CWorld::CWorld()
        : SingletonEntity(entt::null)
        , SystemContext(this)
        , LineBatcherComponent(nullptr)
        , TriangleBatcherComponent(nullptr)
    {
        NetInterface.World = this;
    }

    void CWorld::PaintRenderTarget(CTextureRenderTarget* Target, const FVector2& UV, float RadiusUV, const FVector4& Color, float Strength, float Hardness, CTexture* BrushMask)
    {
        if (Target == nullptr || Target->GetRHIRef() == nullptr)
        {
            return;
        }

        FTexturePaintOp Op;
        Op.Target     = Target->GetTextureResource().RHIImage;
        Op.Mode       = FTexturePaintOp::EMode::Paint;
        Op.CenterUV   = UV;
        Op.RadiusUV   = RadiusUV;
        Op.Color      = Color;
        Op.Strength   = Strength;
        Op.Hardness   = Hardness;
        Op.BrushIndex = (BrushMask != nullptr && BrushMask->GetRHIRef() != nullptr) ? BrushMask->GetRHIRef()->GetResourceID() : -1;
        RenderTargetPaintQueue.enqueue(Move(Op));
    }

    void CWorld::ClearRenderTarget(CTextureRenderTarget* Target, const FVector4& Color)
    {
        if (Target == nullptr || Target->GetRHIRef() == nullptr)
        {
            return;
        }

        FTexturePaintOp Op;
        Op.Target = Target->GetTextureResource().RHIImage;
        Op.Mode   = FTexturePaintOp::EMode::Clear;
        Op.Color  = Color;
        RenderTargetPaintQueue.enqueue(Move(Op));
    }

    void CWorld::DrainRenderTargetPaints(TVector<FTexturePaintOp>& OutOps)
    {
        FTexturePaintOp Op;
        while (RenderTargetPaintQueue.try_dequeue(Op))
        {
            OutOps.push_back(Move(Op));
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

        // Render-target handle type, so Engine.LoadObject("/Game/MyRT") returns a usable value.
        GlobalRef.NewClass<CTextureRenderTarget>("RenderTargetTexture")
            .AddFunction<&CTextureRenderTarget::GetWidth>("GetWidth")
            .AddFunction<&CTextureRenderTarget::GetHeight>("GetHeight")
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
        WorldTable.SetFunction<&LuaBinds::World_SetRotation>("SetRotation");        // SetRotation(entity, quat)
        WorldTable.SetFunction<&LuaBinds::World_GetScale>("GetScale");              // GetScale(entity) -> vector
        WorldTable.SetFunction<&LuaBinds::World_SetScale>("SetScale");              // SetScale(entity, vector)
        WorldTable.SetFunction<&LuaBinds::World_GetComponent>("GetComponent");      // GetComponent(entity, Type)
        WorldTable.SetFunction<&LuaBinds::World_HasComponent>("HasComponent");      // HasComponent(entity, Type)
        WorldTable.SetFunction<&LuaBinds::World_GetScript>("GetScript");            // GetScript(entity) -> script table or nil
        WorldTable.SetFunction<&LuaBinds::World_Fracture>("Fracture");          // Fracture(entity)
        WorldTable.SetFunction<&LuaBinds::World_FractureAt>("FractureAt");      // FractureAt(entity, x, y, z [, strength])

        // World.Debug.* -- screen-space debug text + world debug shapes. Trailing args (thickness, depth-test,
        // duration) are optional. Dev/Debug only (DrawText is a no-op in Shipping).
        {
            Lua::FRef DebugTable = WorldTable.NewTable("Debug");
            DebugTable.SetFunction<&LuaBinds::World_Debug_DrawText>   ("DrawText");    // DrawText(text [, color])
            DebugTable.SetFunction<&LuaBinds::World_Debug_DrawLine>   ("DrawLine");    // DrawLine(start, end, color [, thickness, depthTest, duration])
            DebugTable.SetFunction<&LuaBinds::World_Debug_DrawBox>    ("DrawBox");     // DrawBox(center, halfExtents, rotation, color [, ...])
            DebugTable.SetFunction<&LuaBinds::World_Debug_DrawSphere> ("DrawSphere");  // DrawSphere(center, radius, color [, ...])
            DebugTable.SetFunction<&LuaBinds::World_Debug_DrawCapsule>("DrawCapsule"); // DrawCapsule(start, end, radius, color [, ...])
            DebugTable.SetFunction<&LuaBinds::World_Debug_DrawCone>   ("DrawCone");    // DrawCone(apex, direction, angleRadians, length, color [, ...])
            DebugTable.SetFunction<&LuaBinds::World_Debug_DrawArrow>  ("DrawArrow");   // DrawArrow(start, direction, length, color [, ...])
        }

        // World.<Subsystem> namespaces (World.Physics, ...) resolve per script through this metatable
        // so the single shared World table is correct across every world. See World_SubsystemIndex.
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
        // Editor type for the World table. Subsystem types (PhysicsScene, ...) are auto-derived from
        // their NewClass bindings above; here we only state how they hang off World. `[string]: any`
        // keeps the not-yet-typed flat World.* helpers (FindByName, SpawnPrefab, ...) error-free.
        Lua::FScriptTypeRegistry::Get().RegisterSnippet("World",
            "declare World: {\n"
            "    Physics: PhysicsScene,\n"
            "    Net: NetInterface,\n"
            "    Debug: {\n"
            "        DrawText: (text: string, color: vector?) -> (),\n"
            "        DrawLine: (start: vector, finish: vector, color: vector, thickness: number?, depthTest: boolean?, duration: number?) -> (),\n"
            "        DrawBox: (center: vector, halfExtents: vector, rotation: any, color: vector, thickness: number?, depthTest: boolean?, duration: number?) -> (),\n"
            "        DrawSphere: (center: vector, radius: number, color: vector, thickness: number?, depthTest: boolean?, duration: number?) -> (),\n"
            "        DrawCapsule: (start: vector, finish: vector, radius: number, color: vector, thickness: number?, depthTest: boolean?, duration: number?) -> (),\n"
            "        DrawCone: (apex: vector, direction: vector, angleRadians: number, length: number, color: vector, thickness: number?, depthTest: boolean?, duration: number?) -> (),\n"
            "        DrawArrow: (start: vector, direction: vector, length: number, color: vector, thickness: number?, depthTest: boolean?, duration: number?) -> (),\n"
            "    },\n"
            "    [string]: any,\n"
            "}\n");
#endif

        // RenderTarget.Paint(target, u, v, radius [, r, g, b, a [, strength [, hardness]]])
        // target = LoadObject handle; UV 0..1, radius relative to longer side, color defaults to opaque red.
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
            // A freshly-loaded asset keeps entities in RegistryPending until InitializeWorld swaps them
            // into EntityRegistry; DuplicateWorld serializes pre-init. Write from whichever holds the data.
            FEntityRegistry& Source = (EntityRegistry.storage<entt::entity>().size() > 0)
                ? EntityRegistry
                : RegistryPending;
            ECS::Utils::SerializeRegistry(Ar, Source);
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

        // Compact once load-time churn (tombstones + prefab refresh) settles, so the world starts dense.
        // Before any on_construct hook; reorders elements but nothing here holds a component pointer.
        EntityRegistry.compact();

        // Server-only entities (SNetworkComponent.bNetLoadOnClient == false) never exist on a client. Strip
        // them here -- after the load swap, before scripts attach -- so the client never even loads their
        // scripts. The script-attach hooks aren't connected yet, so this is a quiet removal.
        if (GetNetMode() == ENetMode::Client)
        {
            TVector<entt::entity> ServerOnly;
            for (entt::entity Entity : EntityRegistry.view<SNetworkComponent>())
            {
                if (!EntityRegistry.get<SNetworkComponent>(Entity).bNetLoadOnClient)
                {
                    ServerOnly.push_back(Entity);
                }
            }
            for (entt::entity Entity : ServerOnly)
            {
                EntityRegistry.destroy(Entity);
            }
        }

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
        TriangleBatcherComponent = &EntityRegistry.emplace<FTriangleBatcherComponent>(SingletonEntity);
        EntityRegistry.emplace<FSingletonEntityTag>(SingletonEntity);
        EntityRegistry.emplace<FHideInSceneOutliner>(SingletonEntity);
        
        // Physics scene only for simulating worlds; Jolt reserves ~hundreds of MB up front.
        if (WorldType == EWorldType::Game || WorldType == EWorldType::Simulation)
        {
            PhysicsScene = Physics::GetPhysicsContext()->CreatePhysicsScene(this);
        }
        // Emplaced even when null so ctx().get<>() consumers find the key (value is null in
        // non-simulating worlds and must be null-checked).
        EntityRegistry.ctx().emplace<Physics::IPhysicsScene*>(PhysicsScene.get());
        EntityRegistry.ctx().emplace<FSystemContext&>(SystemContext);
        EntityRegistry.ctx().emplace<CWorld*>(this);

        // Per-world subsystem singletons ticked by their systems: SEventBusSystem flushes FLuaEventBus,
        // STimerSystem advances FTimerManager (both at FrameStart). Scripts reach them by ctx address.
        EntityRegistry.ctx().emplace<FLuaEventBus>();
        EntityRegistry.ctx().emplace<FTimerManager>();

        // System-produced singletons: SCameraSystem owns FCameraGlobalState (active camera + blend) and
        // writes FResolvedSceneView (read in Extract); SScriptSystem owns FScriptFixedUpdateState.
        EntityRegistry.ctx().emplace<FCameraGlobalState>();
        EntityRegistry.ctx().emplace<FResolvedSceneView>();
        EntityRegistry.ctx().emplace<FScriptFixedUpdateState>();

        CreateRenderer();
        UIContext = RmlUi::CreateWorldUI(this);

        // Seed the per-world disabled-system set from the saved world settings before registering systems,
        // so disabled systems are never constructed/started. Tolerant of stale names (ignored below).
        DisabledSystems.clear();
        for (const FName& Name : GetDefaultWorldSettings().DisabledSystems)
        {
            DisabledSystems.insert(Name);
        }
        PendingDisabledSystems = DisabledSystems;

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
        EntityRegistry.on_update    <SScriptComponent>()            .connect<&ThisClass::OnScriptComponentUpdated>(this);
        EntityRegistry.on_destroy   <SScriptComponent>()            .connect<&ThisClass::OnScriptComponentDestroyed>(this);
        // Left connected through teardown: fires at clear() to release widget RTs.
        EntityRegistry.on_destroy   <SWidgetComponent>()            .connect<&ThisClass::OnWidgetComponentDestroyed>(this);
        EntityRegistry.on_construct <SInputComponent>()             .connect<&ThisClass::OnInputComponentConstruct>(this);
        EntityRegistry.on_destroy   <SInputComponent>()             .connect<&ThisClass::OnInputComponentDestroyed>(this);
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

        // Bind loaded input components to this world so their queries resolve to this world's viewport
        // (hooks connect after the load swap, so pre-existing components miss on_construct).
        auto InputView = EntityRegistry.view<SInputComponent>();
        InputView.each([&](entt::entity Entity, SInputComponent& InputComponent)
        {
            InputComponent.World = this;
        });
        
        if (WorldType == EWorldType::Game || WorldType == EWorldType::Simulation)
        {
            const auto AnyCameraView = EntityRegistry.view<SCameraComponent>();
            if (AnyCameraView.begin() == AnyCameraView.end())
            {
                LOG_WARN("CWorld::Initialize: world '{}' has no camera entity; spawning a fallback at (0, 2, 5) looking at origin. Add a camera entity for proper gameplay.",
                    GetName());

                constexpr FVector3 FallbackPos(0.0f, 2.0f, 5.0f);

                const entt::entity Fallback = EntityRegistry.create();
                STransformComponent& Xf = EntityRegistry.emplace<STransformComponent>(Fallback);
                Xf.LocalTransform.Location = FallbackPos;
                Xf.LocalTransform.Rotation = Math::FindLookAtRotation(FVector3(0.0f), FallbackPos);

                SCameraComponent& Cam = EntityRegistry.emplace<SCameraComponent>(Fallback);
                Cam.bAutoActivate = true;
            }
        }

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
        EntityRegistry.on_update<SScriptComponent>().disconnect<&ThisClass::OnScriptComponentUpdated>(this);
        EntityRegistry.on_destroy<SScriptComponent>().disconnect<&ThisClass::OnScriptComponentConstruct>(this);
        
        ForEachUniqueSystem([&](const FSystemVariant& System)
        {
            eastl::visit([&](const auto& Sys) { Sys.Teardown(SystemContext); }, System);
        });
        
        if (WorldType == EWorldType::Game || WorldType == EWorldType::Simulation)
        {
            PhysicsScene->StopSimulate();
        }
        
        EntityRegistry.ctx().get<FLuaEventBus>().Clear();
        EntityRegistry.ctx().get<FTimerManager>().Clear();

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

        // Reconcile any deferred system enable/disable before the gate/tick, so a toggle requested mid-frame
        // is applied here (between frames) and never inside a running system batch.
        ApplyPendingSystemChanges();

        if (Stage == EUpdateStage::FrameStart)
        {
            DeltaTime = Context.GetDeltaTime() * GetDefaultWorldSettings().DeltaTimeScale;
            TimeSinceCreation += DeltaTime;

#if USING(WITH_EDITOR)
            // Migrate runtime-component storage whose schema was edited. Editor-only (schemas immutable
            // at runtime); runs even while paused so editor worlds reflect live edits. Cheap revision compare.
            ECS::Utils::RefreshRuntimeComponentSchemas(EntityRegistry);
#endif
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

        // Deferred Lua events + timers run inside TickSystems now (SEventBusSystem / STimerSystem,
        // both FrameStart/Highest), so they tick before gameplay systems just as the old inline block did.
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
            ScriptComponent->Script->PublishThreadContext();
            ScriptComponent->ReadyFunc.Call(ScriptComponent->Script->Reference);
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
                Component->Script->PublishThreadContext();
                HookRef.Call(Component->Script->Reference);
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
        const FSystemAccess Access = eastl::visit([&](const auto& System) { return System.GetSystemAccess(); }, NewSystem);

        bool StagesModified[(uint8)EUpdateStage::Max] = {};

        for (uint8 i = 0; i < (uint8)EUpdateStage::Max; ++i)
        {
            if (PriorityList.IsStageEnabled((EUpdateStage)i))
            {
                SystemUpdateList[i].push_back(FScheduledSystem{ NewSystem, Access, PriorityList.GetPriorityForStage((EUpdateStage)i) });
                StagesModified[i] = true;
            }
        }

        for (uint8 i = 0; i < (uint8)EUpdateStage::Max; ++i)
        {
            if (!StagesModified[i])
            {
                continue;
            }

            // Lower value = higher priority (Highest=0 .. Low=192), so ascending runs Highest first.
            eastl::sort(SystemUpdateList[i].begin(), SystemUpdateList[i].end(),
                [](const FScheduledSystem& A, const FScheduledSystem& B) { return A.StagePriority < B.StagePriority; });
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
    
    bool CWorld::FractureEntity(entt::entity Entity, const FVector3& Origin, float Strength)
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

        FTransform OwnerTransform = EntityRegistry.get<STransformComponent>(Entity).GetWorldTransform();
        
        FVector3 InheritedVelocity(0.0f);
        if (PhysicsScene)
        {
            if (const SRigidBodyComponent* RB = EntityRegistry.try_get<SRigidBodyComponent>(Entity))
            {
                if (RB->BodyID != 0xFFFFFFFFu)
                {
                    OwnerTransform.Location = PhysicsScene->GetBodyPosition(RB->BodyID);
                    OwnerTransform.Rotation = PhysicsScene->GetBodyRotation(RB->BodyID);
                    InheritedVelocity       = PhysicsScene->GetLinearVelocity(RB->BodyID);
                }
            }
        }

        const FVector3 OwnerScale = OwnerTransform.Scale;

        const float LaunchSpeed = Strength > 0.0f ? Strength : Destructible->ExplosionStrength;
        const float SpinSpeed   = Destructible->SpinStrength;

        // Deterministic per-fragment jitter (good for replays / lockstep): hash the index.
        auto Hash01 = [](uint32 V) -> float
        {
            V ^= V >> 16; V *= 0x7feb352dU; V ^= V >> 15; V *= 0x846ca68bU; V ^= V >> 16;
            return static_cast<float>(V) / static_cast<float>(0xFFFFFFFFU);
        };

        // Inherited momentum + an outward blast (radial from Origin) + random spin on a fresh body.
        auto LaunchBody = [&](uint32 BodyID, const FVector3& WorldCenter, uint32 Seed)
        {
            if (!PhysicsScene || BodyID == 0xFFFFFFFFu)
            {
                return;
            }
            FVector3 Direction = WorldCenter - Origin;
            const float Distance = Math::Length(Direction);
            Direction = Distance > 1e-4f
                ? Direction / Distance
                : Math::Normalize(FVector3(Hash01(Seed) - 0.5f, Hash01(Seed + 1) + 0.25f, Hash01(Seed + 2) - 0.5f));

            const float SpeedJitter = 0.7f + 0.6f * Hash01(Seed + 3);
            const FVector3 LaunchVelocity = InheritedVelocity
                + Direction * (LaunchSpeed * SpeedJitter)
                + FVector3(0.0f, LaunchSpeed * 0.2f, 0.0f);
            PhysicsScene->OnSetVelocityEvent(SSetVelocityEvent{ BodyID, LaunchVelocity });

            if (SpinSpeed > 0.0f)
            {
                const FVector3 Spin(Hash01(Seed + 4) - 0.5f, Hash01(Seed + 5) - 0.5f, Hash01(Seed + 6) - 0.5f);
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

        // Create all fragment bodies in one batch (AddBodiesPrepare/Finalize). BodyIDs valid only after
        // EndBodyBatch, so collect launch impulses and apply them once inserted.
        struct FPendingLaunch { entt::entity Fragment; FVector3 Center; uint32 Seed; };
        TVector<FPendingLaunch> PendingLaunches;
        PendingLaunches.reserve(Pieces.size());

        // Cap fragments at physics body headroom; overflowing Jolt's body/contact buffers trips a hard
        // assert, so clamp + warn instead. Raise World Settings > Physics > Max* for denser destruction.
        uint32 MaxFragments = 0xFFFFFFFFu;
        if (PhysicsScene)
        {
            const uint32 MaxBodies = PhysicsScene->GetMaxBodyCount();
            const uint32 Used      = Math::Min(PhysicsScene->GetBodyCount(), MaxBodies);
            const uint32 Headroom  = MaxBodies - Used;
            MaxFragments = Headroom > 16 ? Headroom - 16 : 0;

            const uint32 Desired = Pieces.empty()
                ? (uint32)Math::Clamp(Destructible->FragmentCount, 2, 512)
                : (uint32)Pieces.size();
            if (Desired > MaxFragments)
            {
                LOG_WARN("FractureEntity: clamped {} fragments to {} (physics body headroom {}/{}). Raise World Settings > Physics > MaxPhysicsBodies.",
                    Desired, MaxFragments, Used, MaxBodies);
            }
        }

        if (PhysicsScene)
        {
            PhysicsScene->BeginBodyBatch();
        }

        if (!Pieces.empty())
        {
            const TVector<TObjectPtr<CMaterialInterface>>& PieceMaterials =
                (CollectionData && !Destructible->Collection->Materials.empty())
                    ? Destructible->Collection->Materials
                    : SourceMesh->Materials;

            // Pre-baked collections cache piece meshes (built at load), so fracture does no per-piece
            // meshlet build / upload. The on-the-fly Voronoi path has no cache and builds each inline.
            const TVector<TObjectPtr<CStaticMesh>>* CachedMeshes =
                CollectionData ? &Destructible->Collection->GetPieceMeshes() : nullptr;

            for (size_t PieceIndex = 0; PieceIndex < Pieces.size() && (uint32)Spawned < MaxFragments; ++PieceIndex)
            {
                const FFracturePiece& Piece = Pieces[PieceIndex];

                CStaticMesh* PieceMesh = CachedMeshes
                    ? (PieceIndex < CachedMeshes->size() ? (*CachedMeshes)[PieceIndex].Get() : nullptr)
                    : Fracture::BuildPieceMesh(Piece, PieceMaterials, "GCPiece");
                if (PieceMesh == nullptr)
                {
                    continue;
                }

                // BuildPieceMesh recenters geometry to the piece centroid (natural pivot + CoM); place
                // the entity at the centroid's world position so pieces reconstruct the object at t=0.
                const FVector3 WorldCenter = OwnerTransform.Location + OwnerTransform.Rotation * (OwnerTransform.Scale * Piece.Center);
                FTransform PieceTransform;
                PieceTransform.Location = WorldCenter;
                PieceTransform.Rotation = OwnerTransform.Rotation;
                PieceTransform.Scale    = OwnerTransform.Scale;

                const entt::entity Fragment = ConstructEntity("Fragment", PieceTransform);
                EntityRegistry.emplace_or_replace<FNeedsTransformUpdate>(Fragment);

                EntityRegistry.emplace<SStaticMeshComponent>(Fragment).StaticMesh = PieceMesh;

                // The collider's on_construct builds the Jolt shape synchronously, so Mesh + bConvex must
                // be set before insertion -- otherwise the body uses default (non-convex) settings, forced Static.
                SMeshColliderComponent ColliderDesc;
                ColliderDesc.Mesh    = PieceMesh;
                ColliderDesc.bConvex = true;
                EntityRegistry.emplace<SMeshColliderComponent>(Fragment, std::move(ColliderDesc));

                EntityRegistry.emplace<SLifetimeComponent>(Fragment).Lifetime = Destructible->FragmentLifetime;
                EntityRegistry.emplace<SFragmentComponent>(Fragment).Source   = entt::to_integral(Entity);
                
                PendingLaunches.push_back({ Fragment, WorldCenter, entt::to_integral(Fragment) + static_cast<uint32>(Spawned) });

                ++Spawned;
            }
        }
        else
        {
            // Fallback (degenerate fracture): subdivide the bounds into a grid of textured box chunks.
            const FAABB& LocalBounds = SourceMesh->GetAABB();
            const FVector3 LocalExtent = Math::Max(LocalBounds.GetSize(), FVector3(0.01f));
            const FVector3 LocalCenter = LocalBounds.GetCenter();
            const int32 Target = Math::Clamp(Destructible->FragmentCount, 2, 512);
            const int32 Dims   = Math::Max(1, static_cast<int32>(std::ceil(std::cbrt(static_cast<float>(Target)))));
            const FVector3 LocalCell = LocalExtent / static_cast<float>(Dims);
            const FVector3 FragScale = OwnerScale / static_cast<float>(Dims);
            const FVector3 ColliderHalf = LocalExtent * 0.5f;
            CStaticMesh* GridMesh = Destructible->FragmentMesh.Get() ? Destructible->FragmentMesh.Get() : SourceMesh;

            for (int32 zi = 0; zi < Dims && Spawned < Target && (uint32)Spawned < MaxFragments; ++zi)
            for (int32 yi = 0; yi < Dims && Spawned < Target && (uint32)Spawned < MaxFragments; ++yi)
            for (int32 xi = 0; xi < Dims && Spawned < Target && (uint32)Spawned < MaxFragments; ++xi)
            {
                const FVector3 CellLocalCenter = LocalBounds.Min + (FVector3(xi, yi, zi) + 0.5f) * LocalCell;
                const FVector3 CellWorldCenter = OwnerTransform.Location + OwnerTransform.Rotation * (OwnerScale * CellLocalCenter);

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

                // Box collider auto-emplaces a Dynamic rigid body, built synchronously from these
                // settings the instant the component is inserted, so set HalfExtent up front.
                SBoxColliderComponent BoxDesc;
                BoxDesc.HalfExtent = ColliderHalf;
                EntityRegistry.emplace<SBoxColliderComponent>(Fragment, std::move(BoxDesc));

                EntityRegistry.emplace<SLifetimeComponent>(Fragment).Lifetime = Destructible->FragmentLifetime;
                EntityRegistry.emplace<SFragmentComponent>(Fragment).Source   = entt::to_integral(Entity);

                PendingLaunches.push_back({ Fragment, CellWorldCenter, entt::to_integral(Fragment) + static_cast<uint32>(Spawned) });

                ++Spawned;
            }
        }

        // Insert all the queued bodies at once, then apply the launch impulses now that BodyIDs exist.
        if (PhysicsScene)
        {
            PhysicsScene->EndBodyBatch();
        }
        for (const FPendingLaunch& Launch : PendingLaunches)
        {
            LaunchBody(EntityRegistry.get<SRigidBodyComponent>(Launch.Fragment).BodyID, Launch.Center, Launch.Seed);
        }

        Destructible->bFractured = true;

        // Retire the original: strip render + physics now so it vanishes this frame; the lifetime system
        // reaps it at FrameEnd -- safe even when called from the entity's own script callback.
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
        return SpawnPrefabAt(Path, FTransform(), entt::null);
    }

    entt::entity CWorld::SpawnPrefabAt(const FName& Path, const FTransform& SpawnTransform, entt::entity Parent)
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

        return Prefab->Instantiate(this, SpawnTransform, Parent);
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
                return;
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

    FVector3 CWorld::GetEntityLocation(entt::entity Entity)
    {
        return GetEntityTransform(Entity).GetWorldLocation();
    }

    void CWorld::SetEntityLocation(entt::entity Entity, FVector3 Location)
    {
        GetEntityTransform(Entity).SetLocation(Location);
    }

    void CWorld::SetEntityRotation(entt::entity Entity, FQuat Rotation)
    {
        GetEntityTransform(Entity).SetRotation(Rotation);
    }

    FVector3 CWorld::TranslateEntity(entt::entity Entity, FVector3 Translation)
    {
        return GetEntityTransform(Entity).Translate(Translation);
    }

    uint32 CWorld::GetNumEntities() const
    {
        return (uint32)EntityRegistry.view<entt::entity>().size();
    }

    void CWorld::SetActiveCamera(entt::entity InEntity) const
    {
        SetActiveCamera(InEntity, 0.0f);
    }

    void CWorld::SetActiveCamera(entt::entity InEntity, float BlendTime, ECameraBlendFunction Function) const
    {
        if (!EntityRegistry.valid(InEntity))
        {
            return;
        }

        if (EntityRegistry.all_of<SCameraComponent>(InEntity))
        {
            SCameraSystem::SetActiveCamera(const_cast<FEntityRegistry&>(EntityRegistry), InEntity, BlendTime, Function);
        }
    }

    SCameraComponent* CWorld::GetActiveCamera() const
    {
        return SCameraSystem::GetActiveCamera(const_cast<FEntityRegistry&>(EntityRegistry));
    }

    entt::entity CWorld::GetActiveCameraEntity() const
    {
        return SCameraSystem::GetActiveCameraEntity(const_cast<FEntityRegistry&>(EntityRegistry));
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
        
        // Inherit the source world's name so logs/profilers identify
        // "NewWorld" rather than the auto-generated "CWorld_1".
        CWorld* PIEWorld = NewObject<CWorld>(/*Package*/ nullptr, OwningWorld->GetName(), FGuid::New(), OF_Transient);

        PIEWorld->PreLoad();
        PIEWorld->Serialize(ReaderProxy);
        PIEWorld->PostLoad();

        return PIEWorld;
    }

    const TVector<CWorld::FScheduledSystem>& CWorld::GetSystemsForUpdateStage(EUpdateStage Stage)
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

    void CWorld::OnInputComponentConstruct(entt::registry& Registry, entt::entity Entity)
    {
        // Bind the component to this world so its input queries resolve to this world's viewport.
        Registry.get<SInputComponent>(Entity).World = this;
    }

    void CWorld::OnInputComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        // Release every action callback this entity registered so its captured Lua FRefs don't keep
        // the script env alive (and can't fire after the entity is gone).
        SInputComponent& Input = Registry.get<SInputComponent>(Entity);
        if (Input.ActionCallbackIds.empty())
        {
            return;
        }
        if (FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport())
        {
            for (uint64 Id : Input.ActionCallbackIds)
            {
                V->GetContext().UnregisterActionCallback(Id);
            }
        }
        Input.ActionCallbackIds.clear();
    }

    void CWorld::OnScriptComponentConstruct(entt::registry& Registry, entt::entity Entity)
    {
        SScriptComponent& ScriptComponent = Registry.get<SScriptComponent>(Entity);
        OnScriptComponentCreated(Entity, ScriptComponent, true);
    }

    void CWorld::OnScriptComponentUpdated(entt::registry& Registry, entt::entity Entity)
    {
        // Fired by entt on_update when the component is patched -- on a client this is the replication "OnRep"
        // for a received SScriptComponent (NetReplication patches each applied component). Reattach only when
        // the ScriptPath actually changed, so a PropertyUpdate that re-sends an unchanged script is a no-op.
        SScriptComponent& ScriptComponent = Registry.get<SScriptComponent>(Entity);
        const FStringView Path = ScriptComponent.ScriptPath.ResolvePath();

        // Already running exactly this script -> nothing to do.
        if (ScriptComponent.Script != nullptr &&
            FStringView(ScriptComponent.Script->Path.c_str(), ScriptComponent.Script->Path.size()) == Path)
        {
            return;
        }

        // Nothing attached and this exact path already failed to load -- don't retry every update.
        if (ScriptComponent.Script == nullptr)
        {
            if (FScriptAttachFailed* Failed = Registry.try_get<FScriptAttachFailed>(Entity))
            {
                if (FStringView(Failed->Path.c_str(), Failed->Path.size()) == Path)
                {
                    return;
                }
            }
        }

        // (Re)load from the now-current ScriptPath. Detaches the old script first if it was swapped.
        ReloadScriptForComponent(Entity, ScriptComponent);

        if (ScriptComponent.Script == nullptr && !Path.empty())
        {
            Registry.emplace_or_replace<FScriptAttachFailed>(Entity).Path.assign(Path.data(), Path.size());
            LOG_WARN("[Net] Replicated script '{}' failed to load on this peer -- marking failed (won't reload until the path changes or the entity respawns).",
                FString(Path.data(), Path.size()).c_str());
        }
        else
        {
            Registry.remove<FScriptAttachFailed>(Entity); // success (or cleared path) clears any prior failure
        }
    }

    void CWorld::SetupScriptComponent(entt::entity Entity, SScriptComponent& ScriptComponent)
    {
        ScriptComponent.Entity = Entity;
        ScriptComponent.World  = this;
        
        const FStringView ResolvedScriptPath = ScriptComponent.ScriptPath.ResolvePath();
        if (ResolvedScriptPath.empty())
        {
            return;
        }

        ScriptComponent.Script = Lua::FScriptingContext::Get().LoadUniqueScriptPath(ResolvedScriptPath);
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
        ScriptComponent.Script->Reference.RawSet("_Events",   &EntityRegistry.ctx().get<FLuaEventBus>());
        ScriptComponent.Script->Reference.RawSet("_Timers",   &EntityRegistry.ctx().get<FTimerManager>());
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

        // Install RPC dispatch wrappers over --@rpc methods (captures originals for receive-side invoke).
        Net::WrapScriptRpcs(ScriptComponent.Script.get());
        
        if (ScriptComponent.Script->ExportsSchema.IsValid())
        {
            Lua::ReconcileOverrides(
                ScriptComponent.Script->ExportsSchema,
                ScriptComponent.Script->ExportDefaults,
                ScriptComponent.PropertyOverrides.Items);

            if (lua_State* ScriptState = ScriptComponent.Script->Reference.GetState())
            {
                ScriptComponent.Script->Reference.Push();
                Lua::ApplyOverridesToScriptTable(
                    ScriptState, -1,
                    ScriptComponent.Script->ExportsSchema,
                    ScriptComponent.PropertyOverrides.Items);
                lua_pop(ScriptState, 1);
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
            ScriptComponent.Script->PublishThreadContext();
            ScriptComponent.AttachFunc.Call(ScriptComponent.Script->Reference);
        }

        if (bRunReady)
        {
            SingletonDispatcher.enqueue<FScriptComponentPendingReady>(Entity);
        }
    }

    void CWorld::OnScriptComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        // Auto-remove all event bus subscriptions owned by this entity.
        Registry.ctx().get<FLuaEventBus>().UnsubscribeEntity(Entity);
        Registry.ctx().get<FTimerManager>().ClearTimersForEntity(Entity);

        SScriptComponent& ScriptComponent = Registry.get<SScriptComponent>(Entity);
        if (ScriptComponent.Script == nullptr || !ScriptComponent.DetachFunc.IsValid())
        {
            return;
        }

        if (IsScriptActiveInWorld(Entity))
        {
            ScriptComponent.Script->PublishThreadContext();
            ScriptComponent.DetachFunc.Call(ScriptComponent.Script->Reference);
        }
    }

    void CWorld::ReloadScriptForComponent(entt::entity Entity, SScriptComponent& ScriptComponent)
    {
        if (ScriptComponent.Script != nullptr && ScriptComponent.DetachFunc.IsValid())
        {
            if (IsScriptActiveInWorld(Entity))
            {
                ScriptComponent.Script->PublishThreadContext();
                ScriptComponent.DetachFunc.Call(ScriptComponent.Script->Reference);
            }
        }

        EntityRegistry.ctx().get<FLuaEventBus>().UnsubscribeEntity(Entity);
        EntityRegistry.ctx().get<FTimerManager>().ClearTimersForEntity(Entity);

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

    void CWorld::SetEntityScript(entt::entity Entity, FStringView Path)
    {
        if (!EntityRegistry.valid(Entity))
        {
            return;
        }

        SScriptComponent& ScriptComponent = EntityRegistry.get_or_emplace<SScriptComponent>(Entity);

        // Already running this exact script -> nothing to do (avoids a needless detach/reattach).
        if (ScriptComponent.Script != nullptr &&
            FStringView(ScriptComponent.Script->Path.c_str(), ScriptComponent.Script->Path.size()) == Path)
        {
            return;
        }

        ScriptComponent.ScriptPath.SetPath(Path);
        ReloadScriptForComponent(Entity, ScriptComponent); // detaches any prior script, attaches the new one

        // On the authority, a networked entity's script change must reach clients: flag it dirty so the next
        // tick sends a PropertyUpdate carrying the new (replicated) ScriptPath, which clients swap to.
        if (NetIsServerMode(GetNetMode()) && EntityRegistry.all_of<SNetworkComponent>(Entity))
        {
            EntityRegistry.emplace_or_replace<FNetDirty>(Entity);
        }
    }

    void CWorld::OnScriptSourceReloaded(FStringView Path)
    {
        // Snapshot the matching entities BEFORE reloading: ReloadScriptForComponent synchronously calls the
        // script's Lua OnDetach/OnAttach, which can create/destroy entities or add/remove SScriptComponent --
        // mutating the pool we'd otherwise be iterating (iterator invalidation + dangling component ref).
        TVector<entt::entity> ToReload;
        auto View = EntityRegistry.view<SScriptComponent>();
        for (entt::entity Entity : View)
        {
            if (View.get<SScriptComponent>(Entity).ScriptPath.ResolvePath() == Path)
            {
                ToReload.push_back(Entity);
            }
        }

        for (entt::entity Entity : ToReload)
        {
            // A prior reload's hook may have destroyed this entity or removed its script; re-check + re-fetch.
            if (!EntityRegistry.valid(Entity))
            {
                continue;
            }
            if (SScriptComponent* Component = EntityRegistry.try_get<SScriptComponent>(Entity))
            {
                ReloadScriptForComponent(Entity, *Component);
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
                // Skip systems disabled for this world; they are never constructed, started, or ticked.
                // A stale disabled name (system since renamed/removed) simply matches nothing here.
                if (const char* Name = Meta.name(); Name && DisabledSystems.count(FName(Name)))
                {
                    continue;
                }

                FEntitySystemWrapper Wrapper;
                Wrapper.Underlying = Meta;
                Wrapper.Instance = Meta.construct();

                FSystemVariant Variant = Wrapper;
                RegisterSystem(Variant);
            }
        }
    }

    void CWorld::ApplyPendingSystemChanges()
    {
        if (!bSystemsDirty)
        {
            return;
        }
        bSystemsDirty = false;

        if (PendingDisabledSystems == DisabledSystems)
        {
            return;
        }

        // Snapshot the currently-active unique systems so we can tell which are newly removed/added.
        THashSet<uint64> BeforeHashes;
        ForEachUniqueSystem([&](const FSystemVariant& System)
        {
            BeforeHashes.insert(eastl::visit([&](const auto& Sys) { return Sys.GetHash(); }, System));
        });

        // Teardown systems about to be disabled while their wrappers are still live (before the rebuild).
        ForEachUniqueSystem([&](const FSystemVariant& System)
        {
            FName Name = eastl::visit([&](const auto& Sys) { return Sys.GetName(); }, System);
            if (!Name.IsNone() && PendingDisabledSystems.count(Name) && !DisabledSystems.count(Name))
            {
                eastl::visit([&](const auto& Sys) { Sys.Teardown(SystemContext); }, System);
            }
        });

        DisabledSystems = PendingDisabledSystems;
        RegisterSystems();

        // Startup systems that are newly present (were not active before the rebuild).
        ForEachUniqueSystem([&](const FSystemVariant& System)
        {
            const uint64 Hash = eastl::visit([&](const auto& Sys) { return Sys.GetHash(); }, System);
            if (BeforeHashes.count(Hash) == 0)
            {
                eastl::visit([&](const auto& Sys) { Sys.Startup(SystemContext); }, System);
            }
        });
    }

    void CWorld::GetAllSystems(TVector<FSystemInfo>& Out) const
    {
        Out.clear();

        for (auto&& [_, Meta] : entt::resolve())
        {
            ECS::ETraits Traits = Meta.traits<ECS::ETraits>();
            if (!EnumHasAnyFlags(Traits, ECS::ETraits::System))
            {
                continue;
            }

            const char* RawName = Meta.name();
            if (RawName == nullptr)
            {
                continue;
            }

            FSystemInfo& Info = Out.emplace_back();
            Info.Name     = FName(RawName);
            Info.bEnabled = PendingDisabledSystems.count(Info.Name) == 0;

            // Stages come from the system's reflected PriorityList (construct a throwaway wrapper to read it).
            FEntitySystemWrapper Wrapper;
            Wrapper.Underlying = Meta;
            Wrapper.Instance   = Meta.construct();
            const FUpdatePriorityList& Priorities = Wrapper.GetUpdatePriorityList();
            for (uint8 i = 0; i < (uint8)EUpdateStage::Max; ++i)
            {
                if (Priorities.IsStageEnabled((EUpdateStage)i))
                {
                    Info.Stages.push_back((EUpdateStage)i);
                }
            }
        }
    }

    bool CWorld::IsSystemEnabled(FName System) const
    {
        return PendingDisabledSystems.count(System) == 0;
    }

    void CWorld::SetSystemEnabled(FName System, bool bEnabled)
    {
        if (System.IsNone())
        {
            return;
        }

        const bool bCurrentlyEnabled = PendingDisabledSystems.count(System) == 0;
        if (bCurrentlyEnabled == bEnabled)
        {
            return;
        }

        if (bEnabled)
        {
            PendingDisabledSystems.erase(System);
        }
        else
        {
            PendingDisabledSystems.insert(System);
        }

        // Persist immediately into the world-settings component (safe to mutate; not the live system list).
        // The actual system-list rebuild is deferred to the next frame via ApplyPendingSystemChanges.
        SDefaultWorldSettings& Settings = GetDefaultWorldSettings();
        Settings.DisabledSystems.clear();
        for (const FName& Name : PendingDisabledSystems)
        {
            Settings.DisabledSystems.push_back(Name);
        }

        bSystemsDirty = true;
    }

    void CWorld::DrawBillboard(FRHIImage* Image, const FVector3& Location, float Scale)
    {
        RenderScene->DrawBillboard(Image, Location, Scale);
    }

    void CWorld::DrawLine(const FVector3& Start, const FVector3& End, const FVector4& Color, float Thickness, bool bDepthTest, float Duration)
    {
        if (IsSuspended())
        {
            return;
        }
        
        LineBatcherComponent->EnqueueLine(Start, End, Color, Thickness, bDepthTest, Duration);
    }

    void CWorld::DrawSolidTriangles(TVector<FSimpleElementVertex>&& Vertices, bool bDepthTest, float Duration)
    {
        if (IsSuspended())
        {
            return;
        }

        TriangleBatcherComponent->EnqueueTriangles(std::move(Vertices), bDepthTest, Duration);
    }

    void CWorld::DrawDebugText(const FString& Text, const FVector4& Color)
    {
#if !defined(LE_SHIPPING)
        if (IsSuspended())
        {
            return;
        }

        FDebugTextLine& Line = DebugTextLines.emplace_back();
        Line.Text  = Text;
        Line.Color = Color;
#endif
    }

    void CWorld::DrainDebugTextLines(TVector<FDebugTextLine>& Out)
    {
        Out = Move(DebugTextLines);
        DebugTextLines.clear();
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
                
                FVector3 NormalEnd = RayResult.Location + RayResult.Normal * 0.5f;
                DrawLine(RayResult.Location, NormalEnd, FColor::Blue, 3.0f,true, Settings.DebugDuration);
                
                DrawBox(RayResult.Location, FVector3(0.05f), FQuat(1.0f, 0.0f, 0.0f, 0.0f), FColor::Yellow, 3.0, true, Settings.DebugDuration);
                
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
        TVector<FScheduledSystem>& Systems = SystemUpdateList[(uint32)Context.GetUpdateStage()];
        const size_t Count = Systems.size();

        auto RunOne = [&](FScheduledSystem& S) { eastl::visit([&](auto& Sys) { Sys.Update(Context); }, S.Variant); };

        // Walk the priority-sorted list and greedily group consecutive systems that conflict with
        // NOTHING already in the current batch; flush the batch (run concurrently) on the first
        // conflict, then continue. Exclusive systems (no declared access) always run alone. A
        // conflicting/lower-priority system lands in a later batch, so priority order is preserved.
        size_t i = 0;
        while (i < Count)
        {
            size_t j = i + 1;
            if (!Systems[i].Access.bExclusive)
            {
                while (j < Count)
                {
                    bool ConflictsWithBatch = Systems[j].Access.bExclusive;
                    for (size_t k = i; !ConflictsWithBatch && k < j; ++k)
                    {
                        ConflictsWithBatch = FSystemAccess::Conflicts(Systems[k].Access, Systems[j].Access);
                    }
                    if (ConflictsWithBatch)
                    {
                        break;
                    }
                    ++j;
                }
            }

            const size_t BatchSize = j - i;
            if (BatchSize <= 1)
            {
                RunOne(Systems[i]);
            }
            else
            {
                // One system per job; the game thread assist-waits, nested ParallelFor inside a system
                // nests fine on fibers.
                Task::ParallelFor(static_cast<uint32>(BatchSize), [&](uint32 Index)
                {
                    DEBUG_ASSERT(!Systems[i + Index].Access.bExclusive); // scheduler invariant
                    RunOne(Systems[i + Index]);
                }, 1);
            }

            i = j;
        }
    }
}
