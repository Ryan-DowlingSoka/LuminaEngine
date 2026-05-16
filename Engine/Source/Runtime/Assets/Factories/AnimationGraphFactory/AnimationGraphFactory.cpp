#include "pch.h"
#include "AnimationGraphFactory.h"

namespace Lumina
{
    CObject* CAnimationGraphFactory::CreateNew(const FName& Name, CPackage* Package)
    {
        return NewObject<CAnimationGraph>(Package, Name);
    }
}
