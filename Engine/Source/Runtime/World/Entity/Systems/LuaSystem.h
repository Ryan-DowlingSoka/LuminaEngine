#pragma once

#include "EntitySystem.h"
#include "Memory/SmartPtr.h"
#include "Containers/String.h"
#include "Scripting/Lua/Reference.h"
#include "Scripting/Lua/ScriptTypes.h"
#include "World/Entity/Registry/EntityRegistry.h"


namespace Lumina
{
    class CWorld;

    // Lua-facing system context handed to a script system's hooks as `ctx`. A thin, per-call view over the
    // live FSystemContext; bound as the Lua class "SystemContext" in CWorld::RegisterLuaModule. The owning
    // FScriptSystemInstance is pointer-stable, so a single pinned userdata wraps one of these per system and
    // only its Ctx pointer is rebound each tick.
    struct FLuaSystemContext
    {
        CWorld*         World = nullptr;
        FSystemContext* Ctx = nullptr;

        RUNTIME_API FEntityRegistry& GetRegistry() const;
        double DeltaTime() const { return Ctx ? Ctx->GetDeltaTime() : 0.0; }
        double Time() const { return Ctx ? Ctx->GetTime() : 0.0; }
    };

    // A loaded Lua-authored ECS system bound to one world. Owns the script module + its resolved hook FRefs.
    // Heap-allocated and pointer-stable (held via TUniquePtr) so the pinned ctx userdata and the
    // FStageSlot/FActiveSystem Self pointers stay valid for the instance's lifetime.
    struct FScriptSystemInstance
    {
        CWorld*                  World = nullptr;
        FString                  Path;
        FName                    Name;
        FUpdatePriorityList      Priorities;

        TSharedPtr<Lua::FScript> Script;
        Lua::FRef                Table;       // the module table (the returned system descriptor)
        Lua::FRef                StartupFn;
        Lua::FRef                UpdateFn;
        Lua::FRef                TeardownFn;
        Lua::FRef                CtxRef;      // pinned userdata wrapping &LuaCtx
        FLuaSystemContext        LuaCtx;
    };

    // FSystemFn trampolines (Self = FScriptSystemInstance*). Always exclusive -- the Lua VM is single-threaded.
    void ScriptSystem_Startup(void* Self, const FSystemContext& Context) noexcept;
    void ScriptSystem_Update(void* Self, const FSystemContext& Context) noexcept;
    void ScriptSystem_Teardown(void* Self, const FSystemContext& Context) noexcept;

    // Loads a script-system module from Path and resolves its hooks + stage/priority metadata. Returns null
    // when the module didn't return a table or defines no Update function.
    RUNTIME_API TUniquePtr<FScriptSystemInstance> LoadScriptSystem(CWorld* World, FStringView Path);

    // Re-resolves an existing instance's script + hooks from disk (hot reload). Keeps the instance pointer.
    RUNTIME_API bool ReloadScriptSystem(FScriptSystemInstance& Instance);
}
