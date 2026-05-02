#pragma once

#include <format>
#include "glm/glm.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include "Core/Object/ObjectMacros.h"
#include "Transform.generated.h"

namespace Lumina
{
    REFLECT()
    struct RUNTIME_API FTransform
    {
        GENERATED_BODY()
    
		PROPERTY(Script, Editable)
        glm::vec3 Location;

        PROPERTY(Script, Editable)
        glm::quat Rotation;

        PROPERTY(Script, Editable)
        glm::vec3 Scale;
        
        FTransform()
            : Location(0.0f),
              Rotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)),
              Scale(1.0f, 1.0f, 1.0f)
        {}

        FTransform(const glm::vec3& InPosition)
            : Location(InPosition),
              Rotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)),
              Scale(1.0f, 1.0f, 1.0f)
        {}

        FTransform(const glm::vec3& location, const glm::vec3& EulerAngles, const glm::vec3& scale)
            : Location(location),
              Rotation(glm::quat(glm::radians(EulerAngles))),
              Scale(scale)
        {
        }

        FTransform(const glm::mat4& InMatrix)
        {
            glm::vec3 Skew;
            glm::vec4 Perspective;
            glm::decompose(InMatrix, Scale, Rotation, Location, Skew, Perspective);
        }

        glm::mat4 GetMatrix() const
        {
            glm::mat4 T = glm::translate(glm::mat4(1.0f), Location);
            glm::mat4 R = glm::mat4_cast(Rotation);
            glm::mat4 S = glm::scale(glm::mat4(1.0f), Scale);

            return T * R * S;
        }
        
        FORCEINLINE glm::vec3 GetForward() const
        {
            return Rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        }

        FORCEINLINE glm::vec3 GetRight() const
        {
            return Rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        }

        FORCEINLINE glm::vec3 GetUp() const
        {
            return Rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        }

        FORCEINLINE void SetRotationFromEuler(const glm::vec3& EulerAngles)
        {
            Rotation = glm::quat(glm::radians(EulerAngles));
        }

        FORCEINLINE void Translate(const glm::vec3& Translation)
        {
            Location += Translation;
        }

        FORCEINLINE void SetLocation(const glm::vec3& NewLocation)
        {
            Location = NewLocation;
        }

        FORCEINLINE void Rotate(const glm::vec3& EulerAngles)
        {
            glm::quat AdditionalRotation = glm::quat(glm::radians(EulerAngles));
            Rotation = AdditionalRotation * Rotation;
        }

        FORCEINLINE void SetScale(const glm::vec3& ScaleFactors)
        {
            Scale *= ScaleFactors;
        }

        bool operator==(const FTransform& Other) const
        {
            return Location == Other.Location &&
                   Rotation == Other.Rotation &&
                   Scale == Other.Scale;
        }

        bool operator!=(const FTransform& Other) const
        {
            return !(*this == Other);
        }

        FTransform operator*(const FTransform& Other) const
        {
            FTransform Result;
            Result.Scale    = Scale * Other.Scale;
            Result.Rotation = Rotation * Other.Rotation;
            Result.Location = (Rotation * (Scale * Other.Location)) + Location;
            return Result;
        }

        FTransform& operator*=(const FTransform& Other)
        {
            *this = operator*(Other);
            return *this;
        }

        FTransform Inverse() const
        {
            FTransform Inv;
            Inv.Scale       = 1.0f / Scale;
            Inv.Rotation    = glm::conjugate(Rotation);
            Inv.Location    = Inv.Rotation * (Inv.Scale * (-Location));
            return Inv;
        }
    };
}

template <>
struct std::formatter<Lumina::FTransform>
{
    constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const Lumina::FTransform& transform, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), 
            "Location: ({:.2f}, {:.2f}, {:.2f}) | Rotation: ({:.2f}, {:.2f}, {:.2f}, {:.2f}) | Scale: ({:.2f}, {:.2f}, {:.2f})",
            transform.Location.x, transform.Location.y, transform.Location.z,
            transform.Rotation.w, transform.Rotation.x, transform.Rotation.y, transform.Rotation.z,
            transform.Scale.x, transform.Scale.y, transform.Scale.z);
    }
};
