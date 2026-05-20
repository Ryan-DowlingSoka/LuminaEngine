#pragma once
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Core/Object/ObjectMacros.h"
#include "Scripting/Lua/ScriptTypes.h"
#include "Scripting/Lua/ScriptPath.h"
#include "Scripting/Lua/ScriptPropertyOverrides.h"
#include "Memory/SmartPtr.h"
#include "Core/UpdateStage.h"
#include "ScriptComponent.generated.h"

namespace Lumina
{
    class CWorld;

    // Runtime presence tags. Added/removed in CWorld::SetupScriptComponent based on which
    // tick hooks the script actually defines, so the script systems iterate filtered views
    // instead of testing each FRef per entity. Plain (non-reflected) like FUpdateStage_*:
    // transient runtime state, never serialized or shown in the editor.
    struct FScriptHasUpdateFn       {};
    struct FScriptHasFixedUpdateFn  {};
    struct FScriptHasEditorUpdateFn {};

    REFLECT(Component, Category = "Gameplay")
    struct RUNTIME_API SScriptComponent
    {
        GENERATED_BODY()

        /** Path to the Luau script file to execute on this entity. */
        PROPERTY(Editable)
        FScriptPath ScriptPath;

        /** Per-instance overrides for values declared in the script's `type Exports = {...}` alias. */
        PROPERTY(Editable)
        FScriptPropertyOverrides PropertyOverrides;

        TSharedPtr<Lua::FScript> Script;

        Lua::FRef       AttachFunc;
        Lua::FRef       ReadyFunc;
        Lua::FRef       UpdateFunc;
        Lua::FRef       DetachFunc;

        // Optional. FixedUpdateFunc ticks at the physics fixed rate (game/simulation
        // worlds); EditorUpdateFunc ticks every frame in an editor world. Like the
        // physics hooks, the base script table ships no no-op fallback, so the FRef
        // stays invalid when the script doesn't define them and the tick is skipped.
        Lua::FRef       FixedUpdateFunc;
        Lua::FRef       EditorUpdateFunc;

        // Optional physics-event hooks. Cached at attach time; the physics
        // scene invokes them directly when contacts begin/end on this entity's
        // body. Contact = solid collision, Overlap = sensor/trigger.
        Lua::FRef       ContactBeginFunc;
        Lua::FRef       ContactEndFunc;
        Lua::FRef       OverlapBeginFunc;
        Lua::FRef       OverlapEndFunc;

        Lua::FRef       ScriptMetaTable;

        /**
         * Handlers for directed messages dispatched via FEntityMessageBus / `Messages:Send`.
         * Populated once at script attach by walking the script table for `On*` functions
         * (see CWorld::OnScriptComponentCreated). Cleared automatically when the component
         * is destroyed -- FRef releases its Lua ref in its dtor.
         */
        THashMap<FName, Lua::FRef>  MessageHandlers;

        CWorld*         World           = nullptr;
        entt::entity    Entity        = entt::null;
        
        EUpdateStage    UpdateStage     = EUpdateStage::PrePhysics;
        
        float           TickRate        = 0.0f;
        float           AccumulatedTime = 0.0f;
    };
}
