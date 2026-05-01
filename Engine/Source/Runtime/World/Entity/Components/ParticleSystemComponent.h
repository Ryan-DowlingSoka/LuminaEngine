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
            , ParameterOverrides(Other.ParameterOverrides)
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
                ParameterOverrides  = Other.ParameterOverrides;
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

        /**
         * Per-instance overrides for user parameters declared on the asset. Only entries that
         * diverge from the asset default need to live here; reads fall back to the asset.
         */
        PROPERTY()
        TVector<FParticleParameter> ParameterOverrides;

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

        //~ Begin User Parameters
        /** True if this component or its asset declares a parameter with the given name. */
        FUNCTION(Script)
        bool HasParameter(FName Name) const
        {
            return FindParameter(Name) != nullptr;
        }

        FUNCTION(Script) 
        float GetFloat(FName Name, float Default = 0.0f) const;
        
        FUNCTION(Script) 
        int32 GetInt(FName Name, int32 Default = 0) const;
        
        FUNCTION(Script) 
        bool GetBool(FName Name, bool Default = false) const;
        
        FUNCTION(Script) 
        glm::vec2 GetVec2(FName Name) const;
        
        FUNCTION(Script) 
        glm::vec3 GetVec3(FName Name) const;
        
        FUNCTION(Script)
        glm::vec4 GetVec4(FName Name) const;
        
        FUNCTION(Script)
        glm::vec4 GetColor(FName Name) const;

        FUNCTION(Script) 
        void SetFloat(FName Name, float Value);
        
        FUNCTION(Script) 
        void SetInt(FName Name, int32 Value);
        
        FUNCTION(Script)
        void SetBool(FName Name, bool Value);
        
        FUNCTION(Script) 
        void SetVec2(FName Name, glm::vec2 Value);
        
        FUNCTION(Script) 
        void SetVec3(FName Name, glm::vec3 Value);
        
        FUNCTION(Script) 
        void SetVec4(FName Name, glm::vec4 Value);
        
        FUNCTION(Script) 
        void SetColor(FName Name, glm::vec4 Value);

        /** Drop the override for this parameter, reverting to the asset default. */
        FUNCTION(Script) 
        void ResetParameter(FName Name);

    private:

        /** Resolve a parameter by name, preferring component overrides over the asset's default. */
        const FParticleParameter* FindParameter(FName Name) const;

        /**
         * Get-or-create the override entry for the given name and type. If the asset declares
         * the parameter with a different type, that's a programmer error and we return nullptr.
         */
        FParticleParameter* GetOrCreateOverride(FName Name, EParticleParameterType ExpectedType);
        //~ End User Parameters
    };
}
