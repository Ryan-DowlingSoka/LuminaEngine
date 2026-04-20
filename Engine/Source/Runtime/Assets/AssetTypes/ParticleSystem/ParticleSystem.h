#pragma once

#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/ObjectMacros.h"
#include "Renderer/RenderResource.h"
#include "ParticleSystem.generated.h"

namespace Lumina
{
    class CTexture;

    /** Emission volume shape used to generate the initial spawn position of each particle. */
    REFLECT()
    enum class RUNTIME_API EParticleEmitterShape : uint8
    {
        Point,
        Sphere,
        Box,
        Cone,
        Ring,
        Disk,
    };

    /** How initial particle velocity is computed. */
    REFLECT()
    enum class RUNTIME_API EParticleVelocityMode : uint8
    {
        /** Velocity is a random vec3 between VelocityMin and VelocityMax. */
        Explicit,
        /** Velocity points outward from the emitter origin with magnitude in SpeedRange. */
        Radial,
    };

    /** Blend mode used when compositing particles into the HDR scene color. */
    REFLECT()
    enum class RUNTIME_API EParticleBlendMode : uint8
    {
        /** Straight alpha: Src.rgb * Src.a + Dst.rgb * (1 - Src.a). */
        Alpha,
        /** Additive: Src.rgb * Src.a + Dst.rgb. Great for fire, sparks, energy. */
        Additive,
        /** Pre-multiplied alpha: Src.rgb + Dst.rgb * (1 - Src.a). */
        PreMultiplied,
        /** Multiplicative: Src.rgb * Dst.rgb. Useful for smoke-darkening. */
        Multiply,
    };

    /** Which simulation shader the emitter should use. */
    REFLECT()
    enum class RUNTIME_API EParticleShaderMode : uint8
    {
        /** Use the engine's data-driven built-in compute shader. All modules are controlled by properties. */
        Default,
        /** Use the compute shader compiled from the particle node graph. */
        Custom,
    };

    /**
     * A reusable GPU particle system asset. Supports a data-driven module pipeline out of the box and
     * an optional graph-compiled custom simulation shader for bespoke behavior.
     */
    REFLECT()
    class RUNTIME_API CParticleSystem : public CObject
    {
        GENERATED_BODY()

    public:

        CParticleSystem() = default;

        void Serialize(FArchive& Ar) override;
        bool IsAsset() const override { return true; }
        void PostLoad() override;
        void OnDestroy() override;

        FRHIComputeShader* GetCustomComputeShader() const { return ComputeShader; }
        bool HasCustomComputeShader() const { return ComputeShader != nullptr; }
        bool UsesCustomShader() const { return ShaderMode == EParticleShaderMode::Custom && HasCustomComputeShader(); }

        /** Minimum validity check used by the renderer before touching the asset. */
        bool IsReadyForSimulation() const
        {
            if (MaxParticles <= 0)
            {
                return false;
            }
            if (ShaderMode == EParticleShaderMode::Custom && !HasCustomComputeShader())
            {
                return false;
            }
            return true;
        }

        /** Compiled SPIR-V bytecode for a graph-generated simulation compute shader. */
        PROPERTY()
        TVector<uint32> ComputeShaderBinaries;

        //~ Begin Simulation
        /** Maximum number of particles that can be alive at once. This pre-allocates the GPU particle buffer. */
        PROPERTY(Editable, Category = "Simulation", ClampMin = 1)
        int32 MaxParticles = 1024;

        /** Continuous spawn rate in particles per second. */
        PROPERTY(Editable, Category = "Simulation", ClampMin = 0.0f)
        float SpawnRate = 50.0f;

        /** One-shot burst count emitted when the system starts. Useful for explosions. */
        PROPERTY(Editable, Category = "Simulation", ClampMin = 0)
        int32 BurstCount = 0;

        /** Total lifetime of the system in seconds. Set to 0 for infinite (used with bLooping = true). */
        PROPERTY(Editable, Category = "Simulation", ClampMin = 0.0f)
        float Duration = 0.0f;

        /** Whether the system restarts after Duration elapses. Infinite systems should keep this on. */
        PROPERTY(Editable, Category = "Simulation")
        bool bLooping = true;

        /** Which simulation shader to run. Default uses the built-in module shader. */
        PROPERTY(Editable, Category = "Simulation")
        EParticleShaderMode ShaderMode = EParticleShaderMode::Default;
        //~ End Simulation

        //~ Begin Emitter Shape
        /** Volume/surface that particles spawn from. */
        PROPERTY(Editable, Category = "Emitter Shape")
        EParticleEmitterShape Shape = EParticleEmitterShape::Point;

        /** Sphere: x = radius. Box: xyz = half extents. Cone: x = base radius, y = height. Ring/Disk: x = outer radius, y = inner radius. */
        PROPERTY(Editable, Category = "Emitter Shape")
        glm::vec3 ShapeSize = glm::vec3(1.0f, 1.0f, 1.0f);

