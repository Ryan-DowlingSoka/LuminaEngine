#pragma once
#include "Core/Engine/Engine.h"
#include "Renderer/ViewVolume.h"
#include "CameraComponent.generated.h"


namespace Lumina
{
    REFLECT(Component)
    struct RUNTIME_API SCameraComponent
    {
        GENERATED_BODY()
        
        SCameraComponent(float fov = 90.0f, float aspect = 16.0f / 9.0f)
            :ViewVolume(fov, aspect)
        {}

        void SetView(const glm::vec3& Position, const glm::vec3& ViewDirection, const glm::vec3& UpDirection)
        {
            ViewVolume.SetView(Position, ViewDirection, UpDirection);
        }
        
        void SetFOV(float NewFOV)
        {
            ViewVolume.SetFOV(NewFOV);
        }
        
        void SetAspectRatio(float NewAspect)
        {
            ViewVolume.SetPerspective(ViewVolume.GetFOV(), NewAspect);
        }

        void SetPosition(const glm::vec3& NewPosition)
        {
            ViewVolume.SetViewPosition(NewPosition);
        }

        float GetFOV() const { return ViewVolume.GetFOV(); }
        float GetAspectRatio() const { return ViewVolume.GetAspectRatio(); }
        const glm::mat4& GetViewMatrix() const { return ViewVolume.GetViewMatrix(); }
        const glm::mat4& GetProjectionMatrix() const { return ViewVolume.GetProjectionMatrix(); }
        const glm::mat4& GetViewProjectionMatrix() const { return ViewVolume.GetViewProjectionMatrix(); }
        const FViewVolume& GetViewVolume() const { return ViewVolume; }
        
        FUNCTION(Script)
        glm::vec3 GetPosition() const { return ViewVolume.GetViewPosition(); }
        
        FUNCTION(Script)
        glm::vec3 GetForwardVector() const { return ViewVolume.GetForwardVector(); }
        
        FUNCTION(Script)
        glm::vec3 GetRightVector() const { return ViewVolume.GetRightVector(); }

        /** Vertical field of view in degrees. */
        PROPERTY(Editable, Category = "Camera")
        float FOV = 90.0f;

        /** When true, this camera activates automatically when the entity is spawned. */
        PROPERTY(Editable, Category = "Camera")
        bool bAutoActivate = false;
        
    private:
        
        FViewVolume ViewVolume;
    };

    struct RUNTIME_API FSwitchActiveCameraEvent
    {
        entt::entity NewActiveEntity;
    };
}
