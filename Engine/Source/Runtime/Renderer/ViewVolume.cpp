#include "pch.h"
#include "ViewVolume.h"


namespace Lumina
{

    glm::vec3 FViewVolume::UpAxis        = glm::vec3(0.0f,  1.0f,  0.0f);
    glm::vec3 FViewVolume::DownAxis      = glm::vec3(0.0f, -1.0f,  0.0f);
    glm::vec3 FViewVolume::RightAxis     = glm::vec3(1.0f,  0.0f,  0.0f);
    glm::vec3 FViewVolume::LeftAxis      = glm::vec3(-1.0f, 0.0f,  0.0f);
    glm::vec3 FViewVolume::ForwardAxis   = glm::vec3(0.0f,  0.0f,  1.0f);
    glm::vec3 FViewVolume::BackwardAxis  = glm::vec3(0.0f,  0.0f, -1.0f);
    
    FViewVolume::FViewVolume(float fov, float aspect, float InNear, float InFar)
        : ViewPosition(glm::vec3(1.0))
        // Seeded so SetView's normalize/cross chain never sees uninitialized memory.
        , ForwardVector(ForwardAxis)
        , UpVector(UpAxis)
        , RightVector(RightAxis)
        , Near(InNear)
        , Far(InFar)
        , FOV(fov)
        , AspectRatio(aspect)
    {
        SetPerspective(fov, aspect);
        SetView(glm::vec3(0.0), ForwardAxis, UpAxis);
    }

    // Y-flip bakes Vulkan +Y-down NDC into the matrix; reverse-Z via swapped Far/Near.
    static glm::mat4 BuildVulkanReverseZPerspective(float FovDegrees, float Aspect, float Near, float Far)
    {
        glm::mat4 P = glm::perspective(glm::radians(FovDegrees), Aspect, Far, Near);
        P[1][1] *= -1.0f;
        return P;
    }

    FViewVolume& FViewVolume::SetNear(float InNear)
    {
        Near = InNear;
        ProjectionMatrix = BuildVulkanReverseZPerspective(FOV, AspectRatio, Near, Far);
        UpdateMatrices();

        return *this;
    }

    FViewVolume& FViewVolume::SetFar(float InFar)
    {
        Far = InFar;
        ProjectionMatrix = BuildVulkanReverseZPerspective(FOV, AspectRatio, Near, Far);
        UpdateMatrices();

        return *this;
    }


    FViewVolume& FViewVolume::SetViewPosition(const glm::vec3& Position)
    {
        ViewPosition = Position;
        UpdateMatrices();

        return *this;
    }

    FViewVolume& FViewVolume::SetView(const glm::vec3& Position, const glm::vec3& ViewDirection, const glm::vec3& UpDirection)
    {
        ViewPosition    = Position;
        UpVector        = glm::normalize(UpDirection);
        ForwardVector   = glm::normalize(ViewDirection);
        RightVector     = glm::normalize(glm::cross(UpVector, ForwardVector));
        UpVector        = glm::normalize(glm::cross(ForwardVector, RightVector));

        UpdateMatrices();

        return *this;
    }


    FViewVolume& FViewVolume::SetPerspective(float fov, float aspect)
    {
        FOV = fov;
        AspectRatio = aspect;

        ProjectionMatrix = BuildVulkanReverseZPerspective(FOV, AspectRatio, Near, Far);
        UpdateMatrices();

        return *this;
    }


    FViewVolume& FViewVolume::SetAspectRatio(float InAspect)
    {
        AspectRatio = InAspect;

        ProjectionMatrix = BuildVulkanReverseZPerspective(FOV, AspectRatio, Near, Far);
        UpdateMatrices();

        return *this;
    }

    FViewVolume& FViewVolume::SetFOV(float InFOV)
    {
        FOV = InFOV;
        ProjectionMatrix = BuildVulkanReverseZPerspective(FOV, AspectRatio, Near, Far);
        UpdateMatrices();

        return *this;
    }

