#pragma once

#include "Core/Object/ObjectMacros.h"
#include "Config/DeveloperSettings.h"
#include "NetworkSettings.generated.h"

namespace Lumina
{
    // Client-side proxy-smoothing preferences.
    REFLECT(MinimalAPI, ConfigFile = "/Config/NetworkSettings.json", DisplayName = "Networking", Category = "Engine")
    class CNetworkSettings : public CDeveloperSettings
    {
        GENERATED_BODY()
    public:

        /** Floor for how many seconds a SimulatedProxy renders behind the newest received server time. */
        PROPERTY(Editable, Category = "Replication", ClampMin = 0.0f, ClampMax = 1.0f)
        float InterpDelay = 0.04f;

        /** Dead-reckon a proxy's position from its last velocity when render time runs past the newest sample. */
        PROPERTY(Editable, Category = "Replication")
        bool bEnableExtrapolation = false;

        /** Maximum seconds to extrapolate past the newest sample before clamping. */
        PROPERTY(Editable, Category = "Replication", ClampMin = 0.0f, ClampMax = 1.0f)
        float MaxExtrapolation = 0.25f;

        /** Per-entity interpolation buffer depth, in multiples of the entity's measured send interval. */
        PROPERTY(Editable, Category = "Replication", ClampMin = 1.0f, ClampMax = 3.0f)
        float InterpBufferIntervals = 1.5f;
    };
}
