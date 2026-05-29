#include "pch.h"
#include "ComponentVisualizer.h"
#include "Core/Math/Color.h"
#include "Renderer/PrimitiveDrawInterface.h"
#include "Tools/Import/ImportHelpers.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/CharacterComponent.h"
#include "world/entity/components/lightcomponent.h"
#include "world/entity/components/physicscomponent.h"
#include "World/Entity/Components/TransformComponent.h"

namespace Lumina
{
    static CComponentVisualizerRegistry* Singleton = nullptr;
    
    
    CComponentVisualizerRegistry& CComponentVisualizerRegistry::Get()
    {
        static std::once_flag Flag;
        std::call_once(Flag, []()
        {
            Singleton = NewObject<CComponentVisualizerRegistry>();
        });

        return *Singleton;
    }

    void CComponentVisualizerRegistry::RegisterComponentVisualizer(CComponentVisualizer* Visualizer)
    {
        if (CStruct* SupportedType = Visualizer->GetSupportedComponentType())
        {
            Visualizers.emplace(SupportedType, Visualizer);
        }
    }

    CComponentVisualizer* CComponentVisualizerRegistry::GetComponentVisualizer(CStruct* Component)
    {
        auto It = Visualizers.find(Component);
        if (It != Visualizers.end())
        {
            return It->second;
        }
        
        return nullptr;
    }

    void CComponentVisualizer::PostCreateCDO()
    {
        CComponentVisualizerRegistry::Get().RegisterComponentVisualizer(this);
    }

    CStruct* CComponentVisualizer_PointLight::GetSupportedComponentType() const
    {
        return SPointLightComponent::StaticStruct();
    }

    void CComponentVisualizer_PointLight::Draw(IPrimitiveDrawInterface* PDI, entt::registry& Registry, entt::entity Entity)
    {
        const SPointLightComponent& PointLight = Registry.get<SPointLightComponent>(Entity);
        const STransformComponent& Transform = Registry.get<STransformComponent>(Entity);
        
        PDI->DrawSphere(Transform.GetWorldLocation(), PointLight.Attenuation, 
            FVector4(PointLight.LightColor, 1.0f), 32, 1.0f, true, 0.0f);
    }

    CStruct* CComponentVisualizer_SpotLight::GetSupportedComponentType() const
    {
        return SSpotLightComponent::StaticStruct();
    }

    void CComponentVisualizer_SpotLight::Draw(IPrimitiveDrawInterface* PDI, entt::registry& Registry, entt::entity Entity)
    {
        const SSpotLightComponent& SpotLight    = Registry.get<SSpotLightComponent>(Entity);
        const STransformComponent& Transform    = Registry.get<STransformComponent>(Entity);
        FVector3 Forward                       = Transform.GetWorldRotation() * FViewVolume::ForwardAxis;

        PDI->DrawCone(Transform.GetWorldLocation(), -Forward, Math::Radians(SpotLight.OuterConeAngle), SpotLight.Attenuation, FVector4(SpotLight.LightColor, 1.0f));
        PDI->DrawCone(Transform.GetWorldLocation(), -Forward, Math::Radians(SpotLight.InnerConeAngle), SpotLight.Attenuation, FVector4(SpotLight.LightColor, 1.0f));
    }

    CStruct* CComponentVisualizer_DirectionalLight::GetSupportedComponentType() const
    {
        return SDirectionalLightComponent::StaticStruct();
    }

    void CComponentVisualizer_DirectionalLight::Draw(IPrimitiveDrawInterface* PDI, entt::registry& Registry, entt::entity Entity)
    {
        const auto& Light       = Registry.get<SDirectionalLightComponent>(Entity);
        const auto& Transform   = Registry.get<STransformComponent>(Entity);
        
        PDI->DrawArrow(Transform.GetWorldLocation(), -Light.Direction, 1.5f, FColor::Yellow, 4.0f);
    }

    CStruct* CComponentVisualizer_SphereCollider::GetSupportedComponentType() const
    {
        return SSphereColliderComponent::StaticStruct();
    }

