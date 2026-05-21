#include "PCH.h"
#include "AudioStreamFactory.h"

namespace Lumina
{
    CObject* CAudioStreamFactory::CreateNew(const FName& Name, CPackage* Package)
    {
        return NewObject<CAudioStream>(Package, Name);
    }
}
