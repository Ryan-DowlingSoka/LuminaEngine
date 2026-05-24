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
        // View matrix is baked lazily in CWorld::Render; camera transform writes up to
        // end of PostPhysics are reflected in the same frame.
    }
}
