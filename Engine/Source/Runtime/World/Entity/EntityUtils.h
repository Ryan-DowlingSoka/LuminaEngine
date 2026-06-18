#pragma once
#include <atomic>
#include "Components/RelationshipComponent.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Core/Serialization/Archiver.h"
#include "Registry/EntityRegistry.h"
#include "TaskSystem/TaskSystem.h"


namespace Lumina
{
	class CStruct;
	struct FTransform;
}

namespace Lumina::ECS::Utils
{
	//-------------------------------------------------------------------------
	// Serialization
	//-------------------------------------------------------------------------

	RUNTIME_API bool SerializeEntity(FArchive& Ar, FEntityRegistry& Registry, entt::entity& Entity);
	RUNTIME_API bool SerializeRegistry(FArchive& Ar, FEntityRegistry& Registry);

	//-------------------------------------------------------------------------
	// Hierarchy
	//-------------------------------------------------------------------------

	RUNTIME_API void ReparentEntity(FEntityRegistry& Registry, entt::entity Child, entt::entity Parent, bool bPreserveWorld = true);
	RUNTIME_API void AddToParent(FEntityRegistry& Registry, entt::entity Child, entt::entity Parent);
	RUNTIME_API void RemoveFromParent(FEntityRegistry& Registry, entt::entity Child);
	RUNTIME_API bool IsDescendantOf(FEntityRegistry& Registry, entt::entity Potential, entt::entity Ancestor);
	RUNTIME_API bool IsChild(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API bool IsParent(FEntityRegistry& Registry, entt::entity Entity);

	// Walk up the parent chain to the topmost ancestor; returns Entity itself when it has no parent.
	RUNTIME_API entt::entity GetRootEntity(FEntityRegistry& Registry, entt::entity Entity);

	RUNTIME_API size_t GetChildCount(FEntityRegistry& Registry, entt::entity Parent);
	RUNTIME_API void CollectChildren(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutChildren);
	RUNTIME_API void CollectDescendants(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutDescendants);

	//-------------------------------------------------------------------------
	// Lifetime
	//-------------------------------------------------------------------------

	RUNTIME_API void DestroyEntity(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API void DestroyEntityHierarchy(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API void DetachImmediateChildren(FEntityRegistry& Registry, entt::entity Entity);

	//-------------------------------------------------------------------------
	// Transform resolve
	//-------------------------------------------------------------------------

	RUNTIME_API void ResolveTransformChain(FEntityRegistry& Registry, entt::entity Entity);

	// Resolve every dirty transform + descendants. Scans the per-component bWorldDirty flag (gated by the
	// signal), so writers never touch a shared pool. Hierarchy roots resolved top-down in parallel. Main thread.
	RUNTIME_API void ResolveAllDirtyTransforms(FEntityRegistry& Registry);

	// The registry-wide dirty signal, cached on each STransformComponent at Bind so setters raise it with one
	// relaxed store and no registry lookup. Creates the state + bridge hook.
	RUNTIME_API std::atomic<bool>* EnsureTransformDirtySignal(FEntityRegistry& Registry);

	// Tag the entity's body (if any) for the physics sync. Single-threaded.
	RUNTIME_API void MarkPhysicsBodyDirtyIfBodied(FEntityRegistry& Registry, entt::entity Entity);

	// External (non-setter) dirtying; the bridge hook converts the tag to the component flag. Single-threaded.
	// Per-frame gameplay uses the component setters instead (ParallelFor-safe).
	RUNTIME_API void MarkTransformDirty(FEntityRegistry& Registry, entt::entity Entity);

	//-------------------------------------------------------------------------
	// Entity transform accessors
	//-------------------------------------------------------------------------

	RUNTIME_API FQuat    GetEntityRotation(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API FVector3 GetEntityScale(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API void     SetEntityScale(FEntityRegistry& Registry, entt::entity Entity, const FVector3& Scale);

	// Set an entity's world transform by converting to the parent-relative local that resolves back to it.
	// The single source of truth for world->local conversion; writing WorldTransform directly is discarded next resolve.
	RUNTIME_API void SetEntityWorldTransform(FEntityRegistry& Registry, entt::entity Entity, const FTransform& WorldTransform);

	//-------------------------------------------------------------------------
	// Reflection / queries
	//-------------------------------------------------------------------------

	RUNTIME_API bool EntityHasTag(const FName& Tag, FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API bool HasComponent(FEntityRegistry& Registry, entt::entity Entity, entt::meta_type Type);

	// Remap every reflected "Entity"-tagged uint32 field (nested structs too) through Map. FRelationshipComponent
	// links are NOT touched here. Unmapped ids stay put when bClearUnmapped is false, else reset to null.
	RUNTIME_API void RemapEntityReferences(FEntityRegistry& Registry, entt::entity Entity, const THashMap<entt::entity, entt::entity>& Map, bool bClearUnmapped);

	NODISCARD RUNTIME_API entt::id_type GetTypeID(FStringView Name);
	NODISCARD RUNTIME_API entt::id_type GetTypeID(const CStruct* Type);

	//-------------------------------------------------------------------------
	// Templates: meta invoke + component / hierarchy iteration
	//-------------------------------------------------------------------------

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

	inline FArchive& operator << (FArchive& Ar, entt::entity& Entity)
	{
		entt::id_type UintEntity = entt::to_integral(Entity);
		Ar << UintEntity;
		Entity = static_cast<entt::entity>(UintEntity);
		return Ar;
	}
}
