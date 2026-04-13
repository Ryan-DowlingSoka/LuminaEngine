#pragma once

#include <algorithm>
#include "RenderResource.h"
#include "Containers/Array.h"
#include "Core/Functional/FunctionRef.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    class FRHIBuffer;
}

namespace Lumina::RenderUtils
{
    /**
     * 
     * @param Buffer Buffer needing a resize
     * @param DesiredSize New desired size.
     * @return true if the buffer was resized.
     */
    RUNTIME_API bool ResizeBufferIfNeeded(FRHIBufferRef& Buffer, uint32 DesiredSize, float GrowthFactor);
    
    RUNTIME_API FRHIImageRef CreateImageFromPixels(TSpan<uint8> PixelData, bool bFlipVertically = true, glm::uvec2 Size = {});
    
    inline uint32 CalculateMipCount(uint32 Width, uint32 Height)
    {
        uint32 Levels = 1;
        while (Width > 1 || Height > 1)
        {
            Width = std::max(Width >> 1, 1u);
            Height = std::max(Height >> 1, 1u);
            ++Levels;
        }
        return Levels;
    }
    
    inline glm::uvec2 SplitAddress(uint64 Address) 
    {
        return glm::uvec2(static_cast<uint32>(Address & 0xFFFFFFFFull), static_cast<uint32>(Address >> 32));
    }

    inline uint32 GetMipDim(uint32 BaseWidth, uint32 Level)
    {
        return std::max(1u, BaseWidth >> Level);
    }

    inline uint32 GetGroupCount(uint32 ThreadCount, uint32 LocalSize)
    {
        return (ThreadCount + LocalSize - 1) / LocalSize;
    };

    constexpr uint32 CreateViewMask(TSpan<uint32> Layers)
    {
        uint32 Mask = 0;
        for(uint32_t layer : Layers)
        {
            Mask |= (1u << layer);
        }
        return Mask;
    }

    template<uint32... Layers>
    requires ((Layers < 32) && ...)
    constexpr uint32 CreateViewMask()
    {
        return ((1u << Layers) | ...);
    }


    inline glm::quat GetCameraRotation(float yaw, float pitch)
    {
        float yawRad = glm::radians(yaw);
        float pitchRad = glm::radians(pitch);

        glm::quat pitchQuat = glm::angleAxis(pitchRad, glm::vec3(1, 0, 0));
        glm::quat yawQuat = glm::angleAxis(yawRad, glm::vec3(0, 1, 0));

        return yawQuat * pitchQuat;
    }

    inline glm::vec3 GetForwardVector(float yaw, float pitch)
    {
        float yawRad = glm::radians(yaw);
        float pitchRad = glm::radians(pitch);
        
        return glm::vec3(
            glm::cos(pitchRad) * glm::sin(yawRad),
            -glm::sin(pitchRad),
            glm::cos(pitchRad) * glm::cos(yawRad)
        );
    }

    inline glm::vec3 GetRightVector(float yaw)
    {
        float yawRad = glm::radians(yaw);
        return glm::vec3(glm::cos(yawRad), 0.0f, -glm::sin(yawRad));
    }

    inline glm::vec3 GetUpVector(float yaw, float pitch)
    {
        return glm::cross(GetRightVector(yaw), GetForwardVector(yaw, pitch));
    }

    inline glm::vec3 GetCameraPosition(const glm::vec3& characterPos, float eyeHeight)
    {
        return characterPos + glm::vec3(0, eyeHeight, 0);
    }
}
