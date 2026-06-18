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

    // Attaches a C# EntityScript to an entity.
    REFLECT(Component, Category = "Gameplay")
    struct RUNTIME_API SCSharpScriptComponent
    {
        GENERATED_BODY()

        SCSharpScriptComponent() = default;

        // Duplicating a script entity must NOT share the source's managed binding.
        SCSharpScriptComponent(const SCSharpScriptComponent& Other)
            : ScriptClass(Other.ScriptClass)
            , PropertyOverrides(Other.PropertyOverrides)
        {
        }

        SCSharpScriptComponent& operator=(const SCSharpScriptComponent& Other)
        {
            if (this != &Other)
            {
                ScriptClass       = Other.ScriptClass;
                PropertyOverrides = Other.PropertyOverrides;
                Instance          = nullptr;
                Generation        = -1;
                BindState         = ECSharpBindState::Unbound;
                CallbackFlags     = 0;
            }
            return *this;
        }

        // Move transfers the binding (ownership), so it is the default memberwise move.
        SCSharpScriptComponent(SCSharpScriptComponent&&) noexcept            = default;
        SCSharpScriptComponent& operator=(SCSharpScriptComponent&&) noexcept = default;

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
