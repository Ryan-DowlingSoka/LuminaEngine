#include "pch.h"
#include "Font.h"
#include "Core/Object/Class.h"

namespace Lumina
{
    void CFont::Serialize(FArchive& Ar)
    {
        Super::Serialize(Ar);

        Ar << FontData;
    }
}
