#pragma once

#include "Core/Object/Object.h"
#include "GameInstance.generated.h"

namespace Lumina
{
    class FEngine;
    struct FWorldContext;
}

namespace Lumina
{
    // Persistent object that lives across world transitions and owns game-wide state.
    REFLECT()
    class RUNTIME_API CGameInstance : public CObject
    {
        GENERATED_BODY()
    public:

        // Called once after the GameInstance is constructed during project load.
        virtual void Init();

        // Called once before the GameInstance is destroyed during engine shutdown / project unload.
        virtual void Shutdown();

        // Called after a world that belongs to this GameInstance finishes InitializeWorld.
        virtual void OnWorldInitialized(FWorldContext* Context) {}

        // Called right before a world that belongs to this GameInstance is torn down.
        virtual void OnWorldTornDown(FWorldContext* Context) {}
    };
}
