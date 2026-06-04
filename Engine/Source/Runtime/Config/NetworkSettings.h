#pragma once

#include "Core/Object/ObjectMacros.h"
#include "Config/DeveloperSettings.h"
#include "NetworkSettings.generated.h"

namespace Lumina
{
    // Client-side proxy-smoothing preferences (global, player-local). Server-authoritative / world-spatial
    // replication tuning (interest management, LOD tiers, keyframe + send rate) lives per-world on
    // SDefaultWorldSettings. Read on the CDO via GetDefault<CNetworkSettings>().
    REFLECT(MinimalAPI, ConfigFile = "/Config/NetworkSettings.json", DisplayName = "Networking", Category = "Engine")
    class CNetworkSettings : public CDeveloperSettings
    {
        GENERATED_BODY()
    public:

        /** Seconds a SimulatedProxy renders behind the newest received server time. Higher = smoother under
         *  jitter/loss but laggier. */
        PROPERTY(Editable, Category = "Replication", ClampMin = 0.0f, ClampMax = 1.0f)
        float InterpDelay = 0.1f;

        /** Extrapolate a proxy's position from its last velocity when render time runs past the newest sample
         *  (e.g. under packet loss), instead of freezing on the last pose. Rotation is always held. */
        PROPERTY(Editable, Category = "Replication")
        bool bEnableExtrapolation = true;

        /** Maximum seconds to extrapolate past the newest sample before clamping. */
        PROPERTY(Editable, Category = "Replication", ClampMin = 0.0f, ClampMax = 1.0f)
        float MaxExtrapolation = 0.25f;
    };
}
