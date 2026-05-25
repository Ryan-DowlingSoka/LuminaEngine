#include "ParticleStockModules.h"
#include "Containers/String.h"
#include "UI/Tools/NodeGraph/Particle/ParticleCompiler.h"

namespace Lumina
{
    static FString ShapeId(EParticleEmitterShape Shape)
    {
        return FString(eastl::to_string((uint32)Shape)) + "u";
    }

    // -------------------------------------------------------------------------
    //  Spawn modules
    // -------------------------------------------------------------------------

    void CParticleModule_SpawnLocation::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        const FString Off = LocalVar(ModuleIndex, "off");
        Compiler.EmitSpawn("float3 " + Off + " = SampleEmitterShape(Seed, " + ShapeId(Shape) + ", "
            + Lit(ShapeSize) + ", radians(" + Lit(ConeAngle) + "), "
            + "SimParams.EmitterForward.xyz, SimParams.EmitterRight.xyz, SimParams.EmitterUp.xyz);");
        Compiler.EmitSpawn("P.Position = SimParams.EmitterPosition.xyz + " + Off + ";");
    }

    void CParticleModule_InitialVelocity::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        switch (Mode)
        {
        case EParticleInitVelocityMode::Explicit:
        {
            const FString R = LocalVar(ModuleIndex, "r");
            Compiler.EmitSpawn("float3 " + R + " = float3(RandUnit(Seed), RandUnit(Seed), RandUnit(Seed));");
            Compiler.EmitSpawn("P.Velocity = lerp(" + Lit(VelocityMin) + ", " + Lit(VelocityMax) + ", " + R + ");");
            break;
        }
        case EParticleInitVelocityMode::Radial:
        {
            const FString D = LocalVar(ModuleIndex, "dir");
            const FString L = LocalVar(ModuleIndex, "len");
            Compiler.EmitSpawn("float3 " + D + " = P.Position - SimParams.EmitterPosition.xyz;");
            Compiler.EmitSpawn("float " + L + " = length(" + D + ");");
            Compiler.EmitSpawn(D + " = (" + L + " > 1e-5) ? (" + D + " / " + L + ") : RandOnUnitSphere(Seed);");
            Compiler.EmitSpawn("P.Velocity = " + D + " * lerp(" + Lit(SpeedRange.x) + ", " + Lit(SpeedRange.y) + ", RandUnit(Seed));");
            break;
        }
        case EParticleInitVelocityMode::Cone:
        {
            const FString D = LocalVar(ModuleIndex, "dir");
            Compiler.EmitSpawn("float3 " + D + " = RandDirectionInCone(Seed, SimParams.EmitterForward.xyz, "
                + "SimParams.EmitterRight.xyz, SimParams.EmitterUp.xyz, radians(" + Lit(ConeAngle) + "));");
            Compiler.EmitSpawn("P.Velocity = " + D + " * lerp(" + Lit(SpeedRange.x) + ", " + Lit(SpeedRange.y) + ", RandUnit(Seed));");
            break;
        }
        }
    }

    void CParticleModule_InitialColor::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        Compiler.EmitSpawn("P.Color = " + Lit(Color) + ";");
    }

    void CParticleModule_InitialSize::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        Compiler.EmitSpawn("P.Size = lerp(" + Lit(SizeRange.x) + ", " + Lit(SizeRange.y) + ", RandUnit(Seed));");
    }

    void CParticleModule_Lifetime::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        Compiler.EmitSpawn("P.Lifetime = lerp(" + Lit(LifetimeRange.x) + ", " + Lit(LifetimeRange.y) + ", RandUnit(Seed));");
    }

    void CParticleModule_InitialRotation::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        Compiler.EmitSpawn("P.Rotation = radians(lerp(" + Lit(RotationRange.x) + ", " + Lit(RotationRange.y) + ", RandUnit(Seed)));");
        Compiler.EmitSpawn("P.RotationSpeed = radians(lerp(" + Lit(RotationSpeedRange.x) + ", " + Lit(RotationSpeedRange.y) + ", RandUnit(Seed)));");
    }

    // -------------------------------------------------------------------------
    //  Update modules
    // -------------------------------------------------------------------------

    void CParticleModule_GravityForce::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        Compiler.EmitUpdate("P.Velocity += " + Lit(Gravity) + " * DeltaTime;");
    }

    void CParticleModule_Drag::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        Compiler.EmitUpdate("P.Velocity *= exp(-" + Lit(Drag) + " * DeltaTime);");
    }

    void CParticleModule_CurlNoiseForce::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        const FString N = LocalVar(ModuleIndex, "turb");
        Compiler.EmitUpdate("float3 " + N + " = CurlishNoise(P.Position, TotalTime, " + Lit(Scale) + ", " + Lit(Speed) + ");");
        Compiler.EmitUpdate("P.Velocity += " + N + " * " + Lit(Strength) + " * DeltaTime;");
    }

    void CParticleModule_ColorOverLife::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        Compiler.EmitUpdate("P.Color = lerp(" + Lit(StartColor) + ", " + Lit(EndColor) + ", LifeRatio);");
    }

    void CParticleModule_SizeOverLife::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        Compiler.EmitUpdate("P.Size = lerp(" + Lit(StartSize) + ", " + Lit(EndSize) + ", LifeRatio);");
    }

    void CParticleModule_Integrate::Generate(FParticleCompiler& Compiler, int32 ModuleIndex)
    {
        Compiler.EmitUpdate("P.Position += P.Velocity * DeltaTime;");
        Compiler.EmitUpdate("P.Rotation += P.RotationSpeed * DeltaTime;");
    }
}
