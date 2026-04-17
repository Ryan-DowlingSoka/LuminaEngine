#pragma once

#include "Core/Object/ObjectHandleTyped.h"
#include "WorldTypes.h"

namespace Lumina
{
    class CWorld;
    class CGameInstance;

    enum class ENetMode : uint8
    {
        Standalone,
        Client,
        ListenServer,
        DedicatedServer,
    };

    // Per-world runtime state owned by FWorldManager. One world = one context.
    // Holds the role the world is playing (type, net mode, PIE status) so systems
    // can branch on it without CWorld itself carrying editor/network concerns.
    struct FWorldContext
    {
        TObjectPtr<CWorld>      World;
        EWorldType              Type            = EWorldType::None;
        ENetMode                NetMode         = ENetMode::Standalone;
        bool                    bPIE            = false;

        // PIE-only: the editor-side source world this was duplicated from.
        TWeakObjectPtr<CWorld>  SourceWorld;

        // Set in Phase 2 once CGameInstance exists; survives world transitions.
        CGameInstance*          GameInstance    = nullptr;
    };
}
