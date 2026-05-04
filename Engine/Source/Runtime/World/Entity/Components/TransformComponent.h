#pragma once

#include <glm/glm.hpp>
#include "Core/Threading/Thread.h"
#include "DirtyComponent.h"
#include "Core/Math/Transform.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Registry/EntityRegistry.h"
#include "TransformComponent.generated.h"

namespace Lumina
{

    REFLECT(Component, HideInComponentList)
    struct RUNTIME_API ALIGN_FOR_FALSE_SHARING STransformComponent
    {
        GENERATED_BODY()
    
        friend class CWorld;
    
        STransformComponent() = default;
        explicit STransformComponent(const FTransform& InTransform)
            : LocalTransform(InTransform)
            , WorldTransform(InTransform)
            , CachedMatrix(InTransform.GetMatrix())
        {}
    
        FUNCTION(Script)
        glm::vec3 GetLocalLocation() const { return LocalTransform.Location; }
    
        FUNCTION(Script)
        glm::quat GetLocalRotation() const { return LocalTransform.Rotation; }
    
        FUNCTION(Script)
        glm::vec3 GetLocalScale()    const { return LocalTransform.Scale; }
    
        FUNCTION(Script)
        glm::vec3 GetLocalRotationAsEuler() const
        {
            return glm::degrees(glm::eulerAngles(LocalTransform.Rotation));
        }
    
        FUNCTION(Script)
        glm::vec3 SetLocalLocation(const glm::vec3& InLocation)
        {
            LocalTransform.Location = InLocation;
            MarkDirty();
            return LocalTransform.Location;
        }
    
        FUNCTION(Script)
        glm::vec3 Translate(const glm::vec3& Delta)
        {
            LocalTransform.Location += Delta;
            MarkDirty();
            return LocalTransform.Location;
        }
    
        FUNCTION(Script)
        glm::quat SetLocalRotation(const glm::quat& InRotation)
        {
            LocalTransform.Rotation = InRotation;
            MarkDirty();
            return LocalTransform.Rotation;
        }
    
        FUNCTION(Script)
        glm::vec3 SetLocalRotationFromEuler(const glm::vec3& EulerDegrees)
        {
            LocalTransform.Rotation = glm::quat(glm::radians(EulerDegrees));
            MarkDirty();
            return GetLocalRotationAsEuler();
        }
    
        FUNCTION(Script)
        glm::vec3 AddLocalRotationFromEuler(const glm::vec3& EulerDegrees)
        {
            LocalTransform.Rotation = glm::quat(glm::radians(EulerDegrees)) * LocalTransform.Rotation;
            MarkDirty();
            return GetLocalRotationAsEuler();
        }
    
        FUNCTION(Script)
        void AddYaw(float Degrees)
        {
            glm::quat YawQuat = glm::angleAxis(glm::radians(Degrees), glm::vec3(0.f, 1.f, 0.f));
            LocalTransform.Rotation = glm::normalize(YawQuat * LocalTransform.Rotation);
            MarkDirty();
        }
    
        FUNCTION(Script)
        void AddPitch(float Degrees, float ClampMin = -89.9f, float ClampMax = 89.9f)
        {
            float Clamped = glm::clamp(Degrees, ClampMin, ClampMax);
            glm::quat PitchQuat = glm::angleAxis(glm::radians(Clamped), LocalTransform.GetRight());
            LocalTransform.Rotation = glm::normalize(PitchQuat * LocalTransform.Rotation);
            MarkDirty();
        }
    
        FUNCTION(Script)
        void AddRoll(float Degrees)
        {
            glm::quat RollQuat = glm::angleAxis(glm::radians(Degrees), LocalTransform.GetForward());
            LocalTransform.Rotation = glm::normalize(RollQuat * LocalTransform.Rotation);
            MarkDirty();
        }
    
        FUNCTION(Script)
        glm::vec3 SetLocalScale(const glm::vec3& InScale)
        {
            LocalTransform.Scale = InScale;
            MarkDirty();
            return LocalTransform.Scale;
        }
    
        FUNCTION(Script)
        glm::vec3 GetWorldLocation() const
        {
            ResolveIfDirty();
            return WorldTransform.Location;
        }
    
