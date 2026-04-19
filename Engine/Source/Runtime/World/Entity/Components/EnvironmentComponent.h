#pragma once
#include "Core/Object/ObjectMacros.h"
#include "EnvironmentComponent.generated.h"

namespace Lumina
{
    REFLECT(Component)
    struct RUNTIME_API SEnvironmentComponent
    {
        GENERATED_BODY()
        
        /** RGB color of the ambient sky light contribution. */
        PROPERTY(Editable, Color, Category = "Ambient Light")
        glm::vec3 AmbientColor = glm::vec3(1.0f);

        /** Brightness multiplier for the ambient light. */
        PROPERTY(Editable, Category = "Ambient Light")
        float Intensity = 0.005f;
        
    };
}
