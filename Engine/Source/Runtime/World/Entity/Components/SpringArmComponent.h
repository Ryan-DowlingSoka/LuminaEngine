#pragma once
#include "Core/Object/ObjectMacros.h"
#include "SpringArmComponent.generated.h"

namespace Lumina
{
    REFLECT(Component)
    struct SSpringArmComponent
    {
        GENERATED_BODY()

        /** Desired distance from the pivot to the camera along the arm (meters). */
        PROPERTY(Editable)
        float TargetArmLength = 3.0f;

        /** Local-space offset applied at the camera end of the arm. */
        PROPERTY(Editable)
        glm::vec3 SocketOffset;

        /** Radius of the sphere probe used for collision testing along the arm. */
        PROPERTY(Editable)
        float ProbeSize = 0.2f;

        /** When true, the arm shrinks to avoid clipping into geometry. */
        PROPERTY(Editable)
        bool bDoCollisionTest = true;

        /** When true, the arm orientation follows the controller's look rotation. */
        PROPERTY(Editable)
        bool bUseControlRotation = true;
    
    };
}
