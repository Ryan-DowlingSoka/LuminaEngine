#include "MaterialReroute.h"

#include "MaterialInput.h"
#include "MaterialOutput.h"
#include "Core/Object/Class.h"

namespace Lumina
{
    CClass* CMaterialReroute::GetInputPinClass() const
    {
        return CMaterialInput::StaticClass();
    }

    CClass* CMaterialReroute::GetOutputPinClass() const
    {
        return CMaterialOutput::StaticClass();
    }
}