    void CComponentVisualizer_SphereCollider::Draw(IPrimitiveDrawInterface* PDI, entt::registry& Registry, entt::entity Entity)
    {
        const SSphereColliderComponent& Sphere = Registry.get<SSphereColliderComponent>(Entity);
        const STransformComponent& Transform = Registry.get<STransformComponent>(Entity);
        
        PDI->DrawSphere(Transform.GetWorldLocation() + Sphere.TranslationOffset, Sphere.Radius * Transform.MaxScale(), FColor::Green, 12, 3.5f, true, 0.0f);
    }

    CStruct* CComponentVisualizer_BoxCollider::GetSupportedComponentType() const
    {
        return SBoxColliderComponent::StaticStruct();
    }

    void CComponentVisualizer_BoxCollider::Draw(IPrimitiveDrawInterface* PDI, entt::registry& Registry, entt::entity Entity)
    {
        const SBoxColliderComponent& Box = Registry.get<SBoxColliderComponent>(Entity);
        const STransformComponent& Transform = Registry.get<STransformComponent>(Entity);
        
        FQuat OffsetQuat(Box.RotationOffset);
        PDI->DrawBox(Transform.GetWorldLocation() + Box.TranslationOffset, Box.HalfExtent * Transform.GetWorldScale(), Transform.GetWorldRotation() * OffsetQuat, FColor::Green, 3.5f, true, 0.0f);
    }

    CStruct* CComponentVisualizer_CapsuleCollider::GetSupportedComponentType() const
    {
        return SCapsuleColliderComponent::StaticStruct();
    }

    void CComponentVisualizer_CapsuleCollider::Draw(IPrimitiveDrawInterface* PDI, entt::registry& Registry, entt::entity Entity)
    {
        const SCapsuleColliderComponent& Capsule = Registry.get<SCapsuleColliderComponent>(Entity);
        const STransformComponent& Transform     = Registry.get<STransformComponent>(Entity);

        // DrawCapsule wants the two cylinder-axis endpoints (caps tangent there), Y-aligned in local space.
        const float Scale     = Transform.MaxScale();
        const FVector3 Center = Transform.GetWorldLocation() + Capsule.TranslationOffset;
        const FQuat WorldRot  = Transform.GetWorldRotation() * FQuat(Capsule.RotationOffset);
        const FVector3 Axis   = WorldRot * FVector3(0.0f, Capsule.HalfHeight * Scale, 0.0f);

        PDI->DrawCapsule(Center - Axis, Center + Axis, Capsule.Radius * Scale, FColor::Green, 12, 3.5f, true, 0.0f);
    }

    CStruct* CComponentVisualizer_CylinderCollider::GetSupportedComponentType() const
    {
        return SCylinderColliderComponent::StaticStruct();
    }

    void CComponentVisualizer_CylinderCollider::Draw(IPrimitiveDrawInterface* PDI, entt::registry& Registry, entt::entity Entity)
    {
        const SCylinderColliderComponent& Cyl = Registry.get<SCylinderColliderComponent>(Entity);
        const STransformComponent& Transform  = Registry.get<STransformComponent>(Entity);

        // No DrawCylinder primitive: stitch one from two rings plus N vertical spokes. Y-aligned in local space.
        const float Scale     = Transform.MaxScale();
        const FVector3 Center = Transform.GetWorldLocation() + Cyl.TranslationOffset;
        const FQuat WorldRot  = Transform.GetWorldRotation() * FQuat(Cyl.RotationOffset);
        const float Radius    = Cyl.Radius * Scale;
        const float HalfH     = Cyl.HalfHeight * Scale;

        const FVector3 Up     = WorldRot * FVector3(0.0f, 1.0f, 0.0f);
        const FVector3 Right  = WorldRot * FVector3(1.0f, 0.0f, 0.0f);
        const FVector3 Fwd    = WorldRot * FVector3(0.0f, 0.0f, 1.0f);
        const FVector3 Top    = Center + Up * HalfH;
        const FVector3 Bottom = Center - Up * HalfH;

        constexpr int kSegments = 24;
        FVector3 PrevTop, PrevBottom;
        for (int i = 0; i <= kSegments; ++i)
        {
            const float A    = (float(i) / float(kSegments)) * Math::TwoPi<float>();
            const float Cs   = Math::Cos(A);
            const float Sn   = Math::Sin(A);
            const FVector3 Offset = Right * (Cs * Radius) + Fwd * (Sn * Radius);
            const FVector3 T = Top    + Offset;
            const FVector3 B = Bottom + Offset;
            if (i > 0)
            {
                PDI->DrawLine(PrevTop,    T, FColor::Green, 3.5f, true, 0.0f);
                PDI->DrawLine(PrevBottom, B, FColor::Green, 3.5f, true, 0.0f);
                // A few vertical spokes (every 6 segments) so the side is readable.
                if ((i % 6) == 0)
                {
                    PDI->DrawLine(T, B, FColor::Green, 3.5f, true, 0.0f);
                }
            }
            PrevTop = T;
            PrevBottom = B;
        }
    }

