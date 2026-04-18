#pragma once

#include <glm/glm.hpp>

#include "AABB.h"
#include "Containers/Array.h"
#include "Core/Profiler/Profile.h"
#include "Platform/Platform.h"

namespace Lumina
{
    struct FFrustum
    {
        enum ESide { LEFT = 0, RIGHT = 1, TOP = 2, BOTTOM = 3, BACK = 4, FRONT = 5, NUM = 6};
        TArray<glm::vec4, NUM> Planes;

        NODISCARD bool IsInside(const glm::vec3& Point)
        {
            LUMINA_PROFILE_SCOPE();
            
            for (int i = 0; i < NUM; i++)
            {
                const glm::vec4& p = Planes[i];
                float dist = p.x * Point.x + p.y * Point.y + p.z * Point.z + p.w;
                if (dist < 0)
                {
                    return false;
                }
            }
            return true;
        }

        NODISCARD bool IsInside(const FAABB& aabb)
        {
            LUMINA_PROFILE_SCOPE();

            for (int i = 0; i < NUM; i++)
            {
                const glm::vec4& p = Planes[i];
                glm::vec3 normal(p.x, p.y, p.z);

                glm::vec3 positive;
                positive.x = (normal.x >= 0.0f) ? aabb.Max.x : aabb.Min.x;
                positive.y = (normal.y >= 0.0f) ? aabb.Max.y : aabb.Min.y;
                positive.z = (normal.z >= 0.0f) ? aabb.Max.z : aabb.Min.z;

                float dist = glm::dot(normal, positive) + p.w;

                if (dist < 0.0f)
                {
                    return false;
                }
            }

            return true;
        }

        /**
         * Returns a frustum that bounds the Minkowski sum of this frustum and the
         * line segment { t * SweepDir : 0 <= t <= SweepDistance }. In practice:
         * takes this (camera) frustum and extends it along the sun-light
         * direction so shadow casters sitting *outside* the camera view but
         * *between* the sun and the camera view still get included in the
         * shadow-cull pass. Planes whose outward-facing normal has a component
         * into -SweepDir are pushed outward by that component's length.
         *
         * SweepDir should point from the shadow caster toward the light (i.e.
         * the sun direction vector, same convention as FLight::Direction).
         */
        NODISCARD FFrustum Extruded(const glm::vec3& SweepDir, float SweepDistance) const
        {
            FFrustum Out;
            for (int i = 0; i < NUM; ++i)
            {
                const glm::vec4& P = Planes[i];
                const glm::vec3 N(P.x, P.y, P.z);
                // Inside half-space is N.X + d >= 0. For points in the swept
                // volume (X or X + t*SweepDir), worst-case is X - max(0, t * dot(N, -SweepDir)).
                // So relax d by max(0, SweepDistance * dot(N, -SweepDir)).
                const float Push = glm::max(0.0f, SweepDistance * glm::dot(N, -SweepDir));
                Out.Planes[i] = glm::vec4(N, P.w + Push);
            }
            return Out;
        }

        static void ComputeFrustumCorners(const glm::mat4& ViewProjection, glm::vec3 OutCorners[8])
        {
            LUMINA_PROFILE_SCOPE();

            glm::mat4 InverseVP = glm::inverse(ViewProjection);

            for (int x = 0; x < 2; ++x)
            {
                for (int y = 0; y < 2; ++y)
                {
                    for (int z = 0; z < 2; ++z)
                    {
                        const int Index = x + y * 2 + z * 4;

                        const glm::vec4 Pt = InverseVP * glm::vec4(
                            2.0f * x - 1.0f,
                            2.0f * y - 1.0f, 
                            2.0f * z - 1.0f, 
                            1.0f);
                        
                        OutCorners[Index] = Pt / Pt.w;
                    }
                }
            }
        }
    };
}
