#pragma once

#include "Core/Object/ObjectMacros.h"
#include "CharacterControllerComponent.generated.h"


namespace Lumina
{
    REFLECT(Component, Category = "Character")
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

        /** Add a velocity impulse to the character (jump pad, knockback, dash).
         *  Override flags replace the existing velocity on that axis instead of
         *  adding to it: horizontal = the X/Z plane, vertical = the Y (up) axis. */
        FUNCTION(Script)
        void Launch(const glm::vec3& Velocity, bool bOverrideHorizontal, bool bOverrideVertical)
        {
            PendingLaunchVelocity     = Velocity;
            bLaunchOverrideHorizontal = bOverrideHorizontal;
            bLaunchOverrideVertical   = bOverrideVertical;
            bLaunchRequested          = true;
        }

        /** Move the character to a world location. Use this instead of writing the
         *  transform directly: the physics capsule owns the entity's position and
         *  a plain transform write is overwritten on the next physics step. */
        FUNCTION(Script)
        void TeleportTo(const glm::vec3& Location)
        {
            PendingTeleportLocation = Location;
            bTeleportRequested      = true;
        }

        /** Accumulated movement input vector, consumed each physics frame. */
        PROPERTY(Script, ReadOnly)
        glm::vec3 MoveInput;

        /** Accumulated look input (X = yaw degrees, Y = pitch degrees). */
        PROPERTY(Script, ReadOnly)
        glm::vec2 LookInput;

        /** Minimum and maximum pitch angles (degrees) the look input is clamped to. */
        PROPERTY(Script, Editable)
        glm::vec2 PitchClamp = glm::vec2(-89.9, 89.9);

        /** True for one frame after Jump() is called; consumed by the movement system. */
        PROPERTY(Script, ReadOnly)
        bool bJumpPressed = false;

        // Transient Launch/Teleport requests, latched into the movement
        // component before the physics step (same path as bJumpPressed).
        glm::vec3 PendingLaunchVelocity     = glm::vec3(0.0f);
        bool      bLaunchRequested          = false;
        bool      bLaunchOverrideHorizontal = false;
        bool      bLaunchOverrideVertical   = false;
        glm::vec3 PendingTeleportLocation   = glm::vec3(0.0f);
        bool      bTeleportRequested        = false;
    };
}
