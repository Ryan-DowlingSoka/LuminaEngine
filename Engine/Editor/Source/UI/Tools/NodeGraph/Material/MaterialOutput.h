#pragma once

#include "MaterialGraphTypes.h"
#include "UI/Tools/NodeGraph/EdNodeGraphPin.h"
#include "MaterialOutput.generated.h"

namespace Lumina
{
    REFLECT()
    class CMaterialOutput : public CEdNodeGraphPin
    {
        GENERATED_BODY()
    public:
        
        float DrawPin() override;
        EComponentMask GetComponentMask() const { return Mask; }
        void SetComponentMask(EComponentMask InMask) { Mask = InMask; }
        
        void SetInputType(EMaterialInputType Type) { InputType = Type; }

        // Compile-only override for the shader var a consumer reads from this pin; empty means use the node's
        // FullName. Function-call nodes set this per output pin. Not serialized; cleared/rebound each compile.
        FString             ResolvedVar;

        EMaterialInputType  InputType = EMaterialInputType::Float;
        EComponentMask      Mask = EComponentMask::None;

    };
}
