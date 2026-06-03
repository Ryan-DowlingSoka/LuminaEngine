#include "pch.h"
#include "NetRpc.h"
#include "NetWorldState.h"
#include "NetReplication.h"
#include "World/World.h"
#include "World/WorldContext.h"
#include "World/Entity/Components/ScriptComponent.h"
#include "World/Entity/Components/NetworkComponent.h"
#include "Scripting/Lua/ScriptTypes.h"
#include "Networking/INetworkTransport.h"
#include "Core/Serialization/NetArchive.h"
#include "Log/Log.h"
#include "lua.h"
#include "lualib.h"

namespace Lumina::Net
{
    namespace
    {
        bool IsServerMode(ENetMode Mode)
        {
            return Mode == ENetMode::ListenServer || Mode == ENetMode::DedicatedServer;
        }

        // Wire tag for a single marshaled Lua argument. Userdata/functions/threads are not sent.
        enum class ELuaArgType : uint8
        {
            Nil = 0,
            Bool,
            Number,   // Lua numbers are doubles (covers ints losslessly to 2^53)
            String,
            Vector,
            Table,    // recursive: count + (key, value) pairs
        };

        constexpr int    MaxRpcArgDepth  = 16;          // guards against cyclic/oversized tables
        constexpr uint32 MaxRpcStringLen = 1u << 20;    // 1 MiB sanity cap on a malformed length

        // Serialize one Lua value (tag + payload) at Index. Stack-neutral.
        void SerializeLuaValue(lua_State* L, int Index, FNetArchive& Ar, int Depth)
        {
            Index = lua_absindex(L, Index);

            auto WriteTag = [&](ELuaArgType T) { uint8 Tag = static_cast<uint8>(T); Ar << Tag; };

            if (Depth >= MaxRpcArgDepth)
            {
                LOG_WARN("[Net] RPC arg nesting exceeds {} levels -- truncating to nil.", MaxRpcArgDepth);
                WriteTag(ELuaArgType::Nil);
                return;
            }

            switch (lua_type(L, Index))
            {
            case LUA_TBOOLEAN:
                {
                    WriteTag(ELuaArgType::Bool);
                    bool bValue = lua_toboolean(L, Index) != 0;
                    Ar.SerializeBit(bValue);
                }
                break;

            case LUA_TNUMBER:
                {
                    WriteTag(ELuaArgType::Number);
                    double Value = lua_tonumber(L, Index);
                    Ar << Value;
                }
                break;

            case LUA_TSTRING:
                {
                    WriteTag(ELuaArgType::String);
                    size_t Len = 0;
                    const char* Str = lua_tolstring(L, Index, &Len);
                    uint32 Len32 = static_cast<uint32>(Len);
                    Ar << Len32;
                    if (Len32 > 0)
                    {
                        Ar.Serialize(const_cast<char*>(Str), static_cast<int64>(Len32));
                    }
                }
                break;

            case LUA_TVECTOR:
                {
                    WriteTag(ELuaArgType::Vector);
                    const float* V = lua_tovector(L, Index);
                    float X = V[0], Y = V[1], Z = V[2], W = 0.0f;
                    Ar << X; Ar << Y; Ar << Z; Ar << W;
                }
                break;

            case LUA_TTABLE:
                {
                    WriteTag(ELuaArgType::Table);
                    // Count first (lua_next order is stable for an unmodified table), then write pairs.
                    uint32 Count = 0;
                    lua_pushnil(L);
                    while (lua_next(L, Index) != 0) { ++Count; lua_pop(L, 1); }
                    Ar << Count;

                    lua_pushnil(L);
                    while (lua_next(L, Index) != 0)
                    {
                        SerializeLuaValue(L, -2, Ar, Depth + 1); // key
                        SerializeLuaValue(L, -1, Ar, Depth + 1); // value
                        lua_pop(L, 1);                            // pop value, keep key for lua_next
                    }
                }
                break;

            default: // nil, function, userdata, thread, buffer
                {
                    const int Type = lua_type(L, Index);
                    if (Type != LUA_TNIL && Type != LUA_TNONE)
                    {
                        LOG_WARN("[Net] RPC arg of type '{}' is not serializable -- sending nil.", lua_typename(L, Type));
                    }
                    WriteTag(ELuaArgType::Nil);
                }
                break;
            }
        }

