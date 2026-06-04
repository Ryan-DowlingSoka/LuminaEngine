#pragma once

#include "Core/Math/Math.h"
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
        FVector3 GetLocalLocation() const { return LocalTransform.Location; }
    
        FUNCTION(Script)
        FQuat GetLocalRotation() const { return LocalTransform.Rotation; }
    
        FUNCTION(Script)
        FVector3 GetLocalScale()    const { return LocalTransform.Scale; }
    
        FUNCTION(Script)
        FVector3 GetLocalRotationAsEuler() const
        {
            return Math::Degrees(Math::EulerAngles(LocalTransform.Rotation));
        }
    
        FUNCTION(Script)
        FVector3 SetLocalLocation(const FVector3& InLocation)
        {
            LocalTransform.Location = InLocation;
            MarkDirty();
            return LocalTransform.Location;
        }
    
        FUNCTION(Script)
        FVector3 Translate(const FVector3& Delta)
        {
            LocalTransform.Location += Delta;
            MarkDirty();
            return LocalTransform.Location;
        }
    
        FUNCTION(Script)
        FQuat SetLocalRotation(const FQuat& InRotation)
        {
            LocalTransform.Rotation = InRotation;
            MarkDirty();
            return LocalTransform.Rotation;
        }
    
        FUNCTION(Script)
        FVector3 SetLocalRotationFromEuler(const FVector3& EulerDegrees)
        {
            LocalTransform.Rotation = FQuat(Math::Radians(EulerDegrees));
            MarkDirty();
            return GetLocalRotationAsEuler();
        }
    
        FUNCTION(Script)
        FVector3 AddLocalRotationFromEuler(const FVector3& EulerDegrees)
        {
            LocalTransform.Rotation = FQuat(Math::Radians(EulerDegrees)) * LocalTransform.Rotation;
            MarkDirty();
            return GetLocalRotationAsEuler();
        }
    
        FUNCTION(Script)
        void AddYaw(float Degrees)
        {
            FQuat YawQuat = Math::AngleAxis(Math::Radians(Degrees), FVector3(0.f, 1.f, 0.f));
            LocalTransform.Rotation = Math::Normalize(YawQuat * LocalTransform.Rotation);
            MarkDirty();
        }
    
        FUNCTION(Script)
        void AddPitch(float Degrees, float ClampMin = -89.9f, float ClampMax = 89.9f)
        {
            float Clamped = Math::Clamp(Degrees, ClampMin, ClampMax);
            FQuat PitchQuat = Math::AngleAxis(Math::Radians(Clamped), LocalTransform.GetRight());
            LocalTransform.Rotation = Math::Normalize(PitchQuat * LocalTransform.Rotation);
            MarkDirty();
        }
    
        FUNCTION(Script)
        void AddRoll(float Degrees)
        {
            FQuat RollQuat = Math::AngleAxis(Math::Radians(Degrees), LocalTransform.GetForward());
            LocalTransform.Rotation = Math::Normalize(RollQuat * LocalTransform.Rotation);
            MarkDirty();
        }
    
        FUNCTION(Script)
        FVector3 SetLocalScale(const FVector3& InScale)
        {
            LocalTransform.Scale = InScale;
            MarkDirty();
            return LocalTransform.Scale;
        }
    
        FUNCTION(Script)
        FVector3 GetWorldLocation() const
        {
            ResolveIfDirty();
            return WorldTransform.Location;
        }
    
        FUNCTION(Script)
        FQuat GetWorldRotation() const
        {
            ResolveIfDirty();
            return WorldTransform.Rotation;
        }
    
        FUNCTION(Script)
        FVector3 GetWorldScale() const
        {
            ResolveIfDirty();
            return WorldTransform.Scale;
        }
    
        FUNCTION(Script)
        FVector3 GetWorldRotationAsEuler() const
        {
            ResolveIfDirty();
            return Math::Degrees(Math::EulerAngles(WorldTransform.Rotation));
        }
    
        FUNCTION(Script)
        FMatrix4 GetWorldMatrix() const
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

        // Cached world transform WITHOUT a resolve.
        const FVector3&   GetWorldLocationCached() const { return WorldTransform.Location; }
        const FQuat&      GetWorldRotationCached() const { return WorldTransform.Rotation; }
        const FVector3&   GetWorldScaleCached()    const { return WorldTransform.Scale; }
        const FMatrix4&   GetWorldMatrixCached()   const { return CachedMatrix; }

        FUNCTION(Script)
        void SetWorldTransform(const FTransform& InTransform)
        {
            // Convert to the parent-relative local transform; assigning WorldTransform directly
            // would be discarded by the next ResolveIfDirty (world = parentWorld * local).
            if (Registry)
            {
                ECS::Utils::SetEntityWorldTransform(*Registry, Entity, InTransform);
            }
        }
        
        FUNCTION(Script)
        void SetLocalTransform(const FTransform& InTransform)
        {
            LocalTransform = InTransform;
            MarkDirty();
        }
    
        FUNCTION(Script)
        FVector3 GetForward() const
        {
            ResolveIfDirty();
            return LocalTransform.GetForward();
        }
    
        FUNCTION(Script)
        FVector3 GetRight()   const
        {
            ResolveIfDirty();
            return LocalTransform.GetRight();
        }
    
        FUNCTION(Script)
        FVector3 GetUp()      const
        {
            ResolveIfDirty();
            return LocalTransform.GetUp();
        }
    
        FUNCTION(Script)
        float MaxScale() const
        {
            return Math::Max(LocalTransform.Scale.x, Math::Max(LocalTransform.Scale.y, LocalTransform.Scale.z));
        }
    
        FUNCTION(Script)
        FVector3 GetLocation() const { return GetLocalLocation(); }
    
        FUNCTION(Script)
        FVector3 GetPosition() const { return GetLocalLocation(); }
    
        FUNCTION(Script)
        FQuat GetRotation() const { return GetLocalRotation(); }
    
        FUNCTION(Script)
        FVector3 GetScale()    const { return GetLocalScale(); }
    
        FUNCTION(Script)
        FVector3 SetLocation(const FVector3& L)    { return SetLocalLocation(L); }
    
        FUNCTION(Script)
        FQuat SetRotation(const FQuat& R)    { return SetLocalRotation(R); }
    
        FUNCTION(Script)
        FVector3 SetScale(const FVector3& S)       { return SetLocalScale(S); }
    
        FUNCTION(Script)
        FVector3 SetRotationFromEuler(const FVector3& E)  { return SetLocalRotationFromEuler(E); }
    
        FUNCTION(Script)
        FVector3 AddRotationFromEuler(const FVector3& E)  { return AddLocalRotationFromEuler(E); }
    
        FUNCTION(Script)
        FVector3 GetRotationAsEuler() const { return GetLocalRotationAsEuler(); }

        // Bind this component to its owning entity. Called after duplication or post-load to rewire the
        // self-referential pointers used by MarkDirty/ResolveIfDirty. World init also does this directly via friend access.
        void Bind(FEntityRegistry& InRegistry, entt::entity InEntity)
        {
            Registry = &InRegistry;
            Entity = InEntity;
        }
        
        void SetRaw(const FVector3& Location, const FQuat& Rotation)
        {
            LocalTransform.Location = Location;
            LocalTransform.Rotation = Rotation;
        }
        
        void SetRaw(const FVector3& Location, const FQuat& Rotation, const FVector3& Scale)
        {
            LocalTransform.Location = Location;
            LocalTransform.Rotation = Rotation;
            LocalTransform.Scale    = Scale;
        }

    public:

        /** Local-space transform relative to the entity's parent (or world if no parent). */
        PROPERTY(Editable, Category = "Transform")
        FTransform LocalTransform;
    
        FTransform WorldTransform;
        FMatrix4  CachedMatrix = FMatrix4(1.f);
    
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
            // No pre-check: ResolveTransformChain does the all_of check + locking, so a stale read
            // can't fire a chain resolve on already-clean data.
            if (Registry)
            {
                ECS::Utils::ResolveTransformChain(*Registry, Entity);
            }
        }
    
        FEntityRegistry*    Registry = nullptr;
        entt::entity        Entity   = entt::null;
    };
    
}
