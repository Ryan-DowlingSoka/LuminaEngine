#pragma once

#include "Platform/GenericPlatform.h"
#include "Networking/NetworkTypes.h"

namespace Lumina
{
    class CWorld;
    namespace Lua { struct FScript; }
}

namespace Lumina::Net
{
    // Install dispatch wrappers on a script's --@rpc methods and capture the originals (for receive-side
    // invocation). After this, calling such a method routes per its ERpcMode instead of running locally.
    void WrapScriptRpcs(Lua::FScript* Script);

    // Decode + authority-gate + invoke an incoming RPC packet on this world. Sender is the connection it
    // arrived on; server-side it must own the target entity for a `server` RPC to be honored.
    void ReceiveScriptRpc(CWorld* World, FConnectionHandle Sender, const uint8* Data, SIZE_T Size);
}
