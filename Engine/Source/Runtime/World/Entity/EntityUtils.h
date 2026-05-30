#pragma once
#include "Components/RelationshipComponent.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Core/Serialization/Archiver.h"
#include "Registry/EntityRegistry.h"
#include "RuntimeComponent.h"
#include "Scripting/Lua/Reference.h"
#include "TaskSystem/TaskSystem.h"


namespace Lumina
{
	class CStruct;
	class CEntityComponentType;
	struct FTransform;
}

namespace Lumina::ECS::Utils
{
	RUNTIME_API bool SerializeEntity(FArchive& Ar, FEntityRegistry& Registry, entt::entity& Entity);
	RUNTIME_API bool SerializeRegistry(FArchive& Ar, FEntityRegistry& Registry);
	RUNTIME_API bool EntityHasTag(const FName& Tag, FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API void ReparentEntity(FEntityRegistry& Registry, entt::entity Child, entt::entity Parent);
	RUNTIME_API void DestroyEntityHierarchy(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API void DetachImmediateChildren(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API void RemoveFromParent(FEntityRegistry& Registry, entt::entity Child);
	RUNTIME_API void AddToParent(FEntityRegistry& Registry, entt::entity Child, entt::entity Parent);
	RUNTIME_API bool IsDescendantOf(FEntityRegistry& Registry, entt::entity Potential, entt::entity Ancestor);
	RUNTIME_API bool IsChild(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API bool IsParent(FEntityRegistry& Registry, entt::entity Entity);

	// Immediate parent, or entt::null when the entity is a root / has no relationship component.
	RUNTIME_API entt::entity GetParent(FEntityRegistry& Registry, entt::entity Entity);

	// Walk up the parent chain to the topmost ancestor; returns Entity itself when it has no parent.
	RUNTIME_API entt::entity GetRootEntity(FEntityRegistry& Registry, entt::entity Entity);

	// Immediate parent up to the root, nearest first. Does not include Entity.
	RUNTIME_API void CollectAncestors(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutAncestors);

	// First direct child whose SNameComponent matches Name, or entt::null. Scoped to Parent's children.
	RUNTIME_API entt::entity FindChildByName(FEntityRegistry& Registry, entt::entity Parent, const FName& Name);

	// As FindChildByName but searches the whole subtree depth-first (pre-order, nearest first).
	RUNTIME_API entt::entity FindDescendantByName(FEntityRegistry& Registry, entt::entity Parent, const FName& Name);

	// Next/previous entry in the parent's sibling list, or entt::null at the ends / when parentless.
	RUNTIME_API entt::entity GetNextSibling(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API entt::entity GetPrevSibling(FEntityRegistry& Registry, entt::entity Entity);

	// Other children of this entity's parent, excluding Entity itself. Empty when parentless.
	RUNTIME_API void CollectSiblings(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutSiblings);
	RUNTIME_API size_t GetChildCount(FEntityRegistry& Registry, entt::entity Parent);
	RUNTIME_API size_t GetSiblingIndex(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API void CollectDescendants(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutDescendants);
	RUNTIME_API void CollectChildren(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutChildren);
	RUNTIME_API bool HasComponent(FEntityRegistry& Registry, entt::entity Entity, entt::meta_type Type);
	RUNTIME_API void ResolveTransformChain(FEntityRegistry& Registry, entt::entity Entity);

	// Runtime (data-authored) components: instances of a CEntityComponentType, stored contiguously per type in FRuntimeComponentStorage.

	// Returns the storage for Type, creating + binding it (or migrating it to the current schema)
	// as needed. The returned reference is stable for the registry's lifetime.
	RUNTIME_API FRuntimeComponentStorage& GetOrCreateRuntimeStorage(FEntityRegistry& Registry, CEntityComponentType* Type);

	// Finds an existing storage for Type without creating one. Null if the type was never added.
	RUNTIME_API FRuntimeComponentStorage* FindRuntimeStorage(FEntityRegistry& Registry, CEntityComponentType* Type);

	// As above but keyed by the storage id (CEntityComponentType::GetStorageId()), so callers can
	// look a storage up without holding a (possibly dangling) type pointer.
	RUNTIME_API FRuntimeComponentStorage* FindRuntimeStorageById(FEntityRegistry& Registry, uint32 StorageId);

	// Adds (default-initialized) / removes / queries a runtime component on one entity. Add returns
	// the value buffer (null for a field-less, tag-like type); Get returns null when absent.
	RUNTIME_API void* AddRuntimeComponent(FEntityRegistry& Registry, entt::entity Entity, CEntityComponentType* Type);
	RUNTIME_API bool  RemoveRuntimeComponent(FEntityRegistry& Registry, entt::entity Entity, CEntityComponentType* Type);
	RUNTIME_API void* GetRuntimeComponent(FEntityRegistry& Registry, entt::entity Entity, CEntityComponentType* Type);
	RUNTIME_API bool  HasRuntimeComponent(FEntityRegistry& Registry, entt::entity Entity, CEntityComponentType* Type);

	// Migrates every runtime storage whose type schema has advanced (Case B fixup). Cheap when none
	// changed; called once per frame at FrameStart.
	RUNTIME_API void RefreshRuntimeComponentSchemas(FEntityRegistry& Registry);

	// Runs the above for every loaded world. Call right after editing a component-type schema so the
	// change propagates immediately (and deterministically) rather than waiting for a world's sweep.
	RUNTIME_API void RefreshAllWorldsRuntimeComponentSchemas();

	// Visits each runtime component on Entity: Func(CEntityComponentType*, CStruct* Layout, void* Data).
	template<typename TFunc>
	void ForEachRuntimeComponent(FEntityRegistry& Registry, entt::entity Entity, TFunc&& Func)
	{
		for (auto&& [Id, Set] : Registry.storage())
		{
			if (FRuntimeComponentStorage::IsRuntimeStorage(Set) && Set.contains(Entity))
			{
				auto& Storage = static_cast<FRuntimeComponentStorage&>(Set);
				eastl::invoke(Func, Storage.GetSchemaType(), Storage.GetLayout(), Set.value(Entity));
			}
		}
	}

	// Bulk-resolve every FNeedsTransformUpdate entity + descendants, then clear the pool (O(dirty)). Lets a
	// system read WorldTransform lock-free in a ParallelFor after one main-thread call. Main thread only.
	RUNTIME_API void ResolveAllDirtyTransforms(FEntityRegistry& Registry);

	// Single mutex guarding the FNeedsTransformUpdate pool + cached matrices during a chain resolve;
	// taken by ResolveTransformChain and MarkDirty. Off-main emplace<FNeedsTransformUpdate> must take it too.
	RUNTIME_API FRecursiveMutex& GetTransformResolveMutex();

	// Tag a transform dirty: always sets FNeedsTransformUpdate; sets FNeedsPhysicsBodyUpdate only when the
	// entity owns a body, so non-physics entities (cameras, lights) don't churn the physics-sync pool.
	RUNTIME_API void MarkTransformDirty(FEntityRegistry& Registry, entt::entity Entity);
	
	RUNTIME_API FVector3 GetEntityLocation(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API FQuat GetEntityRotation(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API FVector3 GetEntityScale(FEntityRegistry& Registry, entt::entity Entity);

	RUNTIME_API void SetEntityLocation(FEntityRegistry& Registry, entt::entity Entity, const FVector3& Location);
	RUNTIME_API void SetEntityRotation(FEntityRegistry& Registry, entt::entity Entity, const FQuat& Rotation);
	RUNTIME_API void SetEntityScale(FEntityRegistry& Registry, entt::entity Entity, const FVector3& Scale);
	
	RUNTIME_API bool IsEntityValid(FEntityRegistry& Registry, entt::entity Entity);
	
	RUNTIME_API FVector3 TranslateEntity(FEntityRegistry& Registry, entt::entity Entity, const FVector3& Translation);
	
	RUNTIME_API entt::entity DuplicateEntity(FEntityRegistry& Registry, entt::entity Entity);

	// Remap every reflected "Entity"-tagged uint32 field (nested structs too) through Map. FRelationshipComponent
	// links are NOT touched here. Unmapped ids stay put when bClearUnmapped is false, else reset to null.
	RUNTIME_API void RemapEntityReferences(FEntityRegistry& Registry, entt::entity Entity, const THashMap<entt::entity, entt::entity>& Map, bool bClearUnmapped);

	// Set an entity's world transform by converting to the parent-relative local that resolves back to it.
	// The single source of truth for world->local conversion; writing WorldTransform directly is discarded next resolve.
	RUNTIME_API void SetEntityWorldTransform(FEntityRegistry& Registry, entt::entity Entity, const FTransform& WorldTransform);

	RUNTIME_API FVector3 GetDirectionVector(FEntityRegistry& Registry, entt::entity To, entt::entity From);
	
	RUNTIME_API void DestroyEntity(FEntityRegistry& Registry, entt::entity Entity);

	NODISCARD RUNTIME_API entt::id_type GetTypeID(FStringView Name);
	NODISCARD RUNTIME_API entt::id_type GetTypeID(const CStruct* Type);
	NODISCARD RUNTIME_API entt::id_type GetTypeID(const Lua::FRef& Obj);

	
	template<typename... Ts, typename TFunc, typename... TArgs>
	void ParallelForEach(FEntityRegistry& Registry, TFunc&& Function, TArgs&&... Args)
	{
		auto View = Registry.view<Ts...>(eastl::forward<TArgs>(Args)...);
		const auto* Entities = View.handle();

		Task::ParallelFor((uint32)Entities->size(), [&](uint32 Index)
		{
			entt::entity EntityID = (*Entities)[Index];

			if (View.contains(EntityID))
			{
				std::apply([&](auto&... Components)
				{
					Function(EntityID, Components...);
				}, View.get(EntityID));
			}
		});
	}

	template<typename ... TArgs>
	entt::meta_any InvokeMetaFunc(const entt::meta_type& MetaType, entt::id_type FunctionID, TArgs&&... Args)
	{
		if (!MetaType)
		{
			return entt::meta_any{};
		}

		auto&& F = MetaType.func(FunctionID);
		if (!F)
		{
			return entt::meta_any{};
		}

		return F.invoke({}, eastl::forward<TArgs>(Args)...);
	}

	template<typename ... TArgs>
	auto InvokeMetaFunc(const entt::id_type& TypeID, entt::id_type FunctionID, TArgs&&... Args)
	{
		return InvokeMetaFunc(entt::resolve(TypeID), FunctionID, Forward<TArgs>(Args)...);
	}

	template<typename TFunc>
	requires(eastl::is_invocable_v<TFunc, void*, entt::basic_sparse_set<>&, entt::meta_type>)
	void ForEachComponent(FEntityRegistry& Registry, entt::entity Entity, TFunc&& Func)
	{
		for (auto&& [ID, Storage] : Registry.storage())
		{
			if (Storage.contains(Entity))
			{
				if (entt::meta_type MetaType = entt::resolve(Storage.info()))
				{
					eastl::invoke(Func, Storage.value(Entity), Storage, MetaType);
				}
			}
		}
	}

	template<typename TFunc>
	void ForEachChild(entt::registry& Registry, entt::entity Parent, TFunc&& Func)
	{
		FRelationshipComponent* ParentRelationship = Registry.try_get<FRelationshipComponent>(Parent);
		if (!ParentRelationship || ParentRelationship->First == entt::null)
		{
			return;
		}

		entt::entity Current = ParentRelationship->First;
		while (Current != entt::null)
		{
			entt::entity Next = entt::null;
			if (FRelationshipComponent* CurrentRelationship = Registry.try_get<FRelationshipComponent>(Current))
			{
				Next = CurrentRelationship->Next;
			}

			eastl::invoke(Func, Current);

			Current = Next;
		}
	}

	template<typename TFunc>
	void ForEachDescendant(entt::registry& Registry, entt::entity Parent, TFunc&& Func)
	{
		ForEachChild(Registry, Parent, [&](entt::entity Child)
		{
			eastl::invoke(Func, Child);
			ForEachDescendant(Registry, Child, Func);
		});
	}

	template<typename TFunc>
	void ForEachDescendantReverse(entt::registry& Registry, entt::entity Parent, TFunc&& Func)
	{
		ForEachChild(Registry, Parent, [&](entt::entity Child)
		{
			ForEachDescendantReverse(Registry, Child, Func);
			eastl::invoke(Func, Child);
		});
	}

	template<typename TFunc>
	void ForEachAncestor(entt::registry& Registry, entt::entity Entity, TFunc&& Func)
	{
		FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity);
		while (Relationship && Relationship->Parent != entt::null)
		{
			eastl::invoke(Func, Relationship->Parent);
			Relationship = Registry.try_get<FRelationshipComponent>(Relationship->Parent);
		}
	}

	inline FArchive& operator << (FArchive& Ar, entt::entity& Entity)
	{
		entt::id_type UintEntity = entt::to_integral(Entity);
		Ar << UintEntity;
		Entity = static_cast<entt::entity>(UintEntity);
		return Ar;
	}
}
