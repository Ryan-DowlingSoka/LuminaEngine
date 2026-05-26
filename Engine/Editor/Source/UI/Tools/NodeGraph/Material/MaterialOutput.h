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

        // Compile-time-only override for the shader variable a consumer reads from this pin. Empty
        // (the default) means "use the owning node's FullName" -- correct for ordinary single-output
        // nodes. A material-function call node sets this per output pin so each of its (heterogeneous)
        // outputs resolves to its own emitted local. Not serialized; cleared/rebound each compile.
        FString             ResolvedVar;

        EMaterialInputType  InputType = EMaterialInputType::Float;
        EComponentMask      Mask = EComponentMask::None;

    };
}
