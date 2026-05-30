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

    // Per-world runtime state owned by FWorldManager (one world = one context). Holds the world's role
    // (type, net mode, PIE) so systems branch on it without CWorld carrying editor/network concerns.
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