    FViewVolume& FViewVolume::Rotate(float Angle, glm::vec3 Axis)
    {
        float Radians   = glm::radians(Angle);
        glm::mat4 R     = glm::rotate(glm::mat4(1), Radians, Axis);
        
        ForwardVector = glm::normalize(R * glm::vec4(ForwardVector, 0));
        UpVector      = glm::normalize(R * glm::vec4(UpVector, 0));
        
        RightVector   = glm::normalize(glm::cross(UpVector, ForwardVector));

        UpdateMatrices();
        return *this;
    }

    glm::mat4 FViewVolume::ToReverseDepthViewProjectionMatrix() const
    {
        // Standard-Z projection (not reverse-Z) for shadow face VPs; keeps Vulkan Y-flip.
        glm::mat4 P = glm::perspective(glm::radians(FOV), AspectRatio, Near, Far);
        P[1][1] *= -1.0f;
        return P * ViewMatrix;
    }

    FFrustum FViewVolume::GetFrustum() const
    {
        const glm::mat4& matrix = ViewProjectionMatrix;

        FFrustum Frustum = {};
        Frustum.Planes[FFrustum::LEFT].x = matrix[0].w + matrix[0].x;
        Frustum.Planes[FFrustum::LEFT].y = matrix[1].w + matrix[1].x;
        Frustum.Planes[FFrustum::LEFT].z = matrix[2].w + matrix[2].x;
        Frustum.Planes[FFrustum::LEFT].w = matrix[3].w + matrix[3].x;

        Frustum.Planes[FFrustum::RIGHT].x = matrix[0].w - matrix[0].x;
        Frustum.Planes[FFrustum::RIGHT].y = matrix[1].w - matrix[1].x;
        Frustum.Planes[FFrustum::RIGHT].z = matrix[2].w - matrix[2].x;
        Frustum.Planes[FFrustum::RIGHT].w = matrix[3].w - matrix[3].x;

        Frustum.Planes[FFrustum::TOP].x = matrix[0].w - matrix[0].y;
        Frustum.Planes[FFrustum::TOP].y = matrix[1].w - matrix[1].y;
        Frustum.Planes[FFrustum::TOP].z = matrix[2].w - matrix[2].y;
        Frustum.Planes[FFrustum::TOP].w = matrix[3].w - matrix[3].y;

        Frustum.Planes[FFrustum::BOTTOM].x = matrix[0].w + matrix[0].y;
        Frustum.Planes[FFrustum::BOTTOM].y = matrix[1].w + matrix[1].y;
        Frustum.Planes[FFrustum::BOTTOM].z = matrix[2].w + matrix[2].y;
        Frustum.Planes[FFrustum::BOTTOM].w = matrix[3].w + matrix[3].y;

        Frustum.Planes[FFrustum::BACK].x = matrix[0].w + matrix[0].z;
        Frustum.Planes[FFrustum::BACK].y = matrix[1].w + matrix[1].z;
        Frustum.Planes[FFrustum::BACK].z = matrix[2].w + matrix[2].z;
        Frustum.Planes[FFrustum::BACK].w = matrix[3].w + matrix[3].z;

        Frustum.Planes[FFrustum::FRONT].x = matrix[0].w - matrix[0].z;
        Frustum.Planes[FFrustum::FRONT].y = matrix[1].w - matrix[1].z;
        Frustum.Planes[FFrustum::FRONT].z = matrix[2].w - matrix[2].z;
        Frustum.Planes[FFrustum::FRONT].w = matrix[3].w - matrix[3].z;

        for (auto i = 0; i < FFrustum::NUM; i++)
        {
            float length = glm::sqrt(Frustum.Planes[i].x * Frustum.Planes[i].x + Frustum.Planes[i].y * Frustum.Planes[i].y + Frustum.Planes[i].z * Frustum.Planes[i].z);
            Frustum.Planes[i] /= length;
        }

        return Frustum;
    }
    
    void FViewVolume::UpdateMatrices()
    {
        ViewMatrix = glm::lookAt(ViewPosition, ViewPosition + ForwardVector, UpVector);
        ViewProjectionMatrix = ProjectionMatrix * ViewMatrix;
    }
}
