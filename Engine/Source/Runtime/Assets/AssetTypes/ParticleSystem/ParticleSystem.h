#pragma once

#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/ObjectMacros.h"
#include "Renderer/RenderResource.h"
#include "ParticleSystem.generated.h"

namespace Lumina
{
    class CTexture;

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

    REFLECT()
    enum class RUNTIME_API EParticleVelocityMode : uint8
    {
        /** Random vec3 between VelocityMin and VelocityMax. */
        Explicit,
        /** Outward from emitter origin with magnitude in SpeedRange. */
        Radial,
    };

    REFLECT()
    enum class RUNTIME_API EParticleBlendMode : uint8
    {
        Alpha,
        Additive,
        PreMultiplied,
        Multiply,
    };

    REFLECT()
    enum class RUNTIME_API EParticleShaderMode : uint8
    {
        Default,
        Custom,
    };

    REFLECT()
    enum class RUNTIME_API EParticleParameterType : uint8
    {
        Float,
        Int,
        Bool,
        Vec2,
        Vec3,
        Vec4,
        Color,
    };

    /** Named, typed value on a particle asset; component overrides reuse the same shape. */
    REFLECT()
    struct RUNTIME_API FParticleParameter
    {
        GENERATED_BODY()

        PROPERTY()
        FName Name;

        PROPERTY()
        EParticleParameterType Type = EParticleParameterType::Float;

        float       Scalar  = 0.0f;
        int32       Integer = 0;
        bool        Boolean = false;
        /** Vec2/Vec3/Vec4/Color storage; higher components zero for narrower types. */
        glm::vec4   Vector  = glm::vec4(0.0f);

        /** Writes only the storage matching Type. */
        bool Serialize(FArchive& Ar);

        void CopyFrom(const FParticleParameter& Other)
        {
            Name    = Other.Name;
            Type    = Other.Type;
            Scalar  = Other.Scalar;
            Integer = Other.Integer;
            Boolean = Other.Boolean;
            Vector  = Other.Vector;
        }

        bool operator==(const FParticleParameter& Other) const
        {
            return Name    == Other.Name
                && Type    == Other.Type
                && Scalar  == Other.Scalar
                && Integer == Other.Integer
                && Boolean == Other.Boolean
                && Vector  == Other.Vector;
        }
    };

    /** Routes a built-in simulation property through a named user parameter. */
    REFLECT()
    struct RUNTIME_API FParticlePropertyBinding
    {
        GENERATED_BODY()

        PROPERTY()
        FName PropertyName;

        PROPERTY()
        FName ParameterName;
    };

    struct SParticleSystemComponent;

    /** GPU particle system asset; data-driven modules with optional graph-compiled custom compute shader. */
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

        PROPERTY()
        TVector<uint32> ComputeShaderBinaries;

        PROPERTY(Editable, Category = "User Parameters")
        TVector<FParticleParameter> UserParameters;

        PROPERTY()
        TVector<FParticlePropertyBinding> PropertyBindings;

        const FParticleParameter* FindUserParameter(const FName& InName) const;

        /** Returns NAME_None if no binding exists. */
        FName GetPropertyBinding(const FName& PropertyName) const;

        /** Pass NAME_None as ParameterName to clear. */
        void SetPropertyBinding(const FName& PropertyName, const FName& ParameterName);

        void ClearPropertyBinding(const FName& PropertyName);
        bool HasPropertyBinding(const FName& PropertyName) const;

        PROPERTY(Editable, Category = "Simulation", ClampMin = 1)
        int32 MaxParticles = 1024;

        PROPERTY(Editable, Category = "Simulation", ClampMin = 0.0f)
        float SpawnRate = 50.0f;

        PROPERTY(Editable, Category = "Simulation", ClampMin = 0)
        int32 BurstCount = 0;

        /** 0 means infinite (used with bLooping). */
        PROPERTY(Editable, Category = "Simulation", ClampMin = 0.0f)
        float Duration = 0.0f;

        PROPERTY(Editable, Category = "Simulation")
        bool bLooping = true;

        PROPERTY(Editable, Category = "Simulation")
        EParticleShaderMode ShaderMode = EParticleShaderMode::Default;

        PROPERTY(Editable, Category = "Emitter Shape")
        EParticleEmitterShape Shape = EParticleEmitterShape::Point;

        /** Sphere: x=radius. Box: xyz=half extents. Cone: x=base radius, y=height. Ring/Disk: x=outer, y=inner. */
        PROPERTY(Editable, Category = "Emitter Shape")
        glm::vec3 ShapeSize = glm::vec3(1.0f, 1.0f, 1.0f);

        /** Cone half-angle in degrees. */
        PROPERTY(Editable, Category = "Emitter Shape", ClampMin = 0.0f, ClampMax = 180.0f)
        float ShapeAngle = 30.0f;

        PROPERTY(Editable, Category = "Velocity")
        EParticleVelocityMode VelocityMode = EParticleVelocityMode::Explicit;

