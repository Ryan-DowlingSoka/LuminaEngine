#include "pch.h"
#include "FCameraManager.h"
#include "World/World.h"

namespace Lumina
{
    FCameraManager::FCameraManager(CWorld* InWorld)
        : Registry(InWorld->GetEntityRegistry())
    {
    }

    SCameraComponent* FCameraManager::GetCameraComponent() const
    {
        return Registry.try_get<SCameraComponent>(ActiveCameraEntity);
    }
}
