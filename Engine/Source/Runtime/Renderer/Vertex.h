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

    // Octahedral 16-16 normal pack. 16 bits per axis fits the same uint slot
    // as the old 8-8-8 SNORM but matches SNORM 16-16-16 quality.
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

    // 16-16-16 unsigned position pack relative to a mesh-global AABB.
    // AABBScale = (max - min) / 65535 so the shader dequant collapses to a
    // MAD per axis. The basis is shared across every meshlet of a mesh, so
    // vertices duplicated at meshlet seams quantize to the same world
    // position; per-meshlet AABBs would T-junction along seams.
    struct FPackedMeshletPosition
    {
        uint32 XY; // X in low 16, Y in high 16
        uint32 Z;  // Z in low 16, high 16 reserved (zero)
    };

    inline FPackedMeshletPosition PackMeshletPosition(glm::vec3 P, glm::vec3 AABBMin, glm::vec3 AABBExtent)
    {
        glm::vec3 Norm = (P - AABBMin) / glm::max(AABBExtent, glm::vec3(1e-12f));
        Norm = glm::clamp(Norm, glm::vec3(0.0f), glm::vec3(1.0f));
        uint32 qx = (uint32)glm::round(Norm.x * 65535.0f) & 0xFFFFu;
        uint32 qy = (uint32)glm::round(Norm.y * 65535.0f) & 0xFFFFu;
        uint32 qz = (uint32)glm::round(Norm.z * 65535.0f) & 0xFFFFu;
        return { qx | (qy << 16), qz };
    }

    inline glm::vec3 UnpackMeshletPosition(uint32 PackedXY, uint32 PackedZ, glm::vec3 AABBMin, glm::vec3 AABBScale)
    {
        uint32 qx =  PackedXY        & 0xFFFFu;
        uint32 qy = (PackedXY >> 16) & 0xFFFFu;
        uint32 qz =  PackedZ         & 0xFFFFu;
        return AABBMin + glm::vec3((float)qx, (float)qy, (float)qz) * AABBScale;
    }

    // Import-time, full-precision vertex. Importers and ThumbnailManager
    // populate these; GenerateMeshlets converts them into the per-meshlet
    // packed FMeshletVertex stream and the array is dropped from disk.
    struct FVertex
    {
        glm::vec3       Position;
        uint32          Normal;
        uint32          UV;
        uint32          Color;

        friend FArchive& operator<<(FArchive& Ar, FVertex& Data)
        {
            Ar << Data.Position;
            Ar << Data.Normal;
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
            Ar << Data.UV;
            Ar << Data.Color;
            Ar << Data.JointIndices;
            Ar << Data.JointWeights;
            return Ar;
        }
    };

    // 20-byte runtime vertex stored per-meshlet. Position is 16-16-16
    // quantized to a mesh-global AABB (carried in FMeshletHeaderGPU),
    // Normal is octahedral 16-16. Mirrored by the shader-side FVertex.
    struct FMeshletVertex
    {
        uint32 PositionXY;
        uint32 PositionZ;
        uint32 Normal;
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

    static_assert(sizeof(FVertex) == 24);
    static_assert(sizeof(FSkinnedVertex) == 32);
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
