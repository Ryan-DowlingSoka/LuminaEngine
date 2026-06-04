#pragma once

#include "ModuleAPI.h"
#include "Containers/Name.h"
#include "Containers/Array.h"

struct lua_State;

namespace Lumina
{
    class CWorld;

    // Pushes a subsystem's Lua value for the active world. Implementations push exactly one value
    // (typically Lua::TStack<T*>::Push of an accessor on World) and must leave the stack net +1.
    using FWorldLuaSubsystemPush = void(*)(lua_State* L, CWorld* World);

    // Registry backing the World.<Name> namespaces (World.Physics, World.Net, ...). The shared World
    // table's __index resolves a key here, then calls the accessor with the script's active world, so
    // a single table stays correct across every world. Engine registers Physics/Net; game modules can
    // register their own subsystems during Lua module bootstrap via RegisterWorldLuaSubsystem.
    class FWorldLuaSubsystemRegistry
    {
    public:

        struct FEntry
        {
            FName                   Name;
            const char*             LuauType;   // editor-only Luau type for the `declare World` snippet; may be null
            FWorldLuaSubsystemPush  Push;
        };

        static RUNTIME_API FWorldLuaSubsystemRegistry& Get();

        // Last registration for a given name wins, so a game module can override an engine subsystem.
        RUNTIME_API void                    Register(FName Name, const char* LuauType, FWorldLuaSubsystemPush Push);
        RUNTIME_API FWorldLuaSubsystemPush  Find(FName Name) const;

        const TVector<FEntry>&              GetEntries() const { return Entries; }

    private:

        TVector<FEntry> Entries;
    };

    // Convenience wrapper around FWorldLuaSubsystemRegistry::Get().Register.
    RUNTIME_API void RegisterWorldLuaSubsystem(FName Name, const char* LuauType, FWorldLuaSubsystemPush Push);
}