        /** Cone half-angle in degrees. Only used when Shape = Cone. */
        PROPERTY(Editable, Category = "Emitter Shape", ClampMin = 0.0f, ClampMax = 180.0f)
        float ShapeAngle = 30.0f;
        //~ End Emitter Shape

        //~ Begin Velocity
        PROPERTY(Editable, Category = "Velocity")
        EParticleVelocityMode VelocityMode = EParticleVelocityMode::Explicit;

        /** Min velocity vector (Explicit mode). Per-particle value is randomized between Min and Max. */
        PROPERTY(Editable, Category = "Velocity")
        glm::vec3 VelocityMin = glm::vec3(-0.5f, 1.0f, -0.5f);

        /** Max velocity vector (Explicit mode). */
        PROPERTY(Editable, Category = "Velocity")
        glm::vec3 VelocityMax = glm::vec3(0.5f, 3.0f, 0.5f);

        /** Speed range (Radial mode). Per-particle speed is randomized between x and y. */
        PROPERTY(Editable, Category = "Velocity")
        glm::vec2 SpeedRange = glm::vec2(1.0f, 3.0f);
        //~ End Velocity

        //~ Begin Lifetime
        /** Per-particle lifetime range, in seconds. Randomized between x (min) and y (max). */
        PROPERTY(Editable, Category = "Lifetime", ClampMin = 0.01f)
        glm::vec2 LifetimeRange = glm::vec2(1.0f, 2.0f);
        //~ End Lifetime

        //~ Begin Physics
        /** Constant acceleration applied each frame. */
        PROPERTY(Editable, Category = "Physics")
        glm::vec3 Gravity = glm::vec3(0.0f, -9.8f, 0.0f);

        /** Linear velocity damping. 0 = no drag, higher = exponential slowdown. */
        PROPERTY(Editable, Category = "Physics", ClampMin = 0.0f)
        float Drag = 0.0f;

        /**
         * Fraction of the emitter's frame-over-frame motion that newly spawned particles
         * inherit. 0 = purely world-space spawns (moving the emitter leaves particles behind),
         * 1 = particles flow along with the emitter's motion. Useful for contrails and anything
         * attached to a moving object.
         */
        PROPERTY(Editable, Category = "Physics", ClampMin = 0.0f, ClampMax = 1.0f)
        float InheritEmitterVelocity = 0.0f;
        //~ End Physics

        //~ Begin Color
        /** Color at spawn (multiplied with texture sample). */
        PROPERTY(Editable, Category = "Color", Color)
        glm::vec4 StartColor = glm::vec4(1.0f, 0.6f, 0.2f, 1.0f);

        /** Color at end of life, smoothly interpolated from StartColor. */
        PROPERTY(Editable, Category = "Color", Color)
        glm::vec4 EndColor = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        //~ End Color

        //~ Begin Size
        /** Spawn size range (picked per-particle). */
        PROPERTY(Editable, Category = "Size", ClampMin = 0.0f)
        glm::vec2 StartSizeRange = glm::vec2(0.2f, 0.3f);

        /** End-of-life size range (picked per-particle). */
        PROPERTY(Editable, Category = "Size", ClampMin = 0.0f)
        glm::vec2 EndSizeRange = glm::vec2(0.0f, 0.0f);
        //~ End Size

        //~ Begin Rotation
        /** Initial rotation range in degrees. */
        PROPERTY(Editable, Category = "Rotation")
        glm::vec2 RotationRange = glm::vec2(0.0f, 0.0f);

        /** Rotation rate range in degrees/second. */
        PROPERTY(Editable, Category = "Rotation")
        glm::vec2 RotationSpeedRange = glm::vec2(0.0f, 0.0f);
        //~ End Rotation

        //~ Begin Noise
        /** Per-axis strength of curl-like noise acceleration applied during simulation. */
        PROPERTY(Editable, Category = "Noise")
        glm::vec3 NoiseStrength = glm::vec3(0.0f);

        /** Spatial frequency of the noise field. Higher = smaller eddies. */
        PROPERTY(Editable, Category = "Noise", ClampMin = 0.0001f)
        float NoiseScale = 1.0f;

        /** How fast the noise field evolves over time. */
        PROPERTY(Editable, Category = "Noise")
        float NoiseSpeed = 1.0f;
        //~ End Noise

        //~ Begin Render
        PROPERTY(Editable, Category = "Render")
        EParticleBlendMode BlendMode = EParticleBlendMode::Additive;

        /** Optional sprite sampled per-particle. Falls back to a soft disc when empty. */
        PROPERTY(Editable, Category = "Render")
        TObjectPtr<CTexture> Texture;

        /** Particles face the camera if true; otherwise they lie flat along the world XZ plane. */
        PROPERTY(Editable, Category = "Render")
        bool bBillboardToCamera = true;

        /** Write to depth buffer (can cause sorting artifacts with translucent particles). */
        PROPERTY(Editable, Category = "Render")
        bool bWriteDepth = false;
        //~ End Render

        FRHIComputeShaderRef ComputeShader;
    };
}