        PROPERTY(Editable, Category = "Velocity")
        glm::vec3 VelocityMin = glm::vec3(-0.5f, 1.0f, -0.5f);

        PROPERTY(Editable, Category = "Velocity")
        glm::vec3 VelocityMax = glm::vec3(0.5f, 3.0f, 0.5f);

        PROPERTY(Editable, Category = "Velocity")
        glm::vec2 SpeedRange = glm::vec2(1.0f, 3.0f);

        PROPERTY(Editable, Category = "Lifetime", ClampMin = 0.01f)
        glm::vec2 LifetimeRange = glm::vec2(1.0f, 2.0f);

        PROPERTY(Editable, Category = "Physics")
        glm::vec3 Gravity = glm::vec3(0.0f, -9.8f, 0.0f);

        PROPERTY(Editable, Category = "Physics", ClampMin = 0.0f)
        float Drag = 0.0f;

        /** 0 = world-space spawns; 1 = particles flow with emitter motion. */
        PROPERTY(Editable, Category = "Physics", ClampMin = 0.0f, ClampMax = 1.0f)
        float InheritEmitterVelocity = 0.0f;

        PROPERTY(Editable, Category = "Color", Color)
        glm::vec4 StartColor = glm::vec4(1.0f, 0.6f, 0.2f, 1.0f);

        PROPERTY(Editable, Category = "Color", Color)
        glm::vec4 EndColor = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

        PROPERTY(Editable, Category = "Size", ClampMin = 0.0f)
        glm::vec2 StartSizeRange = glm::vec2(0.2f, 0.3f);

        PROPERTY(Editable, Category = "Size", ClampMin = 0.0f)
        glm::vec2 EndSizeRange = glm::vec2(0.0f, 0.0f);

        PROPERTY(Editable, Category = "Rotation")
        glm::vec2 RotationRange = glm::vec2(0.0f, 0.0f);

        PROPERTY(Editable, Category = "Rotation")
        glm::vec2 RotationSpeedRange = glm::vec2(0.0f, 0.0f);

        PROPERTY(Editable, Category = "Noise")
        glm::vec3 NoiseStrength = glm::vec3(0.0f);

        PROPERTY(Editable, Category = "Noise", ClampMin = 0.0001f)
        float NoiseScale = 1.0f;

        PROPERTY(Editable, Category = "Noise")
        float NoiseSpeed = 1.0f;

        PROPERTY(Editable, Category = "Render")
        EParticleBlendMode BlendMode = EParticleBlendMode::Additive;

        PROPERTY(Editable, Category = "Render")
        TObjectPtr<CTexture> Texture;

        PROPERTY(Editable, Category = "Render")
        bool bBillboardToCamera = true;

        PROPERTY(Editable, Category = "Render")
        bool bWriteDepth = false;

        FRHIComputeShaderRef ComputeShader;
    };

    /** Per-frame, per-emitter snapshot of simulation properties after binding resolution. */
    struct RUNTIME_API FResolvedParticleParams
    {
        int32                   MaxParticles            = 1024;
        float                   SpawnRate               = 0.0f;
        int32                   BurstCount              = 0;
        float                   Duration                = 0.0f;
        bool                    bLooping                = true;

        EParticleEmitterShape   Shape                   = EParticleEmitterShape::Point;
        glm::vec3               ShapeSize               = glm::vec3(1.0f);
        float                   ShapeAngle              = 30.0f;

        EParticleVelocityMode   VelocityMode            = EParticleVelocityMode::Explicit;
        glm::vec3               VelocityMin             = glm::vec3(0.0f);
        glm::vec3               VelocityMax             = glm::vec3(0.0f);
        glm::vec2               SpeedRange              = glm::vec2(1.0f, 3.0f);
        glm::vec2               LifetimeRange           = glm::vec2(1.0f, 2.0f);

        glm::vec3               Gravity                 = glm::vec3(0.0f, -9.8f, 0.0f);
        float                   Drag                    = 0.0f;
        float                   InheritEmitterVelocity  = 0.0f;

        glm::vec4               StartColor              = glm::vec4(1.0f);
        glm::vec4               EndColor                = glm::vec4(1.0f);
        glm::vec2               StartSizeRange          = glm::vec2(0.2f, 0.3f);
        glm::vec2               EndSizeRange            = glm::vec2(0.0f);
        glm::vec2               RotationRange           = glm::vec2(0.0f);
        glm::vec2               RotationSpeedRange      = glm::vec2(0.0f);

        glm::vec3               NoiseStrength           = glm::vec3(0.0f);
        float                   NoiseScale              = 1.0f;
        float                   NoiseSpeed              = 1.0f;

        EParticleBlendMode      BlendMode               = EParticleBlendMode::Additive;
        bool                    bBillboardToCamera      = true;
        bool                    bWriteDepth             = false;
    };

    /** Bound properties read through component overrides, falling back to asset parameter default. */
    RUNTIME_API FResolvedParticleParams ResolveParticleParams(const CParticleSystem& Asset, const SParticleSystemComponent& Component);
}
