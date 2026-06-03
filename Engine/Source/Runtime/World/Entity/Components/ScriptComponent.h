#pragma once
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Core/Object/ObjectMacros.h"
#include "Scripting/Lua/ScriptTypes.h"
#include "Assets/AssetRef.h"
#include "Scripting/Lua/ScriptPropertyOverrides.h"
#include "Memory/SmartPtr.h"
#include "Core/UpdateStage.h"
#include "ScriptComponent.generated.h"

namespace Lumina
{
    class CWorld;

    // Runtime presence tags set in CWorld::SetupScriptComponent per defined tick hook, so script systems
    // iterate filtered views instead of testing each FRef. Plain (non-reflected), transient, never serialized.
    struct FScriptHasUpdateFn       {};
    struct FScriptHasFixedUpdateFn  {};
    struct FScriptHasEditorUpdateFn {};

    // Client marker, a replicated ScriptPath that failed to load on this peer. Stops the re-attach from
    // reloading it every tick. Holds the failed path so a changed ScriptPath still retries.
    struct FScriptAttachFailed { FString Path; };

    REFLECT(Component, Category = "Gameplay")
    struct RUNTIME_API SScriptComponent
    {
        GENERATED_BODY()

        /** Luau script to execute on this entity. Rename-safe (GUID-backed). Replicated so a server-spawned
         *  entity carries its script (and thus its RPC handlers) to clients, which attach it on receipt. */
        PROPERTY(Editable, Replicated, AssetType = "luau")
        FAssetRef ScriptPath;

        /** Per-instance overrides for the script's --@export-annotated top-level members (Script.<Name> = ...). */
        PROPERTY(Editable)
        FScriptPropertyOverrides PropertyOverrides;

        TSharedPtr<Lua::FScript> Script;

        Lua::FRef       AttachFunc;
        Lua::FRef       ReadyFunc;
        Lua::FRef       UpdateFunc;
        Lua::FRef       DetachFunc;

        // Optional. FixedUpdateFunc ticks at the physics rate; EditorUpdateFunc every frame in editor.
        // No no-op fallback, so an undefined hook leaves the FRef invalid and the tick is skipped.
        Lua::FRef       FixedUpdateFunc;
        Lua::FRef       EditorUpdateFunc;

        // Optional physics-event hooks, cached at attach; the physics scene invokes them on contact
        // begin/end. Contact = solid collision, Overlap = sensor/trigger.
        Lua::FRef       ContactBeginFunc;
        Lua::FRef       ContactEndFunc;
        Lua::FRef       OverlapBeginFunc;
        Lua::FRef       OverlapEndFunc;

        Lua::FRef       ScriptMetaTable;

        /** Handlers for directed messages dispatched via FEntityMessageBus. Populated at script attach by
         *  walking the script table for On* functions, released when the component is destroyed. */
        THashMap<FName, Lua::FRef>  MessageHandlers;

        CWorld*         World           = nullptr;
        entt::entity    Entity        = entt::null;
        
        EUpdateStage    UpdateStage     = EUpdateStage::PrePhysics;
        
        float           TickRate        = 0.0f;
        float           AccumulatedTime = 0.0f;
    };
}
