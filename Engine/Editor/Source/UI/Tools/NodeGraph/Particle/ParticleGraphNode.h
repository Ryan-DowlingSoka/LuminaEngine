#pragma once

#include "UI/Tools/NodeGraph/EdGraphNode.h"
#include "ParticleGraphNode.generated.h"

namespace Lumina
{
    class FParticleCompiler;
}

namespace Lumina
{
    REFLECT()
    class CParticleGraphNode : public CEdGraphNode
    {
        GENERATED_BODY()
    public:

        virtual void GenerateDefinition(FParticleCompiler& Compiler) { }
    };
}
