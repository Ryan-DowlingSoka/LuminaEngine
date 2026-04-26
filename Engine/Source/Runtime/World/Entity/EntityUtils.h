#pragma once
#include "Components/RelationshipComponent.h"
#include "Core/Serialization/Archiver.h"
#include "Registry/EntityRegistry.h"
#include "Scripting/Lua/Reference.h"
#include "TaskSystem/TaskSystem.h"


namespace Lumina
{
	class CStruct;
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
	RUNTIME_API size_t GetChildCount(FEntityRegistry& Registry, entt::entity Parent);
	RUNTIME_API size_t GetSiblingIndex(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API void CollectDescendants(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutDescendants);
	RUNTIME_API void CollectChildren(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutChildren);
	RUNTIME_API bool HasComponent(FEntityRegistry& Registry, entt::entity Entity, entt::meta_type Type);
	RUNTIME_API void ResolveTransformChain(FEntityRegistry& Registry, entt::entity Entity);
	
	RUNTIME_API glm::vec3 GetEntityLocation(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API glm::quat GetEntityRotation(FEntityRegistry& Registry, entt::entity Entity);
	RUNTIME_API glm::vec3 GetEntityScale(FEntityRegistry& Registry, entt::entity Entity);

	RUNTIME_API void SetEntityLocation(FEntityRegistry& Registry, entt::entity Entity, const glm::vec3& Location);
	RUNTIME_API void SetEntityRotation(FEntityRegistry& Registry, entt::entity Entity, const glm::quat& Rotation);
	RUNTIME_API void SetEntityScale(FEntityRegistry& Registry, entt::entity Entity, const glm::vec3& Scale);
	
	RUNTIME_API bool IsEntityValid(FEntityRegistry& Registry, entt::entity Entity);
	
	RUNTIME_API glm::vec3 TranslateEntity(FEntityRegistry& Registry, entt::entity Entity, const glm::vec3& Translation);
	
	RUNTIME_API entt::entity DuplicateEntity(FEntityRegistry& Registry, entt::entity Entity);

	RUNTIME_API glm::vec3 GetDirectionVector(FEntityRegistry& Registry, entt::entity To, entt::entity From);
	
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
