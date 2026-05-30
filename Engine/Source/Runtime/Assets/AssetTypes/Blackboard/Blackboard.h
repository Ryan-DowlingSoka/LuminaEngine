#pragma once

#include "Core/LuminaMacros.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Containers/Array.h"
#include "Core/Math/Math.h"
#include "Blackboard.generated.h"

namespace Lumina
{
    // Blackboard key value type; anim system uses the scalar-carried numerics, Vector/Object are for AI.
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

        // Read-only at runtime (constants/inputs); the editor preview disables editing it.
        ReadOnly = 1 << 0,

        // Hidden from key pickers; for internal/scratch keys not meant to be wired by hand.
        Hidden   = 1 << 1,
    };
    ENUM_CLASS_FLAGS(EBlackboardKeyFlags)

    // Schema-only declaration of one blackboard entry (name, type, default); live per-instance
    // values live in SBlackboardComponent, seeded from these defaults.
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

        /** For Enum keys: registered name of the reflected CEnum (e.g. "EClipLoopMode"); DefaultInt holds the value. */
        PROPERTY(Editable, Category = "Blackboard")
        FName EnumType;

        /** Default for Vector keys. */
        PROPERTY(Editable, Category = "Blackboard")
        FVector3 DefaultVector = FVector3(0.0f);

        /** Default for Object keys. */
        PROPERTY(Editable, Category = "Blackboard")
        TObjectPtr<CObject> DefaultObject;
    };

    // Reusable blackboard schema asset (typed keys + defaults), shared read-only. Live values are per-entity
    // in SBlackboardComponent, so entities sharing a schema keep independent values.
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
