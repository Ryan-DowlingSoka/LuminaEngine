#pragma once
#include "Core/Object/ObjectMacros.h"
#include "WorldSettings.generated.h"


namespace Lumina
{
    REFLECT()
    enum class ESMAAQuality : uint8
    {
        Off,
        Low,
        Medium,
        High,
        Ultra,
    };

    REFLECT()
    enum class EMSAASampleCount : uint8
    {
        Off,
        X2,
        X4,
        X8,
    };

    REFLECT()
    enum class EVariableRateShading : uint8
    {
        Off,        // 1x1 - full rate
        Rate2x2,    // quarter the fragment shader invocations
        Rate4x4,    // sixteenth (clamped to the GPU's max supported rate)
    };

    /** Map the (sequential) reflected enum to its actual GPU sample count. */
    constexpr uint8 GetMSAASampleCount(EMSAASampleCount Quality)
    {
        switch (Quality)
        {
        case EMSAASampleCount::X2: return 2;
        case EMSAASampleCount::X4: return 4;
        case EMSAASampleCount::X8: return 8;
        default:                   return 1;
        }
    }

    REFLECT(Component, HideInComponentList)
    struct RUNTIME_API SDefaultWorldSettings
    {
        GENERATED_BODY()

        /** Entities below this Y position are automatically destroyed. */
        PROPERTY(Editable)
        float WorldKillHeight = -5'000;

        /** Multiplier applied to global gravity strength. */
        PROPERTY(Editable)
        float GravityScale = 1.0f;

        /** Time dilation factor - values below 1 slow the world, above 1 speed it up. */
        PROPERTY(Editable)
        float DeltaTimeScale = 1.0f;

        /** Antialiasing quality. Off disables SMAA; higher qualities detect more edges at higher GPU cost. */
        PROPERTY(Editable, Category = "Rendering")
        ESMAAQuality SMAAQuality = ESMAAQuality::High;

        /** MSAA sample count. Off = no multisampling. Applied at scene init; reload the world to change. */
        PROPERTY(Editable, Category = "Rendering")
        EMSAASampleCount MSAASampleCount = EMSAASampleCount::Off;

        // VRS rate for opted-in passes (sky, particles, translucency, fog, opaque base). Coarser = fewer PS
        // invocations but softer + reduced picker precision (base pass writes it). No-op without pipeline FSR.
        PROPERTY(Editable, Category = "Rendering")
        EVariableRateShading VariableRateShading = EVariableRateShading::Off;

        /** Normalized direction of gravity in world space. */
        PROPERTY(Editable, Category = "Physics")
        FVector3 GravityDirection = FVector3(0.0f, -1.0f, 0.0f);

        /** Fixed physics update rate in Hz. Higher = more accurate but more CPU. */
        PROPERTY(Editable, Category = "Physics")
        float PhysicsHz = 60.0f;

        /** Maximum fixed-step iterations per frame to prevent spiral-of-death under load. */
        PROPERTY(Editable, Category = "Physics")
        uint8 MaxPhysicsSteps = 8;

        /** Interpolate rigid body positions between fixed steps for smoother visuals.
        Disable for debugging or when PhysicsHz matches render rate. */
        PROPERTY(Editable, Category = "Physics")
        bool bEnablePhysicsInterpolation = true;

        /** When on, the simulation is deterministic at the cost of speed (forces contact/island
         sorting and reduces solver parallelism). Enable only for lockstep netcode or replays. */
        PROPERTY(Editable, Category = "Physics")
        bool bDeterministicSimulation = false;

        /** Max rigid bodies the scene pre-allocates for. Drives up-front physics memory; exceeding it at runtime drops new bodies, so raise it for dense worlds. */
        PROPERTY(Editable, Category = "Physics")
        uint32 MaxPhysicsBodies = 65536;

        /** Max simultaneously-overlapping body pairs the broad phase tracks. Dense fracture piles overflow this; overflow trips a Jolt update error. */
        PROPERTY(Editable, Category = "Physics")
        uint32 MaxPhysicsBodyPairs = 98304;

        /** Max contact constraints the solver pre-allocates -- the dominant per-scene physics memory cost. Raise it if dense contact piles start interpenetrating. */
        PROPERTY(Editable, Category = "Physics")
        uint32 MaxPhysicsContactConstraints = 131072;

        /** Global velocity solver iterations. Entities can raise this per-body via NumVelocityStepsOverride. Minimum 2 required for friction. */
        PROPERTY(Editable, Category = "Physics")
        uint32 NumVelocitySteps = 10;

        /** Global position solver iterations. Higher = less penetration overlap. */
        PROPERTY(Editable, Category = "Physics")
        uint32 NumPositionSteps = 2;

        /** Fraction of penetration depth corrected per step (0 = nothing, 1 = all at once). */
        PROPERTY(Editable, ClampMin = 0.0f, ClampMax = 1.0f, Category = "Physics")
        float BaumgarteStabilizationFactor = 0.2f;

