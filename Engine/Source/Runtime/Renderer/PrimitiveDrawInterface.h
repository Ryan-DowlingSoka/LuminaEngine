#pragma once
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    class FRHIImage;
    class FViewVolume;

    class RUNTIME_API IPrimitiveDrawInterface
    {
    public:
        
        IPrimitiveDrawInterface() = default;
        virtual ~IPrimitiveDrawInterface() = default;
        IPrimitiveDrawInterface(const IPrimitiveDrawInterface&) = default;
        IPrimitiveDrawInterface& operator=(const IPrimitiveDrawInterface&) = default;
        IPrimitiveDrawInterface(IPrimitiveDrawInterface&&) = default;
        IPrimitiveDrawInterface& operator=(IPrimitiveDrawInterface&&) = default;
        
        virtual void DrawBillboard(FRHIImage* Image, const glm::vec3& Location, float Scale) = 0;
        
        
        virtual void DrawLine(const glm::vec3& Start, const glm::vec3& End, const glm::vec4& Color, float Thickness = 1.0f, bool bDepthTest = true, float Duration = 0.0f) = 0;
        void DrawBox(const glm::vec3& Center, const glm::vec3& HalfExtents, const glm::quat& Rotation, const glm::vec4& Color, float Thickness = 1.0f, bool bDepthTest = true, float Duration = 0.0f);
        void DrawBoxCorners(const glm::vec3& Center, const glm::vec3& HalfExtents, const glm::quat& Rotation, const glm::vec4& Color, float CornerFraction = 0.25f, float Thickness = 1.0f, bool bDepthTest = true, float Duration = 0.0f);
        void DrawSphere(const glm::vec3& Center, float Radius, const glm::vec4& Color, uint8 Segments = 16, float Thickness = 1.0f, bool bDepthTest = true, float Duration = 0.0f);
        void DrawCapsule(const glm::vec3& Start, const glm::vec3& End, float Radius, const glm::vec4& Color, uint8 Segments = 16, float Thickness = 1.0f, bool bDepthTest = true, float Duration = 0.0f);
        void DrawCone(const glm::vec3& Apex, const glm::vec3& Direction, float AngleRadians, float Length, const glm::vec4& Color, uint8 Segments = 16, uint8 Stacks = 4, float Thickness = 1.0f, bool bDepthTest = true, float Duration = 0.0f);
        void DrawFrustum(const glm::mat4& Matrix, float zNear, float zFar, const glm::vec4& Color, float Thickness = 1.0f, bool bDepthTest = true, float Duration = 0.0f);
        void DrawArrow(const glm::vec3& Start, const glm::vec3& Direction, float Length, const glm::vec4& Color, float Thickness = 1.0f, bool bDepthTest = true, float Duration = 0.0f, float HeadSize = 0.2f);
        void DrawViewVolume(const FViewVolume& ViewVolume, const glm::vec4& Color, float Thickness = 1.0f, bool bDepthTest = true, float Duration = 0.0f);
        
    };
}
