#pragma once

#include "Core/Object/ObjectMacros.h"
#include "HealthComponent.generated.h"

namespace Lumina
{
    REFLECT(Component)
    struct RUNTIME_API SHealthComponent
    {
        GENERATED_BODY()
        
        FUNCTION(Script)
        float ApplyDamage(float Damage) { Health -= Damage; return Health; }
        
        FUNCTION(Script)
        void GiveHealth(float NewHealth) { Health = glm::clamp(Health + NewHealth, 0.0f, MaxHealth); }
        
        /** Current health points of the entity. */
        PROPERTY(Script, Editable, Category = "Health")
        float Health = 100.0f;

        /** Maximum health capacity. Health is clamped to this value. */
        PROPERTY(Script, Editable, Category = "Health")
        float MaxHealth = 100.0f;
    };
}