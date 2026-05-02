#pragma once

#include "Core/Serialization/Archiver.h"

namespace Lumina
{
    inline uint32 PackColor(glm::vec4 color)
    {
        uint8 r = (uint8)(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
        uint8 g = (uint8)(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
        uint8 b = (uint8)(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
        uint8 a = (uint8)(glm::clamp(color.a, 0.0f, 1.0f) * 255.0f);
        return (a << 24) | (b << 16) | (g << 8) | r;
    }

    inline glm::vec4 UnpackColor(uint32 packed)
    {
        uint8 r = (packed >> 0) & 0xFF;
        uint8 g = (packed >> 8) & 0xFF;
        uint8 b = (packed >> 16) & 0xFF;
        uint8 a = (packed >> 24) & 0xFF;

        return glm::vec4(
            (float)r / 255.0f,
            (float)g / 255.0f,
            (float)b / 255.0f,
            (float)a / 255.0f
        );
    }

    // Octahedral 16-16 unit-normal pack.
    inline uint32 PackNormal(glm::vec3 n)
    {
        n /= glm::abs(n.x) + glm::abs(n.y) + glm::abs(n.z) + 1e-12f;
        glm::vec2 e = glm::vec2(n.x, n.y);
        if (n.z < 0.0f)
        {
            e = glm::vec2(
                (1.0f - glm::abs(e.y)) * (e.x >= 0.0f ? 1.0f : -1.0f),
                (1.0f - glm::abs(e.x)) * (e.y >= 0.0f ? 1.0f : -1.0f));
        }
        int32 qx = (int32)glm::round(glm::clamp(e.x, -1.0f, 1.0f) * 32767.0f);
        int32 qy = (int32)glm::round(glm::clamp(e.y, -1.0f, 1.0f) * 32767.0f);
        return ((uint32)(qx & 0xFFFF)) | (((uint32)(qy & 0xFFFF)) << 16);
    }

    inline glm::vec3 UnpackNormal(uint32 packed)
    {
        int16 sx = (int16)(packed & 0xFFFF);
        int16 sy = (int16)((packed >> 16) & 0xFFFF);
        glm::vec2 e = glm::vec2((float)sx, (float)sy) / 32767.0f;
        glm::vec3 n(e.x, e.y, 1.0f - glm::abs(e.x) - glm::abs(e.y));
        if (n.z < 0.0f)
        {
            float nx = (1.0f - glm::abs(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f);
            float ny = (1.0f - glm::abs(n.x)) * (n.y >= 0.0f ? 1.0f : -1.0f);
            n.x = nx;
            n.y = ny;
        }
        return glm::normalize(n);
    }

    // 10-10-10 pack of a meshlet-local integer offset. Dequant is
    // MeshOrigin + (LoInt + q) * GridStep.
    inline uint32 PackMeshletPosition(glm::ivec3 q)
    {
        return  (uint32(q.x) & 0x3FFu)
             | ((uint32(q.y) & 0x3FFu) << 10)
             | ((uint32(q.z) & 0x3FFu) << 20);
    }

    // Octahedral 15-15 tangent + 1-bit handedness packed into uint32 (4-byte aligned).
    // Layout: [0..14]=qx, [15..29]=qy, [30]=hand (1=+, 0=-), [31]=reserved.
    inline uint32 PackTangent(glm::vec3 t, float Sign)
    {
        t /= glm::abs(t.x) + glm::abs(t.y) + glm::abs(t.z) + 1e-12f;
        glm::vec2 e = glm::vec2(t.x, t.y);
        if (t.z < 0.0f)
        {
            e = glm::vec2(
                (1.0f - glm::abs(e.y)) * (e.x >= 0.0f ? 1.0f : -1.0f),
                (1.0f - glm::abs(e.x)) * (e.y >= 0.0f ? 1.0f : -1.0f));
        }
        int32 qx = (int32)glm::round(glm::clamp(e.x, -1.0f, 1.0f) * 16383.0f);
        int32 qy = (int32)glm::round(glm::clamp(e.y, -1.0f, 1.0f) * 16383.0f);
        uint32 Hand = (Sign >= 0.0f) ? 1u : 0u;
        return ((uint32)(qx & 0x7FFFu))
             | (((uint32)(qy & 0x7FFFu)) << 15)
             | (Hand << 30);
    }

    inline glm::vec4 UnpackTangent(uint32 Packed)
    {
        // Sign-extend 15-bit fields into int32.
        auto SignExtend15 = [](uint32 V) -> int32
        {
            int32 X = (int32)(V & 0x7FFFu);
            return (X & 0x4000) ? (X | int32(0xFFFF8000u)) : X;
        };
        int32 sx = SignExtend15(Packed);
        int32 sy = SignExtend15(Packed >> 15);
        glm::vec2 e = glm::vec2((float)sx, (float)sy) / 16383.0f;
        glm::vec3 t(e.x, e.y, 1.0f - glm::abs(e.x) - glm::abs(e.y));
        if (t.z < 0.0f)
        {
            float tx = (1.0f - glm::abs(t.y)) * (t.x >= 0.0f ? 1.0f : -1.0f);
            float ty = (1.0f - glm::abs(t.x)) * (t.y >= 0.0f ? 1.0f : -1.0f);
            t.x = tx;
            t.y = ty;
        }
        float Sign = ((Packed >> 30) & 1u) != 0u ? 1.0f : -1.0f;
        return glm::vec4(glm::normalize(t), Sign);
    }

    // Importers must zero Tangent: dedup byte-compares before MikkTSpace runs.
    // Cannot use member initializer — would break TCanBulkSerialize (needs trivial).
    struct FVertex
    {
        glm::vec3       Position;
        uint32          Normal;
        uint32          Tangent;
        uint32          UV;
        uint32          Color;

        friend FArchive& operator<<(FArchive& Ar, FVertex& Data)
        {
            Ar << Data.Position;
            Ar << Data.Normal;
            Ar << Data.Tangent;
            Ar << Data.UV;
            Ar << Data.Color;
            return Ar;
        }
    };

    struct FSkinnedVertex : FVertex
    {
        glm::u8vec4     JointIndices;
        glm::u8vec4     JointWeights;

        friend FArchive& operator<<(FArchive& Ar, FSkinnedVertex& Data)
        {
            Ar << Data.Position;
            Ar << Data.Normal;
            Ar << Data.Tangent;
            Ar << Data.UV;
            Ar << Data.Color;
            Ar << Data.JointIndices;
            Ar << Data.JointWeights;
            return Ar;
        }
    };

    // 20-byte runtime vertex. Position is 10-10-10 q (see PackMeshletPosition);
    // Tangent is 15-15 octahedral + 1-bit handedness (PackTangent).
    struct FMeshletVertex
    {
        uint32 Position;
        uint32 Normal;
        uint32 Tangent;
        uint32 UV;
        uint32 Color;
    };

    struct FMeshletSkinnedVertex : FMeshletVertex
    {
        uint32 JointIndices;
        uint32 JointWeights;
    };

    struct FSimpleElementVertex
    {
        glm::vec3   Position;
        uint32      Color;
    };

    struct FBillboardVertex
    {
        glm::vec3   Position;
        float       Size;
    };

    static_assert(sizeof(FVertex) == 28);
    static_assert(sizeof(FSkinnedVertex) == 36);
    static_assert(sizeof(FMeshletVertex) == 20);
    static_assert(sizeof(FMeshletSkinnedVertex) == 28);
    static_assert(offsetof(FVertex, Position) == 0);
    static_assert(TCanBulkSerialize<FVertex>::value);
    static_assert(TCanBulkSerialize<FSkinnedVertex>::value);
    static_assert(TCanBulkSerialize<FMeshletVertex>::value);
    static_assert(TCanBulkSerialize<FMeshletSkinnedVertex>::value);
    static_assert(TCanBulkSerialize<FBillboardVertex>::value);
    static_assert(TCanBulkSerialize<FSimpleElementVertex>::value);

}
