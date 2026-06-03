#include "pch.h"
#include "NetReplication.h"
#include "Core/Serialization/NetArchive.h"
#include "Core/Object/Class.h"
#include "Core/Math/Hash/Hash.h"
#include "Containers/Array.h"
#include "World/Entity/EntityUtils.h"
#include "Networking/INetworkTransport.h"
#include "Log/Log.h"
#include "entt/entt.hpp"

namespace Lumina::Net
{
    namespace
    {
        uint32 HashStructName(const FName& Name)
        {
            return Hash::GetHash32(Name.c_str());
        }

        CStruct* StructFromMeta(const entt::meta_type& Type)
        {
            entt::meta_any S = ECS::Utils::InvokeMetaFunc(Type, entt::hashed_string("static_struct"));
            return S ? S.cast<CStruct*>() : nullptr;
        }

        // Live pointer to Entity's component of the given meta type (or null). meta_any has no raw-data
        // accessor in this entt build, so we go through the entt storage that backs the type.
        void* FindComponentPtr(entt::registry& Registry, entt::entity Entity, const entt::meta_type& Type)
        {
            for (auto [Id, Set] : Registry.storage())
            {
                if (Set.contains(Entity) && entt::resolve(Set.info()) == Type)
                {
                    return Set.value(Entity);
                }
            }
            return nullptr;
        }

        // Lazy 32-bit-hash -> meta_type map of every reflected component (those exposing static_struct).
        // Same build -> identical map on both peers, so the hash is a stable cross-peer type id.
        const THashMap<uint32, entt::meta_type>& ComponentTypeMap()
        {
            static const THashMap<uint32, entt::meta_type> Map = []
            {
                THashMap<uint32, entt::meta_type> M;
                for (auto&& [Id, Type] : entt::resolve())
                {
                    CStruct* St = StructFromMeta(Type);
                    if (St != nullptr)
                    {
                        M[HashStructName(St->GetName())] = Type;
                    }
                }
                return M;
            }();
            return Map;
        }
    }

    void WriteEntityComponents(FNetArchive& Ar, entt::registry& Registry, entt::entity Entity)
    {
        struct FComp { uint32 Hash; void* Ptr; CStruct* Struct; };
        TVector<FComp> Comps;

        for (auto [Id, Set] : Registry.storage())
        {
            if (!Set.contains(Entity))
            {
                continue;
            }
            entt::meta_type Type = entt::resolve(Set.info());
            if (!Type)
            {
                continue;
            }
            CStruct* St = StructFromMeta(Type);
            if (St == nullptr)
            {
                continue;
            }
            Comps.push_back({ HashStructName(St->GetName()), Set.value(Entity), St });
        }

        uint16 Count = static_cast<uint16>(Comps.size());
        Ar << Count;
        for (FComp& C : Comps)
        {
            Ar << C.Hash;
            C.Struct->NetSerializeProperties(Ar, C.Ptr);
        }
    }

    void ReadEntityComponents(FNetArchive& Ar, entt::registry& Registry, entt::entity Entity)
    {
        uint16 Count = 0;
        Ar << Count;

        const THashMap<uint32, entt::meta_type>& Map = ComponentTypeMap();
        for (uint16 i = 0; i < Count; ++i)
        {
            uint32 Hash = 0;
            Ar << Hash;
            if (Ar.HasError())
            {
                return;
            }

            auto It = Map.find(Hash);
            if (It == Map.end())
            {
                // No size prefix -> can't skip an unknown component. Same-build peers never hit this; if
                // they do the stream is desynced, so abort this entity.
                LOG_WARN("[Net] Replication: unknown component type hash {} -- aborting.", Hash);
                Ar.SetHasError(true);
                return;
            }

            entt::meta_type Type = It->second;
            CStruct* St = StructFromMeta(Type);
            if (St == nullptr)
            {
                Ar.SetHasError(true);
                return;
            }

            // Write replicated props into the LIVE component (preserving non-replicated fields on update);
            // if absent, emplace a default first. emplace meta func = emplace_or_replace.
            void* Ptr = FindComponentPtr(Registry, Entity, Type);
            if (Ptr == nullptr)
            {
                entt::meta_any Default{};
                ECS::Utils::InvokeMetaFunc(Type, entt::hashed_string("emplace"), entt::forward_as_meta(Registry), Entity, entt::forward_as_meta(Default));
                Ptr = FindComponentPtr(Registry, Entity, Type);
            }

            if (Ptr != nullptr)
            {
                St->NetSerializeProperties(Ar, Ptr);
            }
            else
            {
                Ar.SetHasError(true); // couldn't materialize -> stream would desync; stop.
                return;
            }
        }
    }

    void AppendFramedMessage(TVector<uint8>& Batch, const uint8* Msg, SIZE_T MsgSize)
    {
        if (MsgSize == 0)
        {
            return;
        }
        if (MsgSize > 0xFFFF)
        {
            LOG_WARN("[Net] Message of {} bytes exceeds the 64K frame limit -- dropped.", (uint64)MsgSize);
            return;
        }
        const uint16 Len = static_cast<uint16>(MsgSize);
        Batch.push_back(static_cast<uint8>(Len & 0xFF));
        Batch.push_back(static_cast<uint8>((Len >> 8) & 0xFF));
        Batch.insert(Batch.end(), Msg, Msg + MsgSize);
    }

    void SendFramed(INetworkTransport& Transport, FConnectionHandle Connection, const uint8* Msg, SIZE_T MsgSize, uint8 Channel, ESendMode Mode)
    {
        TVector<uint8> Framed;
        AppendFramedMessage(Framed, Msg, MsgSize);
        Transport.Send(Connection, Framed.data(), static_cast<SIZE_T>(Framed.size()), Channel, Mode);
    }

    void BroadcastFramed(INetworkTransport& Transport, const uint8* Msg, SIZE_T MsgSize, uint8 Channel, ESendMode Mode)
    {
        TVector<uint8> Framed;
        AppendFramedMessage(Framed, Msg, MsgSize);
        Transport.Broadcast(Framed.data(), static_cast<SIZE_T>(Framed.size()), Channel, Mode);
    }

    void ForEachFramedMessage(const uint8* Data, SIZE_T Size, const TFunction<void(const uint8*, SIZE_T)>& Fn)
    {
        SIZE_T Offset = 0;
        while (Offset + 2 <= Size)
        {
            const uint16 Len = static_cast<uint16>(Data[Offset]) | (static_cast<uint16>(Data[Offset + 1]) << 8);
            Offset += 2;
            if (Offset + Len > Size)
            {
                break; // truncated / corrupt
            }
            Fn(Data + Offset, static_cast<SIZE_T>(Len));
            Offset += Len;
        }
    }
}
