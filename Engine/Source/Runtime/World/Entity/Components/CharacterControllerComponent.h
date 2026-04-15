#pragma once

#include "Core/Object/ObjectMacros.h"
#include "CharacterControllerComponent.generated.h"


namespace Lumina
{
    REFLECT(Component)
    struct RUNTIME_API SCharacterControllerComponent
    {
        GENERATED_BODY()
        
        FUNCTION(Script)
        void AddMovementInput(const glm::vec3& Move)
        {
            MoveInput += Move;
        }
        
        FUNCTION(Script)
        void AddLookInput(const glm::vec2& Look)
        {
            LookInput.x += Look.x;
            LookInput.y = glm::clamp(LookInput.y + Look.y, PitchClamp.x, PitchClamp.y);
        }
        
        FUNCTION(Script)
        void AddPitch(float Degrees)
        {
            LookInput.y = glm::clamp(LookInput.y + Degrees, PitchClamp.x, PitchClamp.y);
        }
        
        FUNCTION(Script)
        void AddYaw(float Degrees)
        {
            LookInput.x += Degrees;
        }
        
        FUNCTION(Script)
        glm::vec3 GetLookForward() const
        {
            float Pitch = glm::radians(-LookInput.y);
            float Yaw   = glm::radians(LookInput.x);

            return glm::normalize(glm::vec3(
                glm::cos(Pitch) * glm::sin(Yaw),
                glm::sin(Pitch),
                glm::cos(Pitch) * glm::cos(Yaw)
            ));
        }
        
        FUNCTION(Script)
        glm::vec3 GetLookRight() const
        {
            float Yaw = glm::radians(LookInput.x);
        
            return glm::normalize(glm::vec3(
                glm::cos(Yaw),
                0.0f,
                -glm::sin(Yaw)
            ));
        }
        
        FUNCTION(Script)
        void Jump() { bJumpPressed = true; }
        
        PROPERTY(Script, ReadOnly)
        glm::vec3 MoveInput;

        PROPERTY(Script, ReadOnly)
        glm::vec2 LookInput;
        
        PROPERTY(Script, Editable)
        glm::vec2 PitchClamp = glm::vec2(-89.9, 89.9);

        PROPERTY(Script, ReadOnly)
        bool bJumpPressed = false;
    };
}
