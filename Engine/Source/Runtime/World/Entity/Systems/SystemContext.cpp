#include "pch.h"
#include "SystemContext.h"
#include "World/World.h"
#include "World/Entity/Components/DirtyComponent.h"
#include "World/Entity/Components/LifetimeComponent.h"
#include "world/entity/components/namecomponent.h"

namespace Lumina
{
    FSystemContext::FSystemContext(CWorld* InWorld)
        : World(InWorld)
        , Registry(InWorld->EntityRegistry)
        , Dispatcher(InWorld->SingletonDispatcher)
    {}


    void FSystemContext::SetEntityLifetime(entt::entity Entity, float Lifetime) const
    {
        Registry.get_or_emplace<SLifetimeComponent>(Entity).Lifetime = Lifetime;
    }

    entt::runtime_view FSystemContext::CreateRuntimeView(const THashSet<entt::id_type>& Components) const
    {
        entt::runtime_view RuntimeView;
        
        for (entt::id_type Type : Components)
        {
            entt::meta_type Meta = entt::resolve(Type);
            if (!Meta)
            {
                if (entt::basic_sparse_set<>* Storage = Registry.storage(Type))
                {
                    RuntimeView.iterate(*Storage);
                }
            }
            else if (entt::basic_sparse_set<>* Storage = Registry.storage(Meta.info().hash()))
            {
                RuntimeView.iterate(*Storage);
            }
        }

        return RuntimeView;
    }

    void FSystemContext::ActivateBody(uint32 BodyID)
    {
        World->PhysicsScene->ActivateBody(BodyID);
    }

    void FSystemContext::DeactivateBody(uint32 BodyID)
    {
        World->PhysicsScene->DeactivateBody(BodyID);
    }

    void FSystemContext::ChangeBodyMotionType(uint32 BodyID, EBodyType NewType)
    {
        World->PhysicsScene->ChangeBodyMotionType(BodyID, NewType);
    }

    uint32 FSystemContext::GetEntityBodyID(entt::entity Entity) const
    {
        return World->PhysicsScene ? World->PhysicsScene->GetEntityBodyID(Entity) : ~0u;
    }
    

    TVector<SRayResult> FSystemContext::CastSphere(const SSphereCastSettings& Settings) const
    {
        return World->CastSphere(Settings);
    }

    STransformComponent& FSystemContext::GetEntityTransform(entt::entity Entity) const
    {
        return Get<STransformComponent>(Entity);
    }

    glm::vec3 FSystemContext::TranslateEntity(entt::entity Entity, const glm::vec3& Translation)
    {
        glm::vec3 NewLocation = Registry.get<STransformComponent>(Entity).Translate(Translation);
        return NewLocation;
    }

    void FSystemContext::SetEntityLocation(entt::entity Entity, const glm::vec3& Location)
    {
        Registry.get<STransformComponent>(Entity).SetLocation(Location);
    }

    void FSystemContext::SetEntityRotation(entt::entity Entity, const glm::quat& Rotation)
    {
        Registry.get<STransformComponent>(Entity).SetRotation(Rotation);
    }

    void FSystemContext::SetEntityScale(entt::entity Entity, const glm::vec3& Scale)
    {
        Registry.get<STransformComponent>(Entity).SetScale(Scale);
    }

    void FSystemContext::DrawDebugLine(const glm::vec3& Start, const glm::vec3& End, const glm::vec4& Color, float Thickness, float Duration) const
    {
        World->DrawLine(Start, End, Color, Thickness, true, Duration);
    }

    void FSystemContext::DrawDebugBox(const glm::vec3& Center, const glm::vec3& Extents, const glm::quat& Rotation, const glm::vec4& Color, float Thickness, float Duration) const
    {
        World->DrawBox(Center, Extents, Rotation, Color, Thickness, true, Duration);
    }

    void FSystemContext::DrawDebugSphere(const glm::vec3& Center, float Radius, const glm::vec4& Color, uint8 Segments, float Thickness, float Duration) const
    {
        World->DrawSphere(Center, Radius, Color, Segments, Thickness, true, Duration);
    }

    void FSystemContext::DrawDebugCone(const glm::vec3& Apex, const glm::vec3& Direction, float AngleRadians, float Length, const glm::vec4& Color, uint8 Segments, uint8 Stacks, float Thickness, float Duration) const
    {
        World->DrawCone(Apex, Direction, AngleRadians, Length, Color, Segments, Stacks, Thickness, true, Duration);
    }

    void FSystemContext::DrawFrustum(const glm::mat4& Matrix, float zNear, float zFar, const glm::vec4& Color, float Thickness, float Duration) const
    {
        World->DrawFrustum(Matrix, zNear, zFar, Color, Thickness, true, Duration);
    }

    void FSystemContext::DrawDebugArrow(const glm::vec3& Start, const glm::vec3& Direction, float Length, const glm::vec4& Color, float Thickness, float Duration, float HeadSize) const
    {
        World->DrawArrow(Start, Direction, Length, Color, Thickness, true, Duration, HeadSize);
    }
    
    entt::entity FSystemContext::Create(glm::vec3 Location) const
    {
        LUMINA_PROFILE_SCOPE();

        entt::entity EntityID = Registry.create();
        Registry.emplace<STransformComponent>(EntityID).SetLocation(Location);
        Registry.emplace<SNameComponent>(EntityID).Name = "Entity";
        Registry.emplace_or_replace<FNeedsTransformUpdate>(EntityID);
        return EntityID;
    }
    
    entt::entity FSystemContext::Create() const
    {
        LUMINA_PROFILE_SCOPE();
        
        entt::entity EntityID = Registry.create();
        Registry.emplace<STransformComponent>(EntityID);
        Registry.emplace<SNameComponent>(EntityID).Name = "Entity";
        Registry.emplace_or_replace<FNeedsTransformUpdate>(EntityID);
        return EntityID;
    }

    size_t FSystemContext::GetNumEntities() const
    {
        return Registry.storage<entt::entity>().size();
    }

    bool FSystemContext::IsValidEntity(entt::entity Entity) const
    {
        return Registry.valid(Entity);
    }

    EWorldType FSystemContext::GetWorldType() const
    {
        return World->GetWorldType();
    }
}
