#include "pch.h"
#include "NetReplication.h"
#include "NetWorldState.h"
#include "Core/Serialization/NetArchive.h"
#include "Core/Object/Class.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Math/Hash/Hash.h"
#include "Containers/Array.h"
#include "Assets/AssetRef.h"
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

        // Live pointer to Entity's component of the given meta type, or null. Goes through entt storage
        // since meta_any has no raw-data accessor in this build.
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

        // Lazy hash to meta_type map of every reflected component. Same build gives an identical map on
        // both peers, so the hash is a stable cross-peer type id.
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
                // Can't skip an unknown component without a size prefix. Same-build peers never hit this;
                // abort the entity if they do.
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

            // Write replicated props into the live component, preserving non-replicated fields. Emplace a
            // default first if absent.
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
                Ar.SetHasError(true); // couldn't materialize, stream would desync
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

    namespace
    {
        // Get-or-assign a stable index for an object, queuing an export of its GUID the first time.
        uint32 NetObj_GetOrAssign(FNetObjectMap& Map, CObject* Obj)
        {
            if (Obj == nullptr) { return 0; }
            auto It = Map.ObjToIndex.find(Obj);
            if (It != Map.ObjToIndex.end()) { return It->second; }
            const uint32 Index = Map.NextIndex++;
            Map.ObjToIndex[Obj]    = Index;
            Map.IndexToGuid[Index] = Obj->GetGUID();
            Map.PendingExports.push_back(Index);
            return Index;
        }

        // Resolve an index to an object via the sender's exported GUID, loading on demand if not resident.
        // The result is cached (including null) so a missing asset is tried once, not per reference.
        CObject* NetObj_Resolve(FNetObjectMap& Map, uint32 Index)
        {
            if (Index == 0) { return nullptr; }
            auto Oit = Map.IndexToObject.find(Index);
            if (Oit != Map.IndexToObject.end()) { return Oit->second; } // resolved or already failed
            auto Git = Map.IndexToGuid.find(Index);
            if (Git == Map.IndexToGuid.end()) { return nullptr; }       // export not arrived yet
            CObject* Obj = FindObject<CObject>(Git->second);
            if (Obj == nullptr) { Obj = StaticLoadObject(Git->second); } // not resident, load from disk
            if (Obj == nullptr)
            {
                LOG_WARN("[Net] Replicated object index {} (GUID {}) not found and load failed -- marking failed (null).",
                    Index, Git->second.ToString().c_str());
            }
            Map.IndexToObject[Index] = Obj; // cache (incl. null) so we don't reload on every reference
            return Obj;
        }

        // Get-or-assign a stable index for an asset ref, keyed by GUID (else Path) so it dedupes.
        uint32 NetAsset_GetOrAssign(FNetAssetMap& Map, const FAssetRef& Ref)
        {
            if (Ref.IsNull()) { return 0; }
            const FString& Key = !Ref.Guid.empty() ? Ref.Guid : Ref.Path;
            auto It = Map.KeyToIndex.find(Key);
            if (It != Map.KeyToIndex.end()) { return It->second; }
            const uint32 Index = Map.NextIndex++;
            Map.KeyToIndex[Key]   = Index;
            Map.IndexToRef[Index] = Ref;
            Map.PendingExports.push_back(Index);
            return Index;
        }

        FAssetRef NetAsset_Resolve(FNetAssetMap& Map, uint32 Index)
        {
            if (Index == 0) { return FAssetRef(); }
            auto It = Map.IndexToRef.find(Index);
            return It != Map.IndexToRef.end() ? It->second : FAssetRef();
        }
    }

    void BindWriters(FNetArchive& Ar, FNetWorldState& State)
    {
        Ar.ObjectToNetIndex   = [&State](CObject* O)         { return NetObj_GetOrAssign(State.OutObjects, O); };
        Ar.AssetRefToNetIndex = [&State](const FAssetRef& R) { return NetAsset_GetOrAssign(State.OutAssets, R); };
    }

    void BindReaders(FNetArchive& Ar, FNetWorldState& State, uint32 SenderConn)
    {
        FNetObjectMap& InObj = State.InObjects[SenderConn]; // operator[] default-creates the per-connection entry
        FNetAssetMap&  InAst = State.InAssets[SenderConn];
        Ar.NetIndexToObject   = [&InObj](uint32 I) { return NetObj_Resolve(InObj, I); };
        Ar.NetIndexToAssetRef = [&InAst](uint32 I, FAssetRef& Out) { Out = NetAsset_Resolve(InAst, I); };
    }

    void BuildObjectExport(const FNetObjectMap& Map, const TVector<uint32>& Indices, TVector<uint8>& OutMsg)
    {
        FNetArchive Writer(OutMsg);
        uint8  Type  = static_cast<uint8>(ENetMessage::ObjectExport);
        uint16 Count = static_cast<uint16>(Indices.size());
        Writer << Type;
        Writer << Count;
        for (uint32 Index : Indices)
        {
            auto It = Map.IndexToGuid.find(Index);
            if (It == Map.IndexToGuid.end()) { continue; }
            FGuid Guid = It->second;
            Writer << Index;
            Writer << Guid;
        }
    }

    void ApplyObjectExport(FNetObjectMap& Map, const uint8* Data, SIZE_T Size)
    {
        FNetArchive Reader(Data, Size);
        uint8  Type  = 0;
        uint16 Count = 0;
        Reader << Type;
        Reader << Count;
        for (uint16 i = 0; i < Count; ++i)
        {
            uint32 Index = 0;
            FGuid  Guid;
            Reader << Index;
            Reader << Guid;
            if (Reader.HasError()) { break; }
            Map.IndexToGuid[Index] = Guid;
            Map.IndexToObject.erase(Index); // re-resolve lazily against the latest GUID
        }
    }

    void BuildAssetExport(const FNetAssetMap& Map, const TVector<uint32>& Indices, TVector<uint8>& OutMsg)
    {
        FNetArchive Writer(OutMsg);
        uint8  Type  = static_cast<uint8>(ENetMessage::AssetExport);
        uint16 Count = static_cast<uint16>(Indices.size());
        Writer << Type;
        Writer << Count;
        for (uint32 Index : Indices)
        {
            auto It = Map.IndexToRef.find(Index);
            if (It == Map.IndexToRef.end()) { continue; }
            FString Path = It->second.Path;
            FString Guid = It->second.Guid;
            Writer << Index;
            Writer << Path;
            Writer << Guid;
        }
    }

    void ApplyAssetExport(FNetAssetMap& Map, const uint8* Data, SIZE_T Size)
    {
        FNetArchive Reader(Data, Size);
        uint8  Type  = 0;
        uint16 Count = 0;
        Reader << Type;
        Reader << Count;
        for (uint16 i = 0; i < Count; ++i)
        {
            uint32  Index = 0;
            FString Path, Guid;
            Reader << Index;
            Reader << Path;
            Reader << Guid;
            if (Reader.HasError()) { break; }
            FAssetRef Ref;
            Ref.Path = Path;
            Ref.Guid = Guid;
            Map.IndexToRef[Index] = Ref;
        }
    }
}