    CStruct* CComponentVisualizer_CharacterPhysics::GetSupportedComponentType() const
    {
        return SCharacterPhysicsComponent::StaticStruct();
    }

    void CComponentVisualizer_CharacterPhysics::Draw(IPrimitiveDrawInterface* PDI, entt::registry& Registry, entt::entity Entity)
    {
        const SCharacterPhysicsComponent& Character = Registry.get<SCharacterPhysicsComponent>(Entity);
        const STransformComponent& Transform = Registry.get<STransformComponent>(Entity);

        // Match Jolt: Start/End are cylinder-axis endpoints; Radius scales by MaxScale.
        const FVector3 Location = Transform.GetWorldLocation();
        const FVector3 Axis = Transform.GetWorldRotation() * FVector3(0.0f, Character.HalfHeight, 0.0f);
        const FVector3 Start = Location - Axis;
        const FVector3 End   = Location + Axis;

        PDI->DrawCapsule(Start, End, Character.Radius * Transform.MaxScale(), FVector4(0.0f, 1.0f, 0.0f, 1.0f), 12, 2.0f, true, 0.0f);
    }

    CStruct* CComponentVisualizer_RigidBody::GetSupportedComponentType() const
    {
        return SRigidBodyComponent::StaticStruct();
    }

    void CComponentVisualizer_RigidBody::Draw(IPrimitiveDrawInterface* PDI, entt::registry& Registry, entt::entity Entity)
    {
        const SRigidBodyComponent& Body      = Registry.get<SRigidBodyComponent>(Entity);
        const STransformComponent& Transform = Registry.get<STransformComponent>(Entity);

        if (Math::Dot(Body.CenterOfMassOffset, Body.CenterOfMassOffset) <= 0.0f)
        {
            return;
        }

        const FVector3 WorldCOM = Transform.GetWorldLocation()
            + Transform.GetWorldRotation() * (Body.CenterOfMassOffset * Transform.GetWorldScale());

        PDI->DrawSphere(WorldCOM, 0.08f, FVector4(1.0f, 0.0f, 1.0f, 1.0f), 12, 2.0f, false, 0.0f);
        PDI->DrawLine(Transform.GetWorldLocation(), WorldCOM, FVector4(1.0f, 0.0f, 1.0f, 1.0f), 2.0f, false, 0.0f);
    }

    CStruct* CComponentVisualizer_Camera::GetSupportedComponentType() const
    {
        return SCameraComponent::StaticStruct();
    }

    void CComponentVisualizer_Camera::Draw(IPrimitiveDrawInterface* PDI, entt::registry& Registry, entt::entity Entity)
    {
        const auto& Transform   = Registry.get<STransformComponent>(Entity);
        const auto& Camera      = Registry.get<SCameraComponent>(Entity);

        
        PDI->DrawFrustum(Camera.GetViewProjectionMatrix(), 0.01f, 1000.0f, FColor::White, 4.0f);
        PDI->DrawArrow(Transform.GetWorldLocation(), Transform.GetWorldRotation() * FVector3(0.0, 0.0, 1.0), 3.5f, FColor::Green, 4.0f);
    }
}
