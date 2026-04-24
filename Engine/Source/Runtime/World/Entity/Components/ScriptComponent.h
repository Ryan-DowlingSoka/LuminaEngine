#pragma once
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

    REFLECT(Component)
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
        Lua::FRef       ScriptMetaTable;

        CWorld*         World           = nullptr;
        entt::entity    Entity        = entt::null;
        
        EUpdateStage    UpdateStage     = EUpdateStage::PrePhysics;
        
        float           TickRate        = 0.0f;
        float           AccumulatedTime = 0.0f;
        bool            bRunInEditor    = false;
    };
}
