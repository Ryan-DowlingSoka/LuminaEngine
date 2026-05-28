#pragma once

// fastgltf element traits for Lumina math types — the in-house equivalent of
// fastgltf/glm_element_traits.hpp, so iterateAccessor<FVector3> etc. work after
// the glm removal. Layouts match (tight contiguous components), so fastgltf reads
// straight into them.

#include <fastgltf/tools.hpp>
#include "Core/Math/Vector/Vector.h"
#include "Core/Math/Matrix/Matrix.h"

namespace fastgltf
{
    template<> struct ElementTraits<Lumina::FVector2> : ElementTraitsBase<Lumina::FVector2, AccessorType::Vec2, float> {};
    template<> struct ElementTraits<Lumina::FVector3> : ElementTraitsBase<Lumina::FVector3, AccessorType::Vec3, float> {};
    template<> struct ElementTraits<Lumina::FVector4> : ElementTraitsBase<Lumina::FVector4, AccessorType::Vec4, float> {};

    template<> struct ElementTraits<Lumina::FIntVector2> : ElementTraitsBase<Lumina::FIntVector2, AccessorType::Vec2, std::int32_t> {};
    template<> struct ElementTraits<Lumina::FIntVector3> : ElementTraitsBase<Lumina::FIntVector3, AccessorType::Vec3, std::int32_t> {};
    template<> struct ElementTraits<Lumina::FIntVector4> : ElementTraitsBase<Lumina::FIntVector4, AccessorType::Vec4, std::int32_t> {};

    template<> struct ElementTraits<Lumina::FUIntVector2> : ElementTraitsBase<Lumina::FUIntVector2, AccessorType::Vec2, std::uint32_t> {};
    template<> struct ElementTraits<Lumina::FUIntVector3> : ElementTraitsBase<Lumina::FUIntVector3, AccessorType::Vec3, std::uint32_t> {};
    template<> struct ElementTraits<Lumina::FUIntVector4> : ElementTraitsBase<Lumina::FUIntVector4, AccessorType::Vec4, std::uint32_t> {};

    template<> struct ElementTraits<Lumina::FU8Vector2> : ElementTraitsBase<Lumina::FU8Vector2, AccessorType::Vec2, std::uint8_t> {};
    template<> struct ElementTraits<Lumina::FU8Vector3> : ElementTraitsBase<Lumina::FU8Vector3, AccessorType::Vec3, std::uint8_t> {};
    template<> struct ElementTraits<Lumina::FU8Vector4> : ElementTraitsBase<Lumina::FU8Vector4, AccessorType::Vec4, std::uint8_t> {};

    template<> struct ElementTraits<Lumina::FU16Vector2> : ElementTraitsBase<Lumina::FU16Vector2, AccessorType::Vec2, std::uint16_t> {};
    template<> struct ElementTraits<Lumina::FU16Vector3> : ElementTraitsBase<Lumina::FU16Vector3, AccessorType::Vec3, std::uint16_t> {};
    template<> struct ElementTraits<Lumina::FU16Vector4> : ElementTraitsBase<Lumina::FU16Vector4, AccessorType::Vec4, std::uint16_t> {};

    template<> struct ElementTraits<Lumina::FMatrix2> : ElementTraitsBase<Lumina::FMatrix2, AccessorType::Mat2, float> {};
    template<> struct ElementTraits<Lumina::FMatrix3> : ElementTraitsBase<Lumina::FMatrix3, AccessorType::Mat3, float> {};
    template<> struct ElementTraits<Lumina::FMatrix4> : ElementTraitsBase<Lumina::FMatrix4, AccessorType::Mat4, float> {};
}
