#pragma once

#include "Assets/AssetTypes/Blackboard/Blackboard.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Math/Math.h"
#include "BlackboardComponent.generated.h"

namespace Lumina
{
    // Per-instance runtime value for one blackboard key. Which field is live is
    // determined by the matching FBlackboardKey::Type on the schema.
    struct FBlackboardValue
    {
        float                Scalar = 0.0f;
        FVector3            Vector = FVector3(0.0f);
        TObjectPtr<CObject>  Object;
    };

    // Per-entity blackboard instance. References a CBlackboard schema and owns
    // its own value store, seeded from the schema's defaults the first time it is
    // used. Two entities sharing the same schema asset keep fully independent
    // values (this is the only place mutable blackboard state lives). Gameplay /
    // Lua read and write through the Set*/Get* API; the animation system reads
    // values by key name each frame.
    REFLECT(Component, Category = "AI")
    struct SBlackboardComponent
    {
        GENERATED_BODY()

        /** Schema this instance is initialized from. */
        PROPERTY(Script, Editable, Category = "Blackboard")
        TObjectPtr<CBlackboard> Blackboard;

        FUNCTION(Script)
        RUNTIME_API void SetFloat(const FName& Key, float Value);

        FUNCTION(Script)
        RUNTIME_API float GetFloat(const FName& Key, float Default = 0.0f) const;

        FUNCTION(Script)
        RUNTIME_API void SetInt(const FName& Key, int32 Value);

        FUNCTION(Script)
        RUNTIME_API int32 GetInt(const FName& Key, int32 Default = 0) const;

        FUNCTION(Script)
        RUNTIME_API void SetBool(const FName& Key, bool bValue);

        FUNCTION(Script)
        RUNTIME_API bool GetBool(const FName& Key, bool Default = false) const;

        FUNCTION(Script)
        RUNTIME_API void SetObjectValue(const FName& Key, CObject* Value);

        FUNCTION(Script)
        RUNTIME_API CObject* GetObjectValue(const FName& Key) const;

        FUNCTION(Script)
        RUNTIME_API bool HasKey(const FName& Key) const;

        RUNTIME_API void SetVector(const FName& Key, const FVector3& Value);
        RUNTIME_API FVector3 GetVector(const FName& Key, const FVector3& Default = FVector3(0.0f)) const;

        // (Re)seeds the value store from the schema defaults when first used or
        // when the referenced Blackboard asset changes. Safe to call every frame.
        RUNTIME_API void EnsureInitialized();

    private:

        // Live per-instance values, keyed by name. Not serialized -- runtime only,
        // reset from the schema defaults on init (matching Unreal's blackboard).
        THashMap<FName, FBlackboardValue> Values;

        // Schema the Values store was seeded from; re-seed when it changes.
        const CBlackboard* SeededSchema = nullptr;
    };
}
