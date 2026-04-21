#include "ParticleOutputNode.h"

#include "Core/Object/Cast.h"
#include "UI/Tools/NodeGraph/Particle/ParticleCompiler.h"

namespace Lumina
{
    void CParticleOutputNode::BuildNode()
    {
        InitialVelocityPin = Cast<CParticleInput>(CreatePin(CParticleInput::StaticClass(), "Initial Velocity", ENodePinDirection::Input));
        InitialVelocityPin->SetPinType(EParticlePinType::Float3);
        InitialVelocityPin->SetDefaultFloat3(glm::vec3(0.0f, 2.0f, 0.0f));

        LifetimePin = Cast<CParticleInput>(CreatePin(CParticleInput::StaticClass(), "Lifetime", ENodePinDirection::Input));
        LifetimePin->SetPinType(EParticlePinType::Float);
        LifetimePin->SetDefaultFloat(3.0f);

        GravityPin = Cast<CParticleInput>(CreatePin(CParticleInput::StaticClass(), "Gravity", ENodePinDirection::Input));
        GravityPin->SetPinType(EParticlePinType::Float3);
        GravityPin->SetDefaultFloat3(glm::vec3(0.0f, -9.8f, 0.0f));

        StartColorPin = Cast<CParticleInput>(CreatePin(CParticleInput::StaticClass(), "Start Color", ENodePinDirection::Input));
        StartColorPin->SetPinType(EParticlePinType::Float4);
        StartColorPin->SetDefaultFloat4(glm::vec4(1.0f, 0.5f, 0.1f, 1.0f));

        StartSizePin = Cast<CParticleInput>(CreatePin(CParticleInput::StaticClass(), "Start Size", ENodePinDirection::Input));
        StartSizePin->SetPinType(EParticlePinType::Float);
        StartSizePin->SetDefaultFloat(0.1f);
    }

    void CParticleOutputNode::GenerateDefinition(FParticleCompiler& Compiler)
    {
        if (Compiler.GetContext() == EParticleContext::Spawn)
        {
            const FParticleInputValue Velocity = Compiler.GetInputFloat3(InitialVelocityPin, glm::vec3(0.0f, 2.0f, 0.0f));
            const FParticleInputValue Lifetime = Compiler.GetInputFloat (LifetimePin,       3.0f);
            const FParticleInputValue Color    = Compiler.GetInputFloat4(StartColorPin,     glm::vec4(1.0f, 0.5f, 0.1f, 1.0f));
            const FParticleInputValue Size     = Compiler.GetInputFloat (StartSizePin,      0.1f);

            Compiler.EmitSpawn("Result.InitialVelocity = " + FParticleCompiler::Coerce(Velocity, EParticlePinType::Float3) + ";");
            Compiler.EmitSpawn("Result.Lifetime        = " + FParticleCompiler::Coerce(Lifetime, EParticlePinType::Float)  + ";");
            Compiler.EmitSpawn("Result.StartColor      = " + FParticleCompiler::Coerce(Color,    EParticlePinType::Float4) + ";");
            Compiler.EmitSpawn("Result.StartSize       = " + FParticleCompiler::Coerce(Size,     EParticlePinType::Float)  + ";");
        }
        else
        {
            const FParticleInputValue Gravity = Compiler.GetInputFloat3(GravityPin, glm::vec3(0.0f, -9.8f, 0.0f));

            Compiler.EmitUpdate("float3 Gravity_ = " + FParticleCompiler::Coerce(Gravity, EParticlePinType::Float3) + ";");
            Compiler.EmitUpdate("P.Velocity += Gravity_ * DeltaTime;");
            Compiler.EmitUpdate("P.Position += P.Velocity * DeltaTime;");
            Compiler.EmitUpdate("float LifeRatio_ = saturate(P.Age / max(P.Lifetime, 0.0001));");
            Compiler.EmitUpdate("P.Color.a = 1.0 - LifeRatio_;");
        }
    }
}
