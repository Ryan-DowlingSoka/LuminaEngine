#pragma once

// Jolt/Jolt.h must precede every other Jolt subheader; previously inherited
// from pch.h which is being slimmed.
#include <Jolt/Jolt.h>
#include <Jolt/Core/Color.h>
#include <Jolt/Physics/Body/MotionType.h>
#include "Jolt/Physics/Collision/ObjectLayer.h"
#include "Physics/PhysicsTypes.h"

namespace Lumina::JoltUtils
{
    JPH::ObjectLayer PackToObjectLayer(const FCollisionProfile& Profile);
    
    // Vec3 conversions
    JPH::Vec3 ToJPHVec3(const glm::vec3& Vec);
    glm::vec3 FromJPHVec3(const JPH::Vec3& Vec);
    
    // Vec4 conversions
    JPH::Vec4 ToJPHVec4(const glm::vec4& Vec);
    glm::vec4 FromJPHVec4(const JPH::Vec4& Vec);
    
    // Quaternion conversions
    JPH::Quat ToJPHQuat(const glm::quat& Quat);
    glm::quat FromJPHQuat(const JPH::Quat& Quat);
    
    // Matrix conversions (4x4)
    JPH::Mat44 ToJPHMat44(const glm::mat4& Mat);
    glm::mat4 FromJPHMat44(const JPH::Mat44& Mat);
    
    // RVec3 (double precision) conversions
    JPH::RVec3 ToJPHRVec3(const glm::dvec3& Vec);
    glm::dvec3 FromJPHRVec3(const JPH::RVec3& Vec);
    
    // Color conversions (if you use JPH::Color)
    JPH::Color ToJPHColor(const glm::vec4& Color);
    glm::vec4 FromJPHColor(const JPH::Color& Color);
    
    // Transform conversions (position + rotation)
    JPH::RMat44 ToJPHRMat44(const glm::mat4& Mat);
    glm::mat4 FromJPHRMat44(const JPH::RMat44& Mat);
    
    JPH::EMotionType ToJoltMotionType(EBodyType Type);
}
