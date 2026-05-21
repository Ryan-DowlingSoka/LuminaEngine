#pragma once

#include "Core/LuminaMacros.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Containers/Array.h"
#include "glm/glm.hpp"
#include "Blackboard.generated.h"

namespace Lumina
{
    // Type of value a blackboard key holds. The animation system consumes the
    // numeric types (Float / Int / Bool / Enum, all carried as a scalar);
    // Vector / Object are stored for the AI system.
    REFLECT()
    enum class EBlackboardKeyType : uint8
    {
        Float,
        Int,
        Bool,
        Enum,
        Vector,
        Object,
    };

    // Per-key behavior flags.
    REFLECT()
    enum class EBlackboardKeyFlags : uint8
    {
        None     = 0,

        // Value is set from its default and treated as read-only at runtime
        // (constants / inputs gameplay should not overwrite). The editor's
        // preview panel disables editing it.
        ReadOnly = 1 << 0,

        // Hidden from key pickers (Get Parameter node, transition conditions) --
        // for internal / scratch keys that shouldn't be wired by hand.
        Hidden   = 1 << 1,
    };
    ENUM_CLASS_FLAGS(EBlackboardKeyFlags)

    // Declaration of a single blackboard entry: a name, a type, and a default
    // value. This is schema only -- no live value lives here. Per-instance values
    // are held by SBlackboardComponent, seeded from these defaults.
    REFLECT()
    struct FBlackboardKey
    {
        GENERATED_BODY()

        /** Identifier scripts / the animation graph reference this key by. */
        PROPERTY(Editable, Category = "Blackboard")
        FName Name;

        /** Value type held under this key. */
        PROPERTY(Editable, Category = "Blackboard")
        EBlackboardKeyType Type = EBlackboardKeyType::Float;

        /** Behavior flags (read-only, hidden from pickers, ...). */
        PROPERTY(Editable, Category = "Blackboard")
        EBlackboardKeyFlags Flags = EBlackboardKeyFlags::None;

        /** Default for Float keys (and the 0/1 source for Bool when unset). */
        PROPERTY(Editable, Category = "Blackboard")
        float DefaultFloat = 0.0f;

        /** Default for Int keys, and the value for Enum keys. */
        PROPERTY(Editable, Category = "Blackboard")
        int32 DefaultInt = 0;

        /** Default for Bool keys. */
        PROPERTY(Editable, Category = "Blackboard")
        bool DefaultBool = false;

        /** For Enum keys: the registered name of the reflected CEnum this key
         *  holds (e.g. "EClipLoopMode"). DefaultInt stores the chosen value. */
        PROPERTY(Editable, Category = "Blackboard")
        FName EnumType;

        /** Default for Vector keys. */
        PROPERTY(Editable, Category = "Blackboard")
        glm::vec3 DefaultVector = glm::vec3(0.0f);

        /** Default for Object keys. */
        PROPERTY(Editable, Category = "Blackboard")
        TObjectPtr<CObject> DefaultObject;
    };

    // A reusable blackboard *schema* asset: a named list of typed keys with
    // defaults. Holds no runtime state -- it is shared read-only by reference.
    // Systems read/write live values through a per-entity SBlackboardComponent
    // that is seeded from this schema (mirrors Unreal's UBlackboardData vs
    // UBlackboardComponent split, so two entities sharing a schema keep their
    // own independent values).
    REFLECT()
    class RUNTIME_API CBlackboard : public CObject
    {
        GENERATED_BODY()

    public:

        bool IsAsset() const override { return true; }

        int32 FindKeyIndex(const FName& Name) const;
        const FBlackboardKey* FindKey(const FName& Name) const;

        /** Key declarations that make up this blackboard's schema. */
        PROPERTY(Editable, Category = "Blackboard")
        TVector<FBlackboardKey> Keys;
    };
}
