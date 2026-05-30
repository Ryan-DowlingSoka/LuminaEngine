#pragma once

#include "ParticleModule.h"
#include "Assets/AssetTypes/ParticleSystem/ParticleSystem.h"
#include "Core/Math/Math.h"
#include "ParticleStockModules.generated.h"

namespace Lumina
{
    /** How an Initial Velocity module derives the spawn velocity direction. */
    REFLECT()
    enum class EParticleInitVelocityMode : uint8
    {
        /** Independent per-axis random between Min and Max. */
        Explicit,
        /** Outward from the emitter origin (uses the spawn location offset). */
        Radial,
        /** Random direction inside a cone around the emitter forward. */
        Cone,
    };

    // Spawn modules

    /** Places newborn particles by sampling an emitter shape and writing P.Position. */
    REFLECT()
    class CParticleModule_SpawnLocation : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
        FString GetDisplayName() const override { return "Shape Location"; }
        FString GetCategory() const override { return "Location"; }
        FString GetTooltip() const override { return "Spawn particles on an emitter shape (point, sphere, box, cone, ring, disk)."; }
        uint32 GetAccentColor() const override { return IM_COL32(70, 160, 90, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;

        PROPERTY(Editable, Category = "Shape")
        EParticleEmitterShape Shape = EParticleEmitterShape::Point;

        /** Sphere: x=radius. Box: xyz=half extents. Cone: x=base radius, y=height. Ring/Disk: x=outer, y=inner. */
        PROPERTY(Editable, Category = "Shape")
        FVector3 ShapeSize = FVector3(1.0f);

        /** Cone half-angle in degrees. */
        PROPERTY(Editable, Category = "Shape", ClampMin = 0.0f, ClampMax = 180.0f)
        float ConeAngle = 30.0f;
    };

    /** Sets the initial P.Velocity. */
    REFLECT()
    class CParticleModule_InitialVelocity : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
        FString GetDisplayName() const override { return "Add Velocity"; }
        FString GetCategory() const override { return "Velocity"; }
        FString GetTooltip() const override { return "Give newborn particles a starting velocity."; }
        uint32 GetAccentColor() const override { return IM_COL32(70, 160, 90, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;

        PROPERTY(Editable, Category = "Velocity")
        EParticleInitVelocityMode Mode = EParticleInitVelocityMode::Explicit;

        PROPERTY(Editable, Category = "Velocity")
        FVector3 VelocityMin = FVector3(-0.5f, 1.0f, -0.5f);

        PROPERTY(Editable, Category = "Velocity")
        FVector3 VelocityMax = FVector3(0.5f, 3.0f, 0.5f);

        /** Speed range for Radial / Cone modes. */
        PROPERTY(Editable, Category = "Velocity")
        FVector2 SpeedRange = FVector2(1.0f, 3.0f);

        /** Cone half-angle in degrees for Cone mode. */
        PROPERTY(Editable, Category = "Velocity", ClampMin = 0.0f, ClampMax = 180.0f)
        float ConeAngle = 30.0f;
    };

    /** Sets the initial particle color. */
    REFLECT()
    class CParticleModule_InitialColor : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
        FString GetDisplayName() const override { return "Set Color"; }
        FString GetCategory() const override { return "Color"; }
        FString GetTooltip() const override { return "Set the starting color of newborn particles."; }
        uint32 GetAccentColor() const override { return IM_COL32(70, 160, 90, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;

        PROPERTY(Editable, Category = "Color", Color)
        FVector4 Color = FVector4(1.0f, 0.6f, 0.2f, 1.0f);
    };

    /** Sets the initial particle size (random within a range). */
    REFLECT()
    class CParticleModule_InitialSize : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
        FString GetDisplayName() const override { return "Set Size"; }
        FString GetCategory() const override { return "Size"; }
        FString GetTooltip() const override { return "Set the starting size of newborn particles."; }
        uint32 GetAccentColor() const override { return IM_COL32(70, 160, 90, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;

        PROPERTY(Editable, Category = "Size", ClampMin = 0.0f)
        FVector2 SizeRange = FVector2(0.2f, 0.3f);
    };

    /** Sets how long newborn particles live (random within a range). */
    REFLECT()
    class CParticleModule_Lifetime : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
        FString GetDisplayName() const override { return "Set Lifetime"; }
        FString GetCategory() const override { return "Lifetime"; }
        FString GetTooltip() const override { return "Set how many seconds newborn particles live."; }
        uint32 GetAccentColor() const override { return IM_COL32(70, 160, 90, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;

        PROPERTY(Editable, Category = "Lifetime", ClampMin = 0.01f)
        FVector2 LifetimeRange = FVector2(1.0f, 2.0f);
    };

    /** Sets the initial rotation and rotation speed (random within ranges). */
    REFLECT()
    class CParticleModule_InitialRotation : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
        FString GetDisplayName() const override { return "Set Rotation"; }
        FString GetCategory() const override { return "Rotation"; }
        FString GetTooltip() const override { return "Set starting rotation and spin (degrees)."; }
        uint32 GetAccentColor() const override { return IM_COL32(70, 160, 90, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;

        PROPERTY(Editable, Category = "Rotation")
        FVector2 RotationRange = FVector2(0.0f, 0.0f);

        PROPERTY(Editable, Category = "Rotation")
        FVector2 RotationSpeedRange = FVector2(0.0f, 0.0f);
    };

    // Update modules

    /** Accelerates particles by a constant gravity vector. */
    REFLECT()
    class CParticleModule_GravityForce : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Update; }
        FString GetDisplayName() const override { return "Gravity Force"; }
        FString GetCategory() const override { return "Forces"; }
        FString GetTooltip() const override { return "Apply a constant acceleration each frame."; }
        uint32 GetAccentColor() const override { return IM_COL32(70, 120, 190, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;

        PROPERTY(Editable, Category = "Forces")
        FVector3 Gravity = FVector3(0.0f, -9.8f, 0.0f);
    };

    /** Exponentially damps velocity (framerate-independent). */
    REFLECT()
    class CParticleModule_Drag : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Update; }
        FString GetDisplayName() const override { return "Drag"; }
        FString GetCategory() const override { return "Forces"; }
        FString GetTooltip() const override { return "Slow particles down over time."; }
        uint32 GetAccentColor() const override { return IM_COL32(70, 120, 190, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;

        PROPERTY(Editable, Category = "Forces", ClampMin = 0.0f)
        float Drag = 0.5f;
    };

    /** Adds turbulence via a cheap curl-noise field. */
    REFLECT()
    class CParticleModule_CurlNoiseForce : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Update; }
        FString GetDisplayName() const override { return "Curl Noise Force"; }
        FString GetCategory() const override { return "Forces"; }
        FString GetTooltip() const override { return "Push particles around with animated turbulence."; }
        uint32 GetAccentColor() const override { return IM_COL32(70, 120, 190, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;

        PROPERTY(Editable, Category = "Noise")
        FVector3 Strength = FVector3(1.0f);

        PROPERTY(Editable, Category = "Noise", ClampMin = 0.0001f)
        float Scale = 1.0f;

        PROPERTY(Editable, Category = "Noise")
        float Speed = 1.0f;
    };

    /** Blends color from Start to End over the particle's life. */
    REFLECT()
    class CParticleModule_ColorOverLife : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Update; }
        FString GetDisplayName() const override { return "Color Over Life"; }
        FString GetCategory() const override { return "Color"; }
        FString GetTooltip() const override { return "Fade color (and alpha) across the particle's lifetime."; }
        uint32 GetAccentColor() const override { return IM_COL32(150, 90, 180, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;

        PROPERTY(Editable, Category = "Color", Color)
        FVector4 StartColor = FVector4(1.0f, 0.6f, 0.2f, 1.0f);

        PROPERTY(Editable, Category = "Color", Color)
        FVector4 EndColor = FVector4(1.0f, 0.0f, 0.0f, 0.0f);
    };

    /** Interpolates size from Start to End over the particle's life. */
    REFLECT()
    class CParticleModule_SizeOverLife : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Update; }
        FString GetDisplayName() const override { return "Size Over Life"; }
        FString GetCategory() const override { return "Size"; }
        FString GetTooltip() const override { return "Grow or shrink particles across their lifetime."; }
        uint32 GetAccentColor() const override { return IM_COL32(150, 90, 180, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;

        PROPERTY(Editable, Category = "Size", ClampMin = 0.0f)
        float StartSize = 0.3f;

        PROPERTY(Editable, Category = "Size", ClampMin = 0.0f)
        float EndSize = 0.0f;
    };

    /**
     * Integrates velocity into position (and spin into rotation). Normally the last module in the
     * Update stack so all forces for the frame are accounted for first ("Solve Forces and Velocity").
     */
    REFLECT()
    class CParticleModule_Integrate : public CParticleModule
    {
        GENERATED_BODY()
    public:
        EParticleModuleStage GetStage() const override { return EParticleModuleStage::Update; }
        FString GetDisplayName() const override { return "Solve Forces and Velocity"; }
        FString GetCategory() const override { return "Solve"; }
        FString GetTooltip() const override { return "Move particles by their velocity. Place last in the Update stack."; }
        uint32 GetAccentColor() const override { return IM_COL32(180, 140, 70, 255); }
        void Generate(FParticleCompiler& Compiler, int32 ModuleIndex) override;
    };
}
