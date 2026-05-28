#pragma once

#include <entt/entt.hpp>
#include "ModuleAPI.h"
#include "GUID/GUID.h"
#include "Platform/GenericPlatform.h"

struct lua_State;

namespace Lumina
{
    class CStruct;
    class CEntityComponentType;
    class FPropertyBag;

    struct FDynamicComponentTag final {};

    // Contiguous, runtime-stride EnTT storage backing one CEntityComponentType.
    class FRuntimeComponentStorage final : public entt::basic_sparse_set<entt::entity>
    {
        using base_type = entt::basic_sparse_set<entt::entity>;

    public:

        explicit FRuntimeComponentStorage(const allocator_type& Allocator = {});
        ~FRuntimeComponentStorage() override;

        void BindLayout(CEntityComponentType* Type);
        void RefreshSchema();
        RUNTIME_API void Invalidate();

        bool IsBound() const { return SchemaType != nullptr; }
        CEntityComponentType* GetSchemaType() const { return SchemaType; }
        const FGuid& GetSchemaGuid() const { return SchemaGuid; }

        RUNTIME_API CStruct* GetLayout() const;
        RUNTIME_API static bool IsRuntimeStorage(const base_type& Set);

    private:

        const void* get_at(const std::size_t Pos) const override;
        void swap_or_move(const std::size_t From, const std::size_t To) override;
        void pop(base_type::basic_iterator First, base_type::basic_iterator Last) override;
        void pop_all() override;
        base_type::basic_iterator try_emplace(const entt::entity Entity, const bool ForceBack, const void* Value) override;
        void reserve(const size_type Capacity) override;

        uint8* SlotAt(std::size_t Pos) const { return (Stride == 0) ? nullptr : Packed + Pos * Stride; }
        void EnsureCapacity(uint32 Count);
        void ConstructFromDefaults(uint8* Slot);
        void DestructSlot(uint8* Slot);
        void MigrateTo(CEntityComponentType* Type);

        CEntityComponentType* SchemaType = nullptr;
        FGuid                 SchemaGuid;
        uint32                BoundRevision = 0;
        FPropertyBag*         LayoutBag = nullptr;

        uint8*                Packed = nullptr;
        uint32                Capacity = 0;
        uint32                Stride = 0;
        uint32                ElemAlign = 1;
    };

    RUNTIME_API void RegisterRuntimeComponentMetatable(lua_State* L);
    RUNTIME_API void RegisterRuntimeComponentTypeGlobal(lua_State* L, CEntityComponentType* Type);
    RUNTIME_API void UnregisterRuntimeComponentTypeGlobal(lua_State* L, const FGuid& TypeGuid);
    RUNTIME_API void RegisterAllRuntimeComponentTypeGlobals(lua_State* L);
    RUNTIME_API CEntityComponentType* ResolveRuntimeComponentType(uint32 StorageId);
    RUNTIME_API void PushRuntimeComponent(lua_State* L, entt::registry* Registry, entt::entity Entity, uint32 StorageId);
}

template<typename Allocator>
struct entt::storage_type<Lumina::FDynamicComponentTag, entt::entity, Allocator>
{
    using type = Lumina::FRuntimeComponentStorage;
};
