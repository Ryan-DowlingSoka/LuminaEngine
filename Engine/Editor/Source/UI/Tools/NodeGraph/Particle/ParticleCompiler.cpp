#include "ParticleCompiler.h"
#include "ParticleGraphNode.h"
#include "Core/Object/Cast.h"
#include "Paths/Paths.h"
#include "Platform/Filesystem/FileHelper.h"

namespace Lumina
{
    static constexpr const char* SpawnToken  = "$PARTICLE_SPAWN_FUNC";
    static constexpr const char* UpdateToken = "$PARTICLE_UPDATE_FUNC";

    FString FParticleCompiler::BuildShader() const
    {
        FString Path = Paths::GetEngineResourceDirectory() + "/Shaders/Particles/ParticleSimulateTemplate.slang";
        FString Source;
        if (!FileHelper::LoadFileIntoString(Source, Path))
        {
            return Source;
        }

        // Seed the spawn result with safe defaults so a graph that doesn't wire
        // every output still compiles to something sensible.
        FString SpawnFunc;
        SpawnFunc += "FParticleSpawnResult GetParticleSpawnInfo(float TotalTime, uint SpawnIndex)\n";
        SpawnFunc += "{\n";
        SpawnFunc += "\tFParticleSpawnResult Result;\n";
        SpawnFunc += "\tResult.InitialVelocity = float3(0.0, 0.0, 0.0);\n";
        SpawnFunc += "\tResult.Lifetime        = 1.0;\n";
        SpawnFunc += "\tResult.StartColor      = float4(1.0, 1.0, 1.0, 1.0);\n";
        SpawnFunc += "\tResult.StartSize       = 0.1;\n";
        SpawnFunc += SpawnChunks;
        SpawnFunc += "\treturn Result;\n";
        SpawnFunc += "}\n";

        FString UpdateFunc;
        UpdateFunc += "void SimulateParticle(inout FGPUParticle P, float DeltaTime, float TotalTime)\n";
        UpdateFunc += "{\n";
        UpdateFunc += UpdateChunks;
        UpdateFunc += "}\n";
        
        const size_t SpawnTokenLen  = strlen(SpawnToken);
        const size_t UpdateTokenLen = strlen(UpdateToken);

        for (size_t Pos = Source.find(SpawnToken); Pos != FString::npos; Pos = Source.find(SpawnToken, Pos + SpawnFunc.length()))
        {
            Source.replace(Pos, SpawnTokenLen, SpawnFunc);
        }

        for (size_t Pos = Source.find(UpdateToken); Pos != FString::npos; Pos = Source.find(UpdateToken, Pos + UpdateFunc.length()))
        {
            Source.replace(Pos, UpdateTokenLen, UpdateFunc);
        }

        return Source;
    }

    void FParticleCompiler::EnsureEmitted(CParticleGraphNode* Node, EParticleContext Context)
    {
        if (Node == nullptr)
        {
            return;
        }

        THashSet<CParticleGraphNode*>& EmittedSet = (Context == EParticleContext::Spawn) ? EmittedSpawn : EmittedUpdate;
        if (EmittedSet.find(Node) != EmittedSet.end())
        {
            return;
        }
        EmittedSet.insert(Node);

        const EParticleContext Saved = CurrentContext;
        CurrentContext = Context;
        Node->GenerateDefinition(*this);
        CurrentContext = Saved;
    }