        FUNCTION(Script)
        glm::quat GetWorldRotation() const
        {
            ResolveIfDirty();
            return WorldTransform.Rotation;
        }
    
        FUNCTION(Script)
        glm::vec3 GetWorldScale() const
        {
            ResolveIfDirty();
            return WorldTransform.Scale;
        }
    
        FUNCTION(Script)
        glm::vec3 GetWorldRotationAsEuler() const
        {
            ResolveIfDirty();
            return glm::degrees(glm::eulerAngles(WorldTransform.Rotation));
        }
    
        FUNCTION(Script)
        glm::mat4 GetWorldMatrix() const
        {
            ResolveIfDirty();
            return CachedMatrix;
        }
        
        FUNCTION(Script)
        const FTransform& GetWorldTransform() const
        {
            ResolveIfDirty();
            return WorldTransform; 
        }
        
        FUNCTION(Script)
        void SetWorldTransform(const FTransform& InTransform)
        {
            WorldTransform = InTransform;
            MarkDirty();
        }
        
        FUNCTION(Script)
        void SetLocalTransform(const FTransform& InTransform)
        {
            LocalTransform = InTransform;
            MarkDirty();
        }
    
        FUNCTION(Script)
        glm::vec3 GetForward() const
        {
            ResolveIfDirty();
            return LocalTransform.GetForward();
        }
    
        FUNCTION(Script)
        glm::vec3 GetRight()   const
        {
            ResolveIfDirty();
            return LocalTransform.GetRight();
        }
    
        FUNCTION(Script)
        glm::vec3 GetUp()      const
        {
            ResolveIfDirty();
            return LocalTransform.GetUp();
        }
    
        FUNCTION(Script)
        float MaxScale() const
        {
            return glm::max(LocalTransform.Scale.x, glm::max(LocalTransform.Scale.y, LocalTransform.Scale.z));
        }
    
        FUNCTION(Script)
        glm::vec3 GetLocation() const { return GetLocalLocation(); }
    
        FUNCTION(Script)
        glm::vec3 GetPosition() const { return GetLocalLocation(); }
    
        FUNCTION(Script)
        glm::quat GetRotation() const { return GetLocalRotation(); }
    
        FUNCTION(Script)
        glm::vec3 GetScale()    const { return GetLocalScale(); }
    
        FUNCTION(Script)
        glm::vec3 SetLocation(const glm::vec3& L)    { return SetLocalLocation(L); }
    
        FUNCTION(Script)
        glm::quat SetRotation(const glm::quat& R)    { return SetLocalRotation(R); }
    
        FUNCTION(Script)
        glm::vec3 SetScale(const glm::vec3& S)       { return SetLocalScale(S); }
    
        FUNCTION(Script)
        glm::vec3 SetRotationFromEuler(const glm::vec3& E)  { return SetLocalRotationFromEuler(E); }
    
        FUNCTION(Script)
        glm::vec3 AddRotationFromEuler(const glm::vec3& E)  { return AddLocalRotationFromEuler(E); }
    
        FUNCTION(Script)
        glm::vec3 GetRotationAsEuler() const { return GetLocalRotationAsEuler(); }
    
    public:
        
        /** Local-space transform relative to the entity's parent (or world if no parent). */
        PROPERTY(Editable, Category = "Transform")
        FTransform LocalTransform;
    
        FTransform WorldTransform;
        glm::mat4  CachedMatrix = glm::mat4(1.f);
    
    private:
        
        void MarkDirty() const
        {
            if (!Registry)
            {
                return;
            }

            ECS::Utils::MarkTransformDirty(*Registry, Entity);
        }

        void ResolveIfDirty() const
        {
            // ResolveTransformChain handles the all_of check + locking
            // itself - we drop the unsynced pre-check here so a stale
            // read can't fire a chain resolve on already-clean data.
            if (Registry)
            {
                ECS::Utils::ResolveTransformChain(*Registry, Entity);
            }
        }
    
        FEntityRegistry*    Registry = nullptr;
        entt::entity        Entity   = entt::null;
        bool                bDirty = false;
    };
    
}
