#include "pch.h"
#include "NetReplication.h"
#include "NetWorldState.h"
#include "NetRpc.h"
#include "ScriptRepState.h"
#include "Core/Serialization/NetArchive.h"
#include "Core/Object/Class.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Math/Hash/Hash.h"
#include "Containers/Array.h"
#include "Assets/AssetRef.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/ScriptComponent.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/Entity/Components/NetworkComponent.h"
#include "Scripting/Lua/ScriptTypes.h"
#include "Scripting/Lua/Scripting.h"
#include "Networking/INetworkTransport.h"
#include "Log/Log.h"
#include "entt/entt.hpp"
#include "EASTL/sort.h"
#include "lua.h"
#include "lualib.h"

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

        // Live pointer to Entity's component of the given meta type, or null. O(1): the component's storage
        // is keyed by its type-info hash (the inverse of entt::resolve(Set.info())), so resolve it directly
        // instead of scanning every storage.
        void* FindComponentPtr(entt::registry& Registry, entt::entity Entity, const entt::meta_type& Type)
        {
            if (auto* Set = Registry.storage(Type.info().hash()))
            {
                if (Set->contains(Entity))
                {
                    return Set->value(Entity);
                }
            }
            return nullptr;
        }

        struct FReplType
        {
            uint32          Hash;
            entt::meta_type Type;
            CStruct*        Struct;
        };

        // Every reflected component, sorted by name-hash so the array index is a stable cross-peer type id.
        // Same build => identical set + order on both peers, so we send a 1-byte varint index on the wire
        // instead of the 4-byte hash. ByIndex[i] is the type; HashToIndex maps a local component to its index.
        struct FReplTypeTable
        {
            TVector<FReplType>      ByIndex;
            THashMap<uint32, uint32> HashToIndex;
        };

        const FReplTypeTable& ReplTypes()
        {
            static const FReplTypeTable Table = []
            {
                FReplTypeTable T;
                for (auto&& [Id, Type] : entt::resolve())
                {
                    if (CStruct* St = StructFromMeta(Type))
                    {
                        T.ByIndex.push_back({ HashStructName(St->GetName()), Type, St });
                    }
                }
                eastl::sort(T.ByIndex.begin(), T.ByIndex.end(),
                    [](const FReplType& A, const FReplType& B) { return A.Hash < B.Hash; });
                for (uint32 i = 0; i < static_cast<uint32>(T.ByIndex.size()); ++i)
                {
                    T.HashToIndex[T.ByIndex[i].Hash] = i;
                }
                return T;
            }();
            return Table;
        }

        // Client, apply the script-rep block written by WriteEntityComponents: whitelisted writes into the live
        // script table + optional OnRep_<Field>(old) hooks. Unknown/out-of-range indices (or no live script) are
        // still consumed so the stream stays aligned -- only --@replicated fields are ever written (safety).
        void ReadScriptRepBlock(FNetArchive& Ar, entt::registry& Registry, entt::entity Entity)
        {
            uint16 Count = 0;
            Ar << Count;
            if (Count == 0 || Ar.HasError())
            {
                return;
            }

            SScriptComponent* SC = Registry.valid(Entity) ? Registry.try_get<SScriptComponent>(Entity) : nullptr;
            Lua::FScript* Script = (SC && SC->Script) ? SC->Script.get() : nullptr;

            // A lua_State is needed even with no live script, to consume (discard) values and stay aligned.
            lua_State* L = Script ? Script->Reference.GetState() : Lua::FScriptingContext::Get().GetVM();
            if (L == nullptr)
            {
                Ar.SetHasError(true);
                return;
            }
            if (Script != nullptr)
            {
                Script->PublishThreadContext();
            }

            for (uint16 i = 0; i < Count && !Ar.HasError(); ++i)
            {
                const uint32 RepIndex   = ReadVarUInt(Ar);
                const bool   bApplicable = (Script != nullptr) && (RepIndex < static_cast<uint32>(Script->ReplicatedFields.size()));

                if (!bApplicable)
                {
                    Net::DeserializeLuaValue(L, Ar, 0); // consume + discard (unknown index / no live script)
                    lua_pop(L, 1);
                    continue;
                }

                const FName& FieldName = Script->ReplicatedFields[RepIndex].Name;

                Script->Reference.Push();                        // [table]
                const int TableIdx = lua_gettop(L);

                lua_rawgetfield(L, TableIdx, FieldName.c_str());  // [table][old]
                Net::DeserializeLuaValue(L, Ar, 0);         // [table][old][new]
                lua_pushvalue(L, -1);                            // [table][old][new][new]
                lua_rawsetfield(L, TableIdx, FieldName.c_str());  // [table][old][new](table[field] = new)

                // OnRep_<Field>(self, old) if the script defines it (the script analog of native on_update).
                const FString HookName = FString("OnRep_") + FString(FieldName.c_str());
                lua_rawgetfield(L, TableIdx, HookName.c_str());  // [table][old][new][hookOrNil]
                if (lua_isfunction(L, -1))
                {
                    lua_pushvalue(L, TableIdx);                  // [..][hook][self]
                    lua_pushvalue(L, TableIdx + 1);              // [..][hook][self][old]
                    if (lua_pcall(L, 2, 0, 0) != LUA_OK)         // pops hook + self + old
                    {
                        LOG_ERROR("[Net] OnRep '{}' failed: {}", HookName.c_str(), lua_tostring(L, -1));
                        lua_pop(L, 1);                           // error message
                    }
                }
                else
                {
                    lua_pop(L, 1);                               // non-function (nil)
                }

                lua_pop(L, 3); // table, old, new
            }
        }

        // Client, apply a replicated attachment: reparent Child under the entity owning ParentGuid. No-op when
        // already correct; ParentGuid 0 detaches; an unspawned parent is recorded in PendingAttach and retried
        // when it spawns (DrainPendingAttach). Keep-local reparent -- the child's local transform is replicated.
        void ApplyReplicatedParent(entt::registry& Registry, entt::entity Child, uint32 ParentGuid)
        {
            FNetWorldState* State = Registry.ctx().find<FNetWorldState>();
            if (State == nullptr || !Registry.valid(Child))
            {
                return;
            }

            uint32 CurParentGuid = 0;
            if (const FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Child); Rel && Rel->Parent != entt::null)
            {
                if (const SNetworkComponent* PNet = Registry.try_get<SNetworkComponent>(Rel->Parent))
                {
                    CurParentGuid = PNet->NetGUID.Value;
                }
            }

            const uint32 ChildKey = static_cast<uint32>(entt::to_integral(Child));
            if (CurParentGuid == ParentGuid)
            {
                State->PendingAttach.erase(ChildKey); // already attached as desired
                return;
            }

            if (ParentGuid == 0)
            {
                ECS::Utils::ReparentEntity(Registry, Child, entt::null, /*bPreserveWorld*/ false);
                State->PendingAttach.erase(ChildKey);
                return;
            }

            const entt::entity Parent = State->GuidTable.Find(FNetGUID{ ParentGuid });
            if (Parent != entt::null && Registry.valid(Parent))
            {
                ECS::Utils::ReparentEntity(Registry, Child, Parent, /*bPreserveWorld*/ false);
                State->PendingAttach.erase(ChildKey);
            }
            else
            {
                State->PendingAttach[ChildKey] = ParentGuid; // parent not spawned yet -- retry on its spawn
            }
        }
    }

    void DrainPendingAttach(entt::registry& Registry, FNetWorldState& State, uint32 NewGuid, entt::entity NewEntity)
    {
        for (auto It = State.PendingAttach.begin(); It != State.PendingAttach.end(); )
        {
            if (It->second == NewGuid)
            {
                const entt::entity Child = static_cast<entt::entity>(It->first);
                if (Registry.valid(Child))
                {
                    ECS::Utils::ReparentEntity(Registry, Child, NewEntity, /*bPreserveWorld*/ false);
                }
                It = State.PendingAttach.erase(It);
            }
            else
            {
                ++It;
            }
        }
    }

    bool ParentReplicates(entt::registry& Registry, entt::entity Parent)
    {
        if (Parent == entt::null)
        {
            return false;
        }
        const SNetworkComponent* PNet = Registry.try_get<SNetworkComponent>(Parent);
        return PNet != nullptr && PNet->bReplicates && PNet->bNetLoadOnClient && PNet->NetGUID.Value != 0;
    }

    void WriteNetGuid(FNetArchive& Ar, uint32 Guid)
    {
        const uint32 Encoded = (Guid >= NetGUID_DynamicStart)
            ? (((Guid - NetGUID_DynamicStart) << 1) | 1u)
            : (Guid << 1);
        WriteVarUInt(Ar, Encoded);
    }

    uint32 ReadNetGuid(FNetArchive& Ar)
    {
        const uint32 Encoded = ReadVarUInt(Ar);
        return (Encoded & 1u) ? (NetGUID_DynamicStart + (Encoded >> 1)) : (Encoded >> 1);
    }

    void WriteEntityComponents(FNetArchive& Ar, entt::registry& Registry, entt::entity Entity, const FNetRepContext* Ctx, const TVector<FScriptRepFieldOut>* ScriptFields)
    {
        struct FComp
        {
            uint32 Index;
            void* Ptr;
            CStruct* Struct;
        };
        
        TVector<FComp> Comps;

        const FReplTypeTable& Types = ReplTypes();
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
            const auto It = Types.HashToIndex.find(HashStructName(St->GetName()));
            if (It == Types.HashToIndex.end())
            {
                continue; // not a known replicated type (shouldn't happen for a reflected component)
            }
            Comps.push_back({ It->second, Set.value(Entity), St });
        }

        uint16 Count = static_cast<uint16>(Comps.size());
        Ar << Count;
        for (FComp& C : Comps)
        {
            WriteVarUInt(Ar, C.Index); // compact type id (was a 4-byte hash)
            C.Struct->NetSerializeProperties(Ar, C.Ptr);
        }

        // Script-rep block: --@replicated fields whose net condition passes Ctx. Always present (count 0 when
        // there are none) so ReadEntityComponents can read it unconditionally -- this is what unifies script
        // and native replication on the same wire path.
        const FNetRepContext BroadcastCtx{};
        const FNetRepContext& Rc = Ctx ? *Ctx : BroadcastCtx;
        uint16 SCount = 0;
        if (ScriptFields != nullptr)
        {
            for (const FScriptRepFieldOut& F : *ScriptFields)
            {
                if (RepFieldPasses(F.Cond, Rc)) { ++SCount; }
            }
        }
        Ar << SCount;
        if (ScriptFields != nullptr)
        {
            for (const FScriptRepFieldOut& F : *ScriptFields)
            {
                if (!RepFieldPasses(F.Cond, Rc)) { continue; }
                WriteVarUInt(Ar, F.RepIndex);
                if (!F.Bytes.empty())
                {
                    Ar.Serialize(const_cast<uint8*>(F.Bytes.data()), static_cast<int64>(F.Bytes.size()));
                }
            }
        }

        // Attachment link: the parent's NetGUID (0 = unparented, or a parent that doesn't replicate to clients
        // -- in which case BuildExtract sends this entity's WORLD transform so it still lands correctly).
        // Stateful: rides the spawn baseline + every dirty update; the client resolves it and reparents.
        uint32 ParentGuid = 0;
        if (const FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Entity);
            Rel && Rel->Parent != entt::null && ParentReplicates(Registry, Rel->Parent))
        {
            ParentGuid = Registry.get<SNetworkComponent>(Rel->Parent).NetGUID.Value;
        }
        WriteNetGuid(Ar, ParentGuid);
    }

    TVector<FScriptRepFieldOut> CollectScriptFields(entt::registry& Registry, entt::entity Entity,
        FNetWorldState& State, bool bBaseline, FScriptRepState* DiffState)
    {
        TVector<FScriptRepFieldOut> Out;

        SScriptComponent* SC = Registry.valid(Entity) ? Registry.try_get<SScriptComponent>(Entity) : nullptr;
        Lua::FScript* Script = (SC && SC->Script) ? SC->Script.get() : nullptr;
        if (Script == nullptr || Script->ReplicatedFields.empty())
        {
            return Out;
        }
        lua_State* L = Script->Reference.GetState();
        if (L == nullptr)
        {
            return Out;
        }

        const uint32 N = static_cast<uint32>(Script->ReplicatedFields.size());
        if (DiffState != nullptr && DiffState->LastSent.size() != N)
        {
            DiffState->LastSent.clear();
            DiffState->LastSent.resize(N);
        }

        Script->PublishThreadContext();
        Script->Reference.Push();                  // [table]
        const int TableIdx = lua_gettop(L);

        for (uint32 i = 0; i < N; ++i)
        {
            const Lua::FScriptReplicatedField& Field = Script->ReplicatedFields[i];

            // Serialize with the outgoing object/asset hooks bound so any
            // CObject/asset ref mints a net-index that the existing export step flushes before this message.
            lua_rawgetfield(L, TableIdx, Field.Name.c_str()); // [table][value]
            TVector<uint8> Bytes;
            {
                FNetArchive Writer(Bytes);
                Net::BindWriters(Writer, State);
                Net::SerializeLuaValue(L, -1, Writer, 0);
            }
            lua_pop(L, 1);                                     // [table]

            bool bChanged = true;
            if (!bBaseline && DiffState != nullptr)
            {
                bChanged = (DiffState->LastSent[i] != Bytes);
            }
            if (DiffState != nullptr)
            {
                DiffState->LastSent[i] = Bytes; // seed (baseline) or update (diff)
            }

            if (bBaseline || bChanged)
            {
                FScriptRepFieldOut F;
                F.RepIndex = i;
                F.Cond     = Field.Condition;
                F.Bytes    = eastl::move(Bytes);
                Out.push_back(eastl::move(F));
            }
        }

        lua_pop(L, 1); // table
        return Out;
    }

    void ReadEntityComponents(FNetArchive& Ar, entt::registry& Registry, entt::entity Entity)
    {
        uint16 Count = 0;
        Ar << Count;

        // Components applied this message; patched after the loop to fire on_update<T> (the "OnRep" signal)
        // once the entity is fully updated, so a handler reacting to one component sees the others applied.
        TVector<entt::meta_type> Applied;

        const FReplTypeTable& Types = ReplTypes();
        for (uint16 i = 0; i < Count; ++i)
        {
            const uint32 Index = ReadVarUInt(Ar);
            if (Ar.HasError())
            {
                return;
            }

            if (Index >= static_cast<uint32>(Types.ByIndex.size()))
            {
                // Can't skip an unknown component without a size prefix. Same-build peers never hit this;
                // abort the entity if they do.
                LOG_WARN("[Net] Replication: component type index {} out of range -- aborting.", Index);
                Ar.SetHasError(true);
                return;
            }

            entt::meta_type Type = Types.ByIndex[Index].Type;
            CStruct* St = Types.ByIndex[Index].Struct;
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
                Applied.push_back(Type);
            }
            else
            {
                Ar.SetHasError(true); // couldn't materialize, stream would desync
                return;
            }
        }

        // Fire on_update<T> for each applied component
        entt::meta_any Signal{};
        for (const entt::meta_type& Type : Applied)
        {
            if (!Registry.valid(Entity))
            {
                break; // a prior handler's script logic destroyed the entity
            }
            ECS::Utils::InvokeMetaFunc(Type, entt::hashed_string("patch"), entt::forward_as_meta(Registry), Entity, entt::forward_as_meta(Signal));
        }

        // Script-rep block follows the native components on the wire. Read after the on_update pass so a
        // replicated SScriptComponent has already (re)attached its live script for the field writes / OnRep.
        ReadScriptRepBlock(Ar, Registry, Entity);

        // Attachment link (mirrors WriteEntityComponents): always read to stay aligned, then resolve + reparent.
        const uint32 ParentGuid = ReadNetGuid(Ar);
        ApplyReplicatedParent(Registry, Entity, ParentGuid);
    }

    void AppendFramedMessage(TVector<uint8>& Batch, const uint8* Msg, SIZE_T MsgSize)
    {
        if (MsgSize == 0)
        {
            return;
        }
        if (MsgSize > MaxFramedMessageSize)
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
            if (Index == 0)
            {
                return nullptr;
            }
            auto Oit = Map.IndexToObject.find(Index);
            if (Oit != Map.IndexToObject.end())
            {
                return Oit->second;
            }
            
            auto Git = Map.IndexToGuid.find(Index);
            if (Git == Map.IndexToGuid.end())
            {
                return nullptr;
            }
            
            CObject* Obj = FindObject<CObject>(Git->second);
            if (Obj == nullptr)
            {
                Obj = StaticLoadObject(Git->second);
            }
            
            if (Obj == nullptr)
            {
                LOG_WARN("[Net] Replicated object index {} (GUID {}) not found and load failed -- marking failed (null).", Index, Git->second.ToString().c_str());
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
        
        Ar.NetIndexToObject   = [&InObj](uint32 I)
        {
            return NetObj_Resolve(InObj, I);
        };
        
        Ar.NetIndexToAssetRef = [&InAst](uint32 I, FAssetRef& Out)
        {
            Out = NetAsset_Resolve(InAst, I);
        };
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