        // Deserialize one Lua value and push it. Always pushes exactly one value (nil on error).
        void DeserializeLuaValue(lua_State* L, FNetArchive& Ar, int Depth)
        {
            uint8 TagByte = 0;
            Ar << TagByte;
            if (Ar.HasError() || Depth >= MaxRpcArgDepth)
            {
                lua_pushnil(L);
                return;
            }

            switch (static_cast<ELuaArgType>(TagByte))
            {
            case ELuaArgType::Bool:
                {
                    bool bValue = false;
                    Ar.SerializeBit(bValue);
                    lua_pushboolean(L, bValue);
                }
                break;

            case ELuaArgType::Number:
                {
                    double Value = 0.0;
                    Ar << Value;
                    lua_pushnumber(L, Value);
                }
                break;

            case ELuaArgType::String:
                {
                    uint32 Len = 0;
                    Ar << Len;
                    if (Ar.HasError() || Len > MaxRpcStringLen)
                    {
                        lua_pushnil(L);
                        break;
                    }
                    if (Len == 0)
                    {
                        lua_pushlstring(L, "", 0);
                        break;
                    }
                    TVector<char> Buffer;
                    Buffer.resize(Len);
                    Ar.Serialize(Buffer.data(), static_cast<int64>(Len));
                    if (Ar.HasError()) { lua_pushnil(L); break; }
                    lua_pushlstring(L, Buffer.data(), Len);
                }
                break;

            case ELuaArgType::Vector:
                {
                    float X = 0, Y = 0, Z = 0, W = 0;
                    Ar << X; Ar << Y; Ar << Z; Ar << W;
                    lua_pushvector(L, X, Y, Z, W);
                }
                break;

            case ELuaArgType::Table:
                {
                    uint32 Count = 0;
                    Ar << Count;
                    lua_createtable(L, 0, static_cast<int>(Count));
                    const int TableIdx = lua_gettop(L);
                    for (uint32 i = 0; i < Count && !Ar.HasError(); ++i)
                    {
                        DeserializeLuaValue(L, Ar, Depth + 1); // key
                        DeserializeLuaValue(L, Ar, Depth + 1); // value
                        if (lua_isnil(L, -2))
                        {
                            lua_pop(L, 2); // nil key (unserializable) -- drop the pair
                        }
                        else
                        {
                            lua_rawset(L, TableIdx); // pops key + value
                        }
                    }
                }
                break;

            case ELuaArgType::Nil:
            default:
                lua_pushnil(L);
                break;
            }
        }

        // Pack an RPC invocation: { ENetMessage::ScriptRpc, uint32 NetGUID, uint16 RpcId, uint8 ArgCount,
        // ArgCount marshaled Lua values }. Args are read from the live Lua stack at [FirstArgIndex, top].
        void WriteRpcPacket(TVector<uint8>& Buffer, lua_State* L, int FirstArgIndex, uint32 NetGUID, uint16 RpcId)
        {
            FNetArchive Writer(Buffer);
            uint8 Type = static_cast<uint8>(ENetMessage::ScriptRpc);
            Writer << Type;
            Writer << NetGUID;
            Writer << RpcId;

            const int Top = lua_gettop(L);
            int Available = (FirstArgIndex <= Top) ? (Top - FirstArgIndex + 1) : 0;
            if (Available > 255) { Available = 255; }
            uint8 ArgCount = static_cast<uint8>(Available);
            Writer << ArgCount;
            for (uint8 i = 0; i < ArgCount; ++i)
            {
                SerializeLuaValue(L, FirstArgIndex + static_cast<int>(i), Writer, 0);
            }
        }

