#pragma once

// Jolt/Jolt.h must precede every other Jolt subheader.
#include <Jolt/Jolt.h>
#include <Jolt/Core/Color.h>
#include <Jolt/Physics/Body/MotionType.h>
#include "Jolt/Physics/Collision/ObjectLayer.h"
#include "Physics/PhysicsTypes.h"

namespace Lumina::JoltUtils
{
    JPH::ObjectLayer PackToObjectLayer(const FCollisionProfile& Profile);

    JPH::Vec3 ToJPHVec3(const glm::vec3& Vec);
    glm::vec3 FromJPHVec3(const JPH::Vec3& Vec);

    JPH::Vec4 ToJPHVec4(const glm::vec4& Vec);
    glm::vec4 FromJPHVec4(const JPH::Vec4& Vec);

    JPH::Quat ToJPHQuat(const glm::quat& Quat);
    glm::quat FromJPHQuat(const JPH::Quat& Quat);

    JPH::Mat44 ToJPHMat44(const glm::mat4& Mat);
    glm::mat4 FromJPHMat44(const JPH::Mat44& Mat);

    JPH::RVec3 ToJPHRVec3(const glm::dvec3& Vec);
    glm::dvec3 FromJPHRVec3(const JPH::RVec3& Vec);

    JPH::Color ToJPHColor(const glm::vec4& Color);
    glm::vec4 FromJPHColor(const JPH::Color& Color);

    JPH::RMat44 ToJPHRMat44(const glm::mat4& Mat);
    glm::mat4 FromJPHRMat44(const JPH::RMat44& Mat);

    JPH::EMotionType ToJoltMotionType(EBodyType Type);
}
