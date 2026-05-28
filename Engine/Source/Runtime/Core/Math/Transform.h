#pragma once

#include <format>
#include "Core/Math/Matrix/MatrixMath.h"
#include "Core/Object/ObjectMacros.h"
#include "Transform.generated.h"

namespace Lumina
{
    REFLECT()
    struct RUNTIME_API FTransform
    {
        GENERATED_BODY()
    
		PROPERTY(Script, Editable)
        FVector3 Location;

        PROPERTY(Script, Editable)
        FQuat Rotation;

        PROPERTY(Script, Editable)
        FVector3 Scale;
        
        FTransform()
            : Location(0.0f),
              Rotation(FQuat(1.0f, 0.0f, 0.0f, 0.0f)),
              Scale(1.0f, 1.0f, 1.0f)
        {}

        FTransform(const FVector3& InPosition)
            : Location(InPosition),
              Rotation(FQuat(1.0f, 0.0f, 0.0f, 0.0f)),
              Scale(1.0f, 1.0f, 1.0f)
        {}

        FTransform(const FVector3& location, const FVector3& EulerAngles, const FVector3& scale)
            : Location(location),
              Rotation(FQuat(Math::Radians(EulerAngles))),
              Scale(scale)
        {
        }

        FTransform(const FMatrix4& InMatrix)
        {
            FVector3 Skew;
            FVector4 Perspective;
            Math::Decompose(InMatrix, Scale, Rotation, Location, Skew, Perspective);
        }

        FMatrix4 GetMatrix() const
        {
            FMatrix4 T = Math::Translate(FMatrix4(1.0f), Location);
            FMatrix4 R = Math::ToMatrix4(Rotation);
            FMatrix4 S = Math::Scale(FMatrix4(1.0f), Scale);

            return T * R * S;
        }
        
        FORCEINLINE FVector3 GetForward() const
        {
            return Rotation * FVector3(0.0f, 0.0f, 1.0f);
        }

        FORCEINLINE FVector3 GetRight() const
        {
            return Rotation * FVector3(1.0f, 0.0f, 0.0f);
        }

        FORCEINLINE FVector3 GetUp() const
        {
            return Rotation * FVector3(0.0f, 1.0f, 0.0f);
        }

        FORCEINLINE void SetRotationFromEuler(const FVector3& EulerAngles)
        {
            Rotation = FQuat(Math::Radians(EulerAngles));
        }

        FORCEINLINE void Translate(const FVector3& Translation)
        {
            Location += Translation;
        }

        FORCEINLINE void SetLocation(const FVector3& NewLocation)
        {
            Location = NewLocation;
        }

        FORCEINLINE void Rotate(const FVector3& EulerAngles)
        {
            FQuat AdditionalRotation = FQuat(Math::Radians(EulerAngles));
            Rotation = AdditionalRotation * Rotation;
        }

        FORCEINLINE void SetScale(const FVector3& ScaleFactors)
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
            Inv.Rotation    = Math::Conjugate(Rotation);
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
