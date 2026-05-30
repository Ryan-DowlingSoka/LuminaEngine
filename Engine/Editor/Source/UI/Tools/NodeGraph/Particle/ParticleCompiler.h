#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "UI/Tools/NodeGraph/EdGraphNode.h"
#include "ParticlePin.h"

namespace Lumina
{
    class CParticleGraphNode;

    /** Which function body a node is being emitted into; drives demand-driven emission into the right scope. */
    enum class EParticleContext : uint8
    {
        Spawn,
        Update,
    };

    /** Resolved input pin value; Value is an HLSL expression (literal default or upstream variable name). */
    struct FParticleInputValue
    {
        FString             Value;
        EParticlePinType    Type = EParticlePinType::Float;
    };

    /** Graph-to-HLSL compiler: demand-first walk from the output, each node emitted once per context.
     *  SpawnChunks/UpdateChunks are spliced into ParticleSimulateTemplate.slang. */
    class FParticleCompiler
    {
    public:

        FParticleCompiler() = default;

        FString BuildShader() const;

        void EmitSpawn(const FString& Line)  { SpawnChunks  += "\t" + Line + "\n"; }
        void EmitUpdate(const FString& Line) { UpdateChunks += "\t" + Line + "\n"; }

        /** Emits into the given context regardless of the current context. */
        void Emit(EParticleContext Context, const FString& Line)
        {
            if (Context == EParticleContext::Spawn)
            {
                SpawnChunks += "\t" + Line + "\n";
            }
            else
            {
                UpdateChunks += "\t" + Line + "\n";
            }
        }

        /** Emits into the currently active context. */
        void EmitCurrent(const FString& Line) { Emit(CurrentContext, Line); }

        /** Resolves an input pin to an HLSL expression: connected emits the upstream node once and returns
         *  its full name; unconnected returns the pin default literal. */
        FParticleInputValue GetInputValue(CParticleInput* Pin);

        FParticleInputValue GetInputFloat(CParticleInput* Pin,  float Default = 0.0f);
        FParticleInputValue GetInputFloat3(CParticleInput* Pin, const FVector3& Default = FVector3(0.0f));
        FParticleInputValue GetInputFloat4(CParticleInput* Pin, const FVector4& Default = FVector4(1.0f));

        /** Scalar / float3 / float4 coercion helpers. Used by nodes that need a specific type. */
        static FString Coerce(const FParticleInputValue& Value, EParticlePinType Target);
        static FString TypeName(EParticlePinType Type);

        /** Ensures a node is emitted in the given context, running GenerateDefinition on first call. Idempotent. */
        void EnsureEmitted(CParticleGraphNode* Node, EParticleContext Context);

        void SetContext(EParticleContext Context) { CurrentContext = Context; }
        EParticleContext GetContext() const       { return CurrentContext; }

        bool HasErrors() const                                          { return !Errors.empty(); }
        void AddError(const EdNodeGraph::FError& Error)                 { Errors.push_back(Error); }
        const TVector<EdNodeGraph::FError>& GetErrors() const           { return Errors; }

    private:

        FString SpawnChunks;
        FString UpdateChunks;

        TVector<EdNodeGraph::FError> Errors;

        THashSet<CParticleGraphNode*> EmittedSpawn;
        THashSet<CParticleGraphNode*> EmittedUpdate;

        EParticleContext CurrentContext = EParticleContext::Spawn;
    };
}
