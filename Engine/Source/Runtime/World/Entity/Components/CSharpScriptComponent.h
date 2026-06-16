#pragma once
#include "Containers/String.h"
#include "Core/Object/ObjectMacros.h"
#include "Platform/GenericPlatform.h"
#include "Scripting/ScriptPropertyOverrides.h"
#include "CSharpScriptComponent.generated.h"

namespace Lumina
{
    // Lifecycle state of the managed instance, driven by SCSharpScriptSystem over its component view.
    enum class ECSharpBindState : uint8
    {
        Unbound = 0,    // no instance yet (or generation changed -> needs rebind)
        Attached = 1,   // instance created + OnAttach run; awaiting OnReady
        Ready = 2,      // OnReady run; ticking
    };

    // Attaches a C# EntityScript to an entity. The
    // SCSharpScriptSystem instantiates the managed type named by ScriptClass and ticks it. Instance is a
    // strong GCHandle to the managed object (the only link; no managed lookup table); Instance/Generation
    // /BindState are a transient binding (not serialized) and rebind automatically when the script
    // generation changes (hot reload).
    REFLECT(Component, Category = "Gameplay")
    struct RUNTIME_API SCSharpScriptComponent
    {
        GENERATED_BODY()

        // Full C# type name to run on this entity, e.g. "Game.HelloScript".
        PROPERTY(Editable, Category = "Script")
        FString ScriptClass;

        // Per-instance overrides for the script's [Property] fields (serialized; reconciled against the
        // script's current schema on bind so it survives field add/remove/retype).
        PROPERTY(Editable)
        FScriptPropertyOverrides PropertyOverrides;

        // Opaque managed-instance handle (a strong GCHandle, as void*). Owned by managed; freed on detach.
        void* Instance = nullptr;
        int32 Generation = -1;
        ECSharpBindState BindState = ECSharpBindState::Unbound;

        // Bitmask (DotNet::GetScriptCallbackFlags) of which collision callbacks the script overrides, so
        // physics dispatch skips the managed crossing for the rest. Transient; set when bound.
        int32 CallbackFlags = 0;
    };
}
