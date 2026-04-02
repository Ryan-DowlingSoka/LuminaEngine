#include "pch.h"
#include "UpdateTransformEntitySystem.h"
#include "glm/gtx/string_cast.hpp"
#include "TaskSystem/TaskSystem.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/Transformcomponent.h"

namespace Lumina
{
    void SUpdateTransformEntitySystem::Update(const FSystemContext& SystemContext) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        
        auto View = SystemContext.CreateView<SCameraComponent, STransformComponent>();
        View.each([](SCameraComponent& CameraComponent, const STransformComponent& TransformComponent)
        {
            CameraComponent.SetView(TransformComponent.GetLocation(), TransformComponent.GetForward(), TransformComponent.GetUp());
        });
    }
}
