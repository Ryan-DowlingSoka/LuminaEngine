#pragma once

#include "Core/Serialization/Archiver.h"

namespace Lumina
{

    inline uint32 PackNormal(glm::vec3 normal)
    {
        int x = (int)(glm::clamp(normal.x, -1.0f, 1.0f) * 511.0f);
        int y = (int)(glm::clamp(normal.y, -1.0f, 1.0f) * 511.0f);
        int z = (int)(glm::clamp(normal.z, -1.0f, 1.0f) * 511.0f);
        return ((x & 0x3FF) << 0) | ((y & 0x3FF) << 10) | ((z & 0x3FF) << 20);
    }

    inline uint32 PackColor(glm::vec4 color)
    {
        uint8 r = (uint8)(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
        uint8 g = (uint8)(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
        uint8 b = (uint8)(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
        uint8 a = (uint8)(glm::clamp(color.a, 0.0f, 1.0f) * 255.0f);
        return (a << 24) | (b << 16) | (g << 8) | r;
    }
    
    inline glm::vec3 UnpackNormal(uint32 packed)
    {
        int x = (packed >> 0) & 0x3FF;
        int y = (packed >> 10) & 0x3FF;
        int z = (packed >> 20) & 0x3FF;
    
        if (x & 0x200)
        {
            x |= 0xFFFFFC00;
        }
        if (y & 0x200)
        {
            y |= 0xFFFFFC00;
        }
        if (z & 0x200)
        {
            z |= 0xFFFFFC00;
        }

        return glm::vec3(
            (float)x / 511.0f,
            (float)y / 511.0f,
            (float)z / 511.0f
        );
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
    
    enum class EVertexFormat : uint8
    {
        Static,
        Skinned,
    };
    
    struct alignas(16) FVertex
    {
        glm::vec3       Position;
        uint32          Normal;
        uint32          UV;
        uint32          Color;
        //              4 bytes of padding.
        
        friend FArchive& operator<<(FArchive& Ar, FVertex& Data)
        {
            Ar << Data.Position;
            Ar << Data.Normal;
            Ar << Data.UV;
            Ar << Data.Color;
            
            return Ar;
        }
    };
    
    struct alignas(16) FSkinnedVertex : FVertex
    {
        glm::u8vec4   JointIndices;
        glm::u8vec4   JointWeights;

        
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

    static_assert(offsetof(FVertex, Position) == 0, "Required FVertex::Position to be the first member.");
    static_assert(TCanBulkSerialize<FVertex>::value);
    static_assert(TCanBulkSerialize<FSkinnedVertex>::value);
    static_assert(TCanBulkSerialize<FBillboardVertex>::value);
    static_assert(TCanBulkSerialize<FSimpleElementVertex>::value);

}