        // The dispatch wrapper installed over each --@rpc method. Upvalue 1 = rpc id, upvalue 2 = the
        // original function. Calling self:Method(...) lands here and routes per the rpc's mode.
        int Lua_RpcDispatch(lua_State* L)
        {
            const int RpcId = static_cast<int>(lua_tointeger(L, lua_upvalueindex(1)));

            auto* ThreadData = static_cast<Lua::FScriptThreadData*>(lua_getthreaddata(L));
            if (ThreadData == nullptr || ThreadData->World == nullptr)
            {
                return 0;
            }

            CWorld* World = ThreadData->World;
            const entt::entity Entity = ThreadData->Entity;
            FEntityRegistry& Registry = World->GetEntityRegistry();

            SScriptComponent* ScriptComp = Registry.try_get<SScriptComponent>(Entity);
            if (ScriptComp == nullptr || !ScriptComp->Script ||
                RpcId < 0 || RpcId >= static_cast<int>(ScriptComp->Script->Rpcs.size()))
            {
                return 0;
            }
            const Lua::FScriptRpc& Rpc = ScriptComp->Script->Rpcs[RpcId];

            // Run the original handler locally (upvalue 2), forwarding self + all passed args.
            auto RunLocal = [&]()
            {
                const int NumArgs = lua_gettop(L);
                lua_pushvalue(L, lua_upvalueindex(2));        // original fn
                for (int i = 1; i <= NumArgs; ++i)
                {
                    lua_pushvalue(L, i);                       // self + args
                }
                if (lua_pcall(L, NumArgs, 0, 0) != LUA_OK)
                {
                    LOG_ERROR("[Net] RPC '{}' local invoke failed: {}", Rpc.Name.c_str(), lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            };

            const ENetMode NetMode = World->GetNetMode();
            SNetworkComponent* NetComp = Registry.try_get<SNetworkComponent>(Entity);
            FNetWorldState*    State   = Registry.ctx().find<FNetWorldState>();

            // Not networked (standalone, no transport, or no net identity): behave like a normal call.
            if (NetMode == ENetMode::Standalone || State == nullptr || State->Transport == nullptr || NetComp == nullptr)
            {
                RunLocal();
                return 0;
            }

            const bool      bIsServer = IsServerMode(NetMode);
            const ESendMode SendMode  = Rpc.bReliable ? ESendMode::Reliable : ESendMode::UnreliableSequenced;

            switch (Rpc.Mode)
            {
            case ERpcMode::Server: // client -> server
                if (bIsServer)
                {
                    RunLocal(); // already the authority
                }
                else if (NetComp->LocalRole == ENetRole::AutonomousProxy)
                {
                    TVector<uint8> Buffer;
                    WriteRpcPacket(Buffer, L, 2, NetComp->NetGUID.Value, static_cast<uint16>(RpcId));
                    Net::SendFramed(*State->Transport, State->ServerConnection, Buffer.data(), static_cast<SIZE_T>(Buffer.size()), 0, SendMode);
                }
                else
                {
                    LOG_WARN("[Net] Blocked Server RPC '{}': this client does not own the entity.", Rpc.Name.c_str());
                }
                break;

            case ERpcMode::Multicast: // server -> everyone
                if (bIsServer)
                {
                    RunLocal();
                    TVector<uint8> Buffer;
                    WriteRpcPacket(Buffer, L, 2, NetComp->NetGUID.Value, static_cast<uint16>(RpcId));
                    Net::BroadcastFramed(*State->Transport, Buffer.data(), static_cast<SIZE_T>(Buffer.size()), 0, SendMode);
                }
                else
                {
                    LOG_WARN("[Net] Multicast RPC '{}' may only be sent by the server.", Rpc.Name.c_str());
                }
                break;

            case ERpcMode::Client: // server -> owning client
                if (bIsServer)
                {
                    TVector<uint8> Buffer;
                    WriteRpcPacket(Buffer, L, 2, NetComp->NetGUID.Value, static_cast<uint16>(RpcId));
                    Net::SendFramed(*State->Transport, FConnectionHandle{ NetComp->OwningConnectionId }, Buffer.data(), static_cast<SIZE_T>(Buffer.size()), 0, SendMode);
                }
                else
                {
                    LOG_WARN("[Net] Client RPC '{}' may only be sent by the server.", Rpc.Name.c_str());
                }
                break;
            }

            return 0;
        }
    }

    void WrapScriptRpcs(Lua::FScript* Script)
    {
        if (Script == nullptr || Script->Rpcs.empty())
        {
            return;
        }

        lua_State* L = Script->Reference.GetState();
        if (L == nullptr)
        {
            return;
        }

        Script->RpcHandlers.clear();
        Script->RpcHandlers.reserve(Script->Rpcs.size());

        Script->Reference.Push();                 // [table]
        const int TableIdx = lua_gettop(L);

        for (int i = 0; i < static_cast<int>(Script->Rpcs.size()); ++i)
        {
            const Lua::FScriptRpc& Rpc = Script->Rpcs[i];

            lua_getfield(L, TableIdx, Rpc.Name.c_str()); // [table][fn]
            if (!lua_isfunction(L, -1))
            {
                LOG_WARN("[Net] --@rpc '{}' is not a function on the script table; skipping.", Rpc.Name.c_str());
                Script->RpcHandlers.push_back(Lua::FRef{});
                lua_pop(L, 1);                           // [table]
                continue;
            }

            // Install the dispatch closure over the method (upvalue 1 = id, upvalue 2 = the original fn).
            lua_pushinteger(L, i);                       // [table][fn][id]
            lua_pushvalue(L, -2);                        // [table][fn][id][fn]
            lua_pushcclosure(L, &Lua_RpcDispatch, "RpcDispatch", 2); // [table][fn][closure]
            lua_setfield(L, TableIdx, Rpc.Name.c_str()); // table[name] = closure ; [table][fn]

            // Capture the original LAST: FRef(L, -1) pops the value it refs, leaving just [table].
            Script->RpcHandlers.push_back(Lua::FRef(L, -1)); // [table]
        }

        lua_pop(L, 1);                                   // []
    }

    void ReceiveScriptRpc(CWorld* World, FConnectionHandle Sender, const uint8* Data, SIZE_T Size)
    {
        if (World == nullptr)
        {
            return;
        }

        FNetArchive Reader(Data, Size);
        uint8  Type = 0;
        uint32 Guid = 0;
        uint16 RpcId = 0;
        Reader << Type;     // consumed by the caller's route, read again to advance
        Reader << Guid;
        Reader << RpcId;
        if (Reader.HasError())
        {
            return;
        }

        FEntityRegistry& Registry = World->GetEntityRegistry();
        FNetWorldState*  State    = Registry.ctx().find<FNetWorldState>();
        if (State == nullptr)
        {
            return;
        }

        const entt::entity Entity = State->GuidTable.Find(FNetGUID{ Guid });
        if (Entity == entt::null || !Registry.valid(Entity))
        {
            return;
        }

        SScriptComponent* ScriptComp = Registry.try_get<SScriptComponent>(Entity);
        if (ScriptComp == nullptr || !ScriptComp->Script || RpcId >= ScriptComp->Script->Rpcs.size() || RpcId >= ScriptComp->Script->RpcHandlers.size())
        {
            return;
        }

        const Lua::FScriptRpc& Rpc     = ScriptComp->Script->Rpcs[RpcId];
        SNetworkComponent*     NetComp = Registry.try_get<SNetworkComponent>(Entity);

        // Authority gate: a Server RPC is only honored if the sender owns the target entity. Multicast/
        // Client packets arrive from the server (a client's only peer), so they're trusted.
        if (Rpc.Mode == ERpcMode::Server)
        {
            if (NetComp == nullptr || NetComp->OwningConnectionId != Sender.Value)
            {
                LOG_WARN("[Net] Rejected Server RPC '{}': sender {} does not own the entity (owner {}).",
                    Rpc.Name.c_str(), Sender.Value, NetComp ? NetComp->OwningConnectionId : 0u);
                return;
            }
        }

        if (!ScriptComp->Script->RpcHandlers[RpcId].IsValid())
        {
            return;
        }

        lua_State* L = ScriptComp->Script->Reference.GetState();
        if (L == nullptr)
        {
            return;
        }

        ScriptComp->Script->PublishThreadContext();

        uint8 ArgCount = 0;
        Reader << ArgCount;

        ScriptComp->Script->RpcHandlers[RpcId].Push(); // [fn]
        ScriptComp->Script->Reference.Push();          // [fn][self]
        for (uint8 i = 0; i < ArgCount; ++i)
        {
            DeserializeLuaValue(L, Reader, 0);         // [fn][self][args...]
        }

        if (Reader.HasError())
        {
            lua_pop(L, 2 + static_cast<int>(ArgCount)); // bail, keep the stack balanced
            LOG_WARN("[Net] RPC '{}' dropped: malformed argument payload.", Rpc.Name.c_str());
            return;
        }

        // self + ArgCount args; pcall pops the function and all of them.
        if (lua_pcall(L, 1 + static_cast<int>(ArgCount), 0, 0) != LUA_OK)
        {
            LOG_ERROR("[Net] RPC '{}' invoke failed: {}", Rpc.Name.c_str(), lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
}
