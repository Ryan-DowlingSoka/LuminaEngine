#pragma once

#include "Platform/GenericPlatform.h"
#include "Networking/NetworkTypes.h"

struct lua_State;

namespace Lumina
{
    class CWorld;
    class FNetArchive;
    namespace Lua { struct FScript; }
}

namespace Lumina::Net
{
    // Recursive Lua value codec: tag + payload, with nested-table support (depth-capped at 16, 1 MiB string
    // cap). Shared by RPC argument marshaling and script-field replication (NetReplication). SerializeLuaValue
    // is stack-neutral; DeserializeLuaValue always pushes exactly one value (nil on error).
    void SerializeLuaValue(lua_State* L, int Index, FNetArchive& Ar, int Depth = 0);
    void DeserializeLuaValue(lua_State* L, FNetArchive& Ar, int Depth = 0);

    // Install dispatch wrappers on a script's --@rpc methods and capture the originals (for receive-side
    // invocation). After this, calling such a method routes per its ERpcMode instead of running locally.
    void WrapScriptRpcs(Lua::FScript* Script);

    // Decode + authority-gate + invoke an incoming RPC packet on this world. Sender is the connection it
    // arrived on; server-side it must own the target entity for a `server` RPC to be honored.
    void ReceiveScriptRpc(CWorld* World, FConnectionHandle Sender, const uint8* Data, SIZE_T Size);
}
