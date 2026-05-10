#pragma once
#include "Containers/Array.h"
#include "Core/Engine/Engine.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Renderer/ViewVolume.h"
#include "PostProcessSettings.h"
#include "CameraComponent.generated.h"


namespace Lumina
{
    class CMaterialInterface;
    REFLECT(Component, Category = "Camera")
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

        /** Per-camera color grading + tone mapping. The render scene reads
         *  this from the active camera each frame and applies it during the
         *  final composite pass. Defaults give an identity grade with AGX
         *  tone mapping. */
        PROPERTY(Editable, Category = "Camera")
        SPostProcessSettings PostProcess;

        /** Post-process materials applied in order after tone mapping.
         *  Each entry runs as a fullscreen pass over the previous output;
         *  the material's Emissive output replaces the scene color. Order
         *  matters -- earlier entries are read by later entries via
         *  SceneColor. Materials must have MaterialType = PostProcess. */
        PROPERTY(Editable, Category = "Camera|Post Process")
        TVector<TObjectPtr<CMaterialInterface>> PostProcessMaterials;

    private:

        FViewVolume ViewVolume;
    };

    struct RUNTIME_API FSwitchActiveCameraEvent
    {
        entt::entity NewActiveEntity;
    };
}
