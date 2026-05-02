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

        /** Conservative sphere-vs-frustum; false positives near corners are acceptable for broad-phase. */
        NODISCARD bool IntersectsSphere(const glm::vec3& Center, float Radius) const
        {
            for (int i = 0; i < NUM; ++i)
            {
                const glm::vec4& P = Planes[i];
                const float Dist = P.x * Center.x + P.y * Center.y + P.z * Center.z + P.w;
                if (Dist < -Radius)
                {
                    return false;
                }
            }
            return true;
        }

        NODISCARD bool IsInside(const FAABB& aabb)
        {
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

        /** Sweeps the frustum along SweepDir for shadow culling; SweepDir points toward the light. */
        NODISCARD FFrustum Extruded(const glm::vec3& SweepDir, float SweepDistance) const
        {
            FFrustum Out;
            for (int i = 0; i < NUM; ++i)
            {
                const glm::vec4& P = Planes[i];
                const glm::vec3 N(P.x, P.y, P.z);
                const float Push = glm::max(0.0f, SweepDistance * glm::dot(N, -SweepDir));
                Out.Planes[i] = glm::vec4(N, P.w + Push);
            }
            return Out;
        }

        /** Extracts 6 normalized world-space planes; signed distance >= 0 inside. Matches GPU InFrustum convention. */
        static FFrustum FromViewProjection(const glm::mat4& VP)
        {
            FFrustum Out = {};
            Out.Planes[LEFT]   = glm::vec4(VP[0].w + VP[0].x, VP[1].w + VP[1].x, VP[2].w + VP[2].x, VP[3].w + VP[3].x);
            Out.Planes[RIGHT]  = glm::vec4(VP[0].w - VP[0].x, VP[1].w - VP[1].x, VP[2].w - VP[2].x, VP[3].w - VP[3].x);
            Out.Planes[TOP]    = glm::vec4(VP[0].w - VP[0].y, VP[1].w - VP[1].y, VP[2].w - VP[2].y, VP[3].w - VP[3].y);
            Out.Planes[BOTTOM] = glm::vec4(VP[0].w + VP[0].y, VP[1].w + VP[1].y, VP[2].w + VP[2].y, VP[3].w + VP[3].y);
            Out.Planes[BACK]   = glm::vec4(VP[0].w + VP[0].z, VP[1].w + VP[1].z, VP[2].w + VP[2].z, VP[3].w + VP[3].z);
            Out.Planes[FRONT]  = glm::vec4(VP[0].w - VP[0].z, VP[1].w - VP[1].z, VP[2].w - VP[2].z, VP[3].w - VP[3].z);

            for (int i = 0; i < NUM; ++i)
            {
                const glm::vec3 N(Out.Planes[i].x, Out.Planes[i].y, Out.Planes[i].z);
                const float Length = glm::length(N);
                if (Length > 0.0f)
                {
                    Out.Planes[i] /= Length;
                }
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
