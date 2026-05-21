#pragma once
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/Object.h"
#include "AudioStream.generated.h"

namespace Lumina
{
    REFLECT()
    class RUNTIME_API CAudioStream : public CObject
    {
        GENERATED_BODY()
    public:
        
        bool IsAsset() const override { return true; }
        
    };
}