    FParticleInputValue FParticleCompiler::GetInputValue(CParticleInput* Pin)
    {
        FParticleInputValue Result;

        if (Pin && Pin->HasConnection())
        {
            CParticleOutput* Out = Pin->GetConnection<CParticleOutput>(0);
            if (Out)
            {
                CParticleGraphNode* SourceNode = Cast<CParticleGraphNode>(Out->GetOwningNode());
                if (SourceNode)
                {
                    EnsureEmitted(SourceNode, CurrentContext);
                    Result.Value = SourceNode->GetNodeFullName();
                    Result.Type  = Out->GetPinType();
                    return Result;
                }
            }
        }

        if (Pin != nullptr)
        {
            switch (Pin->GetPinType())
            {
            case EParticlePinType::Float:
            {
                const float V = Pin->GetDefaultFloat();
                Result.Value = FString(eastl::to_string(V));
                Result.Type  = EParticlePinType::Float;
                break;
            }
            case EParticlePinType::Float3:
            {
                const glm::vec3& V = Pin->GetDefaultFloat3();
                Result.Value = "float3(" + FString(eastl::to_string(V.x)) + ", " + FString(eastl::to_string(V.y)) + ", " + FString(eastl::to_string(V.z)) + ")";
                Result.Type  = EParticlePinType::Float3;
                break;
            }
            case EParticlePinType::Float4:
            {
                const glm::vec4& V = Pin->GetDefaultFloat4();
                Result.Value = "float4(" + FString(eastl::to_string(V.x)) + ", " + FString(eastl::to_string(V.y)) + ", " + FString(eastl::to_string(V.z)) + ", " + FString(eastl::to_string(V.w)) + ")";
                Result.Type  = EParticlePinType::Float4;
                break;
            }
            }
        }

        return Result;
    }

    FParticleInputValue FParticleCompiler::GetInputFloat(CParticleInput* Pin, float Default)
    {
        FParticleInputValue V = GetInputValue(Pin);
        if (V.Value.empty())
        {
            V.Value = FString(eastl::to_string(Default));
            V.Type  = EParticlePinType::Float;
        }
        return V;
    }

    FParticleInputValue FParticleCompiler::GetInputFloat3(CParticleInput* Pin, const glm::vec3& Default)
    {
        FParticleInputValue V = GetInputValue(Pin);
        if (V.Value.empty())
        {
            V.Value = "float3(" + FString(eastl::to_string(Default.x)) + ", " + FString(eastl::to_string(Default.y)) + ", " + FString(eastl::to_string(Default.z)) + ")";
            V.Type  = EParticlePinType::Float3;
        }
        return V;
    }

    FParticleInputValue FParticleCompiler::GetInputFloat4(CParticleInput* Pin, const glm::vec4& Default)
    {
        FParticleInputValue V = GetInputValue(Pin);
        if (V.Value.empty())
        {
            V.Value = "float4(" + FString(eastl::to_string(Default.x)) + ", " + FString(eastl::to_string(Default.y)) + ", " + FString(eastl::to_string(Default.z)) + ", " + FString(eastl::to_string(Default.w)) + ")";
            V.Type  = EParticlePinType::Float4;
        }
        return V;
    }

    FString FParticleCompiler::Coerce(const FParticleInputValue& Value, EParticlePinType Target)
    {
        if (Value.Type == Target)
        {
            return Value.Value;
        }

        switch (Target)
        {
        case EParticlePinType::Float:
            return "(" + Value.Value + ").x";

        case EParticlePinType::Float3:
            if (Value.Type == EParticlePinType::Float)
            {
                return "float3(" + Value.Value + ", " + Value.Value + ", " + Value.Value + ")";
            }
            if (Value.Type == EParticlePinType::Float4)
            {
                return "(" + Value.Value + ").xyz";
            }
            break;

        case EParticlePinType::Float4:
            if (Value.Type == EParticlePinType::Float)
            {
                return "float4(" + Value.Value + ", " + Value.Value + ", " + Value.Value + ", " + Value.Value + ")";
            }
            if (Value.Type == EParticlePinType::Float3)
            {
                return "float4(" + Value.Value + ", 1.0)";
            }
            break;
        }
        return Value.Value;
    }

    FString FParticleCompiler::TypeName(EParticlePinType Type)
    {
        switch (Type)
        {
        case EParticlePinType::Float:  return "float";
        case EParticlePinType::Float3: return "float3";
        case EParticlePinType::Float4: return "float4";
        }
        return "float";
    }
}
