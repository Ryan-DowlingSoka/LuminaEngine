#pragma once

#include "Core/DisableAllWarnings.h"
#include "Core/Math/Frustum.h"
#include "Core/Object/ObjectMacros.h"
PRAGMA_DISABLE_ALL_WARNINGS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
PRAGMA_ENABLE_ALL_WARNINGS

namespace Lumina
{
    class RUNTIME_API FViewVolume
    {
    public:

        FViewVolume(float fov = 90.0f, float aspect = 16.0f / 9.0f, float InNear = 0.01f, float InFar = 100000.0f);

        FViewVolume& SetNear(float InNear);
        FViewVolume& SetFar(float InFar);
        FViewVolume& SetViewPosition(const glm::vec3& Position);
        FViewVolume& SetView(const glm::vec3& Position, const glm::vec3& ViewDirection, const glm::vec3& UpDirection);
        FViewVolume& SetPerspective(float InFov, float InAspect);
        FViewVolume& SetAspectRatio(float InAspect);
        FViewVolume& SetFOV(float InFOV);
        FViewVolume& Rotate(float Angle, glm::vec3 Axis);
        
        FORCEINLINE const glm::vec3& GetViewPosition() const { return ViewPosition; }

        FORCEINLINE const glm::mat4& GetViewMatrix() const { return ViewMatrix; }
        FORCEINLINE glm::mat4 GetInverseViewMatrix() const { return glm::inverse(ViewMatrix); }
        FORCEINLINE const glm::mat4& GetViewProjectionMatrix() const { return ViewProjectionMatrix; }
        FORCEINLINE const glm::mat4& GetProjectionMatrix() const { return ProjectionMatrix; }
        FORCEINLINE glm::mat4 GetInverseProjectionMatrix() const { return glm::inverse(ProjectionMatrix); }
        FORCEINLINE const glm::vec3& GetForwardVector() const { return ForwardVector; }
        FORCEINLINE const glm::vec3& GetRightVector() const { return RightVector; }
        FORCEINLINE const glm::vec3& GetUpVector() const { return UpVector; }
        glm::mat4 ToReverseDepthViewProjectionMatrix() const;

        FORCEINLINE float GetNear() const { return Near; }
        FORCEINLINE float GetFar() const { return Far; }
        FORCEINLINE FFrustum GetFrustum() const;
        FORCEINLINE float GetFOV() const { return FOV; }
        FORCEINLINE float GetAspectRatio() const { return AspectRatio; }

        static glm::vec3 UpAxis;
        static glm::vec3 DownAxis;
        static glm::vec3 RightAxis;
        static glm::vec3 LeftAxis;
        static glm::vec3 ForwardAxis;
        static glm::vec3 BackwardAxis;
        
    private:

        void UpdateMatrices();

        glm::vec3           ViewPosition;
        glm::vec3           ForwardVector;
        glm::vec3           UpVector;
        glm::vec3           RightVector;

        glm::mat4           ProjectionMatrix;
        glm::mat4           ViewMatrix;
        glm::mat4           ViewProjectionMatrix;

        float               Near;
        float               Far;
        
        float               FOV = 90.0f;
        float               AspectRatio = 16.0f/9.0f;
    };
}
