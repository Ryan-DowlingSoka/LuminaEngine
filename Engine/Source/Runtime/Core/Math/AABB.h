#pragma once
#include "glm/glm.hpp"
#include "Platform/Platform.h"
#include "Core/Object/ObjectMacros.h"
#include "AABB.generated.h"


namespace Lumina
{
    REFLECT()
    struct FAABB
    {
        GENERATED_BODY()
        
        /** Minimum corner of the axis-aligned bounding box in local space. */
        PROPERTY(Script, Editable)
        glm::vec3 Min;

        /** Maximum corner of the axis-aligned bounding box in local space. */
        PROPERTY(Script, Editable)
        glm::vec3 Max;
        
        FAABB()
            : Min(0.0f), Max(0.0f)
        {}

        FAABB(const glm::vec3& InMin, const glm::vec3& InMax)
            : Min(InMin), Max(InMax)
        {}

        FUNCTION(Script)
        FORCEINLINE float MaxScale() const { return glm::max(GetSize().x, glm::max(GetSize().y, GetSize().z)); }
        
        FUNCTION(Script)
        FORCEINLINE glm::vec3 GetSize() const { return Max - Min; }
        
        FUNCTION(Script)
        FORCEINLINE glm::vec3 GetCenter() const { return Min + GetSize() * 0.5f; }
        
        NODISCARD FAABB ToWorld(const glm::mat4& World) const
        {
            glm::vec3 NewMin = glm::vec3(World[3]);
            glm::vec3 NewMax = glm::vec3(World[3]);

            for (int i = 0; i < 3; i++)
            {
                glm::vec3 Axis = glm::vec3(World[i]);

                glm::vec3 MinContrib = Axis * Min[i];
                glm::vec3 MaxContrib = Axis * Max[i];

                NewMin += glm::min(MinContrib, MaxContrib);
                NewMax += glm::max(MinContrib, MaxContrib);
            }

            return FAABB(NewMin, NewMax);
        }
    };
}