        /** Radius around objects (meters) in which speculative contact points are detected. Too large causes ghost collisions. */
        PROPERTY(Editable, ClampMin = 0.0f, Category = "Physics")
        float SpeculativeContactDistance = 0.02f;

        /** How far bodies are allowed to sink into each other (meters). */
        PROPERTY(Editable, ClampMin = 0.0f, Category = "Physics")
        float PenetrationSlop = 0.02f;

        /** Maximum distance to correct in a single position iteration (meters). */
        PROPERTY(Editable, ClampMin = 0.0f, Category = "Physics")
        float MaxPenetrationDistance = 0.2f;

        /** For LinearCast motion quality: fraction of inner radius a body must move per step to enable casting. */
        PROPERTY(Editable, ClampMin = 0.0f, ClampMax = 1.0f, Category = "Physics")
        float LinearCastThreshold = 0.75f;

        /** For LinearCast motion quality: fraction of inner radius a body may penetrate another. */
        PROPERTY(Editable, ClampMin = 0.0f, ClampMax = 1.0f, Category = "Physics")
        float LinearCastMaxPenetration = 0.25f;

        /** Max distance (meters) to determine if two points lie on the same contact manifold plane. */
        PROPERTY(Editable, ClampMin = 0.0f, Category = "Physics")
        float ManifoldTolerance = 1.0e-3f;

        /** Minimum relative velocity (m/s) for a collision to produce restitution (bounce). Below this, restitution is forced to zero so objects settle. */
        PROPERTY(Editable, ClampMin = 0.0f, Category = "Physics")
        float MinVelocityForRestitution = 1.0f;

        /** Seconds a body must remain nearly still before being allowed to sleep. */
        PROPERTY(Editable, ClampMin = 0.0f, Category = "Physics")
        float TimeBeforeSleep = 0.5f;

        /** Point velocity (m/s) below which a body is considered still for sleep purposes. */
        PROPERTY(Editable, ClampMin = 0.0f, Category = "Physics")
        float SleepVelocityThreshold = 0.03f;

        /** Max relative delta position (m^2) between frames for body-pair contact cache to be reused. */
        PROPERTY(Editable, ClampMin = 0.0f, Category = "Physics")
        float BodyPairCacheMaxDeltaPositionSq = 1.0e-6f;

        /** Max relative delta rotation (as cos(angle/2)) for body-pair cache reuse. Default = cos(1 deg). */
        PROPERTY(Editable, ClampMin = 0.0f, ClampMax = 1.0f, Category = "Physics")
        float BodyPairCacheCosMaxDeltaRotationDiv2 = 0.99984769515639123915701155881391f;

        /** Max angle between contact normals (as cosine) to merge sub-shape manifolds. Default = cos(5 deg). */
        PROPERTY(Editable, ClampMin = 0.0f, ClampMax = 1.0f, Category = "Physics")
        float ContactNormalCosMaxDeltaRotation = 0.99619469809174553229501040247389f;

        /** Max distance (m^2) between old and new contact point to preserve warm-start impulses. Default = 1 cm^2. */
        PROPERTY(Editable, ClampMin = 0.0f, Category = "Physics")
        float ContactPointPreserveLambdaMaxDistSq = 1.0e-4f;

        /** Max body pairs in-flight in the broadphase at once. Lower saves memory, may reduce parallelism. */
        PROPERTY(Editable, Category = "Physics")
        int32 MaxInFlightBodyPairs = 16384;

        /** How many step listeners to notify in one batch. */
        PROPERTY(Editable, Category = "Physics")
        int32 StepListenersBatchSize = 8;

        /** How many listener batches are needed before spawning another job (INT_MAX = no parallelism). */
        PROPERTY(Editable, Category = "Physics")
        int32 StepListenerBatchesPerJob = 1;

        /** Re-apply previous frame's constraint impulses as a solver starting point. Improves convergence. */
        PROPERTY(Editable, Category = "Physics")
        bool bConstraintWarmStart = true;

        /** Cache narrow-phase results when bodies haven't moved relative to each other. */
        PROPERTY(Editable, Category = "Physics")
        bool bUseBodyPairContactCache = true;

        /** Merge contact manifolds with similar normals into one. Reduces solver work. */
        PROPERTY(Editable, Category = "Physics")
        bool bUseManifoldReduction = true;

        /** Split large connected islands into smaller parallel work batches. */
        PROPERTY(Editable, Category = "Physics")
        bool bUseLargeIslandSplitter = true;

        /** Allow bodies to go to sleep globally. Overrides per-body sleep settings when disabled. */
        PROPERTY(Editable, Category = "Physics")
        bool bAllowSleeping = true;

        /** Prevent collision against non-active shared edges on triangle meshes. */
        PROPERTY(Editable, Category = "Physics")
        bool bCheckActiveEdges = true;
    };
}
