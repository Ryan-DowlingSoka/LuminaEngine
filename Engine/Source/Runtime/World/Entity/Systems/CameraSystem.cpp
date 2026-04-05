#include "pch.h"
#include "CameraSystem.h"
#include "World/Entity/Components/CameraComponent.h"

namespace Lumina
{
    namespace Detail
    {
        static void NewCameraConstructed(entt::registry& Registry, entt::entity Entity)
        {
            SCameraComponent& Camera = Registry.get<SCameraComponent>(Entity);
            if (Camera.bAutoActivate)
            {
                Registry.ctx().get<entt::dispatcher&>().trigger<FSwitchActiveCameraEvent>(FSwitchActiveCameraEvent{Entity});
            }
        }
    }

    void SCameraSystem::Startup(const FSystemContext& Context) noexcept
    {
        Context.GetRegistry().on_construct<SCameraComponent>().connect<&Detail::NewCameraConstructed>();
    }

    void SCameraSystem::Teardown(const FSystemContext& Context) noexcept
    {
        
    }

    void SCameraSystem::Update(const FSystemContext& SystemContext) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        
        auto View = SystemContext.CreateView<SCameraComponent, STransformComponent>();
        View.each([](SCameraComponent& CameraComponent, const STransformComponent& TransformComponent)
        {
            CameraComponent.SetView(TransformComponent.GetWorldLocation(), TransformComponent.GetWorldRotation() * glm::vec3(0.0, 0.0, 1.0), TransformComponent.GetWorldRotation() * glm::vec3(0.0, 1.0, 0.0));
        });
    }
}
