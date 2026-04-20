#pragma once

#include "Assets/AssetTypes/ParticleSystem/ParticleSystem.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Renderer/RenderResource.h"
#include "ParticleSystemComponent.generated.h"

namespace Lumina
{
    /** Transient GPU-resident simulation state for a particle emitter instance. */
    struct FParticleGPUState
    {
        FRHIBufferRef   ParticleBuffer;      // RW structured buffer of FGPUParticle (64B stride)
        FRHIBufferRef   SimParamsBuffer;     // Constant buffer, 288 bytes
        FRHIBufferRef   RenderParamsBuffer;  // Constant buffer, 48 bytes
        FRHIBufferRef   SpawnCounterBuffer;  // Single uint, cleared per frame
        uint32          AllocatedMax        = 0;
        float           SpawnAccumulator    = 0.0f;
        float           TotalTime           = 0.0f;
        float           SystemAge           = 0.0f;
        uint32          FrameSeed           = 0u;
        bool            bBurstPending       = true;
        bool            bPendingReset       = false;
        glm::vec3       PrevEmitterPosition = glm::vec3(0.0f);
        bool            bHasPrevPosition    = false;
    };

    REFLECT(Component)
    struct RUNTIME_API SParticleSystemComponent
    {
        GENERATED_BODY()

        SParticleSystemComponent() = default;
        
        
        SParticleSystemComponent(const SParticleSystemComponent& Other)
            : ParticleSystem(Other.ParticleSystem)
            , EmitterOffset(Other.EmitterOffset)
            , SpawnRateMultiplier(Other.SpawnRateMultiplier)
            , TimeScale(Other.TimeScale)
            , bEmit(Other.bEmit)
            , bBurstOnSpawn(Other.bBurstOnSpawn)
        {
        }

        SParticleSystemComponent& operator=(const SParticleSystemComponent& Other)
        {
            if (this != &Other)
            {
                ParticleSystem      = Other.ParticleSystem;
                EmitterOffset       = Other.EmitterOffset;
                SpawnRateMultiplier = Other.SpawnRateMultiplier;
                TimeScale           = Other.TimeScale;
                bEmit               = Other.bEmit;
                bBurstOnSpawn       = Other.bBurstOnSpawn;
                GPUState            = FParticleGPUState{};
            }
            return *this;
        }

        SParticleSystemComponent(SParticleSystemComponent&&) noexcept            = default;
        SParticleSystemComponent& operator=(SParticleSystemComponent&&) noexcept = default;

        /** The particle system asset that drives this emitter. */
        PROPERTY(Editable, Category = "Particle System")
        TObjectPtr<CParticleSystem> ParticleSystem;

        /** Local-space offset applied to the emitter origin relative to the entity transform. */
        PROPERTY(Editable, Category = "Particle System")
        glm::vec3 EmitterOffset = glm::vec3(0.0f);

        /** Scales the asset's continuous spawn rate. 0 disables spawning, 1 uses the asset value. */
        PROPERTY(Editable, Category = "Particle System", ClampMin = 0.0f)
        float SpawnRateMultiplier = 1.0f;

        /** Scales the asset's simulation time step. Useful for slow-motion effects. */
        PROPERTY(Editable, Category = "Particle System", ClampMin = 0.0f)
        float TimeScale = 1.0f;

        /** Whether the emitter is currently spawning new particles. Existing particles keep simulating. */
        PROPERTY(Editable, Category = "Particle System")
        bool bEmit = true;

        /** Auto-trigger the asset's BurstCount the first frame the component is active. */
        PROPERTY(Editable, Category = "Particle System")
        bool bBurstOnSpawn = true;

        /** Live GPU state, populated on the first simulate tick. Not serialized. */
        FParticleGPUState GPUState;

        /**
         * Turn the emitter on. When bReset is true, any currently-alive particles are cleared
         * on the next simulate tick and the burst fires again.
         */
        FUNCTION(Script)
        void Activate(bool bReset = false)
        {
            bEmit = true;
            GPUState.bBurstPending = true;
            if (bReset)
            {
                GPUState.bPendingReset  = true;
                GPUState.SpawnAccumulator = 0.0f;
                GPUState.SystemAge        = 0.0f;
            }
        }

        /**
         * Stop emitting new particles. Existing particles keep simulating until they expire
         * naturally, so smoke trails fade out instead of popping.
         */
        FUNCTION(Script)
        void Deactivate() { bEmit = false; }

        /** True while the emitter is spawning new particles. */
        FUNCTION(Script)
        bool IsActive() const { return bEmit; }
    };
}
