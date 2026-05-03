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
        // View matrix is baked lazily in CWorld::Render from the camera entity's
        // transform, so any script or system that writes the camera transform up
        // to the end of PostPhysics is reflected in the same frame.
    }
}
