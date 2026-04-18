#pragma once
#include "Core/Object/ObjectHandleTyped.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Registry/EntityRegistry.h"


namespace Lumina
{
    class CWorld;

    class FCameraManager
    {
    public:

        FCameraManager(CWorld* InWorld);
        
        FORCEINLINE void SetActiveCamera(entt::entity InEntity) { ActiveCameraEntity = InEntity; }
        
        FORCEINLINE entt::entity GetActiveCameraEntity() const { return ActiveCameraEntity; }
        SCameraComponent* GetCameraComponent() const;
    

    private:

        FEntityRegistry& Registry;
        entt::entity ActiveCameraEntity = entt::null;
    };
}
