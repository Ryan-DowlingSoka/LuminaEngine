#include "pch.h"
#include "EventBusSystem.h"
#include "World/Entity/Events/LuaEventBus.h"

namespace Lumina
{
    void SEventBusSystem::Update(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        Context.GetRegistry().ctx().get<FLuaEventBus>().ProcessDeferred();
    }
}
